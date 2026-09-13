#pragma once

#include "agent/config.hpp"
#include "agent/skill_registry.hpp"

#include <string>
#include <string_view>

namespace agent {

struct CliCommandResult {
    bool handled{false};
    bool exit_requested{false};
    bool model_changed{false};
    std::string output;
};

class CliCommandProcessor {
public:
    CliCommandProcessor(AppConfig& config, const SkillRegistry& skills);
    CliCommandResult process(std::string_view input);

private:
    AppConfig& config_;
    const SkillRegistry& skills_;
};

}  // namespace agent
