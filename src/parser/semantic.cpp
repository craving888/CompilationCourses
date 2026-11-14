#include "parser/semantic.hpp"
#include "ir/int_eval.hpp"
#include <algorithm>
#include <charconv>
#include <unordered_map>

namespace {
using K = NodeKind;
struct Binding { bool immutable; Constant value; };
struct Signature { bool integer; std::size_t arity; };
enum class Type { Invalid, Int, Void };
struct Value { Type type; Constant value = {}; };
enum Exit { Next = 1, Ret = 2, Brk = 4, Cont = 8 };

struct Check {
    Program& p;
    std::unordered_map<std::string,Signature> functions{{"getint",{true,0}}, {"putint",{false,1}}};
    std::vector<std::unordered_map<std::string,Binding>> scopes;
    bool integer = false;
    int loops = 0;

    void error(const Token& t, std::string message) { p.errors.push_back({t.line,std::move(message)}); }
    const Binding* local(const std::string& name) {
        for (auto i = scopes.rbegin(); i != scopes.rend(); ++i) {
            auto b = i->find(name);
            if (b != i->end()) return &b->second;
        }
        return nullptr;
    }
    void define(const Token& t, Binding value) {
        if (!scopes.back().emplace(t.text,value).second) error(t,"duplicate declaration of " + t.text);
    }
    Value expr(const Node& n, bool value_required = true) {
        auto c = [&](int i) { return expr(*n.children[i]); };
        switch (n.kind) {
        case K::Int: {
            int32_t number = 0;
            if (std::from_chars(n.at.text.data(), n.at.text.data()+n.at.text.size(), number).ec != std::errc{}) {
                error(n.at,"integer literal out of range");
                return {Type::Invalid};
            }
            return {Type::Int,number};
        }
        case K::Name:
            if (auto b = local(n.at.text)) return {Type::Int,b->value};
            error(n.at,"undeclared variable " + n.at.text);
            return {Type::Invalid};
        case K::Call: {
            for (const auto& a : n.children) expr(*a);
            auto f = functions.find(n.at.text);
            if (local(n.at.text) || f == functions.end()) {
                error(n.at,"unknown or shadowed function " + n.at.text);
                return {Type::Invalid};
            }
            if (f->second.arity != n.children.size()) error(n.at,"incorrect argument count for " + n.at.text);
            if (!f->second.integer && value_required) {
                error(n.at,"void call used as a value");
                return {Type::Invalid};
            }
            return {f->second.integer ? Type::Int : Type::Void};
        }
        case K::Unary: {
            if (n.at.text == "-" && n.children[0]->kind == K::Int && n.children[0]->at.text == "2147483648")
                return {Type::Int,INT32_MIN};
            auto a = c(0);
            if (!a.value) return a;
            if (n.at.text == "!") a.value = !*a.value;
            if (n.at.text == "-") a.value = integer_result(-int64_t(*a.value));
            return a;
        }
        case K::Binary: {
            auto a = c(0), b = c(1);
            if (a.type != Type::Int || b.type != Type::Int) return {Type::Invalid};
            const auto& op = n.at.text;
            if ((op == "/" || op == "%") && b.value == 0) error(n.at,"division or remainder by zero");
            if (a.value == 0 && op == "&&") return {Type::Int,0};
            if (a.value && *a.value != 0 && op == "||") return {Type::Int,1};
            return {Type::Int,a.value && b.value ? eval_binary(op,*a.value,*b.value) : Constant{}};
        }
        default: return {Type::Invalid};
        }
    }

    int stmt(const Node& n, bool nested = true) {
        auto& a = n.children;
        switch (n.kind) {
        case K::Block: {
            if (nested) scopes.emplace_back();
            int result = Next;
            for (const auto& s : a) {
                auto r = stmt(*s);
                if (result & Next) result = (result & ~Next) | r;
            }
            if (nested) scopes.pop_back();
            return result;
        }
        case K::Decl: case K::Const: {
            Constant v;
            if (a[0]) v = expr(*a[0]).value;
            define(n.at,{n.kind == K::Const, n.kind == K::Const ? v : Constant{}});
            return Next;
        }
        case K::Assign: {
            auto b = local(n.at.text);
            if (!b) error(n.at,"undeclared variable " + n.at.text);
            else if (b->immutable) error(n.at,"assignment to constant " + n.at.text);
            expr(*a[0]);
            return Next;
        }
        case K::If: {
            auto c = expr(*a[0]).value;
            auto yes = stmt(*a[1]), no = a[2] ? stmt(*a[2]) : Next;
            return c ? (*c ? yes : no) : yes | no;
        }
        case K::While: {
            auto c = expr(*a[0]).value;
            ++loops;
            auto body = stmt(*a[1]);
            --loops;
            if (c == 0) return Next;
            return (body & Ret) | ((!c || (body & Brk)) ? Next : 0);
        }
        case K::Break: case K::Continue:
            if (!loops) { error(n.at,n.at.text + " outside a loop"); return Next; }
            return n.kind == K::Break ? Brk : Cont;
        case K::Return:
            if (bool(a[0]) != integer) error(n.at,"return value does not match function type");
            if (a[0]) expr(*a[0], integer);
            return Ret;
        case K::Expr: expr(*a[0], false); return Next;
        default: return Next;
        }
    }
    void run() {
        bool main = false;
        for (const auto& f : p.functions) {
            if (!functions.emplace(f.name.text,Signature{f.returns_int,f.parameters.size()}).second)
                error(f.name,"duplicate function " + f.name.text);
            if (f.name.text == "main") {
                main = true;
                if (!f.returns_int || !f.parameters.empty()) error(f.name,"main must be int main()");
            }
            integer = f.returns_int;
            scopes.emplace_back();
            for (const auto& param : f.parameters) define(param,{false,{}});
            if ((stmt(*f.body,false) & Next) && integer)
                p.errors.push_back({f.body->close_line,"missing return on a reachable path"});
            scopes.pop_back();
        }
        if (!main) p.errors.push_back({p.last_line,"missing main"});
        std::stable_sort(p.errors.begin(),p.errors.end(),[](const auto& a, const auto& b){return a.line < b.line;});
        p.errors.erase(std::unique(p.errors.begin(),p.errors.end(),[](const auto& a,const auto& b){return a.line == b.line;}),p.errors.end());
    }
};
}

void check_semantics(Program& p) { if (p.errors.empty()) Check{p}.run(); }
