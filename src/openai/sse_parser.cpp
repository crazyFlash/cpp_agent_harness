#include "agent/openai/sse_parser.hpp"

#include <utility>

namespace agent::openai {

std::vector<SseEvent> SseParser::feed(std::string_view chunk) {
    buffer_.append(chunk);
    std::vector<SseEvent> output;

    std::size_t newline = 0;
    while ((newline = buffer_.find('\n')) != std::string::npos) {
        std::string line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        consume_line(std::move(line), output);
    }
    return output;
}

std::vector<SseEvent> SseParser::finish() {
    std::vector<SseEvent> output;
    if (!buffer_.empty()) {
        if (buffer_.back() == '\r') {
            buffer_.pop_back();
        }
        consume_line(std::move(buffer_), output);
        buffer_.clear();
    }
    dispatch(output);
    return output;
}

void SseParser::consume_line(std::string line, std::vector<SseEvent>& output) {
    if (line.empty()) {
        dispatch(output);
        return;
    }
    if (line.front() == ':') {
        return;
    }

    const auto separator = line.find(':');
    std::string field = line.substr(0, separator);
    std::string value;
    if (separator != std::string::npos) {
        value = line.substr(separator + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(0, 1);
        }
    }

    if (field == "event") {
        current_.event = std::move(value);
        has_fields_ = true;
    } else if (field == "data") {
        if (!current_.data.empty()) {
            current_.data += '\n';
        }
        current_.data += value;
        has_fields_ = true;
    } else if (field == "id") {
        current_.id = std::move(value);
        has_fields_ = true;
    }
}

void SseParser::dispatch(std::vector<SseEvent>& output) {
    if (!has_fields_) {
        return;
    }
    output.push_back(std::move(current_));
    current_ = SseEvent{};
    has_fields_ = false;
}

}  // namespace agent::openai

