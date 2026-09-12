#pragma once

#include "agent/types.hpp"

#include <vector>

namespace agent {

class IModel {
public:
    virtual ~IModel() = default;
    virtual ModelResponse generate(const std::vector<Message>& messages) = 0;
};

}  // namespace agent

