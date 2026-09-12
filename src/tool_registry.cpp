#include "agent/tool.hpp"

#include <stdexcept>
#include <utility>

namespace agent {

void ToolRegistry::add(std::unique_ptr<ITool> tool) {
    if (!tool) {
        throw std::invalid_argument("cannot register a null tool");
    }
    auto name = tool->definition().name;
    if (name.empty()) {
        throw std::invalid_argument("tool name cannot be empty");
    }
    if (tools_.contains(name)) {
        throw std::invalid_argument("duplicate tool: " + name);
    }
    tools_.emplace(std::move(name), std::move(tool));
}

bool ToolRegistry::contains(const std::string& name) const {
    return tools_.contains(name);
}

std::vector<ToolDefinition> ToolRegistry::definitions() const {
    std::vector<ToolDefinition> result;
    result.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        (void)name;
        result.push_back(tool->definition());
    }
    return result;
}

ToolResult ToolRegistry::execute(const ToolCall& call) const {
    const auto iterator = tools_.find(call.name);
    if (iterator == tools_.end()) {
        return {false, "unknown tool: " + call.name};
    }

    try {
        return iterator->second->execute(call);
    } catch (const std::exception& error) {
        return {false, std::string{"tool threw an exception: "} + error.what()};
    }
}

}  // namespace agent

