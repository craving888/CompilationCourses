# 第三部分：ToyC 内部 IR

选择自定义三地址 IR，使源码到目标的语义转换可独立检查，无需依赖 LLVM。
Module 是按声明顺序排列的函数；函数有参数个数、i32 栈槽及基本块列表；
每条指令有操作码、独立的虚拟值编号、操作数及附加信息。跳转携带基本块编号作为 CFG 边。
虚拟值只定义一次；可能被反复赋值的源变量及逻辑表达式合流结果使用槽，因此没有 phi。
每个块以 jump、branch 或 return 结束。每个作用域有自己的名称到槽映射，查找从内到外。

| 语法成分 | 示例 | 转换 |
|---|---|---|
| 函数、形参 | `int f(int x){return x;}` | 新建函数、入口块；`param 0`，绑定槽并 store |
| 块、空语句 | `{int x; ;}` | 压入/弹出作用域；空语句无指令 |
| 声明及常量 | `int x; const int y=2;` | 两个独立槽；仅对 y 写入初值 |
| 赋值、变量 | `x=y;` | 查表 load y；store x |
| 整数、括号 | `(3)` | const 3；括号只影响 AST |
| 一元、二元 | `-(x+2)*3` | 先生成操作数再发射一元/二元指令 |
| 库函数和用户函数 | `putint(f(x));` | 顺序生成实参，call f 再 call putint |
| if 与 else-if | `if(a) f(a); else if(b) f(b);` | 再嵌套 If，短路分别分流到两个臂并合流 |
| while、跳出/继续 | `while(x){if(y)break;continue;}` | 建 test、body、done，使用循环栈中最内层目标 |
| return | `return x;` 或 `return;` | 计算可选值；以 return 结束当前块 |

`cond(a && b, T, F)` 先把 a 的真边指向 RHS 块、假边直达 F；
`||` 对称，`!` 对调目标。普通 `expr` 返回虚拟值；逻辑表达式作为值时两条边
分别把 0 或 1 写入同一槽，再由合流块读取。这样既保留求值顺序，也不会多执行函数调用。

完整示例：`int main(){int x=getint();while(x>0){if(x==2 || (x==1 && getint()))putint(x);x=x-1;}return 0;}`。
入口读 x 并跳到循环 test，条件真时进入 body。x 等于 2 时直接走真边；
x 不等于 2 才判断是否等于 1，只有该判断为真才调用第二个 getint。
if 合流后递减 x，跳回 test；条件为假时 return 0。用 `./compiler -ir` 查看完整指令。

第四部分读取该结构直接选择 RV64GC 指令；第五部分在相同结构上改写基本块和操作数，
随后仍调用同一后端。此调试文本是 ToyC 自定义 IR，不是可交给 llvm-as 的 LLVM IR。
