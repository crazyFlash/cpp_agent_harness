#pragma once

#include <chrono>
#include <map>
#include <string>

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
    virtual ~IHttpTransport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

}  // namespace agent

