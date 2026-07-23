---
title: M8 Windows 打包与发布验收记录
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-07-22
milestone: M8
---

# M8 Windows 打包与发布验收记录

## 结论

M8 打包实现与当前 WSL2 Linux 可验证部分已完成：冻结边车、Native 安装布局、安装目录资源定位、主题与系统 UI、离线授权实现、发布资产检查及全量回归通过。

M8 尚未完成发布验收。Windows x64 Release 构建、`windeployqt`、Inno Setup 安装包生成、组织证书代码签名、干净 Windows 10/11 安装与全功能人工验收仍须在 Windows 发布机执行；当前产物不得标为 `rc` 或 `stable`。

## 已实现范围

- `sidecar/boundfree_sidecar.spec` 生成 onedir 冻结边车并收集 MNE 动态依赖。
- Native 优先从可执行文件目录加载版本、错误码、i18n 和冻结边车，不依赖源码工作目录。
- CMake install 规则组装 Native、版本、错误码、i18n 与法务资源。
- `installer/BoundFreeEEG.iss` 定义 Windows 10 x64、自选安装目录、快捷方式、卸载和法务文件。
- `build_release.ps1` 固定 MSVC Release、边车冻结、CMake install、`windeployqt`、Inno Setup 与签名顺序；`rc/stable` 缺少签名配置时失败。
- Native 离线授权实现机器码兼容、RSA-2048 PSS-SHA256 校验、保存/加载与启动门禁；正式发布仍须注入实际公钥并执行旧版真实密钥兼容验收。
- 主窗口实现亮/暗主题、清华紫高亮、dock 显隐菜单、文件/视频/退出快捷键、常驻文件和模式状态。
- i18n 静态检查覆盖 `zh_CN/en_US/pseudo` 与保留 locale 的 key 完整性；后端错误保持 `code + params` 并由 UI 本地化。

## 当前环境验证结果

```text
┌──────────────────────────┬──────────────────────────────────────────────┐
│ 场景                     │ 观察结果                                     │
├──────────────────────────┼──────────────────────────────────────────────┤
│ Native CMake build       │ PASS                                         │
│ 系统 UI 行为 smoke       │ 主题、dock、快捷键、状态标签 PASS            │
│ 冻结边车构建             │ PyInstaller onedir PASS，目录约 311 MiB      │
│ 冻结边车 CSV / EDF       │ open/get_slice PASS                          │
│ 冻结边车录制 / 导出      │ 双格式录制、三格式导出 PASS                  │
│ 安装布局                 │ CMake install PASS，Native stage 约 3.0 MiB  │
│ 脱离源码目录启动         │ --version 与 offscreen --smoke-ui PASS       │
│ 发布定向自动化           │ 43 passed                                    │
│ 全量 Python 测试         │ 235 passed                                   │
│ M3 / M4 黄金二进制       │ PASS                                         │
│ 授权实现 smoke           │ 25 项 PASS；启用门禁启动 PASS                │
│ i18n / JSON / 发布资产   │ PASS                                         │
└──────────────────────────┴──────────────────────────────────────────────┘
```

## 已执行命令

```text
cmake --preset wsl2-qt6-conda
cmake --build --preset wsl2-qt6-conda -j
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-m8-system-ui
python3 -m PyInstaller --noconfirm --clean --distpath ../release/sidecar-dist --workpath ../release/sidecar-build boundfree_sidecar.spec
BF_SIDECAR_EXECUTABLE=release/sidecar-dist/boundfree_sidecar/boundfree_sidecar native/build/bf_app --smoke-open-slice tests/fixtures/generated/simple_eeg.csv 0 0.05
BF_SIDECAR_EXECUTABLE=release/sidecar-dist/boundfree_sidecar/boundfree_sidecar native/build/bf_app --smoke-open-slice tests/fixtures/generated/simple_eeg.edf 0 0.05
BF_SIDECAR_EXECUTABLE=release/sidecar-dist/boundfree_sidecar/boundfree_sidecar native/build/bf_app --smoke-m7-record tests/fixtures/generated/simple_eeg.csv /tmp/bf-m8-record
BF_SIDECAR_EXECUTABLE=release/sidecar-dist/boundfree_sidecar/boundfree_sidecar native/build/bf_app --smoke-m7-export tests/fixtures/generated/simple_eeg.csv /tmp/bf-m8-export
cmake --install native/build --prefix release/stage-linux/BoundFreeEEG
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests -q
native/build/m3_dsp_test tests/golden/m3
native/build/m4_spectral_test tests/golden/m4
python3 scripts/check_i18n.py
python3 scripts/check_release_assets.py
python3 -m json.tool contracts/error_codes.json
```

安装树从 `/tmp` 使用绝对路径执行 `bf_app --version` 和 `bf_app --smoke-ui --language en_US` 均成功，验证资源定位不依赖当前工作目录。

## Windows 发布门禁

以下项目均未在当前 WSL2 环境执行，保持未验收：

- [ ] Visual Studio 2022 x64 Release 构建。
- [ ] `windeployqt` 后在无 Qt/Python 开发环境的干净 Windows 10/11 用户上启动。
- [ ] Inno Setup 安装、覆盖安装、卸载和自选目录。
- [ ] 使用正式公钥和旧版真实授权 key 完成激活兼容验收。
- [ ] 打开文件、回放、DSP、分析、视频、录制和三格式导出全流程人工验收。
- [ ] 使用组织证书签署 Native、冻结边车和安装包，并通过 `signtool verify /pa`。
- [ ] 记录证书主题、时间戳服务、安装包 SHA-256、Qt Multimedia/FFmpeg 实际分发许可证清单和 SmartScreen 结果。

## 已知边界

- 当前验证环境为 WSL2 Linux、Qt offscreen，不代表真实 Windows 显示、安装、驱动或媒体后端行为。
- Linux stage 不是交付物；仅用于验证 CMake install 布局与脱离源码目录运行。
- 开发环境 Qt Multimedia 报告 FFmpeg GPL 构建；Windows 发布前必须核对实际随包二进制及许可证，不能沿用开发环境结论。
- 真实组织代码签名证书、正式授权公钥和旧版真实授权样本不在仓库中，必须由发布环境安全注入。
