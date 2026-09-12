#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace agent::openai {

struct SseEvent {
    std::string event{"message"};
    std::string data;
    std::string id;
};

class SseParser {
public:
    std::vector<SseEvent> feed(std::string_view chunk);
    std::vector<SseEvent> finish();

private:
    void consume_line(std::string line, std::vector<SseEvent>& output);
    void dispatch(std::vector<SseEvent>& output);

    std::string buffer_;
    SseEvent current_;
    bool has_fields_{false};
};

}  // namespace agent::openai

