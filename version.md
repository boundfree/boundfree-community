# BoundFree EEG Native Version

当前版本：0.1.86-dev

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

test: 对齐 Windows 打包入口的命名参数转发验证

版本：0.1.86-dev
日期：2026-10-18

作者：BigYellow12138 <1373912749@qq.com>

类型：test

提交标题：
test: 对齐 Windows 打包入口的命名参数转发验证

提交正文：
```text
合并前全量回归发现 Windows 构建入口早已采用 releaseParams 命名参数表，而静态契约测试仍要求调用行包含所有内联参数。更新断言以识别调用前定义的命名参数表，逐项检查 QtRoot、Python、Configuration、LicensePublicKeyFile 的来源及 SkipSigning=true，同时保留对位置参数数组转发的拒绝。生产打包脚本保持原样，补充全量合并门禁验证记录：336 项通过、4 项跳过；冻结边车、发布检查器及真实 Intan 样本相关跳过项不计入发布验收。
```

---

feat: 支持原始与处理后数据独立导出

版本：0.1.85-dev
日期：2026-10-18

作者：BigYellow12138 <1373912749@qq.com>

类型：feat

提交标题：
feat: 支持原始与处理后数据独立导出

提交正文：
```text
导出弹窗集中设置起止时间及数据来源，默认跟随当前显示模式，允许独立选择原始或处理后数据。处理后导出复用 Native DSP，捕获处理配置并保留当前窗口滤波上下文，通过新增 export_samples 二进制接口编码为 EDF/HDF5/CSV，保留输出通道名和标注。同步修复连续导出完成时序及单通道双极名称回退，补齐三格式数值回归、帧校验、多语言文案和协议验收文档。
```

---

feat: 支持原始与处理后波形及分析切换

版本：0.1.84-dev
日期：2026-10-18

作者：BigYellow12138 <1373912749@qq.com>

类型：feat

提交标题：
feat: 支持原始与处理后波形及分析切换

提交正文：
```text
波形工具栏增加原始与处理后数据选择，波形及 PSD/FFT、STFT、频段功率趋势和相干性共用选定输入。切换保留重参考和滤波配置，同步通道数量、名称与分析输入说明，废弃旧分析任务并重新计算。同步播放预取、拖动预览、多语言文案、数据契约及波形模式回归。
```

---

fix: 按操作范围重排 EEG 三栏工作区与预处理控件

版本：0.1.83-dev
日期：2026-10-17

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 按操作范围重排 EEG 三栏工作区与预处理控件

提交正文：
```text
将通道、信号预处理和标注移到左侧；波形显示参数归中栏，分析参数与结果归右栏。集中重参考、陷波总开关、频率和全局带通；逐通道设置默认折叠。文件菜单保留 Ctrl+O，标题栏显示文件名，状态栏显示通道数与采样率。同步四语界面、保留语言资源和布局回归。修复窄栏分析摘要高度与日文相干性参数挤压；CMake 构建、78 项原生集成回归及发布资产检查通过。
```

---

fix: 补齐 Windows 一键打包与运行库依赖

版本：0.1.82-dev
日期：2026-10-17

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 补齐 Windows 一键打包与运行库依赖

提交正文：
```text
增加 WSL 一键同步、Windows 构建、自动验收和安装包回传入口；显式部署官方 VC++ 运行库，并在安装前检查版本与安装结果。修复安装包缺失时未失败退出的问题，增加可选快进构建和产物 SHA-256 输出；调整授权门禁与菜单命中 smoke，适配自动验收环境。
```

---

fix: Windows 桌面启动不再弹出终端

版本：0.1.81-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: Windows 桌面启动不再弹出终端

提交正文：
```text
将 bf_app 标记为 Windows GUI 子系统应用，保留 Qt 命令行 smoke 入口。
```

---

fix: 统一控件尺寸并修复多语言参考菜单裁切

版本：0.1.80-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 统一控件尺寸并修复多语言参考菜单裁切

提交正文：
```text
统一输入与操作控件高度并逐行居中；为参考下拉菜单预留原生边距，增加语言切换与缩放回归。
```

---

fix: 稳定播放期间分析结果刷新并减少文字闪烁

版本：0.1.79-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 稳定播放期间分析结果刷新并减少文字闪烁

提交正文：
```text
后台分析保留上一份完整有效结果，完成后批次替换曲线表格与对应时间；独立pending上下文防止慢任务被重复取消，固定表格列宽；最终12项回归通过。
```

---

fix: 修复多语言文本裁切并提升分析详情可读性

版本：0.1.78-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 修复多语言文本裁切并提升分析详情可读性

提交正文：
```text
参考分析及通道模式下拉按翻译文本展开；分析详情移除内部滚动，以正常字号三列表格组织频段范围与数值；修复标签页祖先裁切和工具栏换行重叠，最终21项回归通过。
```

---

fix: 降低视频面板默认高度并优先扩展波形

版本：0.1.77-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 降低视频面板默认高度并优先扩展波形

提交正文：
```text
视频首次展开目标高度150逻辑像素，窗口增高优先分配给波形；保留手动分隔调整，构建与5项布局视频回归通过。
```

