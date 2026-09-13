#pragma once

#include "agent/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace agent {

class ITool {
public:
    virtual ~ITool() = default;
    virtual ToolDefinition definition() const = 0;
    virtual ToolResult execute(const ToolCall& call) = 0;
};

class ToolRegistry {
public:
    void add(std::unique_ptr<ITool> tool);
    bool contains(const std::string& name) const;
    std::vector<ToolDefinition> definitions() const;
    ToolResult execute(const ToolCall& call) const;

private:
    std::map<std::string, std::unique_ptr<ITool>> tools_;
};

}  // namespace agent
