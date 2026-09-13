#include "agent/config.hpp"

#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace agent {

namespace {

ProviderKind parse_provider(const std::string& value) {
    if (value == "demo") {
        return ProviderKind::Demo;
    }
    if (value == "responses_api") {
        return ProviderKind::ResponsesApi;
    }
    if (value == "local") {
        return ProviderKind::Local;
    }
    throw std::invalid_argument(
        "unknown provider '" + value +
        "'; expected demo, responses_api, or local");
}

std::size_t parse_size(const Json& value, const std::string& field) {
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        throw std::invalid_argument(field + " must be an integer");
    }
    const auto number = value.get<long long>();
    if (number < 0) {
        throw std::invalid_argument(field + " cannot be negative");
    }
    return static_cast<std::size_t>(number);
}

long long parse_environment_integer(const std::string& value,
                                    const std::string& name) {
    std::size_t consumed = 0;
    long long result = 0;
    try {
        result = std::stoll(value, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(name + " must be an integer");
    }
    if (consumed != value.size()) {
        throw std::invalid_argument(name + " must be an integer");
    }
    return result;
}

bool parse_environment_bool(const std::string& value, const std::string& name) {
    if (value == "1" || value == "true" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no") {
        return false;
    }
    throw std::invalid_argument(name + " must be true/false or 1/0");
}

void reject_unknown_fields(const Json& object,
                           const std::initializer_list<const char*> allowed,
                           const std::string& section) {
    if (!object.is_object()) {
        throw std::invalid_argument(section + " must be a JSON object");
    }
    for (const auto& [key, value] : object.items()) {
        (void)value;
        bool found = false;
        for (const auto* candidate : allowed) {
            if (key == candidate) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::invalid_argument(
                "unknown configuration field: " + section + '.' + key);
        }
    }
}

void parse_api(AppConfig& config, const Json& api) {
    reject_unknown_fields(
        api,
        {"base_url", "model", "api_key_env", "require_api_key", "timeout_ms", "store"},
        "api");
    if (api.contains("base_url")) {
        config.api.base_url = api.at("base_url").get<std::string>();
    }
    if (api.contains("model")) {
        config.api.model = api.at("model").get<std::string>();
    }
    if (api.contains("api_key_env")) {
        config.api.api_key_env = api.at("api_key_env").get<std::string>();
    }
    if (api.contains("require_api_key")) {
        config.api.require_api_key = api.at("require_api_key").get<bool>();
    }
    if (api.contains("timeout_ms")) {
        config.api.timeout = std::chrono::milliseconds(
            parse_size(api.at("timeout_ms"), "api.timeout_ms"));
    }
    if (api.contains("store")) {
        config.api.store = api.at("store").get<bool>();
    }
}

void parse_local(AppConfig& config, const Json& local) {
    reject_unknown_fields(local, {"protocol", "endpoint", "model", "options"}, "local");
    if (local.contains("protocol")) {
        config.local.protocol = local.at("protocol").get<std::string>();
    }
    if (local.contains("endpoint")) {
        config.local.endpoint = local.at("endpoint").get<std::string>();
    }
    if (local.contains("model")) {
        config.local.model = local.at("model").get<std::string>();
    }
    if (local.contains("options")) {
        config.local.options = local.at("options");
    }
}

void parse_context(AppConfig& config, const Json& context) {
    reject_unknown_fields(
        context,
        {"max_estimated_tokens", "compact_at_ratio", "keep_recent_messages"},
        "context");
    if (context.contains("max_estimated_tokens")) {
        config.context.max_estimated_tokens = parse_size(
            context.at("max_estimated_tokens"), "context.max_estimated_tokens");
    }
    if (context.contains("compact_at_ratio")) {
        config.context.compact_at_ratio =
            context.at("compact_at_ratio").get<double>();
    }
    if (context.contains("keep_recent_messages")) {
        config.context.keep_recent_messages = parse_size(
            context.at("keep_recent_messages"), "context.keep_recent_messages");
    }
}

void parse_loop(AppConfig& config, const Json& loop) {
    reject_unknown_fields(
        loop, {"max_steps", "max_consecutive_tool_errors"}, "loop");
    if (loop.contains("max_steps")) {
        config.loop.max_steps = parse_size(loop.at("max_steps"), "loop.max_steps");
    }
    if (loop.contains("max_consecutive_tool_errors")) {
        config.loop.max_consecutive_tool_errors = parse_size(
            loop.at("max_consecutive_tool_errors"),
            "loop.max_consecutive_tool_errors");
    }
}

}  // namespace

AppConfig ConfigLoader::load_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open configuration file: " + path.string());
    }

    Json document;
    try {
        input >> document;
    } catch (const Json::exception& error) {
        throw std::runtime_error(
            "invalid configuration file '" + path.string() + "': " + error.what());
    }

    reject_unknown_fields(
        document,
        {"provider", "trace", "skills_directory", "api", "local", "context", "loop"},
        "root");

    AppConfig config;
    if (document.contains("provider")) {
        config.provider = parse_provider(document.at("provider").get<std::string>());
    }
    if (document.contains("trace")) {
        config.trace = document.at("trace").get<bool>();
    }
    if (document.contains("skills_directory")) {
        config.skills_directory =
            document.at("skills_directory").get<std::string>();
    }
    if (document.contains("api")) {
        parse_api(config, document.at("api"));
    }
    if (document.contains("local")) {
        parse_local(config, document.at("local"));
    }
    if (document.contains("context")) {
        parse_context(config, document.at("context"));
    }
    if (document.contains("loop")) {
        parse_loop(config, document.at("loop"));
    }
    return config;
}

