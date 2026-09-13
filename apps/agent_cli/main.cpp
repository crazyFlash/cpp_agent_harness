#include "agent/agent_loop.hpp"
#include "agent/cli_commands.hpp"
#include "agent/config.hpp"
#include "agent/curl_cli_transport.hpp"
#include "agent/openai/responses_model.hpp"
#include "agent/skill_registry.hpp"
#include "agent/tools/calculator_tool.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

namespace {

class DemoModel final : public agent::IModel {
public:
    agent::ModelResponse generate(
        const agent::ModelRequest& request,
        const TextDeltaCallback& = {}) override {
        const auto& messages = request.messages;
        if (messages.empty()) {
            return {"No input was provided.", {}, true, {}};
        }

        const auto& last = messages.back();
        if (last.role == agent::Role::Tool) {
            return {"The tool returned: " + last.content, {}, true, {}};
        }

        if (last.role != agent::Role::User) {
            return {"Waiting for user input.", {}, true, {}};
        }

        constexpr std::string_view prefix = "calc ";
        if (last.content.starts_with(prefix)) {
            agent::ToolCall call;
            call.id = "demo-call-1";
            call.name = "calculator";
            call.arguments = {{"expression", last.content.substr(prefix.size())}};
            return {"I'll calculate that.", {std::move(call)}, false, {}};
        }

        return {
            "Demo model echo: " + last.content +
                "\nTip: enter `calc 21 * 2` to exercise the agent tool loop.",
            {},
            true,
            {},
        };
    }
};

const char* event_name(agent::EventType type) {
    switch (type) {
        case agent::EventType::LoopStarted: return "loop.started";
        case agent::EventType::ModelRequested: return "model.requested";
        case agent::EventType::ModelTextDelta: return "model.text.delta";
        case agent::EventType::ModelResponded: return "model.responded";
        case agent::EventType::ToolStarted: return "tool.started";
        case agent::EventType::ToolFinished: return "tool.finished";
        case agent::EventType::ContextCompacted: return "context.compacted";
        case agent::EventType::LoopFinished: return "loop.finished";
        case agent::EventType::LoopFailed: return "loop.failed";
    }
    return "unknown";
}

struct CliOptions {
    std::optional<std::filesystem::path> config_path;
    bool trace{false};
    bool trace_set{false};
    bool check_config{false};
    bool init_config{false};
    bool demo{false};
    bool help{false};
};

struct StartupConfiguration {
    agent::AppConfig config;
    std::optional<std::string> transient_api_key;
};

constexpr const char* default_config_path = "config/agent.local.json";

CliOptions parse_options(int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("--config requires a file path");
            }
            options.config_path = argv[++index];
        } else if (argument == "--trace") {
            options.trace = true;
            options.trace_set = true;
        } else if (argument == "--no-trace") {
            options.trace = false;
            options.trace_set = true;
        } else if (argument == "--check-config") {
            options.check_config = true;
        } else if (argument == "--init-config") {
            options.init_config = true;
        } else if (argument == "--demo") {
            options.demo = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }
    return options;
}

void print_help(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n\n"
        << "Options:\n"
        << "  --config PATH   Load a JSON configuration file.\n"
        << "  --init-config   Run the interactive API setup wizard.\n"
        << "  --check-config  Validate configuration and exit.\n"
        << "  --demo          Explicitly start the offline demo provider.\n"
        << "  --trace         Enable agent event tracing.\n"
        << "  --no-trace      Disable tracing configured elsewhere.\n"
        << "  -h, --help      Show this help.\n\n"
        << "CPP_AGENT_CONFIG can provide the default configuration path.\n"
        << "Without configuration, an interactive terminal starts API setup.\n";
}

bool stdin_is_interactive() {
#ifndef _WIN32
    return ::isatty(STDIN_FILENO) == 1;
#else
    return true;
#endif
}

std::string prompt_line(const std::string& label,
                        const std::string& default_value = {}) {
    std::cout << label;
    if (!default_value.empty()) {
        std::cout << " [" << default_value << ']';
    }
    std::cout << ": " << std::flush;

    std::string value;
    if (!std::getline(std::cin, value)) {
        throw std::runtime_error("configuration input was cancelled");
    }
    return value.empty() ? default_value : value;
}

