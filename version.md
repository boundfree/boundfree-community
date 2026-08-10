# BoundFree EEG Native Version

当前版本：0.1.55-dev

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

feat: 修复激活对话框邀请码内联输入与启动吊销核验

版本：0.1.55-dev
日期：2026-08-10

作者：BigYellow12138 <1373912749@qq.com>

类型：feat

提交标题：
feat: 修复激活对话框邀请码内联输入与启动吊销核验

提交正文：
```text
(1) 邀请码改为对话框内内联 QLineEdit + 按钮，去掉嵌套 QInputDialog，新增两个 i18n 键。(2) ensureOfflineLicense Level 1 本地通过后调用 checkAndHandleRevocation，8 秒超时 fail-open；服务端返回 revoked/rejected 时删除 license.dat 并重新进入激活流程。
```

---

fix: 修复激活流程三处 bug 及 pseudo.ts 格式瑕疵

版本：0.1.54-dev
日期：2026-08-10

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 修复激活流程三处 bug 及 pseudo.ts 格式瑕疵

提交正文：
```text
(1) onNetworkError 不再 Q_UNUSED 丢弃网络细节，非空 detail 以括号拼接到 network_unavailable 后展示。(2) handleRegisterResponse 202 解析 request_id 为空时转 activationFailed 而非无感继续轮询。(3) extractErrorCode 移除不可达的 activation.network_error 死代码分支。(4) pseudo.ts 三处 [!! !!] 格式瑕疵修正。
```

---

fix: 补充在线激活业务错误码的完整映射

版本：0.1.53-dev
日期：2026-08-10

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 补充在线激活业务错误码的完整映射

提交正文：
```text
activationErrorText 增加 already_exists / invalid_invite_code / invite_revoked / invite_already_used / signing_failed 五个错误码的精准翻译，全部 9 种语言同步补充对应 i18n 条目。未知错误码降级为 error_detail（含错误码）而非通用的 request_failed，方便用户反馈。
```

---

fix: 区分在线激活业务拒绝原因

版本：0.1.52-dev
日期：2026-08-10

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 区分在线激活业务拒绝原因

提交正文：
```text
在线激活客户端保留服务端结构化错误码和参数，激活对话框将机器码冲突、吊销/过期等业务拒绝显示为具体原因，不再误显示为网络不可用。新增释放已吊销设备绑定后的重授权提示，并覆盖全部现有语言。
```

---

fix: 收口在线激活可靠性

版本：0.1.51-dev
日期：2026-08-10

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 收口在线激活可靠性

提交正文：
```text
补齐在线激活与已部署许可证服务的协议兼容：解析嵌套 error.code，兼容重复注册、待审批和拒绝状态，保留拒绝原因；网络失败与网关 5xx 才使用备用 IP，避免业务错误误触发重试。激活码落盘前再次执行本机 RSA 校验，license.dat 改用 QSaveFile 原子提交。新增本地 HTTP 协议回归测试，修复 smoke-license-gate 在自动化环境等待交互对话框的问题，并补齐全部 locale 激活文案。
```

---

feat: 添加繁体中文(zh_TW)为第四稳定语言

版本：0.1.50-dev
日期：2026-07-29

作者：BigYellow12138 <1373912749@qq.com>

类型：feat

提交标题：
feat: 添加繁体中文(zh_TW)为第四稳定语言

提交正文：
```text
将 zh_TW 从待审校 locale 提升为 stable locale，加入语言菜单、CMake
lrelease 构建和发布资产检查。bf_app_zh_TW.ts 原有 156 个英文占位翻译
已替换为完整繁体中文（保留 PSD/STFT/FFT 等技术缩写）。同时为暂不激活
的 de_DE/fr_FR/es_ES/ko_KR 添加设计注释，说明保留 .ts 供未来扩展。

涉及文件：
- native/i18n/bf_app_zh_TW.ts: 完整繁体翻译 (162 keys)
- native/src/i18n/i18n_service.{h,cpp}: kStableLocales 加入 zh_TW
- native/CMakeLists.txt: lrelease 和 install 加入 zh_TW
- native/src/i18n/i18n_locale_format_test.cpp: 测试更新 3→4
- native/src/ui/main_window.cpp: 注释同步
- scripts/windows/build_release.ps1: locale 循环加入 zh_TW
- scripts/windows/verify_windows_release.ps1: 资产检查加入 zh_TW
- scripts/common/check_release_assets.py: locale 列表加入 zh_TW
- scripts/common/check_i18n.py: 注释说明非 stable locale 保留策略
```

---

fix: 澄清 reader.open_failed 错误码的线协议参数与 UI 翻译的分离设计

版本：0.1.49-dev
日期：2026-07-29

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 澄清 reader.open_failed 错误码的线协议参数与 UI 翻译的分离设计

提交正文：
```text
error_codes.json 中 reader.open_failed 的 params 包含 path 和 reason，
但 path 为线协议专用（日志/诊断），有意从用户可见 i18n 字符串中省略。
在 error_codes.json 添加 _note 字段说明此设计，并在 check_i18n.py 的
特殊豁免处补全注释，避免未来维护者将其误判为 bug。
```

---

chore: 重构脚本目录并精简WSL-Windows开发链路

版本：0.1.48-dev
日期：2026-07-29

作者：BigYellow12138 <1373912749@qq.com>

类型：chore

提交标题：
chore: 重构脚本目录并精简WSL-Windows开发链路

提交正文：
```text
将脚本从 scripts/ 扁平目录重组为 scripts/common/、scripts/windows/、scripts/wsl/ 三个职能子目录；新增 config/windows-dev.local.json 配置体系替代硬编码路径；删除冗余的 prepare_windows_handoff.sh 和 run_windows.cmd；同步更新所有测试、installer 及文档中的脚本路径引用。
```

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
