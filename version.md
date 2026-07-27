# BoundFree EEG Native Version

当前版本：0.1.47-dev

版本真值：`version.json`

记录原则：
- Git commit 标题只保留一行中文 Conventional Commit 摘要。
- `version.md` 从 `chore: 启用提交级版本同步规则` 开始记录规范化版本条目。
- 规范化条目记录版本、日期、作者、提交标题和提交正文；不记录 commit hash。
- 旧历史提交不 retroactively 改写；本文件只保留迁移说明，后续提交按本规范执行。

更新类型：
- feat: 用户可见功能或能力
- fix: 缺陷修复
- docs: 仅文档
- test: 仅测试或测试夹具
- chore: 构建、工具链、版本周期或仓库维护
- spike: 技术验证与记录
- refactor: 不改变对外行为的重构
- perf: 性能改进

版本号规则：
- `PATCH`：每个合入 `master` 的有效提交递增一次，常规开发版本保持 `-dev`。
- `MINOR`：阶段性公开能力边界变化时递增，例如完成新的里程碑、引入新的产品线能力、增加新的对外数据/分析/发布能力；递增后 `PATCH` 归零。
- `MAJOR`：出现正式稳定大版本或不兼容产品/数据/授权/IPC 边界时递增；迁移期使用 `0.x.y`，第一个完整客户可用版本发布为 `1.0.0`。
- `rc`：准备发布候选版时将当前开发版本改为 `X.Y.Z-rc.N` 并设置 `release_channel: rc`。
- `stable`：正式发布时去掉 prerelease，并设置 `release_channel: stable`。
- `ipc_protocol_version` 独立维护；只有 IPC envelope、payload、字段语义或不兼容错误码契约变化时才评估递增。

迁移说明：
- 本仓库在该规范启用前已有完整 Git 历史，最早提交为 `a5b5e018928b8626bb834b859c92e8bbd5466b57 spike: 记录 M0 工具链环境`。
- `0.1.1-dev` 至 `0.1.41-dev` 对应规范启用前的本地开发历史；这些提交的完整内容以 `git log` 为准，不在本文件逐条补录。
- 从 `0.1.42-dev` 开始，后续每个有效提交必须同步更新 `version.json` 和本文件，并在本文件记录完整提交说明。

---

fix: 按内容校验CSV信号格式

版本：0.1.47-dev
日期：2026-07-28

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 按内容校验CSV信号格式

提交正文：
```text
移除对 _markers.csv 文件名的读取拦截。CsvReader 在 open 元数据阶段流式验证 time_s 时间轴与全部信号单元格，按内容将事件标注表、缺失时间列、无信号列、非法信号值、非法时间轴和空样本映射为 csv.* 结构化错误码；主 CSV 的同名标注侧车自动回读保持不变。
```

---

fix: 阻止独立打开标注侧车

版本：0.1.46-dev
日期：2026-07-28

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 阻止独立打开标注侧车

提交正文：
```text
在边车 open 协议中识别 _markers.csv 标注侧车并返回 reader.marker_sidecar，防止其被数值 CSV 读取器误解析。主 CSV 仍自动加载同名标注侧车；新增错误码、全 locale 翻译及回归测试。
```

---

test: 覆盖发布版本动态断言

版本：0.1.45-dev
日期：2026-07-28

作者：BigYellow12138 <1373912749@qq.com>

类型：test

提交标题：
test: 覆盖发布版本动态断言

提交正文：
```text
移除 Native 与边车协议测试中硬编码的历史产品版本和 IPC 版本，改为读取 version.json 断言 --version、hello 和 stdio 响应，使版本递增不会掩盖真实协议回归。
```

---

chore: 完善Windows发布交接与验收

版本：0.1.44-dev
日期：2026-07-28

作者：BigYellow12138 <1373912749@qq.com>

类型：chore

提交标题：
chore: 完善Windows发布交接与验收

提交正文：
```text
新增 WSL 到 Windows 的源码同步、Windows handoff 预检和发布验收脚本。安装器改用仓库内简体中文语言文件，并支持用户级 Inno Setup 安装路径。Windows 端已完成构建、stage、安装包和授权手工验证。
```

---

fix: 修复发布构建的授权公钥加载

版本：0.1.43-dev
日期：2026-07-28

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 修复发布构建的授权公钥加载

提交正文：
```text
将嵌入公钥宏定义和生成头文件包含路径应用到承载 LicenseManager 的 bf_core，修复发布构建中公钥为空导致的 not_configured。发布脚本将相对 Python、公钥和 Qt 路径规范化为绝对路径，并在 stage 根复制构建所用公钥作为运行时兜底。
```

---

chore: 启用提交级版本同步规则

版本：0.1.42-dev
日期：2026-07-25

作者：BigYellow12138 <1373912749@qq.com>

类型：chore

提交标题：
chore: 启用提交级版本同步规则

提交正文：
```text
建立提交级版本同步制度：version.json 作为唯一版本真值，根目录 version.md 作为人工可读版本记录。

版本号从 0.1.42-dev 开始按每个有效提交递增 PATCH；MINOR 用于阶段性公开能力边界，MAJOR 用于正式稳定大版本或不兼容产品边界。

新增 scripts/version_sync.py 管理 next/set/check，发布资产检查接入版本同步门禁。旧历史不回写逐条记录，后续提交从本规范开始执行。
```

---
