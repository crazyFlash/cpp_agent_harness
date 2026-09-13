#include "agent/agent_loop.hpp"

#include <exception>
#include <utility>

namespace agent {

AgentLoop::AgentLoop(IModel& model,
                     ToolRegistry& tools,
                     ContextManager& context,
                     LoopConfig config,
                     EventCallback events)
    : model_(model),
      tools_(tools),
      context_(context),
      config_(config),
      events_(std::move(events)) {}

void AgentLoop::emit(EventType type, std::size_t step, std::string detail) const {
    if (events_) {
        events_(AgentEvent{type, step, std::move(detail)});
    }
}

RunResult AgentLoop::run(std::string user_input) {
    context_.append(Message{Role::User, std::move(user_input), {}, {}, false});
    emit(EventType::LoopStarted, 0);

    std::size_t consecutive_tool_errors = 0;

    for (std::size_t step = 1; step <= config_.max_steps; ++step) {
        if (context_.maybe_compact()) {
            emit(EventType::ContextCompacted, step, context_.summary());
        }

        ModelResponse response;
        try {
            emit(EventType::ModelRequested, step);
            response = model_.generate(
                ModelRequest{context_.working_messages(), tools_.definitions()});
            emit(EventType::ModelResponded, step, response.text);
        } catch (const std::exception& error) {
            const std::string reason = std::string{"model error: "} + error.what();
            emit(EventType::LoopFailed, step, reason);
            return {false, reason, step};
        }

        context_.append(Message{
            Role::Assistant, response.text, response.tool_calls, {}, false});

        if (response.tool_calls.empty()) {
            if (response.final) {
                emit(EventType::LoopFinished, step, response.text);
                return {true, response.text, step};
            }

            const std::string reason = "model returned neither a final answer nor a tool call";
            emit(EventType::LoopFailed, step, reason);
            return {false, reason, step};
        }

        for (const auto& call : response.tool_calls) {
            emit(EventType::ToolStarted, step, call.name);
            const auto result = tools_.execute(call);
            emit(EventType::ToolFinished, step, result.output);

            context_.append(Message{
                Role::Tool, result.output, {}, call.id, false});

            if (result.ok) {
                consecutive_tool_errors = 0;
            } else {
                ++consecutive_tool_errors;
                if (consecutive_tool_errors >= config_.max_consecutive_tool_errors) {
                    const std::string reason = "too many consecutive tool errors";
                    emit(EventType::LoopFailed, step, reason);
                    return {false, reason, step};
                }
            }
        }
    }

    const std::string reason = "agent exceeded the maximum number of steps";
    emit(EventType::LoopFailed, config_.max_steps, reason);
    return {false, reason, config_.max_steps};
}

}  // namespace agent
