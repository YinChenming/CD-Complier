# 概述

本项目为电子科技大学计算机科学与工程学院（网络空间安全学院）开设的挑战性课程《程序设计语言与编译》（即编译原理，余盛季老师授课）的课程大作业。本项目的作业适用2025年的课程要求，未必适用于之后课程的要求。

由于课程要求是在老师给的版本基础上实现**扩展功能**和**性能优化**，并**不要求**在实现扩展功能的基础上再做性能优化。因此，我建立了两个分支分别实现这两部分的要求，`feature`分支用于实现课程需要的扩展功能，`optimize`分支用于实现课程需要的性能优化功能，两个分支**无法合并在一起**。

本项目使用`C`/`C++ 17`、`lex`和`yacc`实现，依赖于`C`/`C++17`的工具链、`flex`和`bison`（`yacc`）以及`make`。本项目可以在`MacOS 26.1 (25B78)`上的`Apple clang version 17.0.0`、`flex 2.6.4 Apple(flex-35)`、`bison (GNU Bison) 2.3`（`XCode 26.1.1 (17B100)`捆绑的工具链）和`WSL2 Ubuntu 24.04.3 LTS`上的`gcc 13.3.0`、`flex 2.6.4`、`bison (GNU Bison) 3.8.2`（`GCC`的工具链）上通过编译。

# 项目介绍

本项目实现了一个完整编译器的**前端**和**后端**，可以将自定义的`mini`语言源代码编译得到可在自定义虚拟机上运行的**二进制机器码**。

不论是`master`分支、`feature`分支还是`optimize`分支，其运行的流程都是类似的。首先，项目会编译得到三个可执行文件——`mini`、`asm`和`machine`，其中`mini`会把`.m`源代码编译到`.s`汇编，`asm`会把`.s`汇编编译成供`machine`运行的`.o`二进制机器码。

由于课程提供了两个虚拟机，一个只支持一次性操作**4个字节**，另一个支持**1个字节**和**4个字节**，并且可以报告运行的时钟周期数（会有两个虚拟机当然是因为老师一开始没写完新虚拟机）。`makefile`对两个虚拟机都做了支持。运行下面的语句可以编译得到支持第一个虚拟机的`mini`、`asm`和`machine`：

```bash
make
```

运行下面的语句可以编译得到支持第二个虚拟机的`mini`、`asm`和`machine`：

```bash
make new
```

`mini`、`asm`和`machine`的使用方法都是类似的，以`mini`为例，运行下面语句就能将`.m`源代码编译得到同名的`.x`（三地址码序列）和`.s`（汇编）文件：

```bash
./mini source_file.m
```

中间编译结果统一输出到了`./out`目录下，如果希望清除中间编译结果与编译器的运行结果，可以运行：

```bash
make clean
```

# 课程要求

这里我给出我们这一年的课程项目要求以供参考。按照余老师的说法，每年课程项目的要求都会有不同，会与时俱进，所以这里也只能代表今年的情况。

今年我们要求实现两部分内容，一部分是要扩展原有的`mini`文法实现更多的功能；另一部分是要实现后端的优化，不过只需要在原有的`mini`文法基础上实现。

对于功能测试，测评方式是老师给出一系列测试用例，我们的编译器应当能正常编译这些测试用例并得到正确的运行结果；对于性能测试，同样是老师给出一系列测试用例，老师提供的虚拟机会汇报程序运行的时钟周期数，最后根据自己编译得到的机器码在虚拟机上运行的时钟周期数排榜排名。

## 1. 基本`mini`语法

`mini`语言的设计基本参考了`C`语言，整个程序分为全局变量声明和函数声明，程序的入口在`main`函数。在数据结构上只支持`int`类型，不论常量还是变量，因此在函数声明时**不要求**也**不能**带上变量类型，`max(a,b)`就能“声明”一个函数（`mini`语言不支持前向声明，这里指的是后面跟上大括号包裹的主体就能定义一个函数）。关键字包括`if` `else` `while` `input` `output` `return`，语义和`C`语言中相同。运算符支持`+` `-` `*` `/` `==` `!=` `>=` `<=` `>` `<`。具体文法如下：

