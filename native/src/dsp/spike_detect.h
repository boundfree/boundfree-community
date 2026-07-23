#pragma once

#include <vector>

namespace bf::dsp {

// Spike 阈值检测与波形提取配置
struct SpikeDetectConfig {
    double fs = 30000.0;            // 采样率 Hz（禁止硬编码，由调用方传入）
    double thresholdMad = 4.0;     // N×MAD 阈值倍数（useAbsoluteUv=false 时生效）
    bool useAbsoluteUv = false;     // true 则使用 absoluteThresholdUv 作为阈值
    double absoluteThresholdUv = 50.0; // 绝对阈值（uV）
    bool detectNegative = true;     // true：负向过阈找负峰；false：正向过阈找正峰
    double refractoryMs = 1.0;      // 不应期（死区），毫秒
    double preMs = 0.5;             // 波形快照对齐前窗，毫秒
    double postMs = 1.0;            // 波形快照对齐后窗，毫秒
};

// 单通道检测结果
struct DetectedSpikes {
    std::vector<double> timestamps;              // 秒（对齐峰时刻）
    std::vector<std::vector<double>> waveforms;  // [n_spikes][snippet_len]
    int snippetLen = 0;                          // = round((preMs+postMs)/1000*fs)
};

// 单通道阈值检测。阈值：MAD = median(|x - median(x)|)，
// threshold = thresholdMad * (MAD/0.6745)（近似高斯 std）。
// 过阈后在局部窗口找极值点作为对齐峰，应用不应期，
// 截取 [peak - preSamples, peak + postSamples) 快照。边界不足的 spike 丢弃。
DetectedSpikes detectSpikes(const std::vector<double>& signal,
                            const SpikeDetectConfig& cfg);

}  // namespace bf::dsp
