#include "dsp/spike_detect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace bf::dsp {
namespace {

// Median (modifies input order to avoid copying; operates on a copy)
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

    // Waveform snippet length (number of samples)
    const std::size_t preSamples = static_cast<std::size_t>(
        std::round(cfg.preMs / 1000.0 * cfg.fs));
    const std::size_t postSamples = static_cast<std::size_t>(
        std::round(cfg.postMs / 1000.0 * cfg.fs));
    const std::size_t snippetLen = preSamples + postSamples;
    result.snippetLen = static_cast<int>(snippetLen);
    if (snippetLen == 0) {
        return result;
    }

    // Refractory period (number of samples)
    const std::size_t refractorySamples = static_cast<std::size_t>(
        std::round(cfg.refractoryMs / 1000.0 * cfg.fs));

    // Threshold
    double threshold = 0.0;
    if (cfg.useAbsoluteUv) {
        threshold = cfg.absoluteThresholdUv;
    } else {
        std::vector<double> copy = signal;
        const double med = median(copy);
        const double m = mad(signal, med);
        // 0.6745 is the quantile of the standard normal distribution, MAD / 0.6745 approximates standard deviation
        const double sigma = m / 0.6745;
        threshold = cfg.thresholdMad * sigma;
    }

    if (threshold <= 0.0) {
        return result;
    }

    // For negative peaks: consider -x crossing the threshold (threshold direction is inverted); for positive peaks: cross the threshold directly.
    // When detectNegative=true, only accept the threshold-crossing interval of x <= -threshold;
    // otherwise, accept the threshold-crossing interval of x >= threshold.
    const bool neg = cfg.detectNegative;

    // Comparison of "crossing the threshold" in the effective detection direction.
    // For negative peaks: original signal x, crossing threshold means x <= -threshold.
    // For positive peaks: x >= threshold.
    auto over = [&](double x) {
        return neg ? (x <= -threshold) : (x >= threshold);
    };

    // Find peak index in [start, end) (min for negative peak, max for positive peak)
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

    // Scan continuous threshold-crossing intervals
    std::size_t i = 0;
    while (i < n) {
        if (!over(signal[i])) {
            ++i;
            continue;
        }
        // Enter threshold-crossing interval [i, j)
        std::size_t j = i;
        while (j < n && over(signal[j])) {
            ++j;
        }
        // Find aligned peak within [i, j)
        std::size_t peak = findPeak(i, j);

        // Apply refractory period
        bool accept = true;
        if (haveLast) {
            if (peak < lastAcceptedIdx ||
                peak - lastAcceptedIdx < refractorySamples) {
                accept = false;
            }
        }

        if (accept) {
            // Boundary check
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
