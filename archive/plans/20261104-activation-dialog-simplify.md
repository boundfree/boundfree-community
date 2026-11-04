---
title: 社区版激活窗口简化与授权校验修复实施计划
doc_type: plan
status: active
owner: BoundFree
last_updated: 2026-11-04
---

# 社区版激活窗口简化与授权校验修复实施计划

## 任务与实施位置

用户认为激活窗口过于复杂，“邀请码”与“离线许可证”在用户眼中重复。本轮只改社区版客户端，授权服务端网页留待下一轮。

- 工作树：`/home/vsonic12138/workspace/BoundFree_Workspace/.worktrees/community-activation`
- 分支：`feature/activation-simplify`，**已变基到 `feature/fluentqt-integration` 已提交的 `bbf0ddf`**（为使用 Fluent-Qt 组件），不含主目录中未提交的改动；交付时作为依赖 Fluent 分支的堆叠 PR。首轮实施成果已暂存为一个 WIP 提交，继续在其上修改，最后统一整理提交。
- **第二轮审阅意见见文末“七、审批意见（第二轮）”，与前文冲突时以第七节为准。**
- 由 Codex 实施，Claude 指导与审批。实施方不提交、不推送、不建 PR；完成后由审批方审阅 diff 再决定提交。
- 产品版本保持 `0.2.2-beta.1`，不改 `version.json`。

## 用户已确认的决定

1. **去掉邀请码入口。** 客户端只保留两条路径：在线申请（等待管理员审批）和离线许可证。服务端邀请码接口暂不动，下一轮处理网页时一并下线。
2. **Linux 机器码修复。**
3. **过期提示与自动续期。**
4. **申请时附带主机名、操作系统和可选的单位/联系人。**
5. **启动吊销检查改为后台；离线签发的授权不再联网检查；发现被停用后立即生效（见第七节）。**
6. **激活窗口使用 Fluent-Qt 组件库，与主界面外观一致（见第七节）。**

## 总体原则

- **代码简洁**：删除邀请码相关代码、无用状态（`VERIFYING`）、重复提示与 tooltip，不新增抽象层；能删则删。
- **低阅读成本**：每个区域最多一句短说明；不用“签名”“水位”“payload”等技术词；错误提示说清“发生了什么 + 下一步做什么”，一句话以内。
- **用户只需要在一个时刻做一件事**：窗口按状态切换内容，而不是把所有入口同时铺开。
- 界面文本全部走 i18n，9 份 `.ts`（zh_CN、zh_TW、en_US、ja_JP、ko_KR、de_DE、fr_FR、es_ES、pseudo）同步增删，无用词条删除；`scripts/common/check_i18n.py` 必须通过。
- 后端只返回 code + params 的约定不变。

## 一、激活窗口改版（`native/src/ui/activation_dialog.*`）

### 布局

```
激活 BoundFree EEG

本机机器码
[ 1A2B3C4D-5E6F7A8B-9C0D1E2F-3A4B5C6D ]  [复制]

┌ 主区域（按状态切换，见下表） ─────────────┐
│ 单位或联系人（选填） [                  ] │
│                              [ 申请授权 ] │
│ 管理员审批通过后自动激活。                │
└──────────────────────────────────────────┘

▸ 无法联网？使用离线许可证
   [ 粘贴管理员提供的许可证文本 ]
                                 [ 激活 ]

（状态行：一行文字，必要时右侧附 [重试] / [立即检查]）
```

- 删除：邀请码区块、“请选择一种在线授权方式”、`request_hint` 与重复 tooltip、分隔线。
- 离线区默认折叠；展开/折叠时窗口高度跟随内容（保留现有自适应逻辑）。
- 尽量沿用现有对象名（`machineIdEdit`、`offlineToggle`、`offlinePanel`、`statusLabel`），便于测试；新增对象名保持语义清晰。

### 打开窗口后的自动流程

窗口打开即后台查询一次 `/api/v1/status`（机器码不可用时除外），用户无需点任何按钮：

