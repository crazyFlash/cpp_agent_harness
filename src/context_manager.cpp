#include "agent/context_manager.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace agent {

ContextManager::ContextManager(ContextConfig config) : config_(config) {}

void ContextManager::append(Message message) {
    history_.push_back(std::move(message));
}

void ContextManager::add_instruction(Role role, std::string content) {
    instructions_.push_back(Message{role, std::move(content), {}, {}, true});
}

std::size_t ContextManager::estimate_tokens(const std::string& text) {
    // A deliberately simple, deterministic estimate. Provider-specific tokenizers
    // can replace this without changing the context-management API.
    return std::max<std::size_t>(1, (text.size() + 3) / 4);
}

std::size_t ContextManager::estimated_tokens() const {
    std::size_t total = estimate_tokens(summary_);
    for (const auto& message : instructions_) {
        total += estimate_tokens(message.content) + 4;
    }
    for (const auto& message : history_) {
        total += estimate_tokens(message.content) + 4;
    }
    return total;
}

std::string ContextManager::summarize(const std::vector<Message>& messages) {
    std::ostringstream output;
    output << "Earlier conversation summary:\n";
    for (const auto& message : messages) {
        std::string content = message.content;
        constexpr std::size_t max_line_size = 240;
        if (content.size() > max_line_size) {
            content.resize(max_line_size);
            content += "...";
        }
        std::replace(content.begin(), content.end(), '\n', ' ');
        output << "- " << role_name(message.role) << ": " << content << '\n';
    }
    return output.str();
}

bool ContextManager::maybe_compact() {
    const auto threshold = static_cast<std::size_t>(
        static_cast<double>(config_.max_estimated_tokens) * config_.compact_at_ratio);
    if (estimated_tokens() < threshold ||
        history_.size() <= config_.keep_recent_messages) {
        return false;
    }

    const std::size_t compact_count = history_.size() - config_.keep_recent_messages;
    std::vector<Message> compacted(history_.begin(), history_.begin() + compact_count);

    std::string next_summary;
    if (!summary_.empty()) {
        next_summary = summary_ + "\n";
    }
    next_summary += summarize(compacted);
    summary_ = std::move(next_summary);
    history_.erase(history_.begin(), history_.begin() + compact_count);
    return true;
}

const std::vector<Message>& ContextManager::history() const {
    return history_;
}

std::vector<Message> ContextManager::working_messages() const {
    std::vector<Message> result;
    result.reserve(instructions_.size() + history_.size() + 1);
    result.insert(result.end(), instructions_.begin(), instructions_.end());
    if (!summary_.empty()) {
        result.push_back(Message{Role::Developer, summary_, {}, {}, true});
    }
    result.insert(result.end(), history_.begin(), history_.end());
    return result;
}

const std::string& ContextManager::summary() const {
    return summary_;
}

}  // namespace agent

