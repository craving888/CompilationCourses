#pragma once
#include "parser/ast.hpp"
#include <cstdint>
#include <ostream>

namespace ir {
enum class Op { Constant, Param, Load, Store, Unary, Binary, Call, Jump, Branch, Return };
struct Inst {
    Op op;
    int dst = -1;
    std::vector<int> args = {}; // Virtual values, never block IDs or slots.
    std::string name = {};
    int32_t data = 0; // i32 literal, slot, parameter index or first successor.
    int other = -1;  // Second successor of Branch.
};
using Block = std::vector<Inst>;
struct Function {
    std::string name;
    bool integer;
    int arity;
    std::vector<Block> blocks;
    int slots = 0;
    int values = 0;
};
using Module = std::vector<Function>;

Module lower(const Program& program);
void print(const Module& module, std::ostream& out);
}
