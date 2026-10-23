---
title: Community 版本与发布 SOP
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-23
---

# Community 版本与发布 SOP

本流程仅适用于本仓库。GitHub `Vsonic12138/boundfree-eeg-native` 承载 Actions；Gitee `BigYellow12138/boundfree-community` 可保留代码副本。推送 Gitee 本身不会触发 GitHub Actions；合入 GitHub `master` 才会自动构建。两个远端的可见性独立，不因本流程改变。

## 1. 只记住三个状态

| 面向谁 | 仓库版本 | 安装包版本示例 | 是否自动发布 Release |
| --- | --- | --- | --- |
| 内部开发 | `0.2.2-dev` | `0.2.2-dev.158.1+g12345678` | 否 |
| 小范围试用 | `0.2.2-beta.1` | `0.2.2-beta.1` | 否，维护者选择发版 |
| 正式交付 | `0.2.2` | `0.2.2` | 否，维护者检查草稿后发布 |

`158` 是 Community CI 的运行编号，`1` 是重跑次数，`12345678` 是提交 SHA 前八位。源码不被自动提交回写。完整 SHA、基础版本和本次运行编号在包内 `build-info.json`，Native、边车与安装器读取同一份生成的 `version.json`。运行编号是工作流标识，不是产品 PATCH，也不是跨工作流统一的发布计数。

普通提交无需修改版本。已发布版本的 bug 修复用 PATCH；新阶段能力用 MINOR；重大不兼容变化评估 MAJOR。`1.0.0` 留给第一个完整、可持续交付产品。历史 `v3.*` tag 来自上游历史，不代表 Community 产品版本；本流程只识别 `community-v*`。

## 2. 日常开发与下载安装包

1. 从更新后的 `master` 切功能分支，提交后向 GitHub `master` 创建 PR。
2. PR 自动运行 `Community CI`：编译 Windows/Linux、版本/i18n/资源检查、Python 与格式契约回归、ICA/导出/播放回归、冻结边车、打包与安装态 smoke。
3. 合入 `master` 后再次自动构建该主线提交。**自动构建不会创建 Release，也不会推送 tag。**
4. GitHub 仓库 → **Actions → Community CI → 成功的运行 → Artifacts**，下载 `BoundFreeEEG-Windows-x64` 或 `BoundFreeEEG-Linux-x64`，解压外层 Artifact ZIP。
5. Windows 运行里面的 `BoundFreeEEG_Setup_*.exe`；Linux 解压 `.tar.gz` 后运行 `BoundFreeEEG/BoundFreeEEG.sh`。
6. 核对旁边 `SHA256SUMS-*.txt` 和 `build-info-*.json`。保存缺陷时附上完整版本和提交 SHA。

Artifacts 默认保留 30 天，过期可从同一代码提交重新构建（开发包重跑次数变化）。值得长期保留的试用版应走 beta Release。GitHub 当前仓库访问权限同样限制下载权限；本流程不自动公开仓库。

手动重建：**Actions → Community CI → Run workflow**，选分支。按钮要求工作流已在默认分支存在；首次接入在 PR 上验证，合入后按钮才可正常使用。

## 3. 调整交付阶段版本

在功能分支上执行，示例仅表示操作方式，不表示当前版本已验收：

```bash
# 选择一个开发阶段给试用者
python3 scripts/common/version_sync.py set 0.2.2-beta.1 --type chore --summary "准备首轮试用"
# beta 修复后再发一轮
python3 scripts/common/version_sync.py set 0.2.2-beta.2 --type chore --summary "修复试用反馈并准备第二轮"
# 验收完成，准备正式版
python3 scripts/common/version_sync.py set 0.2.2 --type chore --summary "完成验收并准备正式发布"
# 正式版发布后，开启下一修复周期
python3 scripts/common/version_sync.py next patch --type chore --summary "开启下一修复周期"
# 或开启下一功能阶段，二选一
python3 scripts/common/version_sync.py next minor --type chore --summary "开启下一功能阶段"
python3 scripts/common/version_sync.py check
```

同步工具更新 `version.json`、`version.md`、`README.md`、`AGENTS.md`、`installer/BoundFreeEEG.iss` 五处，检查 diff 后一起提交。不要手动只改 JSON。`ipc_protocol_version` 不随产品版本自动递增；边车 Python 包自身的 setuptools 版本属于内部实现，运行时产品版本仍以 `version.json` 为准。

## 4. 创建 beta / stable Release 草稿

