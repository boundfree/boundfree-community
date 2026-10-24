---
title: 文档索引
doc_type: index
status: active
owner: BoundFree
last_updated: 2026-10-23
---

# 文档索引

日常只读本目录的当前文档。已完成的实施计划、逐次验收报告、旧设计和配图统一保存在 [archive/](archive/README.md)，用于追溯，不作为新的实施指令。

| 要做什么 | 阅读入口 |
| --- | --- |
| 了解产品范围、现状和下一步 | [roadmap.md](roadmap.md) |
| 使用浏览、导出和脑电/眼电分离 | [analysis-guide.md](analysis-guide.md) |
| 理解模块和数据流 | [architecture-design.md](architecture-design.md) |
| 修改 IPC、数值格式、错误码或翻译 | [contracts.md](contracts.md) |
| 查询分支、提交、版本和文档规则 | [development-guide.md](development-guide.md) |
| 完成修改、提交、PR、主线构建和远端同步 | [contribution-sop.md](contribution-sop.md) |
| 更新交付版本、下载安装包或发布 Release | [release-sop.md](release-sop.md) |
| 配置本地 WSL2 / Windows 构建与验证 | [toolchain-wsl2.md](toolchain-wsl2.md) |
| 检查能力覆盖和仍未完成的验收 | [feature-parity-checklist.md](feature-parity-checklist.md) |

代码代理先读根目录 [AGENTS.md](../AGENTS.md)，再加载 [community-delivery](../.agents/skills/community-delivery/SKILL.md)。版本以 [version.json](../version.json) 为准；具体发布证据保存在该版本的验收 Issue，不能把旧报告里的包名当作最新下载入口。

## 维护规则

- 本目录描述当前状态和可复用流程；历史命令、测试计数和逐次交付记录进入 `archive/` 或相应 Issue。
- 新增计划仅在确有未执行任务时创建 `plans/`；完成或被替代后及时归档，不预建空目录。不在当前索引列出逐次报告。
- 归档前将仍有效的使用说明、约束和未验收项合并到上述当前文档；归档不表示验收通过。
- 规则变更在同一 PR 核对规范、SOP、skill 和模板，保持一份操作真值；修改文档路径时同步检查链接和脚本/测试引用。
