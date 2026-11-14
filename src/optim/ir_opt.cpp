#include "optim/ir_opt.hpp"
#include "ir/int_eval.hpp"
#include <algorithm>
#include <map>
#include <tuple>

namespace {
using namespace ir;
std::vector<int> successors(const Block& b) {
    if (b.empty()) return {};
    const auto& i = b.back();
    if (i.op == Op::Jump) return {i.data};
    if (i.op == Op::Branch) return {i.data,i.other};
    return {};
}
bool prune(ir::Function& f) {
    std::vector<bool> seen(f.blocks.size());
    std::vector<int> pending{0};
    while (!pending.empty()) {
        int b = pending.back(); pending.pop_back();
        if (seen[b]) continue;
        seen[b] = true;
        for (int s : successors(f.blocks[b])) pending.push_back(s);
    }
    bool changed = false;
    for (std::size_t b = 0; b < f.blocks.size(); ++b) {
        if (!seen[b] && !f.blocks[b].empty()) { f.blocks[b].clear(); changed = true; }
    }
    return changed;
}

struct Fact {
    enum Kind { Bottom, Known, Varying } kind = Bottom;
    int32_t value = 0;
    bool operator==(const Fact& f) const { return kind == f.kind && value == f.value; }
};
Fact join(Fact a,Fact b) {
    if (a.kind == Fact::Bottom) return b;
    if (b.kind == Fact::Bottom || a == b) return a;
    return {Fact::Varying};
}
using State = std::vector<Fact>;

bool propagate(ir::Function& f) {
    std::vector<Constant> values(f.values);
    for (auto& block : f.blocks)
        for (auto& i : block)
            if (i.op == Op::Constant) values[i.dst] = i.data;
    auto get = [&](int v) {
        return values[v] ? Fact{Fact::Known,*values[v]} : Fact{Fact::Varying};
    };
    auto effective = [&](const Inst& i, const State& s)->Constant {
        if (i.op == Op::Load && s[i.data].kind == Fact::Known) return s[i.data].value;
        if (i.op == Op::Unary && values[i.args[0]]) {
            int32_t a = *values[i.args[0]];
            if (i.name == "+") return a;
            if (i.name == "!") return !a;
            if (i.name == "-") return integer_result(-int64_t(a));
        }
        if (i.op == Op::Binary && values[i.args[0]] && values[i.args[1]])
            return eval_binary(i.name,*values[i.args[0]],*values[i.args[1]]);
        return {};
    };
    std::vector<State> in(f.blocks.size(), State(f.slots));
    std::fill(in[0].begin(),in[0].end(),Fact{Fact::Varying});
    bool update;
    do {
        update = false;
        for (std::size_t b = 0; b < f.blocks.size(); ++b) {
            State state = in[b];
            for (auto& i : f.blocks[b]) if (i.op == Op::Store) state[i.data] = get(i.args[0]);
            for (auto s : successors(f.blocks[b])) {
                for (int v = 0; v < f.slots; ++v) {
                    auto merged = join(in[s][v],state[v]);
                    if (!(merged == in[s][v])) { in[s][v] = merged; update = true; }
                }
            }
        }
    } while (update);
    bool changed = false;
    for (std::size_t b = 0; b < f.blocks.size(); ++b) {
        auto state = std::move(in[b]);
        for (auto& i : f.blocks[b]) {
            if (i.op == Op::Store) state[i.data] = get(i.args[0]);
            if (i.op == Op::Branch && values[i.args[0]]) {
                i = {Op::Jump,-1,{}, {},*values[i.args[0]] ? i.data : i.other};
                changed = true;
            } else if (auto result = effective(i,state)) {
                i = {Op::Constant,i.dst,{}, {},*result};
                changed = true;
            }
        }
    }
    return changed;
}

bool simplify_values(ir::Function& f) {
    std::vector<int> replace(f.values,-1);
    std::vector<Constant> constants(f.values);
    for (auto& b : f.blocks) for (auto& i : b)
        if (i.op == Op::Constant) constants[i.dst] = i.data;
    auto canonical = [&](int v) { while (replace[v] >= 0) v = replace[v]; return v; };
    bool changed = false;
    for (auto& b : f.blocks) {
        std::map<std::tuple<Op,std::string,int,int>,int> available;
        for (auto& i : b) {
            for (auto& v : i.args) v = canonical(v);
            if (i.op != Op::Unary && i.op != Op::Binary) continue;
            const int a = i.args[0], c = i.args.size() == 2 ? i.args[1] : -1;
            int equivalent = -1;
            if (i.op == Op::Unary && i.name == "+") equivalent = a;
            if (i.op == Op::Binary) {
                if ((i.name == "+" || i.name == "-") && constants[c] == 0) equivalent = a;
                if (i.name == "+" && constants[a] == 0) equivalent = c;
                if ((i.name == "*" || i.name == "/") && constants[c] == 1) equivalent = a;
                if (i.name == "*" && constants[a] == 1) equivalent = c;
            }
            auto key = std::make_tuple(i.op,i.name,a,c);
            if (equivalent < 0) {
                auto [entry, inserted] = available.emplace(std::move(key),i.dst);
                if (!inserted) equivalent = entry->second;
            }
            if (equivalent >= 0) { replace[i.dst] = canonical(equivalent); changed = true; }
        }
    }
    if (changed)
        for (auto& b : f.blocks) for (auto& i : b) for (auto& v : i.args) v = canonical(v);
    return changed;
}

bool eliminate(ir::Function& f) {
    using Live = std::vector<bool>;
    std::vector<Live> live_in(f.blocks.size(), Live(f.slots));
    auto live_out = [&](int b) {
        Live live(f.slots);
        for (int s : successors(f.blocks[b]))
            for (int v = 0; v < f.slots; ++v) live[v] = live[v] || live_in[s][v];
        return live;
    };
    bool changed;
    do {
        changed = false;
        for (int b = static_cast<int>(f.blocks.size())-1; b >= 0; --b) {
            auto live = live_out(b);
            for (auto i = f.blocks[b].rbegin(); i != f.blocks[b].rend(); ++i) {
                if (i->op == Op::Store) live[i->data] = false;
                if (i->op == Op::Load) live[i->data] = true;
            }
            if (live != live_in[b]) { live_in[b] = std::move(live); changed = true; }
        }
    } while (changed);
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        auto live = live_out(b);
        auto& block = f.blocks[b];
        int end = static_cast<int>(block.size());
        for (int x = end-1; x >= 0; --x) {
            auto& i = block[x];
            if (i.op == Op::Store) {
                if (!live[i.data]) { changed = true; continue; }
                live[i.data] = false;
            }
            if (i.op == Op::Load) live[i.data] = true;
            --end;
            if (end != x) block[end] = std::move(i);
        }
        block.erase(block.begin(),block.begin()+end);
    }
    std::vector<int> uses(f.values);
    for (auto& b : f.blocks) for (auto& i : b) for (int v : i.args) ++uses[v];
    for (auto& b : f.blocks) {
        const auto first_dead = std::remove_if(b.begin(),b.end(),[&](const Inst& i) {
            return i.dst >= 0 && !uses[i.dst] && i.op != Op::Call;
        });
        if (first_dead != b.end()) { b.erase(first_dead,b.end()); changed = true; }
    }
    return changed;
}
}

void optimize_ir(ir::Module& m) {
    for (auto& f : m) {
        bool changed;
        do {
            changed = prune(f);
            changed |= propagate(f);
            changed |= simplify_values(f);
            changed |= eliminate(f);
        } while (changed);
    }
}
