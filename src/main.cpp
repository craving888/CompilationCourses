#include "parser/semantic.hpp"
#include "ir/ir.hpp"
#include "optim/ir_opt.hpp"
#include "codegen/riscv.hpp"
#include <sstream>
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

static constexpr const char* USAGE = "Usage: ./compiler {-token|-ast|-ir [-opt]|-asm [-opt]} < input.c\n";
int main(int argc, char* argv[]) {
    std::string_view mode;
    bool optimize = false;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << USAGE;
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-opt" && !optimize) optimize = true;
        else if ((arg == "-token" || arg == "-ast" || arg == "-ir" || arg == "-asm") && mode.empty()) mode = arg;
        else { std::cerr << USAGE; return 1; }
    }
    if (mode.empty() || (optimize && mode != "-asm" && mode != "-ir")) { std::cerr << USAGE; return 1; }

    const std::string source{std::istreambuf_iterator<char>(std::cin), {}};
    auto scanned = scan(source);
    if (mode == "-token") { print_tokens(scanned); return 0; }
    auto program = parse(std::move(scanned));
    check_semantics(program);
    if (mode == "-ast") {
        std::cout << (program.errors.empty() ? "accept\n" : "reject\n");
        for (auto& e : program.errors) std::cout << e.line << ' ' << e.message << '\n';
        return 0;
    }
    if (!program.errors.empty()) {
        for (auto& e : program.errors) std::cerr << e.line << ' ' << e.message << '\n';
        return 1;
    }
    try {
        auto module = ir::lower(program);
        if (optimize) optimize_ir(module);
        std::ostringstream result;
        if (mode == "-ir") ir::print(module,result);
        else emit_riscv(module,result);
        std::cout << result.str();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
