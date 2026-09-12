#include "agent/agent_loop.hpp"
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
    agent::ModelResponse generate(const std::vector<agent::Message>& messages) override {
        if (messages.back().role == agent::Role::Tool) {
            return {"answer=" + messages.back().content, {}, true};
        }
        return {
            "using calculator",
            {{"call-1", "calculator", {{"expression", "21 * 2"}}}},
            false,
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

}  // namespace

int main() {
    try {
        test_agent_loop();
        test_unknown_tool();
        test_context_compaction();
        test_skill_registry();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
