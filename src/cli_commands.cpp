#include "agent/cli_commands.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace agent {

namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

std::pair<std::string_view, std::string_view> split_command(std::string_view input) {
    input = trim(input);
    const auto space = input.find_first_of(" \t");
    if (space == std::string_view::npos) return {input, {}};
    return {input.substr(0, space), trim(input.substr(space + 1))};
}

std::string help_text() {
    return
        "Built-in commands:\n"
        "  /help            Show this help\n"
        "  /model           Show the active model\n"
        "  /model MODEL     Switch model for this session\n"
        "  /skills          List loaded skills\n"
        "  /skills NAME     Inspect one skill\n"
        "  /config          Show sanitized runtime configuration\n"
        "  /quit, /exit     Exit";
}

}  // namespace

CliCommandProcessor::CliCommandProcessor(AppConfig& config,
                                         const SkillRegistry& skills)
    : config_(config), skills_(skills) {}

CliCommandResult CliCommandProcessor::process(std::string_view input) {
    input = trim(input);
    if (input.empty() || input.front() != '/') return {};

    const auto [command, argument] = split_command(input);
    if (command == "/quit" || command == "/exit") {
        return {true, true, false, {}};
    }
    if (command == "/help") return {true, false, false, help_text()};
    if (command == "/model") {
        if (argument.empty()) {
            const auto model = config_.provider == ProviderKind::ResponsesApi
                ? config_.api.model
                : config_.provider == ProviderKind::Local
                    ? config_.local.model : std::string{"demo"};
            return {true, false, false,
                    "provider=" + ConfigLoader::provider_name(config_.provider) +
                    " model=" + model};
        }
        if (config_.provider != ProviderKind::ResponsesApi) {
            return {true, false, false,
                    "model switching is currently supported for responses_api only"};
        }
        config_.api.model = std::string{argument};
        return {true, false, true, "model switched to " + config_.api.model};
    }
    if (command == "/skills") {
        if (!argument.empty()) {
            const auto skill = skills_.find(std::string{argument});
            if (!skill) return {true, false, false, "unknown skill: " + std::string{argument}};
            return {true, false, false,
                    skill->name + " - " + skill->description + "\n\n" +
                    skill->instructions};
        }
        const auto loaded = skills_.list();
        if (loaded.empty()) return {true, false, false, "No skills loaded."};
        std::ostringstream output;
        output << "Loaded skills (" << loaded.size() << "):";
        for (const auto& skill : loaded) {
            output << "\n  " << skill.name;
            if (!skill.description.empty()) output << " - " << skill.description;
        }
        return {true, false, false, output.str()};
    }
    if (command == "/config") {
        std::ostringstream output;
        output << "provider=" << ConfigLoader::provider_name(config_.provider)
               << " trace=" << (config_.trace ? "true" : "false");
        if (config_.provider == ProviderKind::ResponsesApi) {
            output << "\nbase_url=" << config_.api.base_url
                   << "\nmodel=" << config_.api.model
                   << "\nstream=" << (config_.api.stream ? "true" : "false")
                   << "\napi_key_env=" << config_.api.api_key_env
                   << " (secret hidden)";
        }
        return {true, false, false, output.str()};
    }
    return {true, false, false,
            "unknown command: " + std::string{command} + "\n" + help_text()};
}

}  // namespace agent
