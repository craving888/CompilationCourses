#include "ir/ir.hpp"
#include <unordered_map>

namespace ir {
namespace {
using K = NodeKind;
struct Lower {
    Function f;
    int current = -1;
    std::vector<std::unordered_map<std::string,int>> scopes;
    std::vector<std::pair<int,int>> loops;

    int block() { f.blocks.emplace_back(); return static_cast<int>(f.blocks.size())-1; }
    int slot() { return f.slots++; }
    void add(Inst ins) { f.blocks.at(current).push_back(std::move(ins)); }
    int value(Inst ins) { int id = ins.dst = f.values++; add(std::move(ins)); return id; }
    int constant(int32_t v) { return value({Op::Constant,-1,{}, {},v}); }
    void go(int target) { add({Op::Jump,-1,{}, {},target}); current = -1; }
    void store(int s,int v) { add({Op::Store,-1,{v},{},s}); }
    int load(int s) { return value({Op::Load,-1,{}, {},s}); }
    int binding(const std::string& name) {
        for (auto i = scopes.rbegin(); i != scopes.rend(); ++i) {
            auto b = i->find(name);
            if (b != i->end()) return b->second;
        }
        throw std::logic_error("IR: undeclared variable " + name);
    }

    void cond(const Node& n,int yes,int no) {
        auto& a = n.children;
        if (n.kind == K::Unary && n.at.text == "!") { cond(*a[0],no,yes); return; }
        if (n.kind == K::Binary && (n.at.text == "&&" || n.at.text == "||")) {
            int rhs = block();
            if (n.at.text == "&&") cond(*a[0],rhs,no);
            else cond(*a[0],yes,rhs);
            current = rhs;
            cond(*a[1],yes,no);
            return;
        }
        int v = expr(n);
        add({Op::Branch,-1,{v},{},yes,no});
        current = -1;
    }
    int boolean_value(const Node& n) {
        int yes = block(), no = block(), join = block(), s = slot();
        cond(n,yes,no);
        current = yes; store(s,constant(1)); go(join);
        current = no; store(s,constant(0)); go(join);
        current = join; return load(s);
    }
    int expr(const Node& n,bool keep = true) {
        auto& a = n.children;
        switch (n.kind) {
        case K::Int: return constant(std::stoi(n.at.text));
        case K::Name: return load(binding(n.at.text));
        case K::Call: {
            Inst call{Op::Call,-1,{},n.at.text};
            for (auto& child : a) call.args.push_back(expr(*child));
            if (keep) return value(std::move(call));
            add(std::move(call)); return -1;
        }
        case K::Unary:
            if (n.at.text == "-" && a[0]->kind == K::Int && a[0]->at.text == "2147483648") return constant(INT32_MIN);
            return value({Op::Unary,-1,{expr(*a[0])},n.at.text});
        case K::Binary:
            if (n.at.text == "&&" || n.at.text == "||") return boolean_value(n);
            return value({Op::Binary,-1,{expr(*a[0]),expr(*a[1])},n.at.text});
        default: throw std::logic_error("IR: expected expression");
        }
    }
    void stmt(const Node& n,bool nested = true) {
        auto& a = n.children;
        // Keep a disconnected block after a terminator so syntax and bindings
        // of later declarations remain represented; the optimizer can prune it.
        if (current < 0) current = block();
        switch (n.kind) {
        case K::Block:
            if (nested) scopes.emplace_back();
            for (auto& child : a) stmt(*child);
            if (nested) scopes.pop_back();
            break;
        case K::Empty: break;
        case K::Decl: case K::Const: {
            int init = a[0] ? expr(*a[0]) : -1;
            auto s = slot();
            scopes.back().emplace(n.at.text,s);
            if (init >= 0) store(s,init);
            break;
        }
        case K::Assign: store(binding(n.at.text),expr(*a[0])); break;
        case K::Expr: expr(*a[0],false); break;
        case K::If: {
            int yes = block(), join = block(), no = a[2] ? block() : join;
            cond(*a[0],yes,no);
            current = yes; stmt(*a[1]); if (current >= 0) go(join);
            if (a[2]) { current = no; stmt(*a[2]); if (current >= 0) go(join); }
            current = join;
            break;
        }
        case K::While: {
            int test = block(), body = block(), done = block();
            go(test); current = test; cond(*a[0],body,done);
            current = body; loops.emplace_back(test,done);
            stmt(*a[1]); loops.pop_back();
            if (current >= 0) go(test);
            current = done;
            break;
        }
        case K::Break: go(loops.back().second); break;
        case K::Continue: go(loops.back().first); break;
        case K::Return: {
            Inst ret{Op::Return};
            if (a[0]) ret.args.push_back(expr(*a[0]));
            add(std::move(ret)); current = -1;
            break;
        }
        default: throw std::logic_error("IR: expected statement");
        }
    }
    Function run(const ::Function& source) {
        current = block();
        scopes.emplace_back();
        for (int i = 0; i < f.arity; ++i) {
            auto s = slot();
            scopes.back().emplace(source.parameters[i].text,s);
            store(s,value({Op::Param,-1,{}, {},i}));
        }
        stmt(*source.body,false);
        if (current >= 0) {
            if (f.integer) add({Op::Return,-1,{constant(0)}}); // Unreachable after return analysis.
            else add({Op::Return});
        }
        return std::move(f);
    }
};
}
Module lower(const Program& program) {
    Module m;
    for (auto& f : program.functions) {
        Lower lowerer{{f.name.text,f.returns_int,static_cast<int>(f.parameters.size()),{}},-1,{},{}};
        m.push_back(lowerer.run(f));
    }
    return m;
}
void print(const Module& module, std::ostream& out) {
    out << "extern int getint(0)\nextern void putint(1)\n";
    for (auto& f : module) {
        out << (f.integer ? "int " : "void ") << f.name << '(' << f.arity << ") slots " << f.slots << '\n';
        for (std::size_t b = 0; b < f.blocks.size(); ++b) {
            if (f.blocks[b].empty()) continue;
            out << "bb" << b << ":\n";
            for (auto& i : f.blocks[b]) {
                out << "  ";
                if (i.dst >= 0) out << '%' << i.dst << " = ";
                switch (i.op) {
                case Op::Constant: out << "const " << i.data; break;
                case Op::Param: out << "param " << i.data; break;
                case Op::Load: out << "load @" << i.data; break;
                case Op::Store: out << "store @" << i.data; break;
                case Op::Unary: out << "unary " << i.name; break;
                case Op::Binary: out << "binary " << i.name; break;
                case Op::Call: out << "call " << i.name; break;
                case Op::Jump: out << "jump bb" << i.data; break;
                case Op::Branch: out << "branch bb" << i.data << " bb" << i.other; break;
                case Op::Return: out << "return"; break;
                }
                for (int arg : i.args) out << " %" << arg;
                out << '\n';
            }
        }
    }
}
}
