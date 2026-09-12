#include "agent/tools/calculator_tool.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace agent {

ToolDefinition CalculatorTool::definition() const {
    return {
        "calculator",
        "Evaluate a binary arithmetic expression such as 21 * 2.",
        {{"expression", "string: left operand, operator, and right operand"}},
    };
}

ToolResult CalculatorTool::execute(const ToolCall& call) {
    const auto argument = call.arguments.find("expression");
    if (argument == call.arguments.end()) {
        return {false, "missing required argument: expression"};
    }

    std::istringstream input(argument->second);
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

