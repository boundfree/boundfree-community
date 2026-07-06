# Git 开发与版本管理规范

本文定义 `boundfree-eeg-native` 的分支、版本号、发布和热修复规则。目标是让开发分支容易并行，发布版本可追溯，程序界面、安装包、Git tag 与授权版本保持一致。

## 基本原则

- `master` 是日常开发集成分支，代表下一个版本的开发状态。
- `release` 是稳定发布分支，只保存已发布或准备发布的稳定提交。
- 功能开发从 `master` 切短分支，完成后合回 `master`。
- 正式发布 tag 只打在 `release` 分支上。
- 版本号不随每个功能分支变化，只在开启新版本周期、准备发布、hotfix 发布时变化。
- 版本号的单一人工维护来源是根目录 [`version.json`](../version.json)。

## 版本号规则

采用 SemVer：

```text
MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]
```

含义如下：

| 字段 | 规则 | 示例 |
| --- | --- | --- |
| `MAJOR` | 不兼容变更，正式商业发布后谨慎增加 | `2.0.0` |
| `MINOR` | 新功能、完成一个重要里程碑、兼容性增强 | `0.2.0` |
| `PATCH` | bug 修复，不改变功能边界 | `0.2.1` |
| `PRERELEASE` | 开发版、内测版、候选版 | `0.2.0-dev`, `1.0.0-rc.1` |
| `BUILD` | 构建元数据，可由脚本追加 Git sha | `0.2.0-dev+g578f1d3` |

迁移期使用 `0.x.y`。第一个完整客户可用版本发布为 `1.0.0`。

## 版本源文件

根目录 `version.json` 是唯一需要手动更新的版本源。

```json
{
  "schema_version": 1,
  "product_name": "BoundFree EEG",
  "version": "0.1.0-dev",
  "release_channel": "dev",
  "ipc_protocol_version": 1
}
```

字段约定：

| 字段 | 用途 |
| --- | --- |
| `schema_version` | `version.json` 自身结构版本，初始为 `1` |
| `product_name` | 程序显示名、安装包显示名 |
| `version` | 产品版本，遵循 SemVer |
| `release_channel` | 发布通道：`dev`、`alpha`、`beta`、`rc`、`stable` |
| `ipc_protocol_version` | 主程序与 Python 边车的 IPC 协议版本，独立于产品版本 |

后续 M1 工程骨架应从该文件生成或读取版本信息：

- C++ 主程序 `bf_app --version` 输出产品版本。
- 程序界面“关于”窗口显示产品版本。
- Python 边车握手返回边车版本和 `ipc_protocol_version`。
- 打包脚本用同一版本号生成安装包文件名。

开发版可以在运行时或构建时追加 Git sha，例如：

```text
BoundFree EEG 0.1.0-dev+g578f1d3
```

正式发布版不追加 `-dev`：

```text
BoundFree EEG 0.1.0
```

## 分支模型

### `master`

- 日常开发集成分支。
- 默认版本号使用开发态，例如 `0.2.0-dev`。
- 接收 `feature/*`、`fix/*`、`spike/*` 分支合并。
- 不在 `master` 上打正式发布 tag。

### `release`

- 稳定发布分支。
- 只接收发布准备提交和 hotfix。
- 版本号必须是正式版或候选版，例如 `0.2.0-rc.1`、`0.2.0`。
- 正式 tag 只打在 `release` 分支，例如 `v0.2.0`。
- 项目初始阶段可以提前创建 `release` 作为受保护占位分支；首个 `rc` 或 `stable` 版本前，不从该占位分支打 tag 或产出正式发布包。

### 开发分支

开发分支从最新 `master` 创建。

命名规则：

| 类型 | 命名 | 用途 |
| --- | --- | --- |
| 功能 | `feature/m1-skeleton` | 新功能或里程碑开发 |
| 修复 | `fix/license-version-check` | 修复开发版问题 |
| 验证 | `spike/gpu-retake` | 技术验证、性能复测 |
| 文档 | `docs/release-process` | 文档或流程调整 |

创建分支：

```bash
git switch master
git pull
git switch -c feature/m1-skeleton
git push -u origin feature/m1-skeleton
```