---

fix: 支持拖动进度时预览波形并合并读片请求

版本：0.1.76-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 支持拖动进度时预览波形并合并读片请求

提交正文：
```text
缓存内即时平移、缓存外80ms合并并限制单次在途，旧结果不得覆盖最新目标；松手准确定位，失败取消保持时间与波形一致；真实CSV边车拖动及最终69项联合回归通过。
```

---

fix: 重整参数标签页并优化常用数值输入

版本：0.1.75-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 重整参数标签页并优化常用数值输入

提交正文：
```text
通道处理事件改固定标签页，逐通道滤波按选中通道编辑；修复频率输入裁切并增加时间窗幅值预设，累计56个不同回归用例通过。
```

---

fix: 修复空白页侧栏残留与数值框箭头

版本：0.1.74-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 修复空白页侧栏残留与数值框箭头

提交正文：
```text
未打开文件隐藏完整数据侧栏并禁用面板入口；显式绘制数值框上下箭头及点击区域；8项离屏交互与布局验证通过，Windows桌面仍待验收。
```

---

fix: 恢复旧版亮暗主题与稳定通道配色

版本：0.1.73-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 恢复旧版亮暗主题与稳定通道配色

提交正文：
```text
EEG界面恢复旧版中性浅色及深蓝灰主题、蓝色和青色谱线、八色通道循环。图外属性随主题更新，保留通用Spike配色。补充已有图元不重算切换、隐藏前置通道颜色保持及双主题四语言布局回归；Windows高DPI及高负载仍待实测。
```

---

fix: 外置分析图属性并修复结果状态刷新

版本：0.1.72-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 外置分析图属性并修复结果状态刷新

提交正文：
```text
四分析页外置标题、图例和摘要，补数值轴与时频bin边界、单趋势点绘制。缓存结构化结果支持语言刷新，失败和空结果清旧图，过期generation不能回填。自动播放等待pending任务完成，避免慢分析反复取消。离屏验证不替代Windows桌面验收。
```

---

fix: 合并录制元数据表单并校验输出目录

版本：0.1.71-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 合并录制元数据表单并校验输出目录

提交正文：
```text
将四个连续录制弹窗合并为一个原生表单，保留完整元数据与多行备注，目录输入时和开始前双重校验，取消不触发录制。复用既有录制控制器；新增表单与主窗口接线回归，执行M7录制导出回归。
```

---

fix: 补齐通道自动幅值滚动与逐行带通设置

版本：0.1.70-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 补齐通道自动幅值滚动与逐行带通设置

提交正文：
```text
增加逐通道自动幅值与110像素最小行高滚动，保留手动幅值；新增继承、关闭、启用三态逐行带通，非法输入保留可修正草稿。换文件清理逐通道覆盖，保留双极输出索引语义。51项不同针对性用例通过；高通道缓存内存与Windows桌面性能未验收。
```

---

fix: 恢复固定工作区布局并拆分常用操作栏

版本：0.1.69-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 恢复固定工作区布局并拆分常用操作栏

提交正文：
```text
按旧版组织顶部信号设置、左侧波形视频、右侧分析与折叠参数、底部播放录制；禁用面板浮动与移动。使用原生换行控件保持常用操作可达，保留同文件定位时的面板隐藏状态，补充四语言尺寸与交互回归。Windows 高 DPI 与桌面验收仍待执行。
```

---

fix: 实现连续播放视窗与有界预取

版本：0.1.68-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 实现连续播放视窗与有界预取

提交正文：
```text
分离缓存数据与可见时间范围，按采样绝对时间连续平移波形，视窗更新不重建显示模型。主窗口预取有界前瞻缓存，慢读取冻结时钟与视频，失败停播，加载新文件隔离旧请求；分析仅使用可见区并限频。新增像素/标注视口测试、慢读取状态集成与真实CSV边车联合播放回归。CMake构建通过，58项定向测试通过；Windows桌面和高负载性能仍待验收。
```

---

perf: 复用读片边车会话并合并过期请求

版本：0.1.67-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：perf

提交标题：
perf: 复用读片边车会话并合并过期请求

提交正文：
```text
以单个持久worker串行复用SidecarClient和同文件handle；换文件及显式重开关闭旧handle，pending槽与generation隔离过期请求，失败后重新连接。修复清理会话导致错误码丢失。协议计数回归覆盖复用、重开、换文件、合并、失败恢复和销毁；CMake构建与真实文件既有回归通过。
```

---

fix: 增加回放拖动预览并隔离定位状态

版本：0.1.66-dev
日期：2026-10-16

作者：BigYellow12138 <1373912749@qq.com>

类型：fix

提交标题：
fix: 增加回放拖动预览并隔离定位状态

提交正文：
```text
进度条拖动即时更新目标秒数，期间冻结EEG时钟并暂停从属视频；松手一次定位，键盘与轨道步进支持定位，程序刷新和跨文件旧拖动不触发误定位。新增真实MainWindow集成冒烟；最终构建通过，分析/回放/浏览器/视频定向回归49项通过。连续滚动、边车复用和布局调整留后续阶段。
```

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
