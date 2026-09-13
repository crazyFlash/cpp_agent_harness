#pragma once

#include "agent/http_transport.hpp"

#include <cstddef>
#include <string>

namespace agent {

struct CurlCliConfig {
    std::string executable{"curl"};
    std::size_t max_response_bytes{8 * 1024 * 1024};
};

class CurlCliTransport final : public IHttpTransport {
public:
    explicit CurlCliTransport(CurlCliConfig config = {});
    HttpResponse send(const HttpRequest& request) override;
    HttpResponse send_stream(const HttpRequest& request,
                             const BodyChunkCallback& on_chunk) override;

private:
    CurlCliConfig config_;
};

}  // namespace agent
