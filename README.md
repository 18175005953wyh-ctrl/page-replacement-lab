# Page Replacement Lab

C11 页面置换算法实验：FIFO、LRU、OPT，支持逐步页框快照、同输入对照和 Belady 异常检测。仅使用 C 标准库，无第三方依赖。

## 项目目标

用可复现的访问序列观察页面置换，而非实际管理操作系统内存。页面是被访问的数据单位，页框是容纳页面的位置；访问的页面不在页框中时发生缺页。页框未满时装入页面不计为替换。

## 算法设计

- FIFO：淘汰最早装入的页面，命中不改变队列顺序。
- LRU：淘汰最近最久未使用的页面。每次访问推进一次整数时钟，命中也更新时间；检查时钟溢出。
- OPT：淘汰下一次访问最远的页面，永不再访问者优先。先排序、压缩页面编号，逆序预计算每个位置的下一次访问位置，模拟时不反复扫描后续序列。它依赖未来信息，是离线理论基准。

空页框优先；受害页选择出现平局时按页框下标决定。比较模式为每种算法独立初始化状态，不修改原始输入。逐步快照通过回调交给输出模块。

## Belady 异常

增加页框数量后，FIFO 的缺页次数可能反而增加。经典序列 `1 2 3 4 1 2 5 1 2 3 4 5` 中，FIFO 从 3 页框的 9 次缺页增加到 4 页框的 10 次。更多页框改变了先进先出的淘汰顺序，并不保证保留下一次需要的页面。LRU、OPT 具有栈性质，不会出现这种异常；程序检查其缺页曲线不递增。

## 项目结构

```text
include/       trace、算法和输出接口
src/           CSV 以外的逐行 trace 解析、算法、输出、命令行
data/          固定实验输入
tests/         核心测试与命令行集成检查
docs/experiments/  实际运行的原始输出
screenshots/   手动运行和 CI 截图
```

## 构建与运行

需要 C11 编译器、CMake 3.20 或以上。Windows 可在 x64 Native Tools Command Prompt for VS 2022 中运行：

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DSTRICT_WARNINGS=ON
cmake --build build
ctest --test-dir build --output-on-failure -V
build\page_replacement_lab.exe --trace data/belady.trace --frames 3 --compare
```

Linux：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSTRICT_WARNINGS=ON
cmake --build build
ctest --test-dir build --output-on-failure -V
./build/page_replacement_lab --trace data/belady.trace --frames 3 --compare
```

Visual Studio 多配置生成器的可执行文件通常在 `build/Debug/`，测试时加 `-C Debug`。

## 输入与命令行

每行一个十进制页面编号，范围 `0..4294967295`。允许首尾空白、空行、以 `#` 开头的整行注释，不接受行内注释、负数、十六进制、小数或多余字段。错误包含文件路径与物理行号；读取失败不会模拟已读取的部分。每行最多 255 字节，CRLF 的 CR 也占一个字节；空输入合法，缺页率显示 `N/A`。

```bat
build\page_replacement_lab.exe --trace data/belady.trace --frames 3 --policy fifo --verbose
build\page_replacement_lab.exe --trace data/belady.trace --frames 3 --policy lru
build\page_replacement_lab.exe --trace data/belady.trace --frames 3 --policy opt
build\page_replacement_lab.exe --trace data/belady.trace --frames 3 --compare
build\page_replacement_lab.exe --trace data/belady.trace --belady 2 6
build\page_replacement_lab.exe --help
```

默认 FIFO，策略名称严格使用小写。页框范围为 1..1024。`--compare` 不能与 `--policy` 同用；`--belady MIN MAX` 不能与 `--frames`、`--policy`、`--compare`、`--verbose` 同用。重复参数、未知参数、缺失值均返回非零退出码。`--help` 单独使用。

## 真实对照实验

