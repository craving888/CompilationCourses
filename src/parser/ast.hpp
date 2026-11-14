#pragma once
#include "frontend.hpp"
#include <memory>

enum class NodeKind {
    Block, Empty, Expr, Assign, Decl, Const, If, While, Break, Continue, Return,
    Int, Name, Call, Unary, Binary
};
struct Node;
using Tree = std::unique_ptr<Node>;
struct Node {
    NodeKind kind;
    Token at;
    std::vector<Tree> children;
};
struct Function {
    Token name;
    bool returns_int;
    std::vector<Token> parameters;
    Tree body;
};
struct Program {
    std::vector<Function> functions;
    std::vector<Diagnostic> errors;
};
Program parse(Scan scanned);
