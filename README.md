# BoundFree EEG Native

BoundFree EEG Native 是 BoundFree 脑电上位机的 C++/Qt6 原生重写项目。新架构采用原生主程序承载 UI、渲染、DSP、授权和国际化，用 Python 编解码边车保留 MNE、pyedflib、h5py、pandas 等科研文件格式能力。

## Current Status

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

当前阶段：M0–M2 门禁与静态浏览已完成；M3 DSP 滤波与黄金比对已落地。下一阶段为 M4 频谱分析。产品版本 0.1.0-dev。

v1 stable 产品边界：BoundFree EEG Native v1 stable 是面向动物电生理科研场景的通用 EEG/LFP/电生理文件分析软件。v1 的主线是完整迁移旧 Python/PySide6 版的文件打开、懒加载、波形浏览、DSP、频谱分析、事件标注、视频同步、片段导出、录制与 Session 骨架、离线授权和 Windows 发布能力。

自研 `ADS1299-4 + ESP32-C3`、BLE 实时采集、真实设备录制、设备状态监控和 12 小时采集长稳测试不作为 v1 stable 门槛，进入 v1.1/v2。v1 仅保留数据源、Session、录制和实时管线的扩展边界。

M0 结论：

- IPC 可进入 M1：初版采用 stdio + JSON 控制消息 + 长度前缀二进制 payload。
- 渲染为 conditional go：Xvfb/Mesa 软件 OpenGL 已验证代码路径，但真实 GPU 验收仍是 M2 前硬门禁。
- 旧 Python 版继续作为参考实现、性能基线和黄金文件来源。

## First Phase

M1-A 只实现基础骨架，不进入业务功能：

- `bf_app --version` 从 `version.json` 输出版本。
- Qt6 空主窗口可启动，显示版本、状态栏和语言菜单。
- i18n 框架落地，支持 `zh_CN`、`en_US`、`pseudo` 实际切换。
- 错误码框架落地，后端统一返回 `code + params`，UI 负责本地化文案。
- Python 边车正式包支持 `--version`、`--stdio`、`hello`。
- C++ 主程序可启动边车、握手、超时处理，并在关闭时停止边车。

不在 M1-A 做真实文件打开、波形渲染、DSP、完整授权校验、录制和导出。

## Documentation

日常开发优先阅读：

| 文档 | 用途 |
| --- | --- |
| [docs/roadmap.md](docs/roadmap.md) | v1 边界、M0 结论、M1-M8 路线和阶段门禁 |
| [docs/phase-1-foundation.md](docs/phase-1-foundation.md) | M1-A 第一阶段执行计划 |
| [docs/contracts.md](docs/contracts.md) | 版本、IPC、错误码、i18n 基础契约 |
| [docs/development-guide.md](docs/development-guide.md) | 分支、版本、文档和验证规范 |
| [docs/architecture-design.md](docs/architecture-design.md) | 长期架构设计 |
| [docs/feature-parity-checklist.md](docs/feature-parity-checklist.md) | 新旧版本功能对齐验收清单 |

历史计划和旧规范已归档到 [docs/archive/](docs/archive/)。

## Repository Layout

```text
boundfree-eeg-native/
├── AGENTS.md                    # 仓库级开发指导
├── README.md                    # 项目入口说明
├── version.json                 # 产品版本与 IPC 协议版本源
├── docs/                        # 当前核心文档与历史归档
├── native/                      # C++/Qt6 主程序
├── sidecar/                     # Python 编解码边车
└── tests/                       # 黄金文件、回归和冒烟测试
```

## Branch And Version Model

- 日常开发基于 `master`。
- 当前第一阶段开发以 M1-A 骨架为基础继续演进；后续功能分支按 `feature/*`、`fix/*`、`docs/*` 拆分。
- `release` 是稳定发布占位分支，首个 `rc` 或 `stable` 版本前不打正式 tag。
- 普通功能分支不修改 `version.json` 的产品版本号。
- 产品版本和 `ipc_protocol_version` 分开维护。
