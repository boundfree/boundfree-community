# BoundFree EEG Native

BoundFree Community 的 C++17 / Qt6 EEG/LFP 科研文件分析上位机。原生主程序负责界面、绘图、DSP、授权与国际化，Python 边车负责科研文件读写。

## GitHub 与 Gitee 的关系

这是同一个 **BoundFree Community** 项目的两个托管地址，共用代码历史、产品版本和发布 tag；不是两套独立产品或开发主线。Community 与商业 Pro 是不同产品，不适用此镜像关系。

| 平台 | 地址与职责 |
| --- | --- |
| GitHub · 研发主仓库 | [Vsonic12138/boundfree-eeg-native](https://github.com/Vsonic12138/boundfree-eeg-native)：Issue、PR、评审、验收、Actions 构建及发布来源 |
| Gitee · 国内镜像 | [BigYellow12138/boundfree-community](https://gitee.com/BigYellow12138/boundfree-community)：同步已验证源码、发布 tag、版本说明和国内安装包下载 |

代码只在 GitHub 经 PR 合入 `master`，验证后单向同步到 Gitee；不在 Gitee 另建 Issue/PR 或独立维护修改。GitHub 当前私有，访问代码、提交 Issue/PR 和下载其产物需要仓库权限；Gitee 当前公开，可浏览源码，已发布官方包允许公开下载但仍需激活。镜像安排不改变源码的 Apache-2.0 权利。

日常开发按 [提交与交付 SOP](docs/contribution-sop.md)，发版按 [版本与发布 SOP](docs/release-sop.md)。两份 SOP 分别说明代码同步与 Release 同步，推送 Git 不能代替发布安装包。

## 当前状态

产品版本 `0.2.2-beta.1`，唯一版本源为 [version.json](version.json)：

```json
{
  "schema_version": 1,
  "product_name": "BoundFree EEG",
  "version": "0.2.2-beta.1",
  "release_channel": "beta",
  "ipc_protocol_version": 1
}
```

已实现文件浏览、原始/处理后数据切换、参考/滤波、频谱、播放与标注、视频同步、录制/导出，以及专用脑电/眼电离线分离工作区。Community 当前聚焦 EEG/LFP 与离线混合信号分析；Pro 的 Spike 采集产品独立维护。本仓库保留的 `bf_spike` 仅为历史原型，不是当前发布承诺。

主线和 PR 均自动构建 Windows x64 Setup 与 Ubuntu 24.04 x86_64 归档。Beta1 的 annotated tag、独立双平台构建与 GitHub 草稿已生成并核对；当前仍是未公开的候选版，Gitee 尚无公开 Release，验收证据与待验项见 [Issue #5](https://github.com/Vsonic12138/boundfree-eeg-native/issues/5)。真实分离效果、目标桌面/干净机/高 DPI、真实授权兼容、完整第三方分发许可及正式版组织签名仍待分别验收，当前不宣称 stable。

## 开源许可与商业扩展

本仓库的自有核心采用 [Apache-2.0](LICENSE)，包含当前 ICA、人工成分标记、分组重建与结果导出；允许科研和商业使用、修改及再分发。第三方作品继续适用各自许可，BoundFree 商标权不随代码许可授予。

未来付费扩展在独立私有仓库维护源码与商业许可，通过公开接口集成，不把专有实现混入本仓库。范围与贡献规则见 [许可与扩展边界](legal/LICENSING.md)。本次仅落实许可：现有官方安装包的整机激活门禁仍待迁移，不能视作免费核心与扩展授权已实现。

官方安装包保留现有整机激活；完成验收并发布后，允许公开下载，使用时仍需有效授权。GitHub 管理发布，Gitee 同步版本说明和国内安装包下载；未验收候选包保持草稿。免费核心免激活及扩展权益迁移留待后续阶段。该官方包交付安排不改变 Apache-2.0 对源码的授权。

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
- [GitHub](https://github.com/Vsonic12138/boundfree-eeg-native) 统一管理 Issue、PR、验收与 Actions；[Gitee](https://gitee.com/BigYellow12138/boundfree-community) 为国内源码与 Release 镜像，不另建 Issue/PR 流程。
- 每次发布后同步两边的版本、tag、说明和安装包校验值。Gitee 超限附件使用分卷与还原脚本，具体下载和同步步骤见发布 SOP；推送源码本身不会同步 Release。

目录：`native/` 原生程序，`sidecar/` 编解码边车，`tests/` 回归，`docs/` 当前文档，`docs/archive/` 历史计划/设计/验收。构建产物和本机配置不入库。
