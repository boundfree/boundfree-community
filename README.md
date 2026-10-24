# BoundFree EEG Native

BoundFree Community 的 C++17 / Qt6 EEG/LFP 科研文件分析上位机。原生主程序负责界面、绘图、DSP、授权与国际化，Python 边车负责科研文件读写。

## 当前状态

产品版本 `0.2.2-dev`，唯一版本源为 [version.json](version.json)：

```json
{
  "schema_version": 1,
  "product_name": "BoundFree EEG",
  "version": "0.2.2-dev",
  "release_channel": "dev",
  "ipc_protocol_version": 1
}
```

已实现文件浏览、原始/处理后数据切换、参考/滤波、频谱、播放与标注、视频同步、录制/导出，以及专用脑电/眼电离线分离工作区。Community 当前聚焦 EEG/LFP 与离线混合信号分析；Pro 的 Spike 采集产品独立维护。本仓库保留的 `bf_spike` 仅为历史原型，不是当前发布承诺。

主线和 PR 均自动构建 Windows x64 Setup 与 Ubuntu 24.04 x86_64 归档。安装态 smoke 已有云端验证；真实分离效果、干净机/高 DPI、正式授权兼容、组织签名与首个实际 beta 草稿仍需分别验收，当前不宣称 stable。

## 开源许可与商业扩展

本仓库的自有核心采用 [Apache-2.0](LICENSE)，包含当前 ICA、人工成分标记、分组重建与结果导出；允许科研和商业使用、修改及再分发。第三方作品继续适用各自许可，BoundFree 商标权不随代码许可授予。

未来付费扩展在独立私有仓库维护源码与商业许可，通过公开接口集成，不把专有实现混入本仓库。范围与贡献规则见 [许可与扩展边界](legal/LICENSING.md)。本次仅落实许可：现有官方安装包的整机激活门禁仍待迁移，不能视作免费核心与扩展授权已实现。

## 使用与开发入口

| 任务 | 入口 |
| --- | --- |
| 下载或重建安装包、更新版本和发布 | [版本与发布 SOP](docs/release-sop.md) |
| 浏览、导出、脑电/眼电分离 | [分析操作说明](docs/analysis-guide.md) |
| 开始开发、提交、PR 与同步远端 | [提交与交付 SOP](docs/contribution-sop.md) |
| 本地 WSL2 / Windows 构建 | [工具链说明](docs/toolchain-wsl2.md) |
| 架构、契约、当前路线与验收边界 | [文档索引](docs/README.md) |
| 代码代理 | [AGENTS.md](AGENTS.md) 与 [community-delivery skill](.agents/skills/community-delivery/SKILL.md)，支持 `$community-delivery` |

首次体验分离可点击“打开教学记录 → 开始分离”；教学记录是自有数学混合源，不是 EEGdenoiseNet。真实记录需要确认通道用途、检查成分并人工标记来源，零线可能表示该组尚无成分；操作与限制见分析说明。

## 仓库与交付规则

- `master` 加短期功能/修复/文档分支，通过 PR 集成；中文 Conventional Commits。
- 普通提交不递增产品版本。交付阶段使用 dev / beta.N / stable，变更由 `version_sync.py` 同步五处元数据；IPC 版本独立维护。
- 普通 CI 生成含运行编号、尝试次数和 SHA 的内部 dev 构建，不回写源码、不创建 tag 或 Release。Artifacts 保留 30 天。
- 对外版本使用 `community-vX.Y.Z[-beta.N]` annotated tag，手动 Release Action 只生成草稿，再按验收和授权决定发布；不覆盖版本与 tag。
- GitHub `origin` 承载 Actions，Gitee `gitee` 保留代码副本；同步 Gitee 不会触发 GitHub 构建。

目录：`native/` 原生程序，`sidecar/` 编解码边车，`tests/` 回归，`docs/` 当前文档，`docs/archive/` 历史计划/设计/验收。构建产物和本机配置不入库。
