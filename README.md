# BoundFree EEG Native

BoundFree EEG Native 是 BoundFree 脑机接口上位机的 C++/Qt6 原生重写项目。本仓库用于承载新的原生主程序、Python 文件编解码边车、工程规范、里程碑计划和后续发布资产。

## Project Status

当前阶段：M0 技术验证完成，M1 工程骨架准备实施。

当前版本源：[version.json](version.json)

```json
{
  "schema_version": 1,
  "product_name": "BoundFree EEG",
  "version": "0.1.0-dev",
  "release_channel": "dev",
  "ipc_protocol_version": 1
}
```

M0 结论为 conditional go：

- IPC 验证通过，M1/M2 初版采用 stdio + JSON 控制 + 长度前缀二进制数据。
- 渲染路径已在 Xvfb/Mesa 软件 OpenGL 下验证，但尚未完成真实 GPU 验收。
- 进入 M2 波形浏览前，必须在真实 Windows/Linux GPU 环境复测 64/128/256 通道渲染帧率。

## Architecture

项目采用 C++/Qt6 原生主程序 + Python 编解码边车架构。

| 模块 | 职责 |
| --- | --- |
| C++/Qt6 主程序 | UI、GPU 波形渲染、DSP、实时管线、授权校验、国际化 |
| Python 边车 | 通过 MNE、pyedflib、h5py、pandas 等库负责所有文件格式读写 |
| IPC 协议 | 控制消息使用 JSON，批量 EEG 数据使用长度前缀二进制 payload |

完整架构见 [docs/architecture-design.md](docs/architecture-design.md)。

## Repository Layout

```text
boundfree-eeg-native/
├── AGENTS.md                    # 仓库级开发指导
├── README.md                    # 项目入口说明
├── version.json                 # 产品版本与 IPC 协议版本源
├── docs/                        # 架构、流程、计划和验收文档
├── native/                      # C++/Qt6 主程序
├── sidecar/                     # Python 编解码边车
└── tests/                       # 黄金文件、回归和冒烟测试
```

## Documentation

文档入口见 [docs/README.md](docs/README.md)。`docs/` 下 Markdown 文件应遵循 [docs/documentation-style.md](docs/documentation-style.md)。

关键文档：

| 文档 | 用途 |
| --- | --- |
| [docs/architecture-design.md](docs/architecture-design.md) | 架构设计与关键技术决策 |
| [docs/git-development.md](docs/git-development.md) | 分支、版本号、发布和 hotfix 规范 |
| [docs/feature-parity-checklist.md](docs/feature-parity-checklist.md) | 新旧版本功能对齐验收清单 |
| [docs/plans/README.md](docs/plans/README.md) | M0-M8 里程碑计划索引 |

## Development Workflow

本仓库采用 `master` 日常开发、`release` 稳定发布、短功能分支并行开发的模型。

- `master`：开发集成分支，默认版本号为 `X.Y.Z-dev`。
- `release`：稳定发布分支，正式 tag 只在该分支创建。
- `feature/*`：从 `master` 切出，用于功能或里程碑开发。
- `fix/*`：从 `master` 切出，用于开发期修复。
- `hotfix/*`：从 `release` 或正式 tag 切出，用于已发布版本的紧急修复。

详细规则见 [docs/git-development.md](docs/git-development.md)。

## Versioning

产品版本遵循 SemVer。`version.json` 是唯一需要人工维护的版本源。后续 C++ 主程序、Python 边车、安装包脚本和程序界面应从该文件读取或生成版本信息。

版本展示要求：

- 开发版示例：`BoundFree EEG 0.1.0-dev+g578f1d3`
- 正式版示例：`BoundFree EEG 1.0.0`
- IPC 协议版本使用 `ipc_protocol_version`，不得与产品版本混用。

## Reference Repository

旧 Python/PySide6 上位机仓库：

```text
../boundfree-eeg-upper-computer
```

该仓库仅作为参考实现、边车迁移来源、性能基线和黄金文件来源。新功能开发和长期演进应在本仓库进行。

## Build Roadmap

| Milestone | Target |
| --- | --- |
| M0 | 技术验证：GPU 波形渲染、C++/Python IPC、旧版性能画像 |
| M1 | 工程骨架：CMake/Qt6、主窗口、边车生命周期、IPC、授权、i18n |
| M2 | 打开文件与波形静态浏览 |
| M3 | DSP 与滤波数值对齐 |
| M4 | PSD、STFT、频段趋势、相干性分析 |
| M5 | 回放控制与事件标注 |
| M6 | 视频同步 |
| M7 | 录制与片段导出 |
| M8 | Windows 打包、代码签名与发布验收 |

详细计划见 [docs/plans/README.md](docs/plans/README.md)。