void ConfigLoader::save_file(const AppConfig& config,
                             const std::filesystem::path& path) {
    validate(config);
    Json document = {
        {"provider", provider_name(config.provider)},
        {"trace", config.trace},
        {"skills_directory", config.skills_directory.string()},
        {"api",
         {
             {"base_url", config.api.base_url},
             {"model", config.api.model},
             {"api_key_env", config.api.api_key_env},
             {"require_api_key", config.api.require_api_key},
             {"timeout_ms", config.api.timeout.count()},
             {"store", config.api.store},
         }},
        {"local",
         {
             {"protocol", config.local.protocol},
             {"endpoint", config.local.endpoint},
             {"model", config.local.model},
             {"options", config.local.options},
         }},
        {"context",
         {
             {"max_estimated_tokens", config.context.max_estimated_tokens},
             {"compact_at_ratio", config.context.compact_at_ratio},
             {"keep_recent_messages", config.context.keep_recent_messages},
         }},
        {"loop",
         {
             {"max_steps", config.loop.max_steps},
             {"max_consecutive_tool_errors",
              config.loop.max_consecutive_tool_errors},
         }},
    };

    if (!path.parent_path().empty()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            throw std::runtime_error(
                "cannot create configuration directory: " + error.message());
        }
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write configuration file: " + path.string());
    }
    output << document.dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("cannot finish configuration file: " + path.string());
    }
}

void ConfigLoader::apply_environment(AppConfig& config,
                                     const EnvironmentReader& environment) {
    if (const auto value = environment("CPP_AGENT_PROVIDER")) {
        config.provider = parse_provider(*value);
    }
    if (const auto value = environment("CPP_AGENT_API_BASE_URL")) {
        config.api.base_url = *value;
    }
    if (const auto value = environment("CPP_AGENT_MODEL")) {
        config.api.model = *value;
    }
    if (const auto value = environment("CPP_AGENT_API_KEY_ENV")) {
        config.api.api_key_env = *value;
    }
    if (const auto value = environment("CPP_AGENT_API_REQUIRE_KEY")) {
        config.api.require_api_key =
            parse_environment_bool(*value, "CPP_AGENT_API_REQUIRE_KEY");
    }
    if (const auto value = environment("CPP_AGENT_API_TIMEOUT_MS")) {
        const auto number =
            parse_environment_integer(*value, "CPP_AGENT_API_TIMEOUT_MS");
        if (number < 0) {
            throw std::invalid_argument("CPP_AGENT_API_TIMEOUT_MS cannot be negative");
        }
        config.api.timeout = std::chrono::milliseconds(number);
    }
    if (const auto value = environment("CPP_AGENT_TRACE")) {
        config.trace = parse_environment_bool(*value, "CPP_AGENT_TRACE");
    }
    validate(config);
}

void ConfigLoader::validate(const AppConfig& config) {
    if (config.skills_directory.empty()) {
        throw std::invalid_argument("skills_directory cannot be empty");
    }
    if (config.context.max_estimated_tokens == 0) {
        throw std::invalid_argument("context.max_estimated_tokens must be greater than zero");
    }
    if (config.context.compact_at_ratio <= 0.0 ||
        config.context.compact_at_ratio > 1.0) {
        throw std::invalid_argument("context.compact_at_ratio must be in (0, 1]");
    }
    if (config.loop.max_steps == 0) {
        throw std::invalid_argument("loop.max_steps must be greater than zero");
    }
    if (config.loop.max_consecutive_tool_errors == 0) {
        throw std::invalid_argument(
            "loop.max_consecutive_tool_errors must be greater than zero");
    }

    if (config.provider == ProviderKind::ResponsesApi) {
        if (config.api.model.empty()) {
            throw std::invalid_argument("api.model is required for responses_api");
        }
        if (!config.api.base_url.starts_with("https://") &&
            !config.api.base_url.starts_with("http://")) {
            throw std::invalid_argument("api.base_url must use http or https");
        }
        if (config.api.timeout.count() <= 0) {
            throw std::invalid_argument("api.timeout_ms must be greater than zero");
        }
        if (config.api.require_api_key && config.api.api_key_env.empty()) {
            throw std::invalid_argument(
                "api.api_key_env is required when require_api_key is true");
        }
    }

    if (config.provider == ProviderKind::Local && config.local.protocol.empty()) {
        throw std::invalid_argument("local.protocol cannot be empty");
    }
}

std::string ConfigLoader::resolve_api_key(
    const AppConfig& config,
    const EnvironmentReader& environment) {
    if (const auto direct = environment("CPP_AGENT_API_KEY")) {
        return *direct;
    }
    if (!config.api.api_key_env.empty()) {
        if (const auto configured = environment(config.api.api_key_env)) {
            return *configured;
        }
    }
    if (config.api.require_api_key) {
        throw std::runtime_error(
            "API key is missing; set CPP_AGENT_API_KEY or " +
            config.api.api_key_env);
    }
    return {};
}

std::string ConfigLoader::responses_endpoint(const ApiConfig& config) {
    std::string endpoint = config.base_url;
    while (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
    }
    if (!endpoint.ends_with("/responses")) {
        endpoint += "/responses";
    }
    return endpoint;
}

std::string ConfigLoader::provider_name(ProviderKind provider) {
    switch (provider) {
        case ProviderKind::Demo: return "demo";
        case ProviderKind::ResponsesApi: return "responses_api";
        case ProviderKind::Local: return "local";
    }
    return "unknown";
}

std::optional<std::string> process_environment(const std::string& name) {
    if (const char* value = std::getenv(name.c_str())) {
        return std::string{value};
    }
    return std::nullopt;
}

}  // namespace agent
