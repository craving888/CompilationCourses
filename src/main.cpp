#include "parser/semantic.hpp"
#include "ir/ir.hpp"
#include <iostream>
#include <iterator>
#include <string_view>

static void print_tokens(const Scan& scanned) {
    if (!scanned.errors.empty()) {
        std::cout << "reject\n";
        for (const auto& e : scanned.errors) std::cout << e.line << ' ' << e.message << '\n';
        std::cout << '\n';
    }
    for (std::size_t i = 0; i + 1 < scanned.tokens.size(); ++i) {
        const auto& t = scanned.tokens[i];
        const auto kind = t.kind == Token::Ident ? "Ident" :
                          t.kind == Token::Number ? "IntConst" : "'" + t.text + "'";
        std::cout << i << ':' << kind << ":\"" << t.text << "\"\n";
    }
}

int main(int argc, char* argv[]) {
    const std::string_view mode = argc >= 2 ? argv[1] : "";
    if (argc == 2 && mode == "--help") {
        std::cout << "Usage: ./compiler {-token|-ast|-ir} < input.c\n";
        return 0;
    }
    if (argc != 2 || (mode != "-token" && mode != "-ast" && mode != "-ir")) {
        std::cerr << "Usage: ./compiler {-token|-ast|-ir} < input.c\n";
        return 1;
    }
    const std::string source{std::istreambuf_iterator<char>(std::cin), {}};
    auto scanned = scan(source);
    if (mode == "-token") {
        print_tokens(scanned);
        return 0;
    }
    auto program = parse(std::move(scanned));
    check_semantics(program);
    if (mode == "-ir") {
        for (const auto& e : program.errors) std::cerr << e.line << ' ' << e.message << '\n';
        if (!program.errors.empty()) return 1;
        ir::print(ir::lower(program), std::cout);
        return 0;
    }
    std::cout << (program.errors.empty() ? "accept\n" : "reject\n");
    for (const auto& e : program.errors) std::cout << e.line << ' ' << e.message << '\n';
}
