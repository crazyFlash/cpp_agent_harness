#pragma once

#include "agent/agent_loop.hpp"
#include "agent/context_manager.hpp"
#include "agent/types.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace agent {

enum class ProviderKind {
    Demo,
    ResponsesApi,
    Local,
};

struct ApiConfig {
    std::string base_url{"https://api.openai.com/v1"};
    std::string model;
    std::string api_key_env{"OPENAI_API_KEY"};
    bool require_api_key{true};
    std::chrono::milliseconds timeout{60000};
    bool store{false};
    bool stream{true};
};

struct LocalModelConfig {
    std::string protocol{"reserved"};
    std::string endpoint;
    std::string model;
    Json options{Json::object()};
};

struct AppConfig {
    ProviderKind provider{ProviderKind::Demo};
    bool trace{false};
    std::filesystem::path skills_directory{"skills"};
    ApiConfig api;
    LocalModelConfig local;
    ContextConfig context;
    LoopConfig loop;
};

using EnvironmentReader =
    std::function<std::optional<std::string>(const std::string&)>;

class ConfigLoader {
public:
    static AppConfig load_file(const std::filesystem::path& path);
    static void save_file(const AppConfig& config,
                          const std::filesystem::path& path);
    static void apply_environment(AppConfig& config,
                                  const EnvironmentReader& environment);
    static void validate(const AppConfig& config);

    static std::string resolve_api_key(const AppConfig& config,
                                       const EnvironmentReader& environment);
    static std::string responses_endpoint(const ApiConfig& config);
    static std::string provider_name(ProviderKind provider);
};

std::optional<std::string> process_environment(const std::string& name);

}  // namespace agent
