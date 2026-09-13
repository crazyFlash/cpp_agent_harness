#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agent {

class LineEditor {
public:
    using CompletionProvider =
        std::function<std::vector<std::string>(std::string_view)>;

    explicit LineEditor(CompletionProvider completions = {});
    std::optional<std::string> read_line(std::string_view prompt);

private:
    void redraw(std::string_view prompt, std::string_view buffer) const;
    void complete(std::string_view prompt, std::string& buffer) const;

    CompletionProvider completions_;
    std::vector<std::string> history_;
};

}  // namespace agent
