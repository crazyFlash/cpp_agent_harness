#include "agent/agent_loop.hpp"
#include "agent/config.hpp"
#include "agent/curl_cli_transport.hpp"
#include "agent/openai/responses_model.hpp"
#include "agent/skill_registry.hpp"
#include "agent/tools/calculator_tool.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

class DemoModel final : public agent::IModel {
public:
    agent::ModelResponse generate(const agent::ModelRequest& request) override {
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
    bool help{false};
};

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
        << "  --check-config  Validate configuration and exit.\n"
        << "  --trace         Enable agent event tracing.\n"
        << "  --no-trace      Disable tracing configured elsewhere.\n"
        << "  -h, --help      Show this help.\n\n"
        << "CPP_AGENT_CONFIG can provide the default configuration path.\n";
}

agent::AppConfig load_config(const CliOptions& options) {
    std::optional<std::filesystem::path> path = options.config_path;
    if (!path) {
        if (const auto configured = agent::process_environment("CPP_AGENT_CONFIG")) {
            path = *configured;
        }
    }

    agent::AppConfig config;
    if (path) {
        config = agent::ConfigLoader::load_file(*path);
    }
    agent::ConfigLoader::apply_environment(config, agent::process_environment);
    if (options.trace_set) {
        config.trace = options.trace;
    }
    agent::ConfigLoader::validate(config);
    return config;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.help) {
            print_help(argv[0]);
            return 0;
        }
        const auto config = load_config(options);
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
        if (config.provider == agent::ProviderKind::Demo) {
            model = std::make_unique<DemoModel>();
        } else if (config.provider == agent::ProviderKind::ResponsesApi) {
            transport = std::make_unique<agent::CurlCliTransport>();
            agent::openai::ResponsesConfig responses;
            responses.endpoint = agent::ConfigLoader::responses_endpoint(config.api);
            responses.api_key = agent::ConfigLoader::resolve_api_key(
                config, agent::process_environment);
            responses.model = config.api.model;
            responses.timeout = config.api.timeout;
            responses.store = config.api.store;
            responses.require_api_key = config.api.require_api_key;
            model = std::make_unique<agent::openai::ResponsesModel>(
                *transport, std::move(responses));
        } else {
            std::cerr
                << "error: local model protocol '" << config.local.protocol
                << "' is reserved but not implemented; use responses_api for a "
                   "Responses-compatible local server\n";
            return 2;
        }

        const bool trace = config.trace;
        agent::AgentLoop loop(
            *model,
            tools,
            context,
            config.loop,
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

        std::cout << "cpp_agent_harness (provider="
                  << agent::ConfigLoader::provider_name(config.provider) << ", "
                  << skill_count << " skill(s) loaded)\n"
                  << "Type 'quit' to exit.\n";

        std::string input;
        while (std::cout << "> " && std::getline(std::cin, input)) {
            if (input == "quit" || input == "exit") {
                break;
            }
            const auto result = loop.run(input);
            std::cout << (result.ok ? "assistant: " : "error: ")
                      << result.output << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