| 服务器结果 | 主区域显示 |
|---|---|
| 查询中 | 状态行“正在检查授权状态…”，申请按钮暂时禁用 |
| `not_found` | 申请表单（上图） |
| `pending_approval` | 等待面板：“已提交申请，正在等待管理员审批。通过后会自动激活；也可以先关闭软件，下次打开时会自动检查。” + [立即检查]；每 30 秒自动查询 |
| `active` | 自动调用 `fetchLicense` 领取授权 → 成功即关闭窗口进入主界面 |
| `expired` | “授权已到期，请联系管理员续期。续期后点击立即检查。” + [立即检查]；不显示申请表单（服务端会返回 already_exists） |
| `revoked` / `rejected` | “授权已被管理员停用/申请未通过，请联系管理员。” + [立即检查]；不显示申请表单 |
| 网络失败 | 显示申请表单；状态行“无法连接授权服务器，请检查网络后重试，或使用离线许可证。” + [重试]；不在界面拼接 Qt 英文原始错误（仅可写日志） |

- 已过期的管理员续期后，窗口一打开就会自动领取新授权，这就是“自动续期”的实现。
- 申请成功（202）或 `already_exists` 后都进入等待面板并立即查一次状态。
- 激活成功提示带上到期日期：“激活成功，授权有效期至 {expires_at}。”

### 本地授权失败的提示（`licenseErrorText`）

按 `LicenseResult.status` 给出明确提示，不再一律显示“激活码无效”：

| 状态 | 提示（中文示意） |
|---|---|
| Expired | 本机授权已于 {expires_at} 到期。（随后由自动查询决定能否续期） |
| WrongMachine | 这份授权属于另一台电脑，不能在本机使用。 |
| InvalidSignature / KeyFormatError | 许可证文本无效，请确认完整复制后重试。 |
| Clock*/SaveFailed | 沿用现有 `error.license.*` 文案，必要时精简 |
| Missing | 不显示错误（首次激活的正常状态） |

离线许可证输入为空时提示“请先粘贴许可证文本。”。

### 申请附带信息

- `registerDevice` 请求体增加：`hostname`（`QSysInfo::machineHostName()`）、`os`（`QSysInfo::prettyProductName()`）、`org_name`（用户选填）。
- 按服务端限制截断：`org_name` ≤128、`hostname` ≤128、`os` ≤64 字符；空值可省略或传空串。
- `OnlineActivationClient` 删除 `activateWithCode`；`fetchLicense` 保留。

## 二、Linux 机器码（`native/src/license/machine_id.cpp`）

- 非 Windows 读取顺序：`BF_MOCK_MACHINE_GUID`（开发/测试覆盖，保留）→ `/etc/machine-id` → `/var/lib/dbus/machine-id`；去空白，校验非空。
- Windows 行为与机器码算法保持不变（兼容旧版密钥）。
- **任何平台读到空值都不得计算机器码**：`currentMachineId()` / `machineIdCanonical()` 的调用方在 GUID 为空时得到空结果；激活窗口显示“无法读取本机机器码，请联系管理员。”，禁用申请与离线激活；`LicenseManager` 校验时视为失败而不是匹配空哈希。
- 为可测试性，可把文件读取抽成接收路径列表的小函数；不引入新类。

## 三、启动检查改为后台（`native/src/app/main.cpp` + 激活窗口静态函数）

- 本地授权有效时立即进入主界面，不再同步阻塞等待网络。
- 仅 `mode == "online"` 的授权做后台状态检查；`managed_offline` 与无 mode 的旧授权不联网。
- 后台检查在主窗口显示后异步发起，超时/网络失败静默放行（保持 fail-open）。
- 服务器明确返回 `revoked` / `rejected` / `not_found` 时：删除本地 `license.dat`，弹出一次非阻塞提示“本机授权已被管理员停用，下次启动时需要重新激活。”。**不强制退出**，避免中断正在进行的采集或录制。
- 删除不再使用的同步 `QEventLoop` 路径；保持代码简短。

