// Spike DSP 引擎自测：阈值检测 + 波形提取 + PCA/K-Means 排序。
// 风格参考 m4_spectral_test.cpp，自写 main，失败返回非零。
//
// 测试一：合成含已知 spike 的信号（基线噪声 + 在已知时刻插入负向尖峰模板），
//         验证 detectSpikes 的 timestamps 数量与位置接近真值（容差 ±2 样本）。
// 测试二：合成两类明显不同形状的波形各 N 个，验证 sortSpikes 能分成 2 个
//         主要簇，同类多数被分到同一簇（聚类纯度 >= 0.8 判定）。

#include "dsp/spike_detect.h"
#include "dsp/spike_sort.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

// 简单确定性 PRNG（xorshift64*），用于合成可复现的测试信号
struct TestRng {
    uint64_t state;
    explicit TestRng(uint64_t s) : state(s == 0 ? 1 : s) {}
    uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    double unit() {
        return static_cast<double>(next() >> 11) / static_cast<double>(1ULL << 53);
    }
    // 近似高斯（Box-Muller 简化版：两个均匀之和中心极限近似）
    double gauss() {
        double s = 0.0;
        for (int i = 0; i < 12; ++i) {
            s += unit();
        }
        return s - 6.0;
    }
};

// 负向尖峰模板：以 peakIdx 为负峰对齐，preMs=0.5, postMs=1.0, fs=30000
// 模板长度 = 45 样本，负峰位于 preSamples=15 处
std::vector<double> makeNegativeTemplate(int preSamples, int postSamples) {
    int len = preSamples + postSamples;
    std::vector<double> t(static_cast<std::size_t>(len), 0.0);
    // 形状：快速下降到 -amplitude（在 preSamples 处），随后较慢回升
    const double amplitude = 100.0;  // uV
    for (int i = 0; i < len; ++i) {
        int rel = i - preSamples;
        if (rel <= 0) {
            // 上升沿（由 0 降到 -amplitude），使用抛物线
            double frac = preSamples > 0 ? static_cast<double>(i) / preSamples : 1.0;
            t[static_cast<std::size_t>(i)] = -amplitude * frac;
        } else {
            // 下降沿（由 -amplitude 回到 0），指数衰减
            double decay = std::exp(-rel / 8.0);
            t[static_cast<std::size_t>(i)] = -amplitude * decay;
        }
    }
    return t;
}

bool testDetect() {
    const double fs = 30000.0;
    const int n = static_cast<int>(fs);  // 1 秒
    const int preSamples = 15;
    const int postSamples = 30;
    auto tmpl = makeNegativeTemplate(preSamples, postSamples);

    TestRng rng(20240724);
    // 基线噪声：std ~ 5 uV，spike 振幅 ~100 uV，远超噪声
    std::vector<double> signal(n, 0.0);
    for (int i = 0; i < n; ++i) {
        signal[i] = rng.gauss() * 2.0;
    }

    // 在已知时刻插入尖峰（peak 对齐到 knownPeakIdx）
    std::vector<int> knownPeaks = {1000, 5000, 10000, 15000, 20000, 25000};
    for (int pk : knownPeaks) {
        if (pk - preSamples < 0 || pk + postSamples > n) {
            continue;
        }
        for (int k = 0; k < preSamples + postSamples; ++k) {
            signal[pk - preSamples + k] += tmpl[k];
        }
    }

    bf::dsp::SpikeDetectConfig cfg;
    cfg.fs = fs;
    cfg.thresholdMad = 6.0;
    cfg.detectNegative = true;
    cfg.refractoryMs = 1.0;  // 30 样本
    cfg.preMs = 0.5;          // 15 样本
    cfg.postMs = 1.0;         // 30 样本

    bf::dsp::DetectedSpikes res = bf::dsp::detectSpikes(signal, cfg);

    if (res.timestamps.size() != knownPeaks.size()) {
        std::cerr << "FAIL detect: expected " << knownPeaks.size()
                  << " spikes, got " << res.timestamps.size() << "\n";
        return false;
    }

    const double tolSec = 2.0 / fs;  // ±2 样本
    for (std::size_t i = 0; i < knownPeaks.size(); ++i) {
        double expected = knownPeaks[i] / fs;
        double got = res.timestamps[i];
        if (std::fabs(got - expected) > tolSec) {
            std::cerr << "FAIL detect: spike " << i << " expected ~"
                      << expected << " got " << got << "\n";
            return false;
        }
    }
    // 验证 snippet 长度
    if (res.snippetLen != preSamples + postSamples) {
        std::cerr << "FAIL detect: snippetLen expected "
                  << preSamples + postSamples << " got " << res.snippetLen << "\n";
        return false;
    }
    // 验证每个波形长度
    for (const auto& wf : res.waveforms) {
        if (static_cast<int>(wf.size()) != res.snippetLen) {
            std::cerr << "FAIL detect: waveform length mismatch\n";
            return false;
        }
    }
    std::cout << "PASS detect: n=" << res.timestamps.size()
              << " snippetLen=" << res.snippetLen << "\n";
    return true;
}