2026-10-12，Windows x64、MSVC 19.44、NMake Debug 严格警告构建。以下代码与输入对应提交 `e5da10b56362a78568d8b5c047bdf3fe644f247c`；后续文档提交不改变实验代码。

固定 `data/belady.trace`、3 页框，运行上面的 `--compare` 命令：

| Policy | Accesses | Hits | Faults | Replacements | Fault rate |
|---|---:|---:|---:|---:|---:|
| FIFO | 12 | 3 | 9 | 6 | 75.00% |
| LRU | 12 | 2 | 10 | 7 | 83.33% |
| OPT | 12 | 5 | 7 | 4 | 58.33% |

运行 `--belady 2 6` 的实际结果：

| Frames | FIFO faults | LRU faults | OPT faults |
|---|---:|---:|---:|
| 2 | 12 | 12 | 9 |
| 3 | 9 | 10 | 7 |
| 4 | 10 | 8 | 6 |
| 5 | 5 | 5 | 5 |
| 6 | 5 | 5 | 5 |

程序输出 `Belady anomaly detected`，并指出 FIFO 的 3→4 页框、9→10 次缺页。

这组输入中 LRU 并未优于 FIFO。另一组 `data/locality.trace`（3 页框）中，FIFO 为 7 次缺页，LRU、OPT 均为 5 次：反复访问 0、1，使 LRU 保留这些最近使用的页面，FIFO 的装入顺序不会因命中更新。结果依赖访问序列，不能预设某个在线算法总是最好。

原始输出：[三策略比较](docs/experiments/compare-three.txt)、[页框扫描](docs/experiments/belady-scan.txt)、[FIFO 逐步快照](docs/experiments/fifo-verbose.txt)、[局部性对照](docs/experiments/locality-compare.txt)。

## 自动化测试

本地实际运行：**35 项核心测试、55 项命令行检查全部通过，共 90 项**。CTest 将其组织为两个测试入口，原始汇总为：

```text
100% tests passed, 0 tests failed out of 2
```

覆盖解析错误、行号、空输入、动态扩容、最大页面编号、FIFO 命中不改顺序、LRU 时钟更新、OPT 下一次访问、独立状态、参数冲突、快照、Belady 异常及统计不变量。对 729 条长度为 6 的小序列、1..3 页框，使用独立穷举受害页的 oracle 校验 OPT 最优缺页数，并检查 LRU/OPT 曲线。

始终检查 `hits + faults == accesses`、`replacements <= faults`；比较模式检查 OPT 缺页数不大于 FIFO/LRU。

## GitHub Actions

工作流 `Page Replacement Lab tests` 在 push、pull_request 或手动触发时，对 `ubuntu-latest / Debug` 和 `windows-latest / Debug` 严格构建并运行测试。

**远程 CI 尚未确认：仓库尚未发布。** 发布后应记录实际通过的提交编号和 Actions 运行链接；本地通过不能替代双平台远程结果。

## 时间复杂度

设访问数为 N、不同页面数为 U、页框数为 F。FIFO/LRU 的实现每次扫描页框，模拟为 O(NF)，页框空间为 O(F)。OPT 复制并排序 N 个编号、压缩后进行二分查找，预处理为 O(N log N + N log U)，模拟为 O(NF)，总辅助空间为 O(N+U+F)。扫描多个页框配置会分别执行模拟和 OPT 预处理；verbose 输出量随 N×F 增长。

## 与缓存模拟器的区别

这里比较虚拟内存页面的置换，仅使用页面编号和页框数量。缓存模拟器还涉及字节地址、块大小、组索引和相联度；本项目没有缓存组映射，也不把缓存缺失分类混入页面置换实验。

## 当前限制

不模拟页表、TLB、脏页、磁盘 I/O、真实缺页耗时或真实操作系统进程。OPT 需要完整未来序列；输入全部保存在内存中。大输入或大范围扫描可能耗时，逐步输出尤其大。输入要求普通文本，不支持 UTF-8 BOM。命令行输出为英文，便于跨平台核对。
