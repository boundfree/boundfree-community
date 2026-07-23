#pragma once

#include <vector>

namespace bf::dsp {

// Spike sorting configuration (PCA dimension reduction + K-Means clustering)
struct SpikeSortConfig {
    int nComponents = 3;    // Number of PCA components
    int nClusters = 3;     // Number of K-Means clusters
    int maxIters = 100;
    unsigned seed = 42;    // Deterministic random seed
};

struct SpikeSortResult {
    std::vector<int> labels;                     // Cluster ID for each spike [0, nClusters)
    std::vector<std::vector<double>> pcaCoords;  // [n_spikes][nComponents]
};

// waveforms: [n_spikes][snippet_len]
// Steps: 1) Mean centering (subtract mean from each column); 2) PCA (Jacobi eigenvalue decomposition of covariance matrix);
// 3) Project to the first nComponents to get pcaCoords; 4) K-Means (fixed seed, deterministic).
// If n_spikes < nClusters, each spike forms its own cluster, labels = 0..n-1.
SpikeSortResult sortSpikes(const std::vector<std::vector<double>>& waveforms,
                           const SpikeSortConfig& cfg);

}  // namespace bf::dsp
