#pragma once

#include <vector>

namespace bf::dsp {

// Spike 排序配置（PCA 降维 + K-Means 聚类）
struct SpikeSortConfig {
    int nComponents = 3;    // PCA 主成分数
    int nClusters = 3;     // K-Means 簇数
    int maxIters = 100;
    unsigned seed = 42;    // 确定性随机种子
};

struct SpikeSortResult {
    std::vector<int> labels;                     // 每个 spike 的簇号 [0, nClusters)
    std::vector<std::vector<double>> pcaCoords;  // [n_spikes][nComponents]
};

// waveforms: [n_spikes][snippet_len]
// 步骤：1) 中心化（每列减均值）；2) PCA（协方差矩阵 Jacobi 特征分解）；
// 3) 投影到前 nComponents 得 pcaCoords；4) K-Means（固定 seed，确定性）。
// n_spikes < nClusters 时每个 spike 单独成簇，labels = 0..n-1。
SpikeSortResult sortSpikes(const std::vector<std::vector<double>>& waveforms,
                           const SpikeSortConfig& cfg);

}  // namespace bf::dsp
