#include "dsp/spike_detect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace bf::dsp {
namespace {

// 中位数（修改输入顺序以避免拷贝；对副本操作）
double median(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    const std::size_t n = v.size();
    auto mid = v.begin() + n / 2;
    std::nth_element(v.begin(), mid, v.end());
    const double m = v[n / 2];
    if (n % 2 == 0) {
        auto prev = std::max_element(v.begin(), mid);
        return (*prev + m) * 0.5;
    }
    return m;
}

// MAD = median(|x - median(x)|)
double mad(const std::vector<double>& signal, double med) {
    std::vector<double> devs;
    devs.reserve(signal.size());
    for (double x : signal) {
        devs.push_back(std::fabs(x - med));
    }
    return median(devs);
}

}  // namespace

DetectedSpikes detectSpikes(const std::vector<double>& signal,
                            const SpikeDetectConfig& cfg) {
    DetectedSpikes result;
    const std::size_t n = signal.size();
    if (n == 0 || cfg.fs <= 0.0) {
        return result;
    }

    // 波形快照长度（样本数）
    const std::size_t preSamples = static_cast<std::size_t>(
        std::round(cfg.preMs / 1000.0 * cfg.fs));
    const std::size_t postSamples = static_cast<std::size_t>(
        std::round(cfg.postMs / 1000.0 * cfg.fs));
    const std::size_t snippetLen = preSamples + postSamples;
    result.snippetLen = static_cast<int>(snippetLen);
    if (snippetLen == 0) {
        return result;
    }

    // 不应期（样本数）
    const std::size_t refractorySamples = static_cast<std::size_t>(
        std::round(cfg.refractoryMs / 1000.0 * cfg.fs));

    // 阈值
    double threshold = 0.0;
    if (cfg.useAbsoluteUv) {
        threshold = cfg.absoluteThresholdUv;
    } else {
        std::vector<double> copy = signal;
        const double med = median(copy);
        const double m = mad(signal, med);
        // 0.6745 为标准正态分布分位数，MAD/0.6745 近似标准差
        const double sigma = m / 0.6745;
        threshold = cfg.thresholdMad * sigma;
    }

    if (threshold <= 0.0) {
        return result;
    }

    // 对负峰：考虑 -x 过阈（阈值方向反转）；对正峰：直接过阈
    // 用 detectNegative=true 时，只接受 x <= -threshold 的过阈区间；
    // 反之接受 x >= threshold 的过阈区间。
    const bool neg = cfg.detectNegative;

    // 有效检测方向上的"越过阈值"比较
    // 对负峰：原始信号 x，过阈指 x < -threshold
    // 对正峰：x > threshold
    auto over = [&](double x) {
        return neg ? (x <= -threshold) : (x >= threshold);
    };

    // 在 [start, end) 范围内寻找极值索引（负峰找 min，正峰找 max）
    auto findPeak = [&](std::size_t start, std::size_t end) -> std::size_t {
        std::size_t pk = start;
        double v = signal[start];
        for (std::size_t i = start + 1; i < end; ++i) {
            double x = signal[i];
            if (neg) {
                if (x < v) {
                    v = x;
                    pk = i;
                }
            } else {
                if (x > v) {
                    v = x;
                    pk = i;
                }
            }
        }
        return pk;
    };

    std::size_t lastAcceptedIdx = 0;
    bool haveLast = false;

    // 扫描连续过阈段
    std::size_t i = 0;
    while (i < n) {
        if (!over(signal[i])) {
            ++i;
            continue;
        }
        // 进入过阈段 [i, j)
        std::size_t j = i;
        while (j < n && over(signal[j])) {
            ++j;
        }
        // 在 [i, j) 内找对齐峰
        std::size_t peak = findPeak(i, j);

        // 应用不应期
        bool accept = true;
        if (haveLast) {
            if (peak < lastAcceptedIdx ||
                peak - lastAcceptedIdx < refractorySamples) {
                accept = false;
            }
        }

        if (accept) {
            // 边界检查
            if (peak >= preSamples && peak + postSamples <= n) {
                std::size_t start = peak - preSamples;
                std::vector<double> wf(snippetLen);
                for (std::size_t k = 0; k < snippetLen; ++k) {
                    wf[k] = signal[start + k];
                }
                result.timestamps.push_back(static_cast<double>(peak) / cfg.fs);
                result.waveforms.push_back(std::move(wf));
                lastAcceptedIdx = peak;
                haveLast = true;
            }
        }

        i = j;
    }

    return result;
}

}  // namespace bf::dsp
