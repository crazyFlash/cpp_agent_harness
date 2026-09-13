#pragma once

#include "agent/types.hpp"

#include <vector>

namespace agent {

class IModel {
public:
    virtual ~IModel() = default;
    virtual ModelResponse generate(const ModelRequest& request) = 0;
};

}  // namespace agent
