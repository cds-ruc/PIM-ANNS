# PIMANN

唯一代码仓库。默认分支以 PIMANN-TOS/main 为基础；`experiments/single-task/` 保存 TOS 单 PU 微基准及源码模板。论文、正式画图脚本和必要绘图输入统一在相邻的 `../PIMANN-paper/`。

## 保留的版本

| 分支 | 用途 |
|---|---|
| `main` | TOS 系统代码及微基准 |
| `versions/atc-ae` | 公开 ATC AE 代码与原实验协议 |
| `versions/atc-development` | 早期 ATC 开发版本 |
| `versions/atc-debug` | 原 debug 历史 |
| `versions/rag` | RAG/Python 接口版本 |
| `versions/single-task` | 微基准原始历史 |
| `versions/sdk-source` | 独立 SDK 源码及其改动 |

这些是同一个 Git 仓库内的版本分支。TOS 与 RAG 的 host、内核和 Python 接口不同，尚未做功能合并，不能只复制 Python 文件后声称全部功能已兼容。原提交号见 `CONSOLIDATION.json`。

## 构建和实验

原环境及使用说明见 [README-upstream.md](README-upstream.md)。保留改造过的 FAISS、UPMEM SDK、内核变体、数据配置和原始参数扫描列表。

```bash
cmake -S . -B build -DFAISS_ENABLE_GPU=OFF -DBUILD_TESTING=OFF
cmake --build build --target main -j
```

2026-10-27 本机 CMake 配置成功；DPU 构建因缺少 `dpu-upmem-dpurte-clang` 停止，未运行硬件实验。旧实验脚本仍含原机器路径，执行前需按机器设置路径；归档未改动实验控制和扫描参数。

旧预生成图、重复日志和临时输出已从主线工作目录移除。执行脚本仍负责生成新实验结果；正式画图入口为论文仓库 `figures/experiments/scripts/build.py`。必要日志只保存在论文仓库中。
