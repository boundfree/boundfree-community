---
title: 文档索引
doc_type: index
status: active
owner: BoundFree
last_updated: 2026-11-10
---

# 文档索引

本目录只放当前有效的文档。按你要做的事找入口：

## 使用软件

| 要做什么 | 文档 |
| --- | --- |
| 打开数据文件、浏览波形、滤波、频谱分析、导出、脑电/眼电/肌电分离 | [analysis-guide.md](analysis-guide.md) |
| 连接设备（模拟器、BFSP、RT-BCI）、实时查看、原始录制、设备连续 ICA | [device-acquisition.md](device-acquisition.md) |

## 理解和修改代码

| 要做什么 | 文档 |
| --- | --- |
| 了解程序组成、代码放在哪里、数据怎么流动 | [architecture-design.md](architecture-design.md) |
| 修改 IPC、数值格式、错误码或翻译 | [contracts.md](contracts.md) |
| 开发 BFSP 设备协议或新的传输方式 | [bfsp-protocol.md](bfsp-protocol.md)、[bfsp-transports.md](bfsp-transports.md) |
| 使用 Fluent-Qt 控件和统一主题 | [fluentqt-integration.md](fluentqt-integration.md) |
| 搭建本地环境（路径变量与外置构建目录）、构建、同步 Windows 编译副本 | [toolchain-wsl2.md](toolchain-wsl2.md) |

## 提交和发布

| 要做什么 | 文档 |
| --- | --- |
| 从建分支到合入主线的步骤 | [contribution-sop.md](contribution-sop.md) |
| 分支命名、提交格式、评审规则、文档格式 | [development-guide.md](development-guide.md) |
| 版本规则、安装包核验、Release、Gitee 国内下载 | [release-sop.md](release-sop.md) |
| 开源许可、贡献归属、商业扩展边界 | [../legal/LICENSING.md](../legal/LICENSING.md) |

## 了解现状

| 要做什么 | 文档 |
| --- | --- |
| 产品范围、已有能力、当前优先事项 | [roadmap.md](roadmap.md) |
| 哪些已实现、哪些还没验收 | [feature-parity-checklist.md](feature-parity-checklist.md) |
| 接下来要做的结构与文档调整（临时计划，做完即删） | [restructure-plan.md](restructure-plan.md) |

代码代理先读根目录 [AGENTS.md](../AGENTS.md)，再加载仓库技能 [community-delivery](../.agents/skills/community-delivery/SKILL.md)。版本以 [version.json](../version.json) 为准。

## 每份文档管什么

同一件事只在一份文档里展开，其他地方用链接指过去：

| 文档 | 只管 | 不管 |
| --- | --- | --- |
| `AGENTS.md` | 规则和入口 | 功能现状、进度 |
| `architecture-design.md` | 结构、分层、数据流、行为约束 | 测试计数、安装包名称、操作步骤 |
| `roadmap.md` | 范围、已有能力、优先事项 | 实现细节 |
| `feature-parity-checklist.md` | 已实现与未验收的清单 | 流程 |
| `development-guide.md` | 规则 | 操作命令 |
| `contribution-sop.md` | 日常交付步骤 | 版本与发版 |
| `release-sop.md` | 版本、打包、发布 | 日常提交 |
| `analysis-guide.md`、`device-acquisition.md` | 怎么用 | 内部实现 |

## 维护规则

- 本目录描述当前状态。完成或被替代的计划、报告和旧设计直接删除，靠 Git 历史追溯；删除前把仍有效的说明、约束和未验收项并入当前文档。删除不表示验收通过。
- 逐次交付的命令、测试计数和证据记录在对应的 PR 或验收 Issue，不写进本目录。
- 确有未执行的任务时才临时建计划文档，完成后删除。
- 新增、改名或删除文档时更新本索引，并检查链接和脚本、测试里的路径引用。
- 流程规则变化时，在同一个 PR 里核对规范、SOP、技能和模板。
