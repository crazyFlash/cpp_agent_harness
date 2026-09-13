#pragma once

#include "agent/config.hpp"
#include "agent/skill_registry.hpp"
#include "agent/tool.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace agent {

struct CliCommandResult {
    bool handled{false};
    bool exit_requested{false};
    bool model_changed{false};
    std::string output;
};

class CliCommandProcessor {
public:
    CliCommandProcessor(AppConfig& config,
                        const SkillRegistry& skills,
                        const ToolRegistry& tools,
                        ContextManager& context);
    CliCommandResult process(std::string_view input);
    std::vector<std::string> completions(std::string_view input) const;
    void record_run(const RunResult& result);

private:
    std::string format_context() const;
    std::string format_usage() const;

    AppConfig& config_;
    const SkillRegistry& skills_;
    const ToolRegistry& tools_;
    ContextManager& context_;
    TokenUsage last_usage_;
    TokenUsage total_usage_;
    std::size_t turn_count_{0};
};

}  // namespace agent
