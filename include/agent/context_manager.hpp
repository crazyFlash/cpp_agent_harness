#pragma once

#include "agent/types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace agent {

struct ContextConfig {
    std::size_t max_estimated_tokens{4096};
    double compact_at_ratio{0.70};
    std::size_t keep_recent_messages{6};
};

struct ContextStats {
    std::size_t estimated_tokens{0};
    std::size_t max_estimated_tokens{0};
    std::size_t compact_threshold_tokens{0};
    std::size_t history_messages{0};
    std::size_t instruction_messages{0};
    std::size_t compaction_count{0};
    bool has_summary{false};
};

class ContextManager {
public:
    explicit ContextManager(ContextConfig config = {});

    void append(Message message);
    void add_instruction(Role role, std::string content);

    bool maybe_compact();
    bool compact_now();
    void clear_history();
    std::size_t estimated_tokens() const;
    ContextStats stats() const;
    const std::vector<Message>& history() const;
    std::vector<Message> working_messages() const;
    const std::string& summary() const;

private:
    static std::size_t estimate_tokens(const std::string& text);
    static std::string summarize(const std::vector<Message>& messages);

    ContextConfig config_;
    std::vector<Message> instructions_;
    std::vector<Message> history_;
    std::string summary_;
    std::size_t compaction_count_{0};
};

}  // namespace agent
