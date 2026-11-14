#pragma once
#include <string>
#include <vector>

struct Diagnostic { int line; std::string message; };
struct Token {
    enum Kind { Word, Ident, Number, End } kind;
    std::string text;
    int line;
};
struct Scan { std::vector<Token> tokens; std::vector<Diagnostic> errors; };
Scan scan(const std::string& source);
