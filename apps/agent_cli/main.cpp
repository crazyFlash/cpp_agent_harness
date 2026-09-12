#include "agent/agent_loop.hpp"
#include "agent/skill_registry.hpp"
#include "agent/tools/calculator_tool.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace {

class DemoModel final : public agent::IModel {
public:
    agent::ModelResponse generate(const std::vector<agent::Message>& messages) override {
        if (messages.empty()) {
            return {"No input was provided.", {}, true};
        }

        const auto& last = messages.back();
        if (last.role == agent::Role::Tool) {
            return {"The tool returned: " + last.content, {}, true};
        }

        if (last.role != agent::Role::User) {
            return {"Waiting for user input.", {}, true};
        }

        constexpr std::string_view prefix = "calc ";
        if (last.content.starts_with(prefix)) {
            agent::ToolCall call;
            call.id = "demo-call-1";
            call.name = "calculator";
            call.arguments["expression"] = last.content.substr(prefix.size());
            return {"I'll calculate that.", {std::move(call)}, false};
        }

        return {
            "Demo model echo: " + last.content +
                "\nTip: enter `calc 21 * 2` to exercise the agent tool loop.",
            {},
            true,
        };
    }
};

const char* event_name(agent::EventType type) {
    switch (type) {
        case agent::EventType::LoopStarted: return "loop.started";
        case agent::EventType::ModelRequested: return "model.requested";
        case agent::EventType::ModelResponded: return "model.responded";
        case agent::EventType::ToolStarted: return "tool.started";
        case agent::EventType::ToolFinished: return "tool.finished";
        case agent::EventType::ContextCompacted: return "context.compacted";
        case agent::EventType::LoopFinished: return "loop.finished";
        case agent::EventType::LoopFailed: return "loop.failed";
    }
    return "unknown";
}

}  // namespace

int main(int argc, char** argv) {
    agent::ToolRegistry tools;
    tools.add(std::make_unique<agent::CalculatorTool>());

    agent::SkillRegistry skills;
    const auto skill_count = skills.load_directory("skills");

    agent::ContextManager context;
    context.add_instruction(
        agent::Role::System,
        "You are the demo agent in cpp_agent_harness. Use tools when needed.");

    DemoModel model;
    const bool trace = argc > 1 && std::string{argv[1]} == "--trace";
    agent::AgentLoop loop(
        model,
        tools,
        context,
        {},
        [trace](const agent::AgentEvent& event) {
            if (trace) {
                std::cerr << "[trace] " << event_name(event.type)
                          << " step=" << event.step;
                if (!event.detail.empty()) {
                    std::cerr << " detail=" << event.detail;
                }
                std::cerr << '\n';
            }
        });

    std::cout << "cpp_agent_harness demo (" << skill_count << " skill(s) loaded)\n"
              << "Type 'quit' to exit. Try: calc 21 * 2\n";

    std::string input;
    while (std::cout << "> " && std::getline(std::cin, input)) {
        if (input == "quit" || input == "exit") {
            break;
        }
        const auto result = loop.run(input);
        std::cout << (result.ok ? "assistant: " : "error: ")
                  << result.output << '\n';
    }
}

