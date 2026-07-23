// Spike DSP engine self-test: threshold detection + waveform extraction + PCA/K-Means sorting.
// Style references m4_spectral_test.cpp, custom main, returns non-zero on failure.
//
// Test 1: Synthesize signal containing known spikes (baseline noise + insert negative peak template at known times),
//         verify that the count and locations of detectSpikes timestamps are close to ground truth (tolerance of +/- 2 samples).
// Test 2: Synthesize N waveforms of two distinct shapes, verify that sortSpikes can divide them into 2
//         major clusters, and that the majority of the same class is sorted into the same cluster (cluster purity >= 0.8).

#include "dsp/spike_detect.h"
#include "dsp/spike_sort.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace {

// Simple deterministic PRNG (xorshift64*), used to synthesize reproducible test signals
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
    // Approximate Gaussian (simplified Box-Muller: central limit theorem approximation from sum of 12 uniform variables)
    double gauss() {
        double s = 0.0;
        for (int i = 0; i < 12; ++i) {
            s += unit();
        }
        return s - 6.0;
    }
};

// Negative peak template: aligned at peakIdx, preMs=0.5, postMs=1.0, fs=30000
// Template length = 45 samples, negative peak at preSamples=15
std::vector<double> makeNegativeTemplate(int preSamples, int postSamples) {
    int len = preSamples + postSamples;
    std::vector<double> t(static_cast<std::size_t>(len), 0.0);
    // Shape: falls rapidly to -amplitude (at preSamples), then recovers slowly
    const double amplitude = 100.0;  // uV
    for (int i = 0; i < len; ++i) {
        int rel = i - preSamples;
        if (rel <= 0) {
            // Rising edge (falls from 0 to -amplitude), using a parabola
            double frac = preSamples > 0 ? static_cast<double>(i) / preSamples : 1.0;
            t[static_cast<std::size_t>(i)] = -amplitude * frac;
        } else {
            // Falling edge (recovers from -amplitude to 0), exponential decay
            double decay = std::exp(-rel / 8.0);
            t[static_cast<std::size_t>(i)] = -amplitude * decay;
        }
    }
    return t;
}

bool testDetect() {
    const double fs = 30000.0;
    const int n = static_cast<int>(fs);  // 1 second
    const int preSamples = 15;
    const int postSamples = 30;
    auto tmpl = makeNegativeTemplate(preSamples, postSamples);

    TestRng rng(20240724);
    // Baseline noise: std ~ 5 uV, spike amplitude ~100 uV, far exceeding noise
    std::vector<double> signal(n, 0.0);
    for (int i = 0; i < n; ++i) {
        signal[i] = rng.gauss() * 2.0;
    }

    // Insert spikes at known times (peak aligned to knownPeakIdx)
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
    cfg.refractoryMs = 1.0;  // 30 samples
    cfg.preMs = 0.5;          // 15 samples
    cfg.postMs = 1.0;         // 30 samples

    bf::dsp::DetectedSpikes res = bf::dsp::detectSpikes(signal, cfg);

    if (res.timestamps.size() != knownPeaks.size()) {
        std::cerr << "FAIL detect: expected " << knownPeaks.size()
                  << " spikes, got " << res.timestamps.size() << "\n";
        return false;
    }

    const double tolSec = 2.0 / fs;  // +/- 2 samples
    for (std::size_t i = 0; i < knownPeaks.size(); ++i) {
        double expected = knownPeaks[i] / fs;
        double got = res.timestamps[i];
        if (std::fabs(got - expected) > tolSec) {
            std::cerr << "FAIL detect: spike " << i << " expected ~"
                      << expected << " got " << got << "\n";
            return false;
        }
    }
    // Verify snippet length
    if (res.snippetLen != preSamples + postSamples) {
        std::cerr << "FAIL detect: snippetLen expected "
                  << preSamples + postSamples << " got " << res.snippetLen << "\n";
        return false;
    }
    // Verify the length of each waveform
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
    // Synthesize N waveforms each of two distinct shapes
    const int nPerClass = 40;
    const int nTotal = 2 * nPerClass;
    const int snippetLen = 45;
    TestRng rng(99);

    std::vector<std::vector<double>> waveforms;
    waveforms.reserve(nTotal);
    std::vector<int> trueLabels;
    trueLabels.reserve(nTotal);

    // Class A: negative sharp peak, template same as in detect test (amplitude 80 uV + small noise)
    // Class B: biphasic wave (positive then negative), shape is distinctly different
    for (int cls = 0; cls < 2; ++cls) {
        for (int i = 0; i < nPerClass; ++i) {
            std::vector<double> wf(snippetLen, 0.0);
            int pre = 15;
            for (int k = 0; k < snippetLen; ++k) {
                int rel = k - pre;
                double v = 0.0;
                if (cls == 0) {
                    // Class A: negative sharp
                    const double amp = 80.0;
                    if (rel <= 0) {
                        double frac = pre > 0 ? static_cast<double>(k) / pre : 1.0;
                        v = -amp * frac;
                    } else {
                        v = -amp * std::exp(-rel / 8.0);
                    }
                } else {
                    // Class B: biphasic (positive then negative)
                    const double amp = 70.0;
                    if (rel <= 0) {
                        double frac = pre > 0 ? static_cast<double>(k) / pre : 1.0;
                        v = amp * frac;
                    } else {
                        v = -amp * std::exp(-rel / 10.0);
                    }
                }
                v += rng.gauss() * 3.0;  // Small noise
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

    // Cluster ID range [0, nClusters)
    for (int lbl : res.labels) {
        if (lbl < 0 || lbl >= cfg.nClusters) {
            std::cerr << "FAIL sort: label out of range " << lbl << "\n";
            return false;
        }
    }

    // Purity evaluation: for each cluster, calculate the proportion of the majority ground truth class
    // Since cluster IDs and ground truth class IDs may be flipped, take the mapped purity
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
