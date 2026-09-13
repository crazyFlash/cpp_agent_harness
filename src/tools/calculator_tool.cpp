#include "agent/tools/calculator_tool.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace agent {

ToolDefinition CalculatorTool::definition() const {
    return {
        "calculator",
        "Evaluate a binary arithmetic expression such as 21 * 2.",
        {
            {"type", "object"},
            {"properties",
             {{"expression",
               {{"type", "string"},
                {"description", "Left operand, operator, and right operand."}}}}},
            {"required", {"expression"}},
            {"additionalProperties", false},
        },
    };
}

ToolResult CalculatorTool::execute(const ToolCall& call) {
    if (!call.arguments.is_object() || !call.arguments.contains("expression") ||
        !call.arguments["expression"].is_string()) {
        return {false, "missing required argument: expression"};
    }

    std::istringstream input(call.arguments["expression"].get<std::string>());
    double left = 0.0;
    double right = 0.0;
    char operation = '\0';
    if (!(input >> left >> operation >> right)) {
        return {false, "expected an expression like: 21 * 2"};
    }

    double value = 0.0;
    switch (operation) {
        case '+': value = left + right; break;
        case '-': value = left - right; break;
        case '*': value = left * right; break;
        case '/':
            if (std::abs(right) < 1e-15) {
                return {false, "division by zero"};
            }
            value = left / right;
            break;
        default:
            return {false, "unsupported operator"};
    }

    std::ostringstream output;
    output << std::setprecision(15) << value;
    return {true, output.str()};
}

}  // namespace agent
