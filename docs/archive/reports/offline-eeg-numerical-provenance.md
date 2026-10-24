---
title: 离线脑电眼电分离数值来源与对照证据
doc_type: report
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-10-20
archived_at: 2026-10-23
---

# 离线脑电眼电分离数值来源与对照证据

本报告记录 Community 原生分离数值模块的迁移范围、可复现命令和已运行证据。用户已明确授权按已确认方案迁移 Pro 的自有离线分析功能，并允许子代理协作。本报告只覆盖受控合成数据数值验证，不代表真实电极记录效果、临床用途、Windows 实机或安装包验收。

## 来源与公开边界

| 输入参考 | Community 对应文件 | 内容 |
| --- | --- | --- |
| Pro `Engine/Processing/eegicacore.h/.cpp` | `native/src/dsp/ica_core.h/.cpp` | 小矩阵 Jacobi 特征分解、秩检查、白化、对称 FastICA、分组投影、零相位高通 |
| Pro `Engine/Processing/eeganalysis.cpp` 的数值段 | `native/src/dsp/eeg_separation.h/.cpp` | Blackman 窗 sinc 抗混叠、观测/参考分工、参考相关与高频能量线索 |
| Pro `tests/eeg-ica-core.cpp` | `native/src/dsp/ica_core_test.cpp` | 受控三源、16 路独立源和失败条件回归 |

上述 Pro 文件在本次检查时属于工作区中未提交的新增文件；Git 历史不能提供逐行来源证明。文件自身的 BoundFree 标识、本地功能说明和实现边界支持其为本工作区新增实现。此次移植没有引入 RHX 文件读取、Intan 设备控制、Qt 界面、商业授权、私钥或第三方 ICA 实现；没有增加算法依赖。此说明是本地迁移清单，不应当作独立法律审计结论。

参考文件 SHA-256（2026-10-20）：

```text
9686771c05152bd50a721c9403926358675934a1bfd4acfea34c6d85daa2450e  eegicacore.cpp
a376ecb1eb37c9bc2e237026ceb3651c48c7710fb6ff109a2fb7e0e5d24c3519  eegicacore.h
455df611f8db7ef240cdf75ce2530d04efeaa13037bfadc987ed70bac48fc155  eeganalysis.cpp
```

Community 在保持数值算法行为的同时增加了结构化错误码、矩阵尺寸/非有限值校验、上下文完整性检查、协作取消和重建恒等式验收。输入为 double µV，因此 256 MiB 输入数组上限按每个样本 8 字节计算；Pro 读取层使用 float 原始片段，不能直接沿用其按 4 字节的估算。

## 数值与分组约定

- 2–32 路观测，观测加参考至多 40 路；分析范围 5–120 秒，输出 250/500 Hz，不上采样，输出至多 60000 点。
- 降采样采用对称 Blackman 窗 sinc，截止频率为输出采样率的 0.4 倍，半宽为 `ceil(12 * sourceRate / outputRate)` 个源样本。文件真实边界可夹紧并重新归一化；普通片段边界缺少上下文时拒绝运行。
- 拟合副本使用 1 Hz 高通；重建使用陷波后但未高通的副本，保留已有输入中的慢变化。固定种子 42、默认最多 600 次迭代、容差 `1e-6`。
- 分组编号为 0 脑电候选、1 眼电估计、2 肌电贡献、3 未确定。修改标签只投影，不重复拟合。参考相关和高频比均是线索，不是分类准确率或自动生理标签。
- 各组贡献和残差不重复添加均值：`analysis = mean + eeg + eog + emg + unresolved + residual`。独立成分为任意单位；投影为 µV。
- ADC 饱和、物理单位、文件时间轴和源文件指纹由边车按格式验证；纯数值核心不会从 µV 数值猜测 ADC 上下限。

## 独立数值测试

环境：Ubuntu GCC 11.4.0，C++17，Linux `6.18.33.2-microsoft-standard-WSL2`。从 `boundfree-community/` 执行：

```bash
g++ -O2 -std=c++17 -Wall -Wextra -Wpedantic native/src/dsp/ica_core.cpp native/src/dsp/ica_core_test.cpp -o /tmp/bf-community-ica-core-test
/tmp/bf-community-ica-core-test

g++ -O2 -std=c++17 -Wall -Wextra -Wpedantic native/src/dsp/ica_core.cpp native/src/dsp/eeg_separation.cpp native/src/dsp/eeg_separation_test.cpp -o /tmp/bf-community-separation-test
/tmp/bf-community-separation-test
```

