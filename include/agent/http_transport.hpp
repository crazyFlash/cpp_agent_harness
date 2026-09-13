#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace agent {

struct HttpRequest {
    std::string method{"POST"};
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    std::chrono::milliseconds timeout{30000};
};

struct HttpResponse {
    int status_code{0};
    std::map<std::string, std::string> headers;
    std::string body;
};

class IHttpTransport {
public:
    using BodyChunkCallback = std::function<void(std::string_view)>;

    virtual ~IHttpTransport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
    virtual HttpResponse send_stream(const HttpRequest& request,
                                     const BodyChunkCallback& on_chunk) {
        auto response = send(request);
        if (!response.body.empty()) {
            on_chunk(response.body);
        }
        return response;
    }
};

}  // namespace agent