## 四、明确不在本轮范围

- 服务端（到期当天的判定口径、过期状态落库、限流、网页 UI、邀请码下线）留到网页轮次；客户端到期判定（到期当天可用）本轮不改。
- 备用 IP、时钟回拨保护逻辑不改。
- 不改安装包、CI、版本号。

## 五、测试与验证

在工作树中使用独立构建目录，不要覆盖其他构建：

```bash
TC=/home/vsonic12138/workspace/BoundFree_Workspace/toolchains/qt6
$TC/bin/cmake -S native -B /mnt/boudfree-dev/build/community-activation \
  -DCMAKE_PREFIX_PATH=$TC -DCMAKE_CXX_COMPILER=$TC/bin/x86_64-conda-linux-gnu-g++
$TC/bin/cmake --build /mnt/boudfree-dev/build/community-activation --parallel 4
```

必须通过并记录输出：

- `activation_dialog_test`（`QT_QPA_PLATFORM=offscreen`）：更新为新布局；覆盖打开即查询、各服务器状态对应的主区域、等待面板、过期/机器不符/无效文本提示、空输入提示、机器码不可用时禁用操作、无邀请码控件。
- `online_activation_client_test`：申请请求体包含 hostname/os/org_name 且截断正确；删除邀请码用例。
- `license_manager_clock_test`、`clock_rollback_test`、`bf_license_smoke`：无回归。
- 新增或扩展机器码测试：mock 覆盖优先、文件读取、空文件/缺失文件返回空、空 GUID 不产生机器码。
- `python3 scripts/common/check_i18n.py`、`python3 scripts/common/version_sync.py check`、`git diff --check`。
- 以 `-DBF_REQUIRE_LICENSE=ON -DBF_LICENSE_PUBLIC_KEY_FILE=$PWD/license_public.pem` 另建目录构建 `bf_app`，运行 `--smoke-license-gate` 确认门禁路径正常。
- 离屏截图：中文与英文各一张（默认态、等待审批态、离线区展开态），保存到构建目录供审阅，不提交。

## 六、交付

- 修改相关文档中描述激活方式的段落（如 README、`docs/` 中提到邀请码的地方），只同步受影响内容。
- 完成后汇报：改动文件清单、删除/新增的 i18n 词条、每项测试命令与结果、截图路径、未验证项。不提交、不推送。

## 七、审批意见（第二轮）

首轮成果方向正确（邀请码已删、机器码空值已拦截、申请附带信息、打开即查询）。以下为必须执行的调整。

### 7.1 吊销后立即生效（替代第三节“不强制退出”）

后台检查仍在主窗口显示后异步发起，网络失败/超时静默放行。服务器明确返回 `revoked` / `rejected` / `not_found` 时：

1. 删除本地 `license.dat`；
2. 以主窗口为父弹出模态提示 `app.activation.revoked_on_server`；
3. 随即以模态方式打开激活窗口（初始结果按 Missing 处理）；
4. 重新激活成功则继续使用；关闭或取消则调用主窗口 `close()` 退出（走现有 `closeEvent`，以便设备与录制按既有流程收尾）。

`checkAndHandleRevocation` 中以下预防性代码删除：`originalLicense` 比对、`machineId.isEmpty()` 判断（授权已校验通过时不可能为空）、删除失败的 `qWarning` 分支、额外的 8 秒 `deleteLater` 计时器（请求本身已有超时）。只保留一个清理路径。

### 7.2 使用 Fluent-Qt 组件（`native/src/ui/fluent_controls.h`、`collapsible_panel.h`）