bool prompt_yes_no(const std::string& label, bool default_value) {
    while (true) {
        std::cout << label << (default_value ? " [Y/n]: " : " [y/N]: ")
                  << std::flush;
        std::string value;
        if (!std::getline(std::cin, value)) {
            throw std::runtime_error("configuration input was cancelled");
        }
        std::transform(
            value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        if (value.empty()) {
            return default_value;
        }
        if (value == "y" || value == "yes") {
            return true;
        }
        if (value == "n" || value == "no") {
            return false;
        }
        std::cout << "Please enter y or n.\n";
    }
}

std::string prompt_secret(const std::string& label) {
    std::cout << label << ": " << std::flush;
#ifndef _WIN32
    termios original{};
    const bool changed = ::tcgetattr(STDIN_FILENO, &original) == 0;
    if (changed) {
        auto hidden = original;
        hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);
    }
#endif

    std::string value;
    const bool read = static_cast<bool>(std::getline(std::cin, value));

#ifndef _WIN32
    if (changed) {
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        std::cout << '\n';
    }
#endif
    if (!read) {
        throw std::runtime_error("configuration input was cancelled");
    }
    return value;
}

StartupConfiguration run_configuration_wizard() {
    StartupConfiguration startup;
    startup.config.provider = agent::ProviderKind::ResponsesApi;

    std::cout << "No API configuration was found.\n"
              << "Configure a Responses API endpoint (OpenAI or compatible local server).\n";
    startup.config.api.base_url = prompt_line(
        "API base URL", startup.config.api.base_url);
    while (startup.config.api.model.empty()) {
        startup.config.api.model = prompt_line("Model");
        if (startup.config.api.model.empty()) {
            std::cout << "Model is required.\n";
        }
    }
    startup.config.api.require_api_key =
        prompt_yes_no("Does this endpoint require an API key?", true);
    if (startup.config.api.require_api_key) {
        startup.config.api.api_key_env = prompt_line(
            "API key environment variable", startup.config.api.api_key_env);
        const auto configured_key =
            agent::process_environment(startup.config.api.api_key_env);
        if (!configured_key || configured_key->empty()) {
            auto key = prompt_secret("API key (used for this run only)");
            if (key.empty()) {
                throw std::runtime_error("API key cannot be empty");
            }
            startup.transient_api_key = std::move(key);
        }
    } else {
        startup.config.api.api_key_env.clear();
    }

    agent::ConfigLoader::validate(startup.config);
    if (prompt_yes_no(
            std::string{"Save non-secret settings to "} + default_config_path + '?',
            true)) {
        agent::ConfigLoader::save_file(startup.config, default_config_path);
        std::cout << "Saved configuration to " << default_config_path
                  << " (API key was not saved).\n";
    }
    return startup;
}