```yacc
<program> ::= <function_declaration_list>
<function_declaration_list> ::= <function_declaration> | <function_declaration_list> <function_declaration>
<function_declaration> ::= <function> | <declaration>
<declaration> ::= <INT> <variable_list> ";"
<variable_list> ::= <IDENTIFIER> | <variable_list> "," <IDENTIFIER>
<function> ::= <function_head> "(" <parameter_list> ")" <block>
<function_head> ::= <IDENTIFIER>
<parameter_list> ::= E | <IDENTIFIER> | <parameter_list> "," <IDENTIFIER>
<statement> ::= <assignment_statement> ";" | <input_statement> ";" | <output_statement> ";" | <call_statement> ";" | <return_statement> ";" | <if_statement> | <while_statement> | <block>
<block> ::= "{" <declaration_list> <statement_list> "}"
<declaration_list> ::= E | <declaration_list> <declaration>
<statement_list> ::= <statement> | <statement_list> <statement>
<assignment_statement> ::= <IDENTIFIER> "=" <expression>
<expression> ::= <expression> "+" <expression> | <expression> "-" <expression> | <expression> "*" <expression> | <expression> "/" <expression> | "-" <expression> | <expression> "==" <expression> | <expression> "!=" <expression> | <expression> "<" <expression> | <expression> "<=" <expression> | <expression> ">" <expression> | <expression> ">=" <expression> | "(" <expression> ")" | <INTEGER> | <IDENTIFIER> | <call_expression>
<argument_list> ::= E | <expression_list>
<expression_list> ::= <expression> | <expression_list> "," <expression>
<input_statement> ::= "input" <IDENTIFIER>
<output_statement> ::= "output" <IDENTIFIER> | "output" <TEXT>
<return_statement> ::= "return" <expression>
<if_statement> ::= "if" "(" <expression> ")" <block> | "if" "(" <expression> ")" <block> "else" <block>
<while_statement> ::= "while" "(" <expression> ")" <block>
<call_statement> ::= <IDENTIFIER> "(" <argument_list> ")"
<call_expression> ::= <IDENTIFIER> "(" <argument_list> ")"
<INTEGER> ::= [0-9]*
<IDENTIFIER> ::= [A-Za-z]([A-Za-z]|[0-9])*
<TEXT> ::= "\"[^\"]*\""
```

## 2. 扩展`mini`语法（40分）

扩展`mini`语法至少应该支持下面的内容，具体语法与`C`语言相同：

- 扩展数据类型，支持`char`；
- 支持**指针**和**指针变量声明**和**取地址运算符**`&`、**解引用运算符**`*`；
- 支持**（多维）数组**和**下标运算符**`[]`，其中下标运算符第二操作数只会是**常量**，即形如`a[1]`是合法的，而`a[i]`是非法的；
- 支持**结构体**和**成员运算符**`.` `->`，其中结构体需要支持**嵌套结构体**，结构体成员需要支持`int`和`char`成员、指针成员、数组成员，不要求实现数组指针座位数组成员；
- 支持`while`循环和`for`循环，支持`break`和`continue`语句；
- 支持`switch`、`case`和`default`；
- 支持`char`类型作为函数的参数和返回值。

## 3. 编译器优化（20分）

这块没有强制要求实现的编译器优化算法，理论上你可以什么都不实现。这部分老师会提供一个**基线**版本，实现了一些基本的局部优化和全局优化，包括但不限于**常量折叠**、**常量传播**、**复制传播**、**死代码删除**、**消除公共子表达式**、**循环不变量外提**等。如果你的编译器性能超过了老师的基线版本，那么这20分就拿到了；如果没有超过，那会在两种算法中取得分高的一个——一个是评估你的编译器与老师编译器性能差距，按百分比给分；一个是在所有剩下的人中排名，根据排名从高到低给分。

这里补充一句，由于我们今年测试用例设计的不太好（测试用例可以由同学投稿），都是针对循环的测试用例，导致**循环不变量外提**和**寄存器分配**的收益极大，逼近老师的基线相对来说还是很容易的。