| 现有 | 改为 |
|---|---|
| QPushButton | `ActionButton`；“申请授权”用 Accent，其余 Standard。同一时刻最多一个 Accent 按钮 |
| QLineEdit（机器码、单位） | `TextInput` |
| QPlainTextEdit（离线许可证） | `fluent::textfields::TextEdit`（有 `toPlainText` / `setPlaceholderText`） |
| QToolButton 折叠 + QFrame | `CollapsiblePanel` |
| localErrorLabel_ / resultPanel_ / statusLabel_ + refreshBtn_ | **一个** `fluent::status_info::InfoBar`，`actionWidget` 放“立即检查/重试”按钮，不可关闭 |
| 标题 QLabel | `fluent::textfields::Label`，使用 Typography 的标题角色 |

- InfoBar 严重程度：进行中/等待审批 = Informational；到期、停用、未通过、网络失败 = Warning；许可证无效、不属于本机、读不到机器码 = Error。
- **窗口内同一时刻只有一条消息。** 服务器结果到达后替换本地错误（例如本地“已于某日到期”被“授权已到期，请联系管理员续期”替换），不并列显示。
- 主题：把 `MainWindow::applyTheme` 中构造 QPalette 的部分提取为 `fluent_controls.h` 里的一个 inline 函数，主窗口和激活窗口共用；激活窗口同时应用 `nativeFluentStyle()`。**删除激活窗口手写的 `setStyleSheet`。** 应用目前不持久化主题，激活窗口按默认浅色即可。
- 复制机器码后按钮文字短暂变为“已复制”（约 2 秒），不另加提示。
- `activation_dialog_test` 的 CMake 目标需链接 Fluent 库。

### 7.3 代码简洁要求

- 不为界面已经阻止的情况写防御代码：按钮已禁用就不在槽函数里再判断。`busy_` 判断只保留在会被定时器触发的 `onRefreshStatus`。
- 删除 `handleStatusResponse` 中 `activation.not_found` 错误码分支：`/api/v1/status` 对不存在的机器返回 200 + `status: not_found`，该分支不会触发。`handleActivateResponse` 中的同名分支保留。
- 不写 `resizeToContent` 一类手工计算高度的代码；优先用布局的尺寸约束让窗口随内容变化，用截图验证。
- 注释只解释“为什么”，不复述代码在做什么；不新增类或间接层（7.2 的调色板函数除外）。
- 完成后 `activation_dialog.cpp` 应明显短于首轮版本。

### 7.4 文案规范

- 一条消息只说两件事：发生了什么、下一步做什么。最多两个短句，不用分号串联。
- 用日常词：“许可证”“授权”“机器码”可以用；不用“签名”“水位”“时间保护状态”“payload”等。
- 不重复：按钮已写“申请授权”，旁边不再解释“点击申请授权”；标题和提示不说同一件事。
- 英文（en_US）按英语母语习惯独立书写，句首大写、简短，不逐字直译；其他语言参照中英文同等简短。

zh_CN 定稿（键名可沿用首轮；`error.license.*` 先确认其他位置是否引用，只改文字不改键）：

