#pragma once

#include "agent/context_manager.hpp"
#include "agent/model.hpp"
#include "agent/tool.hpp"
#include "agent/types.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace agent {

struct LoopConfig {
    std::size_t max_steps{16};
    std::size_t max_consecutive_tool_errors{3};
};

struct RunResult {
    bool ok{false};
    std::string output;
    std::size_t steps{0};
    TokenUsage usage;
};

using EventCallback = std::function<void(const AgentEvent&)>;

class AgentLoop {
public:
    AgentLoop(IModel& model,
              ToolRegistry& tools,
              ContextManager& context,
              LoopConfig config = {},
              EventCallback events = {});

    RunResult run(std::string user_input);

private:
    void emit(EventType type, std::size_t step, std::string detail = {}) const;

    IModel& model_;
    ToolRegistry& tools_;
    ContextManager& context_;
    LoopConfig config_;
    EventCallback events_;
};

}  // namespace agent
