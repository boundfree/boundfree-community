#pragma once

#include <vector>

namespace bf::dsp {

// 共享 Spike 数据模型。EEG 线用 [n_channels, n_samples] + Spectrum/Spectrogram；
// Spike 线用「按簇组织的放电序列 + 波形快照」作为分析层与渲染层之间的统一载体。
// 所有与采样率相关的量（snippetLen、时间戳）均由检测阶段按 fs 推导，禁止硬编码。

// 单个簇（putative single unit）的放电序列与波形。
struct SpikeCluster {
    int id = 0;                                  // 簇号 [0, nClusters)
    std::vector<double> timestamps;              // 该簇所有放电时刻，单位秒，升序
    std::vector<std::vector<double>> waveforms;  // [n_spikes][snippetLen] 对齐波形快照
    std::vector<double> meanWaveform;            // [snippetLen] 平均波形（模板）
};

// 一个通道（或一次检测+排序）的完整 Spike 分析结果。
// 这是 spike_analysis_service 产出、所有 Spike 视图消费的统一模型。
struct SpikeAnalysis {
    double fs = 0.0;               // 采样率 Hz
    double duration = 0.0;         // 记录时长秒（用于 raster/PSTH 时间轴）
    int snippetLen = 0;            // 波形快照长度（样本）
    double preMs = 0.5;            // 快照对齐前窗（用于波形视图时间轴）
    double postMs = 1.0;           // 快照对齐后窗
    int channel = 0;               // 源通道
    int totalSpikes = 0;           // 所有簇放电总数
    std::vector<SpikeCluster> clusters;
};

}  // namespace bf::dsp
