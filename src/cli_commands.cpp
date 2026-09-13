#include "agent/cli_commands.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>

namespace agent {

namespace {

constexpr std::string_view command_names[] = {
    "/help", "/status", "/model", "/tools", "/skills", "/mcp",
    "/context", "/usage", "/history", "/compact", "/clear", "/new",
    "/trace", "/stream", "/config", "/quit", "/exit",
};

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
        "Built-in commands (press Tab to complete):\n"
        "  /status          Show model, context, and token status\n"
        "  /model [MODEL]   Show or switch the session model\n"
        "  /tools [NAME]    List or inspect available tools\n"
        "  /skills [NAME]   List or inspect loaded skills\n"
        "  /mcp             Show MCP runtime/server status\n"
        "  /context         Show context budget and compaction state\n"
        "  /usage           Show last-turn and session token usage\n"
        "  /history         Show conversation history\n"
        "  /compact         Compact older context now\n"
        "  /clear, /new     Start a fresh conversation context\n"
        "  /trace [on|off]  Show or change event tracing\n"
        "  /stream [on|off] Show or change API streaming\n"
        "  /config          Show sanitized runtime configuration\n"
        "  /help            Show this help\n"
        "  /quit, /exit     Exit";
}

std::string short_text(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    constexpr std::size_t limit = 100;
    if (text.size() > limit) text = text.substr(0, limit) + "...";
    return text;
}

std::optional<bool> parse_toggle(std::string_view value) {
    if (value == "on" || value == "true" || value == "1") return true;
    if (value == "off" || value == "false" || value == "0") return false;
    return std::nullopt;
}

}  // namespace

CliCommandProcessor::CliCommandProcessor(AppConfig& config,
                                         const SkillRegistry& skills,
                                         const ToolRegistry& tools,
                                         ContextManager& context)
    : config_(config), skills_(skills), tools_(tools), context_(context) {}

void CliCommandProcessor::record_run(const RunResult& result) {
    last_usage_ = result.usage;
    total_usage_ += result.usage;
    ++turn_count_;
}

std::string CliCommandProcessor::format_context() const {
    const auto stats = context_.stats();
    const auto percent = stats.max_estimated_tokens == 0
        ? 0.0
        : 100.0 * static_cast<double>(stats.estimated_tokens) /
              static_cast<double>(stats.max_estimated_tokens);
    std::ostringstream output;
    output << "context≈" << stats.estimated_tokens << '/'
           << stats.max_estimated_tokens << " (" << std::fixed
           << std::setprecision(1) << percent << "%)"
           << " threshold=" << stats.compact_threshold_tokens
           << " messages=" << stats.history_messages
           << " summary=" << (stats.has_summary ? "yes" : "no")
           << " compactions=" << stats.compaction_count;
    return output.str();
}

std::string CliCommandProcessor::format_usage() const {
    std::ostringstream output;
    if (last_usage_.reported) {
        output << "last: input=" << last_usage_.input_tokens
               << " output=" << last_usage_.output_tokens
               << " total=" << last_usage_.total_tokens
               << " cached=" << last_usage_.cached_tokens
               << " reasoning=" << last_usage_.reasoning_tokens;
    } else {
        output << "last: unavailable (provider did not report usage)";
    }
    output << "\nsession(" << turn_count_ << " turns): ";
    if (total_usage_.reported) {
        output << "input=" << total_usage_.input_tokens
               << " output=" << total_usage_.output_tokens
               << " total=" << total_usage_.total_tokens;
    } else {
        output << "unavailable";
    }
    return output.str();
}

