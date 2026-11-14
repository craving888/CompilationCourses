# ToyC

从 C++17 / Flex 词法分析，经递归下降、AST、静态语义检查和三地址 IR，
生成 RV64GC 汇编。详见 [IR 设计](DESIGN.md)和[课程规范](https://lightsmilew.github.io/compiler-principles/)。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ln -sfn build/compiler compiler
./compiler -token < input.c
./compiler -ast < input.c
./compiler -ir < input.c
./compiler -asm < input.c > input.s
riscv64-unknown-elf-gcc -march=rv64gc -mabi=lp64d -nostdlib -static \
  input.s third_party/toyc/libtoyc.a -o input
qemu-riscv64 ./input < input.in
```

Flex 使用最长匹配，非法字符诊断后恢复。语法分析对各优先级用循环进行左结合，
一元运算递归，else 匹配最近 if；错误恢复在边界重新同步。语义检查使用作用域栈、
按声明顺序登记函数，并组合语句的正常、返回、跳出及继续出口。

| IR | RV64GC |
|---|---|
| 常量、载入、写入 | li、lw、sw |
| 32 位加减乘除取余 | addw、subw、mulw、divw、remw |
| 比较和逻辑取反 | slt / xor / seqz / snez / xori |
| 条件与循环 | 条件跳转与基本块标签 |
| 调用、返回 | call、统一尾声、ret |

栈帧从 sp 起依次为外传实参、局部 i32 槽、基线虚拟值槽、对齐区和 ra；
总大小按 16 字节对齐。前 8 个参数由 a0–a7 传递，余者占 8 字节栈槽，
被调用者从原 sp 处读取；返回值用 a0。每次调用前活跃值都在固定槽中。
没有 phi 指令：IR 在两条短路结果边写同一槽，合流块读取，因此无需并行复制。
