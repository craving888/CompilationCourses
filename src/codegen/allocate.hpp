#pragma once
#include "ir/ir.hpp"

struct Allocation {
    std::vector<int> regs;  // -1 means a stack placement, otherwise t2–t5.
    std::vector<int> spill;
    std::vector<int> locals; // Slot -> s1–s8 for frequently used locals.
    int spills = 0;
    int saved = 0;
};
Allocation allocate_values(const ir::Function& f, bool optimize);
void peephole_virtual(ir::Function& f);
