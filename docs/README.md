---
title: 文档索引
doc_type: index
status: active
owner: BoundFree
last_updated: 2026-11-02
---

# 文档索引

日常只读本目录的当前文档。已完成的实施计划、逐次验收报告、旧设计和配图统一保存在 [archive/](archive/README.md)，用于追溯，不作为新的实施指令。

| 要做什么 | 阅读入口 |
| --- | --- |
| 了解产品范围、现状和下一步 | [roadmap.md](roadmap.md) |
| 打开数据文件、设备录制文件夹、内置示例、通道用途、IC切换/分类、分组贡献、波形坐标与滚轮快捷操作 | [analysis-guide.md](analysis-guide.md) |
| 连接设备并在主波形区查看、RT-BCI 8/16 通道选择与 HTTP 诊断、ICA 模拟信号、周期波形与原始录制 | [device-acquisition.md](device-acquisition.md) |
| 开发 BFSP 设备协议与传输适配 | [bfsp-protocol.md](bfsp-protocol.md)、[bfsp-transports.md](bfsp-transports.md) |
| 理解模块和数据流 | [architecture-design.md](architecture-design.md) |
| 修改 IPC、数值格式、错误码或翻译 | [contracts.md](contracts.md) |
| 查询分支命名、评审合入规则、平台保护与提交格式 | [development-guide.md](development-guide.md) |
| 从功能需求建分支、提交 PR、评审返修、合入、主线 CI、镜像同步与清理 | [contribution-sop.md](contribution-sop.md) |
| 版本规则/更新、安装包核验、Release 与 Gitee 国内下载 | [release-sop.md](release-sop.md) |
| 配置统一 Qt 基线、Linux 源码 / Windows 编译副本分工、安全同步与验证 | [toolchain-wsl2.md](toolchain-wsl2.md) |
| 检查能力覆盖和仍未完成的验收 | [feature-parity-checklist.md](feature-parity-checklist.md) |
| 确定开源许可、贡献与商业扩展归属 | [许可与扩展边界](../legal/LICENSING.md) |

Fluent-Qt 控件、统一主题、分离页分区、共用拖动分隔条与回归入口见 [fluentqt-integration.md](fluentqt-integration.md)。

代码代理先读根目录 [AGENTS.md](../AGENTS.md)，再加载 [community-delivery](../.agents/skills/community-delivery/SKILL.md)。版本以 [version.json](../version.json) 为准；具体发布证据保存在该版本的验收 Issue，不能把旧报告里的包名当作最新下载入口。

同一规则只在所属文档展开：开发规范管约束，提交 SOP 管日常交付，发布 SOP 管版本和发布。README、代理入口和技能保留导航与关键约束；跨流程用链接交接，不复制整套步骤。

## 维护规则

- 本目录描述当前状态和可复用流程；历史命令、测试计数和逐次交付记录进入 `archive/` 或相应 Issue。
- 新增计划仅在确有未执行任务时创建 `plans/`；完成或被替代后及时归档，不预建空目录。不在当前索引列出逐次报告。
- 归档前将仍有效的使用说明、约束和未验收项合并到上述当前文档；归档不表示验收通过。
- 规则变更在同一 PR 核对规范、SOP、skill 和模板，保持一份操作真值；修改文档路径时同步检查链接和脚本/测试引用。

## 当前实施计划

- [统一设备与录制文件的信号工作台](../archive/plans/20261030-unified-signal-workbench.md)：按用户指定存放于 `archive/plans/`，实施状态与验收边界以计划正文为准。