| 键 | 文案 |
|---|---|
| title | 激活 BoundFree EEG |
| machine_id_label | 本机机器码 |
| copy / copied | 复制 / 已复制 |
| org_name | 单位或联系人（选填） |
| request | 申请授权 |
| approval_hint | 管理员审批通过后自动激活。 |
| offline_section | 无法联网？使用离线许可证 |
| paste_placeholder | 粘贴管理员提供的许可证 |
| activate_btn | 激活 |
| activated | 激活成功，授权有效期至 {expires_at}。 |
| status_waiting | 已提交申请，等待管理员审批。通过后会自动激活，也可以先关闭软件，下次打开时自动检查。 |
| status_refresh / retry | 立即检查 / 重试 |
| checking / registering / activating | 正在检查授权状态… / 正在提交申请… / 正在激活… |
| network_unavailable | 连不上授权服务器。请检查网络后重试，或使用离线许可证。 |
| status_expired | 授权已到期。请联系管理员续期，续期后点击“立即检查”。 |
| status_revoked | 授权已被管理员停用。如有疑问请联系管理员。 |
| status_rejected | 申请未通过。如有疑问请联系管理员。 |
| local_expired | 本机授权已于 {expires_at} 到期。 |
| wrong_machine | 这份许可证属于另一台电脑，不能在本机使用。 |
| invalid_key | 许可证无效，请确认已完整复制。 |
| empty_license | 请先粘贴许可证。 |
| machine_id_unavailable | 读取不到本机机器码，无法激活。请联系管理员。 |
| status_machine_already_bound | 本机已绑定其他授权，请联系管理员处理。 |
| request_failed | 请求没有成功，请稍后重试。 |
| error_signing_failed | 服务器暂时无法签发授权，请联系管理员。 |
| error_invalid_machine_id | 本机机器码无效，请联系管理员。 |
| error_rate_limited | 操作太频繁，请 {retry_after} 秒后再试。 |
| revoked_on_server | 本机授权已被管理员停用，需要重新激活才能继续使用。 |
| error.license.clock_rollback | 电脑时间比上次使用时更早。请把系统时间调准后重新打开软件。（若 i18n 检查要求保留时间参数，末尾加“上次使用：{last_seen_utc}”） |
| error.license.clock_state_missing / clock_state_corrupted | 本机授权记录异常。联网后会自动重新获取授权；无法联网请联系管理员。 |
| error.license.clock_state_write_failed / save_failed | 无法保存授权，请检查用户目录是否可写。 |

### 7.5 构建补充

Fluent 分支需要 Fluent-Qt 源码，使用本地副本避免联网下载；构建目录换新：

```bash
TC=/home/vsonic12138/workspace/BoundFree_Workspace/toolchains/qt6
$TC/bin/cmake -S native -B /mnt/boudfree-dev/build/community-activation-fluent \
  -DCMAKE_PREFIX_PATH=$TC -DCMAKE_CXX_COMPILER=$TC/bin/x86_64-conda-linux-gnu-g++ \
  -DFETCHCONTENT_SOURCE_DIR_BF_FLUENTQT=/mnt/f/workspace/BoudFree/.dev/deps/fluentqt-766e13c8
```

第五节的测试与截图要求不变；截图另加“授权已到期”和“许可证无效”两种状态，中英文各一套。

## 八、审批意见（第三轮）

第二轮已达成：Fluent 组件、单一 InfoBar、立即生效、文案定稿、预防性代码删除。以下按截图与代码审阅继续调整。

### 8.1 界面

1. **按钮尺寸**：本窗口按钮不用 Small 尺寸，高度与输入框一致（机器码行的“复制”目前明显矮于输入框）。Standard 按钮需能一眼看出是按钮（有边框或底色），不能像纯文字。
2. **InfoBar 排版**：现在图标单独占一行，文字缩进到下一行，“立即检查”又占一行，显得空。要求图标与第一行文字对齐、无空行；操作按钮与文字左对齐或位于右侧。先查 InfoBar 的 title/message/singleLine 用法，选最自然的一种，用截图确认。
3. **申请区**：单位输入框与“申请授权”按钮放在同一行（按钮在右）；“管理员审批通过后自动激活。”放在下方，使用次要文字颜色（`fluent::textfields::Label` 的 Secondary 文本色）。
4. **离线区展开后**：内容与标题保持一致的内边距，输入框不贴边；“激活”按钮在内容区右下，留出边距。

### 8.2 代码

1. `buildUi` 中 `setPalette(fluentPalette())` 重复两次，删一次。
2. 尺寸代码只保留必要的一处约束：`setMinimumWidth`、`addStrut`、`setPreferredWidth`、`setMultiLineMinHeight`、`setMultiLineActionMinHeight` 逐个确认是否必需，不必需的删除。
3. InfoBar 按视觉顺序直接创建并加入布局，不要 `insertWidget(indexOf(...))`。
4. 离线区用 `CollapsiblePanel::setContentLayout` 传入自己的布局，不要 `parentWidget()->layout()->setAlignment(...)`。
5. 授权失败的严重程度判断（Expired 为 Warning，其余为 Error）目前写了多处，合并为一个小函数。
6. `LicenseManager::saveLicense` 成功时把已校验的 payload 放进返回结果；`onOfflineActivate` 直接使用，删除再次解码和 `"2099-12-31"` 默认值。
7. 时间回拨提示中的 `{last_seen_utc}` / `{current_utc}` 目前是 UTC ISO 原文（如 `2026-11-04T08:00:00.000Z`），显示前转为本地时间 `yyyy-MM-dd HH:mm`。

