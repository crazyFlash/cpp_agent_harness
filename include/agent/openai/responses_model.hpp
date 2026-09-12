#pragma once

#include "agent/http_transport.hpp"
#include "agent/model.hpp"

#include <chrono>
#include <string>

namespace agent::openai {

struct ResponsesConfig {
    std::string endpoint{"https://api.openai.com/v1/responses"};
    std::string api_key;
    std::string model;
    std::chrono::milliseconds timeout{60000};
    bool store{false};
};

class ResponsesCodec {
public:
    static Json encode_request(const ModelRequest& request,
                               const ResponsesConfig& config,
                               bool stream = false);
    static ModelResponse decode_response(const Json& response);
};

class ResponsesModel final : public IModel {
public:
    ResponsesModel(IHttpTransport& transport, ResponsesConfig config);
    ModelResponse generate(const ModelRequest& request) override;

private:
    IHttpTransport& transport_;
    ResponsesConfig config_;
};

}  // namespace agent::openai
