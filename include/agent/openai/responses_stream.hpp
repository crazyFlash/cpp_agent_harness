#pragma once

#include "agent/openai/sse_parser.hpp"
#include "agent/types.hpp"

#include <map>
#include <string>
#include <vector>

namespace agent::openai {

class ResponsesStreamAssembler {
public:
    std::string consume(const SseEvent& event);
    ModelResponse result() const;
    bool completed() const;

private:
    struct PendingCall {
        std::string call_id;
        std::string name;
        std::string arguments;
    };

    void consume_item(const Json& item);
    PendingCall& pending_call(const std::string& item_id);

    std::string text_;
    std::string response_id_;
    std::vector<PendingCall> calls_;
    std::map<std::string, std::size_t> call_indices_;
    bool completed_{false};
    bool saw_terminal_response_{false};
    ModelResponse terminal_response_;
};

}  // namespace agent::openai