### 8.3 验证

重新构建并运行第五节与 7.5 的全部检查；重新生成中英文五种状态截图（默认、等待审批、离线展开、授权到期、许可证无效），另加一张中文“读取不到机器码”状态。

## 九、审批意见（第四轮：国际化）

> 范围修正：产品只发布 zh_CN、zh_TW、ja_JP、en_US。质量核对、截图与截断断言只针对这四种；ko_KR/de_DE/fr_FR/es_ES 仅为不编译、不进包的保留源文件，只需满足 `check_i18n.py` 的键与占位符要求；pseudo 仅用于测试。以下第 1、2 条及第 5 条中涉及这些语言的部分不再执行。

用户要求国际化不得遗漏。`check_i18n.py` 只校验键、空译与占位符，以下为人工核对发现的问题：

1. **未翻译**：`error.license.missing`、`error.license.invalid_signature`、`error.license.expired` 在 ko_KR、de_DE、fr_FR、es_ES 中仍为英文原文。它们属于 `contracts/error_codes.json` 的通用错误码，主窗口按 `error.<code>` 显示，必须翻译（保留占位符）。
2. **西班牙语语气不一致**：es_ES 其余文案以 usted 为主（约 27:3），本轮新增的激活文案却用 tú（Pega、Comprueba、Contacta、Conéctate、Corrígela…）。统一改为 usted。`status_revoked` 的 “Ponte en contacto con él” 不自然，改为与 `status_rejected` 同一句式。
3. **语义对齐**：各语言含义以 en_US 为准，不增不减。例如 `request_failed` 在 zh_TW/ja/ko/de/fr/es 中多了“或使用离线许可证”，en_US/zh_CN 只说“稍后重试”；`status_waiting` 各语言需包含“可以先关闭软件”的意思。逐键核对激活相关全部词条。
4. **术语一致**：每种语言内“许可证 / 授权 / 机器码 / 激活 / 管理员”的译法与该语言文件其余部分一致（先在该文件中查已有用法），同一窗口内不混用两种说法。
5. **全部语言截图与截断检查**：截图循环从 zh_CN/en_US 扩展到 zh_TW、ja_JP、ko_KR、de_DE、fr_FR、es_ES 与 pseudo；并在测试中加入断言：所有按钮和标签的文字完整显示（不被截断、省略或换行挤出控件），窗口宽度在各语言下不超出合理范围（不超过 720 像素）。德语、法语长文案需重点确认。
6. 验证：重新运行 `check_i18n.py`、`activation_dialog_test`（含全部语言截图）及第五节其余检查；汇报每种语言修改的键和截图目录。

## 十、审批意见（第五轮：收尾）

1. InfoBar 在无操作按钮且文字换行时仍按 `setMultiLineActionMinHeight(90)` 留出高度，ja_JP“读取不到机器码”截图下方出现大块空白。无操作按钮时不应保留按钮高度。
2. en_US `status_waiting` 改为：`Request sent. Waiting for administrator approval. The app activates automatically once approved, and you can close it and check again next time you open it.`；若仍显冗长，可取 `Request sent. Waiting for administrator approval. It activates automatically once approved — you can close the app in the meantime.`（二选一，以截图可读为准）。
3. en_US 统一使用 “your administrator”：`status_revoked` 改为 `Your administrator disabled your license. Contact them with any questions.`
4. ja_JP 引用按钮名时加「」：`status_expired` 中写作「今すぐ確認」。
5. 重新运行 activation_dialog_test（四种正式语言 + pseudo 截图）与 check_i18n，汇报。
