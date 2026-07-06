---
title: WSL2 Qt 工具链
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-07-06
---

# WSL2 Qt 工具链

BoundFree EEG Native 当前在 WSL2 中使用 Qt6/conda 工具链构建。仓库通过稳定入口引用工具链，避免在 CMake 命令中直接写 conda 环境内部路径。

## Toolchain Path

稳定入口：

```bash
/home/vsonic12138/workspace/BoundFree_Workspace/toolchains/qt6
```

当前指向：

```bash
/home/vsonic12138/miniconda3/envs/bf-qt6-spike
```

如需迁移 Qt 环境，更新 symlink 即可：

```bash
ln -sfn /path/to/qt6-env /home/vsonic12138/workspace/BoundFree_Workspace/toolchains/qt6
```

## Build

从 `native/` 目录执行：

```bash
cmake --preset wsl2-qt6-conda
cmake --build --preset wsl2-qt6-conda
```

## Verify

M1-A 基础验证：

```bash
scripts/verify_m1_foundation.sh
```

快速验证跳过 CMake build 和 UI smoke，适合已经构建过的本地迭代：

```bash
scripts/verify_m1_foundation.sh --fast
```
