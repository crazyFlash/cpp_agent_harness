#include "agent/openai/responses_model.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace agent::openai {

namespace {

void append_instruction(std::ostringstream& output, const Message& message) {
    if (output.tellp() > 0) {
        output << "\n\n";
    }
    output << '[' << role_name(message.role) << "]\n" << message.content;
}

Json message_to_input(const Message& message) {
    switch (message.role) {
        case Role::User:
            return {{"role", "user"}, {"content", message.content}};
        case Role::Assistant:
            return {{"role", "assistant"}, {"content", message.content}};
        case Role::Tool:
            if (message.tool_call_id.empty()) {
                throw std::invalid_argument("tool message is missing tool_call_id");
            }
            return {
                {"type", "function_call_output"},
                {"call_id", message.tool_call_id},
                {"output", message.content},
            };
        case Role::System:
        case Role::Developer:
            break;
    }
    throw std::invalid_argument("instruction message cannot be encoded as input");
}

ToolCall decode_function_call(const Json& item) {
    ToolCall call;
    call.id = item.value("call_id", item.value("id", std::string{}));
    call.name = item.value("name", std::string{});

    const auto encoded_arguments = item.value("arguments", std::string{"{}"});
    try {
        call.arguments = Json::parse(encoded_arguments);
    } catch (const Json::parse_error& error) {
        throw std::runtime_error(
            std::string{"invalid function-call arguments from model: "} + error.what());
    }

    if (call.id.empty() || call.name.empty() || !call.arguments.is_object()) {
        throw std::runtime_error("incomplete function call in model response");
    }
    return call;
}

}  // namespace

Json ResponsesCodec::encode_request(const ModelRequest& request,
                                    const ResponsesConfig& config,
                                    bool stream) {
    if (config.model.empty()) {
        throw std::invalid_argument("Responses API model cannot be empty");
    }

    Json body = {
        {"model", config.model},
        {"store", config.store},
        {"stream", stream},
        {"input", Json::array()},
        {"tools", Json::array()},
    };

    std::ostringstream instructions;
    for (const auto& message : request.messages) {
        if (message.role == Role::System || message.role == Role::Developer) {
            append_instruction(instructions, message);
            continue;
        }

        if (message.role == Role::Assistant && !message.tool_calls.empty()) {
            if (!message.content.empty()) {
                body["input"].push_back(message_to_input(message));
            }
            for (const auto& call : message.tool_calls) {
                body["input"].push_back({
                    {"type", "function_call"},
                    {"call_id", call.id},
                    {"name", call.name},
                    {"arguments", call.arguments.dump()},
                });
            }
            continue;
        }

        body["input"].push_back(message_to_input(message));
    }

    if (!instructions.str().empty()) {
        body["instructions"] = instructions.str();
    }

    for (const auto& tool : request.tools) {
        if (tool.name.empty()) {
            throw std::invalid_argument("tool definition name cannot be empty");
        }
        body["tools"].push_back({
            {"type", "function"},
            {"name", tool.name},
            {"description", tool.description},
            {"parameters", tool.parameters_schema},
            {"strict", true},
        });
    }

    return body;
}

ModelResponse ResponsesCodec::decode_response(const Json& response) {
    if (!response.is_object()) {
        throw std::runtime_error("Responses API returned a non-object payload");
    }
    if (response.contains("error") && !response["error"].is_null()) {
        const auto& error = response["error"];
        throw std::runtime_error(
            "Responses API error: " + error.value("message", error.dump()));
    }

    ModelResponse result;
    result.response_id = response.value("id", std::string{});

    const auto output = response.value("output", Json::array());
    if (!output.is_array()) {
        throw std::runtime_error("Responses API output must be an array");
    }

    for (const auto& item : output) {
        const auto type = item.value("type", std::string{});
        if (type == "function_call") {
            result.tool_calls.push_back(decode_function_call(item));
            continue;
        }
        if (type != "message") {
            continue;
        }
        for (const auto& content : item.value("content", Json::array())) {
            if (content.value("type", std::string{}) == "output_text") {
                result.text += content.value("text", std::string{});
            }
        }
    }

    const auto status = response.value("status", std::string{"completed"});
    if (status == "failed" || status == "cancelled" || status == "incomplete") {
        throw std::runtime_error("Responses API terminated with status: " + status);
    }
    result.final = result.tool_calls.empty() && status == "completed";
    return result;
}

ResponsesModel::ResponsesModel(IHttpTransport& transport, ResponsesConfig config)
    : transport_(transport), config_(std::move(config)) {}

ModelResponse ResponsesModel::generate(const ModelRequest& request) {
    if (config_.api_key.empty()) {
        throw std::invalid_argument("Responses API key cannot be empty");
    }

    const auto body = ResponsesCodec::encode_request(request, config_);
    HttpRequest http_request;
    http_request.url = config_.endpoint;
    http_request.headers = {
        {"Authorization", "Bearer " + config_.api_key},
        {"Content-Type", "application/json"},
    };
    http_request.body = body.dump();
    http_request.timeout = config_.timeout;

    const auto response = transport_.send(http_request);
    if (response.status_code < 200 || response.status_code >= 300) {
        throw std::runtime_error(
            "Responses API HTTP status " + std::to_string(response.status_code));
    }

    try {
        return ResponsesCodec::decode_response(Json::parse(response.body));
    } catch (const Json::parse_error& error) {
        throw std::runtime_error(
            std::string{"Responses API returned invalid JSON: "} + error.what());
    }
}

}  // namespace agent::openai