bool testSort() {
    // 合成两类形状明显不同的波形各 N 个
    const int nPerClass = 40;
    const int nTotal = 2 * nPerClass;
    const int snippetLen = 45;
    TestRng rng(99);

    std::vector<std::vector<double>> waveforms;
    waveforms.reserve(nTotal);
    std::vector<int> trueLabels;
    trueLabels.reserve(nTotal);

    // 类 A：负向尖锐峰，模板同 detect 测试中的模板（振幅 80 uV + 小噪声）
    // 类 B：双向波（先正后负），形状明显不同
    for (int cls = 0; cls < 2; ++cls) {
        for (int i = 0; i < nPerClass; ++i) {
            std::vector<double> wf(snippetLen, 0.0);
            int pre = 15;
            for (int k = 0; k < snippetLen; ++k) {
                int rel = k - pre;
                double v = 0.0;
                if (cls == 0) {
                    // 类 A：负向尖锐
                    const double amp = 80.0;
                    if (rel <= 0) {
                        double frac = pre > 0 ? static_cast<double>(k) / pre : 1.0;
                        v = -amp * frac;
                    } else {
                        v = -amp * std::exp(-rel / 8.0);
                    }
                } else {
                    // 类 B：双向（先正后负）
                    const double amp = 70.0;
                    if (rel <= 0) {
                        double frac = pre > 0 ? static_cast<double>(k) / pre : 1.0;
                        v = amp * frac;
                    } else {
                        v = -amp * std::exp(-rel / 10.0);
                    }
                }
                v += rng.gauss() * 3.0;  // 小噪声
                wf[k] = v;
            }
            waveforms.push_back(std::move(wf));
            trueLabels.push_back(cls);
        }
    }

    bf::dsp::SpikeSortConfig cfg;
    cfg.nComponents = 3;
    cfg.nClusters = 2;
    cfg.maxIters = 100;
    cfg.seed = 42;

    bf::dsp::SpikeSortResult res = bf::dsp::sortSpikes(waveforms, cfg);

    if (static_cast<int>(res.labels.size()) != nTotal) {
        std::cerr << "FAIL sort: label count " << res.labels.size()
                  << " expected " << nTotal << "\n";
        return false;
    }
    if (static_cast<int>(res.pcaCoords.size()) != nTotal) {
        std::cerr << "FAIL sort: pcaCoords count mismatch\n";
        return false;
    }
    for (const auto& pc : res.pcaCoords) {
        if (static_cast<int>(pc.size()) != cfg.nComponents) {
            std::cerr << "FAIL sort: pcaCoords dim mismatch\n";
            return false;
        }
    }

    // 簇号范围 [0, nClusters)
    for (int lbl : res.labels) {
        if (lbl < 0 || lbl >= cfg.nClusters) {
            std::cerr << "FAIL sort: label out of range " << lbl << "\n";
            return false;
        }
    }

    // 纯度评估：对每个簇，看其内多数真值类别占比
    // 由于簇号与真值类号可能反转，取映射后的纯度
    std::vector<int> clusterToClass(cfg.nClusters, -1);
    for (int c = 0; c < cfg.nClusters; ++c) {
        int cnt[2] = {0, 0};
        for (int i = 0; i < nTotal; ++i) {
            if (res.labels[i] == c) {
                cnt[trueLabels[i]]++;
            }
        }
        clusterToClass[c] = (cnt[1] > cnt[0]) ? 1 : 0;
    }
    int correct = 0;
    for (int i = 0; i < nTotal; ++i) {
        if (clusterToClass[res.labels[i]] == trueLabels[i]) {
            correct++;
        }
    }
    double purity = static_cast<double>(correct) / nTotal;
    if (purity < 0.8) {
        std::cerr << "FAIL sort: purity " << purity << " < 0.8\n";
        return false;
    }
    std::cout << "PASS sort: purity=" << purity << " nClusters="
              << cfg.nClusters << "\n";
    return true;
}

}  // namespace

int main() {
    bool ok = true;
    ok = testDetect() && ok;
    ok = testSort() && ok;
    if (!ok) {
        std::cerr << "FAIL spike_dsp_test\n";
        return 1;
    }
    std::cout << "PASS spike_dsp_test\n";
    return 0;
}
