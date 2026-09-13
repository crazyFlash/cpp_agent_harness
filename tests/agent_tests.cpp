#include "agent/agent_loop.hpp"
#include "agent/config.hpp"
#include "agent/curl_cli_transport.hpp"
#include "agent/openai/responses_model.hpp"
#include "agent/openai/responses_stream.hpp"
#include "agent/openai/sse_parser.hpp"
#include "agent/skill_registry.hpp"
#include "agent/tools/calculator_tool.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void require_throws(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
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

void test_configuration_loading_and_environment() {
    const auto path = std::filesystem::temp_directory_path() /
                      "cpp-agent-harness-config-test.json";
    {
        std::ofstream file(path);
        file << R"({
            "provider": "responses_api",
            "trace": false,
            "api": {
                "base_url": "http://127.0.0.1:8080/v1/",
                "model": "",
                "api_key_env": "TEST_API_KEY",
                "require_api_key": tru,
                "timeout_ms": 2500,
                "store": false
            },
            "context": {"max_estimated_tokens": 2048},
            "loop": {"max_steps": 7}
        })";
    }

    require_throws(
        [&]() { (void)agent::ConfigLoader::load_file(path); },
        "invalid JSON configuration should fail");

    {
        std::ofstream file(path);
        file << R"({
            "provider": "responses_api",
            "trace": false,
            "api": {
                "base_url": "http://127.0.0.1:8080/v1/",
                "model": "",
                "api_key_env": "TEST_API_KEY",
                "require_api_key": true,
                "timeout_ms": 2500,
                "store": false
            },
            "context": {"max_estimated_tokens": 2048},
            "loop": {"max_steps": 7}
        })";
    }

    auto config = agent::ConfigLoader::load_file(path);
    const std::map<std::string, std::string> environment{
        {"CPP_AGENT_MODEL", "environment-model"},
        {"CPP_AGENT_TRACE", "true"},
        {"TEST_API_KEY", "test-secret"},
    };
    const auto reader = [&environment](const std::string& name)
        -> std::optional<std::string> {
        const auto value = environment.find(name);
        if (value == environment.end()) {
            return std::nullopt;
        }
        return value->second;
    };
    agent::ConfigLoader::apply_environment(config, reader);

    require(config.provider == agent::ProviderKind::ResponsesApi,
            "provider should load from file");
    require(config.api.model == "environment-model",
            "environment should override model from file");
    require(config.trace, "environment should override trace");
    require(config.context.max_estimated_tokens == 2048,
            "context configuration should load");
    require(config.loop.max_steps == 7, "loop configuration should load");
    require(agent::ConfigLoader::resolve_api_key(config, reader) == "test-secret",
            "API key should resolve through the configured environment name");
    require(agent::ConfigLoader::responses_endpoint(config.api) ==
                "http://127.0.0.1:8080/v1/responses",
            "Responses endpoint should normalize its slash");

    const auto saved_path = std::filesystem::temp_directory_path() /
                            "cpp-agent-harness-saved-config.json";
    agent::ConfigLoader::save_file(config, saved_path);
    const auto saved = agent::ConfigLoader::load_file(saved_path);
    require(saved.api.model == "environment-model",
            "saved configuration should be loadable");
    std::ifstream saved_file(saved_path);
    const auto saved_json = agent::Json::parse(saved_file);
    require(!saved_json["api"].contains("api_key"),
            "saved configuration must not contain an API key");

    std::filesystem::remove(path);
    std::filesystem::remove(saved_path);
}

void test_local_configuration_is_reserved() {
    agent::AppConfig config;
    config.provider = agent::ProviderKind::Local;
    config.local.protocol = "future-protocol";
    config.local.options = {{"arbitrary", 42}};
    agent::ConfigLoader::validate(config);
    require(config.local.options["arbitrary"] == 42,
            "local provider should preserve protocol-specific options");
}

void test_responses_model_without_api_key() {
    FakeHttpTransport transport;
    transport.response = {
        200,
        {},
        R"({"id":"resp_local","status":"completed","output":[{"type":"message","content":[{"type":"output_text","text":"local ok"}]}]})",
    };
    agent::openai::ResponsesConfig config;
    config.endpoint = "http://127.0.0.1:8080/v1/responses";
    config.model = "local-model";
    config.require_api_key = false;
    agent::openai::ResponsesModel model(transport, config);

    agent::ModelRequest request;
    request.messages.push_back({agent::Role::User, "hello", {}, {}, false});
    const auto response = model.generate(request);
    require(response.text == "local ok", "keyless local API should return text");
    require(!transport.last_request.headers.contains("Authorization"),
            "keyless local API should not send authorization");
}

void test_curl_transport_rejects_header_injection() {
    agent::CurlCliTransport transport;
    agent::HttpRequest request;
    request.url = "http://127.0.0.1:1/v1/responses";
    request.headers["Authorization"] = "Bearer safe\r\nInjected: value";
    require_throws(
        [&]() { (void)transport.send(request); },
        "HTTP transport should reject CRLF header injection before execution");
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
        test_configuration_loading_and_environment();
        test_local_configuration_is_reserved();
        test_responses_model_without_api_key();
        test_curl_transport_rejects_header_injection();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
