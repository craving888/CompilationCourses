# ToyC

C++17 和 Flex。Flex 依据正则生成 DFA，先选择最长匹配，再按规则顺序打破平局；
因此完整标识符不会被关键字切开。独立的注释状态记录未闭合注释的起点。
非法字符产生诊断但不进入 Token 流，扫描继续前进。源行号从 0 开始。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ln -sfn build/compiler compiler
./compiler -token < input.c
```

课程：[词法分析](https://lightsmilew.github.io/compiler-principles/docs/labs/part1-lexer)。