StartupConfiguration load_config(const CliOptions& options) {
    if (options.demo && (options.config_path || options.init_config)) {
        throw std::invalid_argument(
            "--demo cannot be combined with --config or --init-config");
    }
    if (options.demo) {
        StartupConfiguration startup;
        startup.config.provider = agent::ProviderKind::Demo;
        startup.config.trace = options.trace_set && options.trace;
        return startup;
    }

    std::optional<std::filesystem::path> path = options.config_path;
    if (!path) {
        if (const auto configured = agent::process_environment("CPP_AGENT_CONFIG")) {
            path = *configured;
        }
    }

    if (!path && !options.init_config &&
        std::filesystem::exists(default_config_path)) {
        path = default_config_path;
    }

    StartupConfiguration startup;
    if (path) {
        startup.config = agent::ConfigLoader::load_file(*path);
    } else if (agent::process_environment("CPP_AGENT_PROVIDER")) {
        // Environment-only startup is useful for containers and CI.
    } else if (options.check_config) {
        throw std::runtime_error(
            "no configuration found; provide --config or create " +
            std::string{default_config_path});
    } else if (options.init_config || stdin_is_interactive()) {
        startup = run_configuration_wizard();
    } else {
        throw std::runtime_error(
            "no API configuration found in a non-interactive session; provide "
            "--config, set CPP_AGENT_CONFIG, or use --demo");
    }
    agent::ConfigLoader::apply_environment(
        startup.config, agent::process_environment);
    if (options.trace_set) {
        startup.config.trace = options.trace;
    }
    agent::ConfigLoader::validate(startup.config);
    return startup;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.help) {
            print_help(argv[0]);
            return 0;
        }
        auto startup = load_config(options);
        auto& config = startup.config;
        if (options.check_config) {
            std::cout << "configuration ok: provider="
                      << agent::ConfigLoader::provider_name(config.provider) << '\n';
            return 0;
        }

        agent::ToolRegistry tools;
        tools.add(std::make_unique<agent::CalculatorTool>());

        agent::SkillRegistry skills;
        const auto skill_count = skills.load_directory(config.skills_directory);

        agent::ContextManager context(config.context);
        context.add_instruction(
            agent::Role::System,
            "You are an agent running in cpp_agent_harness. Use tools when needed.");

        std::unique_ptr<agent::IHttpTransport> transport;
        std::unique_ptr<agent::IModel> model;
        if (config.provider == agent::ProviderKind::ResponsesApi) {
            transport = std::make_unique<agent::CurlCliTransport>();
        } else if (config.provider == agent::ProviderKind::Local) {
            std::cerr
                << "error: local model protocol '" << config.local.protocol
                << "' is reserved but not implemented; use responses_api for a "
                   "Responses-compatible local server\n";
            return 2;
        }

        const auto rebuild_model = [&]() {
            if (config.provider == agent::ProviderKind::Demo) {
                model = std::make_unique<DemoModel>();
                return;
            }
            agent::openai::ResponsesConfig responses;
            responses.endpoint = agent::ConfigLoader::responses_endpoint(config.api);
            responses.api_key = startup.transient_api_key
                ? *startup.transient_api_key
                : agent::ConfigLoader::resolve_api_key(
                      config, agent::process_environment);
            responses.model = config.api.model;
            responses.timeout = config.api.timeout;
            responses.store = config.api.store;
            responses.require_api_key = config.api.require_api_key;
            responses.stream = config.api.stream;
            model = std::make_unique<agent::openai::ResponsesModel>(
                *transport, std::move(responses));
        };
        rebuild_model();
        agent::CliCommandProcessor commands(config, skills);

        std::cout << "cpp_agent_harness (provider="
                  << agent::ConfigLoader::provider_name(config.provider) << ", "
                  << skill_count << " skill(s) loaded)\n"
                  << "Type /help for commands, /quit to exit.\n";

        std::string input;
        while (std::cout << "> " && std::getline(std::cin, input)) {
            if (input == "quit" || input == "exit") break;
            const auto command = commands.process(input);
            if (command.handled) {
                if (!command.output.empty()) std::cout << command.output << '\n';
                if (command.exit_requested) break;
                if (command.model_changed) rebuild_model();
                continue;
            }

            bool streamed = false;
            std::size_t stream_step = 0;
            agent::AgentLoop loop(
                *model, tools, context, config.loop,
                [&](const agent::AgentEvent& event) {
                    if (event.type == agent::EventType::ModelTextDelta) {
                        if (!streamed || stream_step != event.step) {
                            if (streamed) std::cout << '\n';
                            std::cout << "assistant: ";
                            streamed = true;
                            stream_step = event.step;
                        }
                        std::cout << event.detail << std::flush;
                    }
                    if (config.trace) {
                        std::cerr << "[trace] " << event_name(event.type)
                                  << " step=" << event.step;
                        if (event.type != agent::EventType::ModelTextDelta &&
                            !event.detail.empty()) {
                            std::cerr << " detail=" << event.detail;
                        }
                        std::cerr << '\n';
                    }
                });
            const auto result = loop.run(input);
            if (streamed) std::cout << '\n';
            if (!result.ok) {
                std::cout << "error: " << result.output << '\n';
            } else if (!streamed) {
                std::cout << "assistant: " << result.output << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