std::vector<std::string> CliCommandProcessor::completions(
    std::string_view input) const {
    std::vector<std::string> result;
    for (const auto command : command_names) {
        if (command.starts_with(input)) result.emplace_back(command);
    }
    if (input.starts_with("/skills ")) {
        const auto prefix = input.substr(8);
        for (const auto& skill : skills_.list()) {
            if (skill.name.starts_with(prefix)) {
                result.push_back("/skills " + skill.name);
            }
        }
    } else if (input.starts_with("/tools ")) {
        const auto prefix = input.substr(7);
        for (const auto& tool : tools_.definitions()) {
            if (tool.name.starts_with(prefix)) {
                result.push_back("/tools " + tool.name);
            }
        }
    } else if (input.starts_with("/trace ")) {
        for (const std::string value : {"on", "off"}) {
            const auto candidate = "/trace " + value;
            if (candidate.starts_with(input)) result.push_back(candidate);
        }
    } else if (input.starts_with("/stream ")) {
        for (const std::string value : {"on", "off"}) {
            const auto candidate = "/stream " + value;
            if (candidate.starts_with(input)) result.push_back(candidate);
        }
    }
    return result;
}

CliCommandResult CliCommandProcessor::process(std::string_view input) {
    input = trim(input);
    if (input.empty() || input.front() != '/') return {};
    const auto [command, argument] = split_command(input);

    if (command == "/quit" || command == "/exit") return {true, true, false, {}};
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
    if (command == "/tools") {
        const auto definitions = tools_.definitions();
        if (!argument.empty()) {
            const auto found = std::find_if(
                definitions.begin(), definitions.end(), [&](const auto& tool) {
                    return tool.name == argument;
                });
            if (found == definitions.end()) {
                return {true, false, false, "unknown tool: " + std::string{argument}};
            }
            return {true, false, false,
                    found->name + " - " + found->description + "\nschema: " +
                    found->parameters_schema.dump(2)};
        }
        std::ostringstream output;
        output << "Available tools (" << definitions.size() << "):";
        for (const auto& tool : definitions) {
            output << "\n  " << tool.name << " - " << tool.description;
        }
        return {true, false, false, output.str()};
    }
    if (command == "/mcp") {
        return {true, false, false,
                "MCP servers: 0 configured\n"
                "MCP stdio runtime is planned for M5; /mcp is the status entry point."};
    }
    if (command == "/context") return {true, false, false, format_context()};
    if (command == "/usage") return {true, false, false, format_usage()};
    if (command == "/status") {
        return {true, false, false,
                process("/model").output + "\n" + format_context() + "\n" +
                    format_usage()};
    }
    if (command == "/history") {
        const auto& history = context_.history();
        if (history.empty()) return {true, false, false, "Conversation history is empty."};
        std::ostringstream output;
        output << "Conversation history (" << history.size() << "):";
        for (std::size_t index = 0; index < history.size(); ++index) {
            output << "\n  " << index + 1 << ". " << role_name(history[index].role)
                   << ": " << short_text(history[index].content);
        }
        return {true, false, false, output.str()};
    }
    if (command == "/clear" || command == "/new") {
        context_.clear_history();
        last_usage_ = {};
        total_usage_ = {};
        turn_count_ = 0;
        return {true, false, false, "conversation context cleared"};
    }
    if (command == "/compact") {
        const bool compacted = context_.compact_now();
        return {true, false, false,
                compacted ? "context compacted\n" + format_context()
                          : "nothing to compact\n" + format_context()};
    }
    if (command == "/trace") {
        if (argument.empty()) {
            return {true, false, false,
                    std::string{"trace="} + (config_.trace ? "on" : "off")};
        }
        const auto enabled = parse_toggle(argument);
        if (!enabled) return {true, false, false, "usage: /trace [on|off]"};
        config_.trace = *enabled;
        return {true, false, false,
                std::string{"trace="} + (config_.trace ? "on" : "off")};
    }
    if (command == "/stream") {
        if (argument.empty()) {
            return {true, false, false,
                    std::string{"stream="} + (config_.api.stream ? "on" : "off")};
        }
        const auto enabled = parse_toggle(argument);
        if (!enabled) return {true, false, false, "usage: /stream [on|off]"};
        config_.api.stream = *enabled;
        return {true, false, config_.provider == ProviderKind::ResponsesApi,
                std::string{"stream="} + (config_.api.stream ? "on" : "off")};
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