已运行输出：

```text
Synthetic source 0 |r|=0.999973
Synthetic source 1 |r|=1
Synthetic source 2 |r|=0.999805
Reconstruction max error uV=2.84217e-14; EEG projection RRMSE=0.00915319
16-channel synthetic worst |r|=0.996978; iterations=10
PASS 16 ICA core checks (synthetic data only)
800 Hz alias amplitude at 200 Hz = 0.00137932 uV
Five-group reconstruction max error = 2.84217e-14 uV
PASS 33 separation checks (synthetic data only)
```

覆盖固定种子复现、参考独立于观测拟合、未确定默认归组、标签不改变源、取消、非有限输入、平线参考、低秩、非法通道 ID、缺失重采样上下文、区间溢出、非法陷波、未收敛、250 Hz 输出、50 Hz 陷波前原始副本、矩阵尺寸异常和重建破坏拒绝。

地址与未定义行为检查：

```bash
g++ -O1 -g -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer native/src/dsp/ica_core.cpp native/src/dsp/eeg_separation.cpp native/src/dsp/eeg_separation_test.cpp -o /tmp/bf-community-separation-sanitized
/tmp/bf-community-separation-sanitized
```

已运行通过 33 项分离检查，无 sanitizer 报告。

## Pro 同输入逐值对照

以下保留 2026-10-20 的历史逐值对照命令。2026-10-23 已按产品范围从 Pro 移除脑电眼电离线模块，当前 Pro 检出不再提供这些源文件；这段历史命令不再是当前工作区的可执行验证入口。Community 的独立数值测试和应用测试均在本仓库内完成，不依赖 Pro 文件。

从 BoundFree 工作区根目录执行以下自包含命令。它同时编译 Pro 原数值核心和 Community 数值核心，以同一 6 路 7500 点合成混合比较所有输出矩阵、均值、秩、迭代数与收敛值。两个命名空间不会冲突。

```bash
cat > /tmp/bf-community-pro-parity.cpp <<'CPP'
#include "eegicacore.h"
#include "ica_core.h"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
int main() {
    EEGICA::Matrix input(6, std::vector<double>(7500));
    std::mt19937 rng(852);
    for (size_t t = 0; t < 7500; ++t) {
        double sources[6];
        for (auto& source : sources) {
            const double u = (double(rng())+.5)/4294967296.0-.5;
            source = -std::copysign(1.,u)*std::log(1-2*std::abs(u));
        }
        for (int c=0;c<6;++c) {
            input[c][t]=20+c;
            for (int k=0;k<6;++k) input[c][t] += (c==k?10.:.8*std::sin(c+2*k+1))*sources[k];
        }
    }
    for (int notch : {0,50,60}) {
        EEGICA::Options proOptions; proOptions.notchHz=notch;
        bf::separation::ica::Options communityOptions; communityOptions.notchHz=notch;
        const auto pro=EEGICA::fit(input,proOptions);
        const auto community=bf::separation::ica::fit(input,communityOptions);
        if (pro.analysis!=community.analysis || pro.sources!=community.sources || pro.mixing!=community.mixing ||
            pro.unmixing!=community.unmixing || pro.residual!=community.residual || pro.mean!=community.mean ||
            pro.rank!=community.rank || pro.iterations!=community.iterations || pro.convergence!=community.convergence)
            throw std::runtime_error("migrated numerical output differs");
        std::cout<<"Pro/Community exact double parity: notch="<<notch<<" rank="<<community.rank<<" iterations="<<community.iterations<<'\n';
    }
}
CPP
g++ -O2 -std=c++17 -Iboundfree-pro/Engine/Processing -Iboundfree-community/native/src/dsp boundfree-pro/Engine/Processing/eegicacore.cpp boundfree-community/native/src/dsp/ica_core.cpp /tmp/bf-community-pro-parity.cpp -o /tmp/bf-community-pro-parity
/tmp/bf-community-pro-parity
```

已运行输出：

```text
Pro/Community exact double parity: notch=0 rank=6 iterations=8
Pro/Community exact double parity: notch=50 rank=6 iterations=6
Pro/Community exact double parity: notch=60 rank=6 iterations=6
```

本证据是在同一编译器、同一进程中的 double 逐值相等，不保证跨编译器、跨架构位级相同。跨平台验收应按数值容差、成分排列/符号及原通道贡献进行比较。源相关与 RRMSE 的阈值只适用于这些受控合成源，不能推广为真实脑电/眼电分离质量保证。

