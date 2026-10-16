# BoundFree EEG Native Version

当前版本：0.1.65-dev

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

fix: 恢复频谱与相干分析曲线绘制

版本：0.1.65-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 恢复频谱与相干分析曲线绘制

提交正文：
```text
补回 SpectrumWidget 路径绘制；新增覆盖 PSD、FFT、相干性、亮暗主题和频带开关的曲线差分回归。修复前12组均失败，修复后定向pytest 3项通过，CMake配置与全量构建通过。记录分阶段UI修复计划；本提交不包含回放和布局改造。
```

---

fix: 统一绘图口径并修复波形坐标轴标签排布

版本：0.1.64-dev
日期：2026-10-15

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 统一绘图口径并修复波形坐标轴标签排布

提交正文：
```text
新增 native/ui/plot_style.h 作为波形浏览器、频谱、时频、Spike 四个绘图组件共用的边距、主题配色与 1/2/5 刻度算法真值来源，analysis_widgets.cpp 与 spike_widgets.cpp 删除各自的重复副本。

波形浏览器改为：±幅度刻度只标最外两行并整体限制在绘图区内，通道名按文字高度抽稀并按可用宽度省略，时间刻度文字夹在控件内避免右缘裁切，没有可见通道时仍绘制坐标轴。

刻度数值改由 I18nService::qlocale() 传入的 locale 渲染，不再使用系统 locale。

新增 plot_style_test 覆盖边距、刻度算法、轴数值格式化与标签抽稀判定；--smoke-waveform-render 的断言改为只统计轴文字像素，并新增无可见通道用例。

顺带修正 Windows 验收脚本在出厂 Windows PowerShell 5.1 下无法运行的三处问题：脚本缺少 UTF-8 BOM 导致中文 here-string 解析失败；param() 默认值引用 $PSScriptRoot（5.1 下为空）导致启动即报错；Invoke-Checked 的 2>&1 会把原生命令 stderr 当致命错误，而系统 UI smoke 的诊断行正是走 stderr。

bf_app 的边车 smoke 失败时改为打印 code 与线协议 params（含 reason），无需改跑 UI 即可定位边车侧异常原因。

同步状态记录与 SOP 文档：M8 验收记录补入 Windows x64 构建、安装包生成与自动化验收的执行证据（工具链、命令、安装包 SHA-256、验收产物目录）并调整发布门禁勾选；README 与 docs/README 同步当前状态，README 的 Windows Dev Build 段补入验收步骤与已知坑（WSL 启动时资源根回退、边车冷启动首次打开 MNE 格式的 3 秒超时、脚本的 PowerShell 5.1 兼容要求、工作副本改名后需重建 build 目录）。全仓库扫除失效的 scripts 路径引用（scripts/*.py → scripts/common/*.py、scripts/verify_m1_foundation.sh 与 scripts/sync_to_windows.sh → scripts/wsl/、scripts/build_windows.ps1 → scripts/windows/），覆盖当前文档、历史验收报告、golden 生成器与 meta.json、以及相应测试断言。

Windows 工作副本目录由 `E:\workspace\BoudFree\boundfree-eeg-native` 改名为 `E:\workspace\BoudFree\boundfree-community`，本机 `config/windows-dev.local.json` 的 `wsl_sync_target` 同步更新；仓库内 `boundfree-eeg-native` 名称引用（AGENTS/README/docs/native-spike 命令示例/构建脚本注释）统一为 `boundfree-community`。
```

---

fix: 防止本地授权时间回拨绕过

版本：0.1.63-dev
日期：2026-08-12

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 防止本地授权时间回拨绕过

提交正文：
```text
新增单调 UTC 时间水位，许可证启动校验和离线保存均拒绝缺失、损坏或超过 300 秒的本地时间回拨状态，且不会静默重建或降低水位。在线激活在本机重新验证服务端 RSA 签名后成为唯一允许恢复水位的路径。新增模块边界与 LicenseManager 真实保存链路回归，端到端测试仅使用临时目录和运行时生成的 RSA-2048 测试密钥，不读取或写入任何生产密钥、授权文件或用户数据。同步结构化错误码、多语言提示、授权设计和 M8 验收记录。
```

---

fix: 修复激活窗口布局与离线折叠联动

版本：0.1.62-dev
日期：2026-08-12

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 修复激活窗口布局与离线折叠联动

