---
title: Windows 打包入口命名参数验证对齐
doc_type: report
status: completed
owner: BoundFree
last_updated: 2026-10-18
---

# Windows 打包入口命名参数验证对齐

`0.1.86-dev` 修正合并前全量测试发现的既有静态契约偏差。`scripts/windows/build_windows.ps1` 使用命名参数表调用 `build_release.ps1`，旧测试却要求所有参数直接出现在调用行，因此全量检查出现 335 passed、4 skipped、1 failed。

## 修正范围

生产打包脚本没有修改。测试兼容直接命名参数与命名参数表转发，并检查参数表在调用前定义、QtRoot/Python/Configuration/LicensePublicKeyFile 的实际变量来源及 SkipSigning=true。既有位置参数数组转发限制继续保留。

该脚本和测试在本次数据模式功能开始前的提交 `f9ad28f` 中已是同样内容，因此作为独立测试修复提交，未混入波形或导出功能提交。

## 验证

`python3 -m pytest tests/scripts/test_windows_build_entry.py -q` 通过，结果为 **18 passed in 0.03s**。额外通过临时变异输入确认：缺少 QtRoot、Python 来源错误、SkipSigning=false、引用未定义参数表四种情况都会被断言拒绝。

本记录只确认静态参数契约，不表示完成 Windows 实际打包、安装或桌面人工验收。

## 合并前全量回归

在 `0.1.86-dev` 从仓库根目录执行：

```bash
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests -q -rs
```

结果为 **336 passed、4 skipped，耗时 148.45s**。所有已执行测试通过，跳过项不作为验收通过证据：

| 跳过项 | 原因 |
| --- | --- |
| `tests/native/test_m8_runtime_layout.py` | 冻结边车未生成 |
| `tests/scripts/test_m8_release_assets.py` | 当前检查器调用无法导入 `check_release_assets` |
| `tests/sidecar/test_intan_reader.py` 两项 | 缺少真实 Intan RHD/RHS 样本 |

开发分支回归门禁已通过。M3/M4 黄金测试、国际化、版本同步与差异空白检查通过；Windows 实际发布和真实 Intan 格式验收仍按各自门禁执行。
