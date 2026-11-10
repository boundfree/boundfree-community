# BoundFree EEG Native

BoundFree Community 的 C++17 / Qt6 EEG/LFP 科研文件分析上位机。原生主程序负责界面、绘图、DSP、授权与国际化，Python 边车负责科研文件读写。

## GitHub 与 Gitee 的关系

这是同一个 **BoundFree Community** 项目的两个托管地址，共用代码历史、产品版本和发布 tag；不是两套独立产品或开发主线。Community 与商业 Pro 是不同产品，不适用此镜像关系。

| 平台 | 地址与职责 |
| --- | --- |
| GitHub · 研发主仓库 | [boundfree/boundfree-community](https://github.com/boundfree/boundfree-community)：Issue、PR、评审、验收、Actions 构建及发布来源 |
| Gitee · 国内镜像 | [BigYellow12138/boundfree-community](https://gitee.com/BigYellow12138/boundfree-community)：同步已验证源码、发布 tag、版本说明和国内安装包下载 |

GitHub 与 Gitee 均公开；协作权限、远端配置和单向同步规则统一见提交 SOP。

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

主线和 PR 均自动构建 Windows x64 Setup 与 Ubuntu 24.04 x86_64 归档。Beta1 的 annotated tag、独立双平台构建与 GitHub 草稿已生成并核对；当前仍是未公开的候选版，Gitee 尚无公开 Release，验收证据与待验项见 [Issue #5](https://github.com/boundfree/boundfree-community/issues/5)。真实分离效果、目标桌面/干净机/高 DPI、真实授权兼容、完整第三方分发许可及正式版组织签名仍待分别验收，当前不宣称 stable。

## 开源许可与商业扩展

本仓库的自有核心采用 [Apache-2.0](LICENSE)，包含当前 ICA、人工成分标记、分组重建与结果导出；允许科研和商业使用、修改及再分发。第三方作品继续适用各自许可，BoundFree 商标权不随代码许可授予。

未来付费扩展在独立私有仓库维护源码与商业许可，通过公开接口集成，不把专有实现混入本仓库。范围与贡献规则见 [许可与扩展边界](legal/LICENSING.md)。本次仅落实许可：现有官方安装包的整机激活门禁仍待迁移，不能视作免费核心与扩展授权已实现。

官方包仍需激活，免费核心与扩展权益迁移尚未实现；公开下载、签名和验收安排统一见 [发布 SOP 第 5 节](docs/release-sop.md#5-对外发布门槛)，不改变 Apache-2.0 源码权利。

激活时可在线申请授权，等待管理员审批后自动激活；无法联网时可粘贴管理员提供的离线许可证。在线授权到期后，管理员续期即可在下次打开激活窗口时自动领取。

## 使用与开发入口

本地采用 Linux/WSL 开发、Windows 副本编译的分工；Windows 副本从 Linux 单向同步。目录、环境与同步保护见 [工具链说明](docs/toolchain-wsl2.md)。

| 任务 | 入口 |
| --- | --- |
| 下载或重建安装包、更新版本和发布 | [版本与发布 SOP](docs/release-sop.md) |
| 浏览、导出、脑电/眼电分离 | [分析操作说明](docs/analysis-guide.md) |
| 功能需求、建分支、PR 评审合入、同步与清理 | [提交与交付 SOP](docs/contribution-sop.md) |
| 本地 WSL2 / Windows 构建 | [工具链说明](docs/toolchain-wsl2.md) |
| 架构、契约、当前路线与验收边界 | [文档索引](docs/README.md) |
| 代码代理 | [AGENTS.md](AGENTS.md) 与 [community-delivery skill](.agents/skills/community-delivery/SKILL.md)，支持 `$community-delivery` |

首次体验分离可点击“打开教学记录 → 开始分离”；教学记录是自有数学混合源，不是 EEGdenoiseNet。真实记录需要确认通道用途、检查成分并人工标记来源，零线可能表示该组尚无成分；操作与限制见分析说明。



目录：`native/` 原生程序，`sidecar/` 编解码边车，`tests/` 回归，`docs/` 当前文档。历史计划、报告和旧设计不保留在仓库，靠 Git 历史追溯。构建产物和本机配置不入库。
