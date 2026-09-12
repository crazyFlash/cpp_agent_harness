#pragma once

#include "agent/tool.hpp"

namespace agent {

class CalculatorTool final : public ITool {
public:
    ToolDefinition definition() const override;
    ToolResult execute(const ToolCall& call) override;
};

}  // namespace agent

