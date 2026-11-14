#include "codegen/riscv.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace {
using ir::Op;
using ir::Inst;

struct Emitter {
    const ir::Function& f;
    std::ostream& out;
    int index;
    int64_t outgoing = 0, frame = 0;
    std::string label(int block) const {
        return ".Ltoy_" + std::to_string(index) + "_" + (block < 0 ? "exit" : std::to_string(block));
    }
    void ins(const std::string& text) { out << "    " << text << '\n'; }
    void memory(const std::string& op,const std::string& reg,int64_t offset) {
        if (offset <= 2047) ins(op + " " + reg + ", " + std::to_string(offset) + "(sp)");
        else {
            ins("li t6, " + std::to_string(offset));
            ins("add t6, sp, t6");
            ins(op + " " + reg + ", 0(t6)");
        }
    }
    void adjust(int64_t delta) {
        if (delta >= -2048 && delta <= 2047) ins("addi sp, sp, " + std::to_string(delta));
        else {
            ins("li t6, " + std::to_string(delta));
            ins("add sp, sp, t6");
        }
    }
    int64_t slot(int id) const { return outgoing + int64_t(4) * id; }
    void get(const std::string& reg,int id) { memory("lw",reg,slot(f.slots+id)); }
    void put(const std::string& reg,int id) { memory("sw",reg,slot(f.slots+id)); }
    static std::string arg(int id) { return "a" + std::to_string(id); }

    void binary(const std::string& op) {
        static const std::unordered_map<std::string,std::string> plain{
            {"+","addw"},{"-","subw"},{"*","mulw"},{"/","divw"},{"%","remw"},{"<","slt"}
        };
        auto p = plain.find(op);
        if (p != plain.end()) ins(p->second + " t0, t0, t1");
        else if (op == ">") ins("slt t0, t1, t0");
        else if (op == "==" || op == "!=") {
            ins("xor t0, t0, t1");
            ins(std::string(op == "==" ? "seqz" : "snez") + " t0, t0");
        } else if (op == "<=" || op == ">=") {
            ins(op == "<=" ? "slt t0, t1, t0" : "slt t0, t0, t1");
            ins("xori t0, t0, 1");
        } else throw std::logic_error("RV64: unsupported binary operator " + op);
    }

    void emit(const Inst& i) {
        switch (i.op) {
        case Op::Constant: ins("li t0, " + std::to_string(i.data)); put("t0",i.dst); break;
        case Op::Param:
            if (i.data < 8) put(arg(i.data),i.dst);
            else { memory("lw","t0",frame + 8LL*(i.data-8)); put("t0",i.dst); }
            break;
        case Op::Load: memory("lw","t0",slot(i.data)); put("t0",i.dst); break;
        case Op::Store: get("t0",i.args[0]); memory("sw","t0",slot(i.data)); break;
        case Op::Unary:
            get("t0",i.args[0]);
            if (i.name == "-") ins("negw t0, t0");
            else if (i.name == "!") ins("seqz t0, t0");
            put("t0",i.dst);
            break;
        case Op::Binary:
            get("t0",i.args[0]); get("t1",i.args[1]); binary(i.name); put("t0",i.dst);
            break;
        case Op::Call:
            for (int j = 0; j < static_cast<int>(i.args.size()); ++j) {
                if (j < 8) get(arg(j),i.args[j]);
                else { get("t0",i.args[j]); memory("sd","t0",8LL*(j-8)); }
            }
            ins("call " + i.name);
            if (i.dst >= 0) put("a0",i.dst);
            break;
        case Op::Jump: ins("j " + label(i.data)); break;
        case Op::Branch:
            get("t0",i.args[0]);
            ins("beqz t0, 1f");
            ins("j " + label(i.data));
            out << "1:\n";
            ins("j " + label(i.other));
            break;
        case Op::Return:
            if (!i.args.empty()) get("a0",i.args[0]);
            ins("j " + label(-1));
            break;
        default: throw std::logic_error("RV64: unsupported IR instruction");
        }
    }
    void run() {
        for (auto& b : f.blocks)
            for (auto& i : b)
                if (i.op == Op::Call && i.args.size() > 8)
                    outgoing = std::max(outgoing,int64_t(8)*static_cast<int64_t>(i.args.size()-8));
        frame = (slot(f.slots+f.values) + 8 + 15) & ~int64_t(15);
        out << ".globl " << f.name << "\n.type " << f.name << ", @function\n.p2align 2\n" << f.name << ":\n";
        adjust(-frame);
        memory("sd","ra",frame-8);
        for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
            if (f.blocks[b].empty()) continue;
            out << label(b) << ":\n";
            for (auto& i : f.blocks[b]) emit(i);
        }
        out << label(-1) << ":\n";
        memory("ld","ra",frame-8);
        adjust(frame);
        ins("ret");
        out << ".size " << f.name << ", .-" << f.name << '\n';
    }
};
}

void emit_riscv(const ir::Module& module, std::ostream& out) {
    out << ".text\n";
    for (int i = 0; i < static_cast<int>(module.size()); ++i) Emitter{module[i],out,i}.run();
    out << ".section .note.GNU-stack,\"\",@progbits\n";
}