提交正文：
```text
统一激活窗口按钮与输入控件的尺寸、圆角、边框和悬停样式，主操作仅以主题色与字重区分；机器码与邀请码输入框、操作按钮等高对齐。修复离线许可证面板展开后验证按钮被窗口裁切的问题：展开时按内容所需自动增高窗口。修复收起后面板隐藏但窗口高度不恢复的问题：收起时按折叠内容所需缩回高度，初始窗口尺寸同样由内容自适应，消除底部大面积空白。
```

---

test: 添加统一激活页面交互回归

版本：0.1.61-dev
日期：2026-08-11

作者：BigYellow12138 <1373912749@qq.com>

类型：test

提交标题：
test: 添加统一激活页面交互回归

提交正文：
```text
新增 Qt 离屏回归测试，验证离线许可证入口默认收起、展开后的输入区，以及在线申请提交后邀请码入口被锁定。
```

---

feat: 重构统一激活页面并明确授权路径

版本：0.1.60-dev
日期：2026-08-11

作者：BigYellow12138 <1373912749@qq.com>

类型：feat

提交标题：
feat: 重构统一激活页面并明确授权路径

提交正文：
```text
将在线申请、邀请码激活和离线许可证明确分为三个用户可理解的入口；离线许可证默认折叠，仅在需要时展开。删除启动流程中重复的离线输入窗口，申请提交后禁用邀请码切换以避免机器码占用冲突。同步全部语言文案并保留本地验签、许可证保存和既有在线协议。
```

---

fix: 收紧云端激活状态契约与错误本地化

版本：0.1.59-dev
日期：2026-08-11

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 收紧云端激活状态契约与错误本地化

提交正文：
```text
在线状态协议只传递固定状态枚举和到期日，不再将管理端自由备注下发至 Qt。补齐激活错误码本地化映射、限流倒计时提示、服务端失效状态处理与完整网络回归，确保审批、拒绝、解绑、签发和限流均按结构化契约处理。
```

---

fix: 注销解绑后在线核验本地授权失效

版本：0.1.58-dev
日期：2026-08-11

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 注销解绑后在线核验本地授权失效

提交正文：
```text
服务端注销/解绑会清空 machine_id，状态接口对旧机器码返回 not_found。启动在线核验将 revoked、rejected、not_found 均视为服务器明确失效：删除本地 license.dat 并进入重新激活流程；网络错误与超时仍 fail-open。补充 not_found 在线状态回归测试，并将提示文案改为覆盖注销或吊销。
```

---

chore: 明确版本自动同步流程并同步文档版本号

版本：0.1.57-dev
日期：2026-08-11

作者：BigYellow12138 <1373912749@qq.com>

类型：chore

提交标题：
chore: 明确版本自动同步流程并同步文档版本号

提交正文：
```text
AGENTS.md（仓库级与工作区级）补充版本流程约束：每次版本递增必须走 scripts/common/version_sync.py next/set，由脚本自动同步 version.json、version.md、README.md、AGENTS.md、installer/BoundFreeEEG.iss 五处版本号，提交前 check 校验一致，禁止手工只改 version.json。同步 README.md、AGENTS.md、installer/BoundFreeEEG.iss 三处滞后版本号。
```

---

fix: Linux 接入 OpenSSL 验签后端，打通在线激活验证

版本：0.1.56-dev
日期：2026-08-11

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: Linux 接入 OpenSSL 验签后端，打通在线激活验证

提交正文：
```text
非 Windows 构建的 license_crypto 验签后端从空实现（恒 false）改为 OpenSSL EVP：RSA-PSS/MGF1-SHA256，RSA_PSS_SALTLEN_AUTO 恢复盐长（等价 Python PSS.AUTO 与 CNG 盐长扫描），输入原始消息、内部 SHA256，与 Windows CNG 分支语义一致。CMake 在非 Windows 平台 find_package(OpenSSL) 并链接 OpenSSL::Crypto（bf_core / bf_license_smoke）。bf_license_smoke 新增 OpenSSL 往返测试：生成 RSA-2048 临时密钥、EVP_DigestSign 以 MAX 盐长签名（镜像服务器行为）、验证接受与篡改/垃圾签名/空公钥拒绝；同步更新"后端可用"与"无效 Key"断言。修复后 WSL2/CI 可完整走通在线激活闭环，并已用 license-authority Key Pair B 签发真实激活码端到端验证通过（Valid / WrongMachine / 篡改拒绝）。
```

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

新增 scripts/common/version_sync.py 管理 next/set/check，发布资产检查接入版本同步门禁。旧历史不回写逐条记录，后续提交从本规范开始执行。
```

---
