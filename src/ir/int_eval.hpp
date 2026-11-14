#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

using Constant = std::optional<int32_t>;
inline Constant integer_result(int64_t v) {
    if (v < INT32_MIN || v > INT32_MAX) return {};
    return static_cast<int32_t>(v);
}
inline Constant eval_binary(std::string_view op, int32_t a, int32_t b) {
    if (op == "+") return integer_result(int64_t(a) + b);
    if (op == "-") return integer_result(int64_t(a) - b);
    if (op == "*") return integer_result(int64_t(a) * b);
    if (op == "/" || op == "%") {
        if (!b || (a == INT32_MIN && b == -1)) return {};
        return op == "/" ? a / b : a % b;
    }
    if (op == "==") return a == b;
    if (op == "!=") return a != b;
    if (op == "<") return a < b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    if (op == ">=") return a >= b;
    if (op == "&&") return a != 0 && b != 0;
    if (op == "||") return a != 0 || b != 0;
    return {};
}
