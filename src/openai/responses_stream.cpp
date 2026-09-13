#include "agent/openai/responses_stream.hpp"

#include "agent/openai/responses_model.hpp"

#include <stdexcept>

namespace agent::openai {

ResponsesStreamAssembler::PendingCall& ResponsesStreamAssembler::pending_call(
    const std::string& item_id) {
    const auto existing = call_indices_.find(item_id);
    if (existing != call_indices_.end()) {
        return calls_[existing->second];
    }
    const auto index = calls_.size();
    call_indices_.emplace(item_id, index);
    calls_.push_back(PendingCall{});
    return calls_.back();
}

void ResponsesStreamAssembler::consume_item(const Json& item) {
    if (item.value("type", std::string{}) != "function_call") {
        return;
    }
    const auto item_id = item.value("id", std::string{});
    if (item_id.empty()) {
        throw std::runtime_error("streamed function call is missing item id");
    }
    auto& call = pending_call(item_id);
    call.call_id = item.value("call_id", call.call_id);
    call.name = item.value("name", call.name);
    call.arguments = item.value("arguments", call.arguments);
}

void ResponsesStreamAssembler::consume(const SseEvent& event) {
    if (event.data.empty() || event.data == "[DONE]") {
        return;
    }

    Json payload;
    try {
        payload = Json::parse(event.data);
    } catch (const Json::parse_error& error) {
        throw std::runtime_error(
            std::string{"invalid JSON in Responses API stream: "} + error.what());
    }

    const auto type = payload.value("type", event.event);
    if (type == "response.output_text.delta") {
        text_ += payload.value("delta", std::string{});
    } else if (type == "response.output_item.added" ||
               type == "response.output_item.done") {
        if (payload.contains("item")) {
            consume_item(payload["item"]);
        }
    } else if (type == "response.function_call_arguments.delta") {
        const auto item_id = payload.value("item_id", std::string{});
        if (item_id.empty()) {
            throw std::runtime_error("function argument delta is missing item_id");
        }
        pending_call(item_id).arguments += payload.value("delta", std::string{});
    } else if (type == "response.function_call_arguments.done") {
        const auto item_id = payload.value("item_id", std::string{});
        if (item_id.empty()) {
            throw std::runtime_error("function argument completion is missing item_id");
        }
        auto& call = pending_call(item_id);
        call.arguments = payload.value("arguments", call.arguments);
    } else if (type == "response.created") {
        if (payload.contains("response")) {
            response_id_ = payload["response"].value("id", std::string{});
        }
    } else if (type == "response.completed") {
        if (!payload.contains("response")) {
            throw std::runtime_error("response.completed is missing response payload");
        }
        terminal_response_ = ResponsesCodec::decode_response(payload["response"]);
        saw_terminal_response_ = true;
        completed_ = true;
    } else if (type == "response.failed" || type == "error") {
        throw std::runtime_error("Responses API stream reported failure: " + payload.dump());
    }
}

ModelResponse ResponsesStreamAssembler::result() const {
    if (saw_terminal_response_) {
        return terminal_response_;
    }

    ModelResponse response;
    response.text = text_;
    response.response_id = response_id_;
    for (const auto& pending : calls_) {
        if (pending.call_id.empty() || pending.name.empty()) {
            throw std::runtime_error("stream ended with an incomplete function call");
        }

        ToolCall call;
        call.id = pending.call_id;
        call.name = pending.name;
        try {
            call.arguments = Json::parse(
                pending.arguments.empty() ? "{}" : pending.arguments);
        } catch (const Json::parse_error& error) {
            throw std::runtime_error(
                std::string{"invalid streamed function-call arguments: "} + error.what());
        }
        response.tool_calls.push_back(std::move(call));
    }
    response.final = completed_ && response.tool_calls.empty();
    return response;
}

bool ResponsesStreamAssembler::completed() const {
    return completed_;
}

}  // namespace agent::openai
