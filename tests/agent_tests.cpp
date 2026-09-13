#include "agent/agent_loop.hpp"
#include "agent/openai/responses_model.hpp"
#include "agent/openai/responses_stream.hpp"
#include "agent/openai/sse_parser.hpp"
#include "agent/skill_registry.hpp"
#include "agent/tools/calculator_tool.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class ToolCallingModel final : public agent::IModel {
public:
    agent::ModelResponse generate(const agent::ModelRequest& request) override {
        const auto& messages = request.messages;
        if (messages.back().role == agent::Role::Tool) {
            return {"answer=" + messages.back().content, {}, true, {}};
        }
        return {
            "using calculator",
            {{"call-1", "calculator", agent::Json{{"expression", "21 * 2"}}}},
            false,
            {},
        };
    }
};

void test_agent_loop() {
    agent::ToolRegistry tools;
    tools.add(std::make_unique<agent::CalculatorTool>());
    agent::ContextManager context;
    ToolCallingModel model;
    agent::AgentLoop loop(model, tools, context);

    const auto result = loop.run("What is 21 * 2?");
    require(result.ok, "loop should succeed");
    require(result.output == "answer=42", "loop should return the tool result");
    require(result.steps == 2, "loop should take two model steps");
}

void test_unknown_tool() {
    agent::ToolRegistry tools;
    const auto result = tools.execute({"call-x", "missing", {}});
    require(!result.ok, "unknown tool should fail");
    require(result.output.find("unknown tool") != std::string::npos,
            "unknown tool error should be descriptive");
}

void test_context_compaction() {
    agent::ContextManager context({80, 0.5, 2});
    context.add_instruction(agent::Role::System, "Pinned instruction");
    for (int index = 0; index < 6; ++index) {
        context.append({agent::Role::User,
                        "A deliberately long message for compaction number " +
                            std::to_string(index),
                        {},
                        {},
                        false});
    }

    require(context.maybe_compact(), "context should compact over its budget");
    require(context.history().size() == 2, "recent messages should be retained");
    require(context.summary().find("number 0") != std::string::npos,
            "summary should include older messages");
    require(context.working_messages().front().content == "Pinned instruction",
            "instructions should remain pinned");
}

void test_skill_registry() {
    const auto root = std::filesystem::temp_directory_path() /
                      "cpp-agent-harness-skill-test";
    const auto skill_root = root / "review";
    std::filesystem::create_directories(skill_root);
    {
        std::ofstream file(skill_root / "SKILL.md");
        file << "---\nname: review\ndescription: Review code.\n---\nCheck ownership.";
    }

    agent::SkillRegistry registry;
    require(registry.load_directory(root) == 1, "one skill should load");
    const auto skill = registry.find("review");
    require(skill.has_value(), "loaded skill should be found");
    require(skill->instructions == "Check ownership.",
            "skill instructions should be parsed");

    std::filesystem::remove_all(root);
}

class FakeHttpTransport final : public agent::IHttpTransport {
public:
    agent::HttpResponse send(const agent::HttpRequest& request) override {
        last_request = request;
        return response;
    }

    agent::HttpRequest last_request;
    agent::HttpResponse response;
};

void test_responses_request_codec() {
    agent::ModelRequest request;
    request.messages = {
        {agent::Role::System, "Be concise.", {}, {}, true},
        {agent::Role::User, "Calculate it.", {}, {}, false},
        {agent::Role::Assistant,
         "",
         {{"call-1", "calculator", agent::Json{{"expression", "6 * 7"}}}},
         {},
         false},
        {agent::Role::Tool, "42", {}, "call-1", false},
    };
    request.tools = {agent::CalculatorTool{}.definition()};

    const agent::openai::ResponsesConfig config{
        "https://example.test/v1/responses", "secret", "test-model"};
    const auto body = agent::openai::ResponsesCodec::encode_request(request, config);

    require(body["model"] == "test-model", "request should include model");
    require(body["instructions"].get<std::string>().find("Be concise.") !=
                std::string::npos,
            "request should separate instructions");
    require(body["input"].size() == 3, "request should contain user, call, and output");
    require(body["input"][1]["type"] == "function_call",
            "assistant call should use Responses API item format");
    require(body["input"][2]["type"] == "function_call_output",
            "tool result should use Responses API item format");
    require(body["tools"][0]["strict"] == true,
            "tool definitions should use strict schemas");
}

void test_responses_model_with_fake_transport() {
    FakeHttpTransport transport;
    transport.response = {
        200,
        {},
        agent::Json{
            {"id", "resp_123"},
            {"status", "completed"},
            {"output",
             agent::Json::array({
                 {{"type", "function_call"},
                  {"id", "fc_123"},
                  {"call_id", "call_123"},
                  {"name", "calculator"},
                  {"arguments", R"({"expression":"8 * 9"})"}},
             })},
        }.dump(),
    };

    agent::openai::ResponsesModel model(
        transport,
        {"https://example.test/v1/responses", "test-key", "test-model"});
    agent::ModelRequest request;
    request.messages.push_back({agent::Role::User, "8 * 9", {}, {}, false});
    request.tools.push_back(agent::CalculatorTool{}.definition());

    const auto response = model.generate(request);
    require(!response.final, "function call response should continue the loop");
    require(response.response_id == "resp_123", "response id should be retained");
    require(response.tool_calls.size() == 1, "one function call should decode");
    require(response.tool_calls[0].id == "call_123", "call_id should be used for output");
    require(response.tool_calls[0].arguments["expression"] == "8 * 9",
            "function arguments should decode as JSON");
    require(transport.last_request.headers.at("Authorization") == "Bearer test-key",
            "transport request should carry bearer auth");
}

void test_sse_parser_across_chunks() {
    agent::openai::SseParser parser;
    std::vector<agent::openai::SseEvent> events;
    for (const std::string chunk : {
             "event: response.output_",
             "text.delta\r\ndata: {\"delta\":",
             "\"hel",
             "lo\"}\r\n\r\n",
         }) {
        auto next = parser.feed(chunk);
        events.insert(events.end(), next.begin(), next.end());
    }

    require(events.size() == 1, "one SSE event should be framed");
    require(events[0].event == "response.output_text.delta",
            "SSE event name should survive chunking");
    require(agent::Json::parse(events[0].data)["delta"] == "hello",
            "SSE data should survive chunking");
}

void test_streamed_function_call_assembly() {
    agent::openai::ResponsesStreamAssembler assembler;
    assembler.consume({
        "response.output_item.added",
        R"({"type":"response.output_item.added","item":{"id":"fc_1","type":"function_call","call_id":"call_1","name":"calculator","arguments":""}})",
        {}});
    assembler.consume({
        "response.function_call_arguments.delta",
        R"({"type":"response.function_call_arguments.delta","item_id":"fc_1","delta":"{\"expression\":"})",
        {}});
    assembler.consume({
        "response.function_call_arguments.delta",
        R"({"type":"response.function_call_arguments.delta","item_id":"fc_1","delta":"\"10 + 5\"}"})",
        {}});

    const auto response = assembler.result();
    require(response.tool_calls.size() == 1, "stream should assemble one call");
    require(response.tool_calls[0].id == "call_1", "stream should preserve call id");
    require(response.tool_calls[0].arguments["expression"] == "10 + 5",
            "stream should concatenate and parse argument deltas");
}

}  // namespace

int main() {
    try {
        test_agent_loop();
        test_unknown_tool();
        test_context_compaction();
        test_skill_registry();
        test_responses_request_codec();
        test_responses_model_with_fake_transport();
        test_sse_parser_across_chunks();
        test_streamed_function_call_assembly();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