开发分支一般不修改 `version.json`。只有明确任务是“开启新版本周期”或“准备发布”时才修改版本号。

### hotfix 分支

`hotfix/*` 用于修复已经发布给用户的正式版本。它从 `release` 或正式 tag 切出，不从 `master` 切出，避免把 `master` 上未完成的新功能带入补丁安装包。

示例：

```bash
git switch release
git pull
git switch -c hotfix/0.2.1-license-crash
```

修复完成后：

```bash
git switch release
git merge hotfix/0.2.1-license-crash
git tag -a v0.2.1 -m "BoundFree EEG v0.2.1"

git switch master
git merge hotfix/0.2.1-license-crash
```

## 日常开发流程

1. 从 `master` 拉取最新代码。
2. 创建短开发分支。
3. 在分支内提交小步变更。
4. 本地构建、测试、冒烟检查通过。
5. 合并回 `master`。
6. 删除已经合并的开发分支。

推荐提交信息：

```text
feat: 增加边车握手协议
fix: 修复授权版本判断
docs: 补充发布流程
spike: 记录真实 GPU 复测结果
chore: 更新版本号到 0.2.0-dev
```

## 开启新版本周期

当准备进入下一个版本开发周期时，在 `master` 上更新 `version.json`。

示例：发布 `0.1.0` 后，开启 `0.2.0` 开发周期。

```bash
git switch master
git pull
# 修改 version.json: version = 0.2.0-dev, release_channel = dev
git add version.json
git commit -m "chore: 开启 0.2.0 开发周期"
git push
```

之后从该节点切出功能分支。

## 发布流程

1. 确认 `master` 已包含要发布的全部提交。
2. 从 `master` 创建发布准备分支，例如 `release-prep/v0.2.0`。
3. 修改 `version.json` 为候选版，例如 `0.2.0-rc.1`，`release_channel` 为 `rc`。
4. 构建安装包并完成 QA。
5. QA 通过后修改 `version.json` 为正式版，例如 `0.2.0`，`release_channel` 为 `stable`。
6. 合并到 `release`。
7. 在 `release` 上创建 annotated tag。
8. 从 tag 构建发布包，上传 Gitee Release。
9. 将发布提交同步回 `master`。
10. 在 `master` 上开启下一个开发版本，例如 `0.3.0-dev`。

tag 命名：

```bash
git tag -a v0.2.0 -m "BoundFree EEG v0.2.0"
git push origin v0.2.0
```

正式 tag 不复用、不覆盖。已经公开发布的 tag 如需修复，只能发布更高 patch 版本。

## 发布前检查

- `git status --short` 为空。
- 当前分支与发布流程匹配：正式 tag 在 `release` 上。
- `version.json` 中的 `version` 与 tag 一致，例如 `version = 0.2.0` 对应 `v0.2.0`。
- `release_channel` 与版本一致：正式版为 `stable`，候选版为 `rc`。
- 主程序、边车、安装包显示同一产品版本。
- `ipc_protocol_version` 与主程序、边车协议实现一致。
- 安装包文件名包含产品版本。
- 发布说明、SHA256、第三方许可清单已同步。

## 授权版本策略

旧版授权 payload 已包含 `version` 字段。新仓库继续保留该字段，但授权不应绑定到 patch 级别，否则 `1.0.1` 这类修复版会导致客户重新申请授权。

推荐规则：

- 授权限制到主版本或主次版本，例如 `1` 或 `1.0`。
- 程序版本 `1.0.0`、`1.0.1`、`1.0.2` 可共用 `1.0` 授权。
- 程序版本 `1.1.0` 是否需要新授权，由商业策略决定。
- 授权校验失败时，UI 显示当前程序版本和授权允许版本，方便售后定位。

## 不要做的事

- 不要在每个 feature 分支上修改 `version.json`。
- 不要在 `master` 上打正式发布 tag。
- 不要覆盖已经发布过的 tag。
- 不要把安装包、构建目录、私钥或 keygen 工具打进正式发布包。
- 不要把产品版本和 IPC 协议版本混成一个字段。
