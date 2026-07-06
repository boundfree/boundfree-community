# BoundFree EEG 上位机 · C++/Qt6 原生版

BoundFree 脑机接口上位机的 **C++/Qt6 原生重写版**。在保持旧 Python 版全部功能不变的前提下，
迁移到原生主程序以获得更高的性能天花板、更确定的实时延迟（摆脱 GIL）和更长的可维护寿命。

## 架构一句话

> **C++/Qt6 原生主程序**（UI · GPU 渲染 · DSP · 实时管线 · 授权）
> **＋ Python 编解码边车**（用 MNE/pyedflib/h5py 负责所有文件格式读写）。

完整设计见 [`docs/architecture-design.md`](docs/architecture-design.md)。

## 版本与分支管理

本仓库采用 `master` 日常开发、`release` 稳定发布、短功能分支并行开发的模型。
产品版本号的单一来源是根目录 [`version.json`](version.json)，后续主程序、Python 边车和安装包都应从该文件读取或生成版本信息。

Git 开发、版本号更新、发布和 hotfix 规则见 [`docs/git-development.md`](docs/git-development.md)。

## 仓库关系

| 仓库 | 角色 |
| --- | --- |
| `boundfree-eeg-native`（本仓库） | 新的 C++/Qt6 主程序 + Python 边车，**唯一继续演进的仓库** |
| `../boundfree-eeg-upper-computer`（旧 Python 版） | **仅作参考**：边车实现基础、DSP/交互参考、黄金文件产生器 |

## 目录规划

```
boundfree-eeg-native/
├── docs/
│   ├── architecture-design.md      # 架构设计（spec）
│   ├── feature-parity-checklist.md # 功能对齐验收清单
│   └── plans/                      # 分里程碑的实施计划
├── native/                         # C++/Qt6 主程序（随 M1 起建）
├── sidecar/                        # Python 编解码边车（随 M1 起建）
└── tests/                          # 黄金文件与回归测试
```

## 实施路线

采用**滚动式规划（rolling-wave）**：Milestone 0 技术验证 spike 先行并已详细拆解；
M1–M8 已给出任务级计划，其最细粒度步骤在各自前置里程碑通过后再定稿。
计划索引见 [`docs/plans/README.md`](docs/plans/README.md)。