## 控制器与独立边车集成

新增 `native/src/controller/eeg_separation_controller_test.cpp` 使用 `QCoreApplication` 和公开控制器 API，实际启动独立 Python stdio 边车。RHD/RHS 使用本仓库固定的 `tests/fixtures/generated/offline-eeg/` 自有合成文件（来源与哈希见该目录 README），测试不再依赖外部 Pro 检出或工作区资源。HDF5、CSV、EDF 由测试自包含 Python 片段生成。每次运行创建唯一结果目录，不覆盖已有文件。

从工作区根目录运行（Python 环境需有当前边车依赖、`neo`、`h5py` 与 `pyedflib`；路径为本次实际验证环境）：

```bash
cd boundfree-community/native
cmake --preset wsl2-qt6-conda
cmake --build build --target eeg_separation_controller_test -j 4
cd ../..
BF_PYTHON_EXECUTABLE=/tmp/bf-analysis-io-env/bin/python boundfree-community/native/build/eeg_separation_controller_test "$PWD/boundfree-community" /tmp/bf-offline-eeg-integration
```

已运行通过以下场景：

- RHD/RHS 通道顺序、引用通道不进入 ICA、3 个分量和每路 6000 点；两格式的抗混叠原始输出 double 逐值相等。
- RHD 未确定/人工标签、RHS、HDF5、CSV、EDF 与教学记录八文件导出；逐点回读 CSV，时间轴相对源文件、float64 数值不变、均值只添加一次。
- 无物理单位 CSV 发出 `unitRequired`，取消后不保留输入源；显式确认 `uV` 后实际读取、拟合与导出，来源清单记录 `input_unit_explicit_by_user=true`。
- 标签变化撤销确认，撤销标记不重新拟合；改设置使结果、确认与撤销历史失效。
- 取消计算、计算中换源、检查中再次换源，旧代次不能覆盖当前结果。
- 已有结果目录拒绝覆盖，保留内存结果；源字节变化后拒绝计算与导出，拒绝导出时不发布正式目录。
- 随包教学记录按 SHA-256、通道与采样元数据识别并预填用途；实际分离得到 3 个分量。示例标记仅重建并撤销确认，一次撤销可恢复全部未确定，原分解对象保持不变。改分析区间后示例标记不可用；同名同通道但字节变化的教学副本不会触发模板。
- 在检查、拟合及真实写出暂存文件期间销毁控制器，取消并回收所有在途线程，退出后没有正式半成品或暂存残留。
- 等待真实暂存目录出现后取消导出，UI `cancel()` 在 250 ms 测试门槛内返回；立即打开教学源并重新分离，旧导出不会发布或覆盖新结果。该门槛仅是回归断言，不是通用性能承诺。
- 进入发布提交阶段时，修改设置与替换来源被阻止到发布确认完成，避免旧回调污染新输入状态。

本次真实控制器完整回归通过，代表性结果目录为 `/tmp/pytest-of-vsonic12138/pytest-14/test_offline_separation_real_s0/eeg-controller-dCcPxq/`。常规导出的五组 CSV 回读最大重建误差为 `2.84217e-14 µV`。测试中逐点 CSV 与时间轴断言纳入检查总数，不能把这些单元格断言解释成独立测试场景。

`tests/native/test_offline_eeg_separation.py` 将两个数值可执行程序、真实控制器与 Qt 工作区可执行程序接入日常 pytest。仅缺少对应已构建程序或可选 Python 依赖时跳过；产品失败仍为测试失败。可复现控制器与数值验证：

```bash
BF_PYTHON_EXECUTABLE=/tmp/bf-analysis-io-env/bin/python /tmp/bf-analysis-io-env/bin/python -m pytest -q boundfree-community/tests/native/test_offline_eeg_separation.py -k 'not workspace'
BF_PYTHON_EXECUTABLE=/tmp/bf-analysis-io-env/bin/python /tmp/bf-analysis-io-env/bin/python -m pytest -q boundfree-community/tests/sidecar/test_analysis_io.py
```

边车专用测试在本次环境为 `28 passed`；两项数值加控制器包装测试为 `3 passed`。Qt 工作区、冻结边车、Windows 实机 UI、高 DPI、新手人工走查与真实电极数据由各自验收报告记录，本报告不据此替代其验证。
