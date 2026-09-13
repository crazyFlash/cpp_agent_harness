#include "agent/line_editor.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <termios.h>
#include <unistd.h>
#endif

namespace agent {

namespace {

#ifndef _WIN32
class RawTerminal {
public:
    RawTerminal() {
        if (::tcgetattr(STDIN_FILENO, &original_) != 0) {
            throw std::runtime_error(
                "cannot read terminal settings: " + std::string{std::strerror(errno)});
        }
        auto raw = original_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
            throw std::runtime_error(
                "cannot enable terminal line editor: " +
                std::string{std::strerror(errno)});
        }
        active_ = true;
    }

    ~RawTerminal() {
        if (active_) ::tcsetattr(STDIN_FILENO, TCSANOW, &original_);
    }

private:
    termios original_{};
    bool active_{false};
};

bool interactive_terminal() {
    return ::isatty(STDIN_FILENO) == 1 && ::isatty(STDOUT_FILENO) == 1;
}
#else
bool interactive_terminal() { return false; }
#endif

std::string common_prefix(const std::vector<std::string>& values) {
    if (values.empty()) return {};
    std::string prefix = values.front();
    for (const auto& value : values) {
        while (!value.starts_with(prefix)) prefix.pop_back();
    }
    return prefix;
}

void erase_last_utf8_codepoint(std::string& value) {
    if (value.empty()) return;
    std::size_t start = value.size() - 1;
    while (start > 0 &&
           (static_cast<unsigned char>(value[start]) & 0xC0U) == 0x80U) {
        --start;
    }
    value.erase(start);
}

}  // namespace

LineEditor::LineEditor(CompletionProvider completions)
    : completions_(std::move(completions)) {}

void LineEditor::redraw(std::string_view prompt, std::string_view buffer) const {
    std::cout << "\r\033[2K" << prompt << buffer << std::flush;
}

void LineEditor::complete(std::string_view prompt, std::string& buffer) const {
    if (!completions_) return;
    auto candidates = completions_(buffer);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    if (candidates.empty()) return;

    const auto prefix = common_prefix(candidates);
    if (prefix.size() > buffer.size()) {
        buffer = prefix;
        redraw(prompt, buffer);
        return;
    }
    if (candidates.size() == 1) {
        buffer = candidates.front();
        redraw(prompt, buffer);
        return;
    }
    std::cout << '\n';
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0) std::cout << "  ";
        std::cout << candidates[index];
    }
    std::cout << '\n';
    redraw(prompt, buffer);
}

std::optional<std::string> LineEditor::read_line(std::string_view prompt) {
    if (!interactive_terminal()) {
        std::cout << prompt << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) return std::nullopt;
        return line;
    }

#ifdef _WIN32
    return std::nullopt;
#else
    RawTerminal terminal;
    std::string buffer;
    std::size_t history_index = history_.size();
    std::cout << prompt << std::flush;
    while (true) {
        unsigned char character = 0;
        const auto count = ::read(STDIN_FILENO, &character, 1);
        if (count == 0) continue;
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(
                "cannot read terminal input: " + std::string{std::strerror(errno)});
        }
        if (character == '\r' || character == '\n') {
            std::cout << '\n';
            if (!buffer.empty() &&
                (history_.empty() || history_.back() != buffer)) {
                history_.push_back(buffer);
            }
            return buffer;
        }
        if (character == 4 && buffer.empty()) {
            std::cout << '\n';
            return std::nullopt;
        }
        if (character == 3) {
            std::cout << "^C\n";
            return std::nullopt;
        }
        if (character == 9) {
            complete(prompt, buffer);
            continue;
        }
        if (character == 127 || character == 8) {
            erase_last_utf8_codepoint(buffer);
            redraw(prompt, buffer);
            continue;
        }
        if (character == 12) {
            redraw(prompt, buffer);
            continue;
        }
        if (character == 27) {
            unsigned char sequence[2]{};
            if (::read(STDIN_FILENO, &sequence[0], 1) != 1 ||
                ::read(STDIN_FILENO, &sequence[1], 1) != 1 ||
                sequence[0] != '[') {
                continue;
            }
            if (sequence[1] == 'A' && !history_.empty()) {
                if (history_index > 0) --history_index;
                buffer = history_[history_index];
                redraw(prompt, buffer);
            } else if (sequence[1] == 'B') {
                if (history_index < history_.size()) ++history_index;
                buffer = history_index < history_.size()
                    ? history_[history_index] : std::string{};
                redraw(prompt, buffer);
            }
            continue;
        }
        if (character >= 32) {
            buffer.push_back(static_cast<char>(character));
            std::cout.put(static_cast<char>(character));
            std::cout.flush();
        }
    }
#endif
}

}  // namespace agent