1. 在本仓库 **Issues → New issue → Community 发版验收** 创建一次发版的验收记录，填入目标版本、变更范围、目标系统、验证结果和限制。
2. 把版本准备 PR 合入 GitHub `master`，等待该提交的 CI 全部成功。确认这份源代码就是要交付的版本。
3. 在该提交创建 annotated tag（以下以 beta 为例）：

```bash
git fetch origin master --tags
git tag -a community-v0.2.2-beta.1 <已通过CI的完整提交SHA> -m "Community 0.2.2-beta.1"
git push origin refs/tags/community-v0.2.2-beta.1
```

4. **Actions → Community Release → Run workflow**，从 `master` 执行，输入已有 tag 和本仓库验收 Issue 的完整 URL。stable 还需确认人工验收已完成。
5. 工作流校验 annotated tag、tag 与 JSON 一致、提交属于 `master` 历史、Issue 属于本仓库且可读取、目标 Release 不存在。通过后重新构建该 tag 的源码、执行测试、安装态 smoke 并上传双平台产物。
6. 两个平台都成功后，在 **Releases** 生成草稿。beta 带 prerelease 标记；stable 不带。检查包名、版本、SHA256、变更说明与验收 Issue，再由维护者点击 **Publish release**。
7. 可按需把已验证代码和 tag 同步到 Gitee；本流程不自动创建 Gitee Release。

不得给 `dev` 创建 Release。tag 创建本身不会自动发布。手动 `Community CI` 也不会创建草稿。Release 构建从精确提交重建，不从另一个 CI 随意取包；草稿引用的是自己的构建运行与 SHA256。

## 5. 对外发布门槛

beta：明确试用范围与已知限制；关键打开/播放/分析/导出路径通过回归；至少在实际目标 Windows 或 Linux 桌面试运行。unsigned beta 可用于试用，必须在说明中明确签名状态。

stable：完成承诺功能验收，无已知数据丢失、错误结果和高频崩溃；完成干净 Windows 10/11 安装、旧版升级、卸载、桌面操作、正式授权兼容与代表性科研文件验证；发布 Linux 时另需目标 Linux 桌面验收。把证据放入本次验收 Issue。runner 的命令行和安装测试不等同于完整干净机、真实 GPU、真实硬件或算法效果验收。

正式 Windows 包继续强制 Authenticode 签名。配置仓库 Actions Secrets：

| Secret | 用途 |
| --- | --- |
| `BF_CODE_SIGN_PFX_BASE64` | 可在 CI 使用的组织代码签名 PFX 的 base64 编码 |
| `BF_CODE_SIGN_PASSWORD` | PFX 密码 |

这是 Windows 软件签名证书，与授权系统签发私钥无关。流水线仅使用仓库已有 `license_public.pem` 嵌入授权校验公钥；绝不需要授权签发私钥。签名证书缺失时 stable 会失败，不允许绕过为 unsigned stable。受硬件保护且不可导出的签名证书需另接组织签名服务，不能直接使用此 PFX 入口。

## 6. 失败、重跑与版本不可变

- PR / master CI 失败：打开失败 step，修复后提交；可用 Re-run failed jobs 排查临时网络问题。测试报告在 `reports-*` Artifact。
- 依赖或网络失败：可以重跑；开发包生成新的尝试编号，不覆盖旧包。依赖直接版本已固定，runner 镜像和传递依赖仍可能变化，完整可复现环境尚需锁定。
- Release 构建失败且尚无草稿：若是环境问题，在同一 tag 上重跑；若需修改源码，创建新 beta.N 或新 PATCH，不移动 tag。
- 已有草稿或已发布 Release：工作流拒绝覆盖。改用新版本；不替换已经对外分发的同版本安装包。
- 默认只读 token；仅生成 Release 草稿的 job 有 `contents: write`。PR 构建不会获得签名 secrets。
- Windows 下载的是 Setup 安装包；Linux 当前为 **Ubuntu 24.04 x86_64** 归档，含 Qt、冻结边车和主要动态库，不承诺其他发行版或 AppImage 兼容。仍依赖目标桌面、驱动和系统服务。

## 7. 首次接入的验证边界

先通过真实 GitHub PR 运行验证构建、测试与产物。合入默认分支后再验证手动 CI 和 Release 入口。未经实际运行的入口必须在验收报告注明“待验证”；不能为了演练直接发布正式版或把开发版本改成稳定版。当前结果见 `docs/reports/community-release-automation.md`。

参考：[GitHub 触发事件](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)、[Artifacts](https://docs.github.com/en/actions/tutorials/store-and-share-data)、[SemVer](https://semver.org/)。
