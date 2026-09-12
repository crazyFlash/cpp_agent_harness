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

struct ModelResponse {
    std::string text;
    std::vector<ToolCall> tool_calls;
    bool final{true};
    std::string response_id;
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
