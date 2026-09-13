#pragma once

#include "agent/types.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace agent {

class IModel {
public:
    using TextDeltaCallback = std::function<void(std::string_view)>;

    virtual ~IModel() = default;
    virtual ModelResponse generate(const ModelRequest& request,
                                   const TextDeltaCallback& on_text_delta = {}) = 0;
};

}  // namespace agent
