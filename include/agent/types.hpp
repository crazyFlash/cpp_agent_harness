#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace agent {

using Json = nlohmann::json;

enum class Role {
    System,
    Developer,
    User,
    Assistant,
    Tool,
};

struct ToolCall {
    std::string id;
    std::string name;
    Json arguments{Json::object()};
};

struct ToolDefinition {
    std::string name;
    std::string description;
    Json parameters_schema{Json::object()};
};

struct Message {
    Role role{Role::User};
    std::string content;
    std::vector<ToolCall> tool_calls;
    std::string tool_call_id;
    bool pinned{false};
};

struct TokenUsage {
    std::size_t input_tokens{0};
    std::size_t output_tokens{0};
    std::size_t total_tokens{0};
    std::size_t cached_tokens{0};
    std::size_t reasoning_tokens{0};
    bool reported{false};

    TokenUsage& operator+=(const TokenUsage& other) {
        input_tokens += other.input_tokens;
        output_tokens += other.output_tokens;
        total_tokens += other.total_tokens;
        cached_tokens += other.cached_tokens;
        reasoning_tokens += other.reasoning_tokens;
        reported = reported || other.reported;
        return *this;
    }
};

struct ModelResponse {
    std::string text;
    std::vector<ToolCall> tool_calls;
    bool final{true};
    std::string response_id;
    TokenUsage usage;
};

struct ModelRequest {
    std::vector<Message> messages;
    std::vector<ToolDefinition> tools;
};

struct ToolResult {
    bool ok{false};
    std::string output;
};

enum class EventType {
    LoopStarted,
    ModelRequested,
    ModelTextDelta,
    ModelResponded,
    ToolStarted,
    ToolFinished,
    ContextCompacted,
    LoopFinished,
    LoopFailed,
};

struct AgentEvent {
    EventType type;
    std::size_t step{0};
    std::string detail;
};

inline std::string role_name(Role role) {
    switch (role) {
        case Role::System: return "system";
        case Role::Developer: return "developer";
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool: return "tool";
    }
    return "unknown";
}

}  // namespace agent
