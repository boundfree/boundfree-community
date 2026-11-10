#include "dsp/spectral.h"

#include "pocketfft_hdronly.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace bf::dsp {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDbFloor = 1e-12;

void validateSignal(const std::vector<double>& signal, double fs) {
    if (signal.empty()) {
        throw std::invalid_argument("analysis: empty signal");
    }
    if (!(fs > 0.0)) {
        throw std::invalid_argument("analysis: sampling rate must be positive");
    }
}

std::vector<double> symmetricHann(int n) {
    if (n <= 0) {
        throw std::invalid_argument("analysis: window length must be positive");
    }
    std::vector<double> window(static_cast<std::size_t>(n), 1.0);
    if (n == 1) {
        return window;
    }
    for (int i = 0; i < n; ++i) {
        window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
    }
    return window;
}

std::vector<double> periodicHann(int n) {
    if (n <= 0) {
        throw std::invalid_argument("analysis: window length must be positive");
    }
    std::vector<double> window(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
    }
    return window;
}

std::vector<std::complex<double>> rfft(const std::vector<double>& values) {
    const std::size_t n = values.size();
    std::vector<std::complex<double>> out(n / 2 + 1);
    pocketfft::shape_t shape{n};
    pocketfft::stride_t inStride{static_cast<std::ptrdiff_t>(sizeof(double))};
    pocketfft::stride_t outStride{static_cast<std::ptrdiff_t>(sizeof(std::complex<double>))};
    pocketfft::r2c(shape, inStride, outStride, 0, true, values.data(), out.data(), 1.0);
    return out;
}

std::vector<double> oneSidedDensity(const std::vector<double>& signal, const std::vector<double>& window, double fs) {
    const std::size_t n = signal.size();
    std::vector<double> windowed(n);
    double mean = std::accumulate(signal.begin(), signal.end(), 0.0) / static_cast<double>(n);
    double windowEnergy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        windowed[i] = (signal[i] - mean) * window[i];
        windowEnergy += window[i] * window[i];
    }
    const auto spectrum = rfft(windowed);
    std::vector<double> density(spectrum.size());
    const double scale = 1.0 / (fs * windowEnergy);
    for (std::size_t i = 0; i < spectrum.size(); ++i) {
        density[i] = std::norm(spectrum[i]) * scale;
    }
    if (n > 1) {
        const std::size_t lastInterior = (n % 2 == 0) ? spectrum.size() - 1 : spectrum.size();
        for (std::size_t i = 1; i < lastInterior; ++i) {
            density[i] *= 2.0;
        }
    }
    return density;
}

std::vector<double> frequencies(int n, double fs) {
    std::vector<double> out(static_cast<std::size_t>(n / 2 + 1));
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<double>(i) * fs / n;
    }
    return out;
}

int normalizedSegmentLength(std::size_t signalLength, double fs, int requested) {
    if (signalLength < 2) {
        throw std::invalid_argument("analysis: signal needs at least two samples");
    }
    int n = requested;
    if (n <= 0) {
        n = std::max(256, static_cast<int>(fs * 2.0));
    }
    return std::min(n, static_cast<int>(signalLength));
}

}  // namespace

const std::array<Band, 5>& eegBands() {
    static const std::array<Band, 5> bands = {{
        {"delta", 0.5, 4.0, {100, 100, 255, 60}},
        {"theta", 4.0, 8.0, {100, 200, 100, 60}},
        {"alpha", 8.0, 13.0, {255, 200, 50, 60}},
        {"beta", 13.0, 30.0, {255, 120, 50, 60}},
        {"gamma", 30.0, 100.0, {200, 50, 200, 60}},
    }};
    return bands;
}

Spectrum computeFftAmplitude(const std::vector<double>& signal, double fs) {
    validateSignal(signal, fs);
    const std::vector<double> window = symmetricHann(static_cast<int>(signal.size()));
    std::vector<double> windowed(signal.size());
    const double windowSum = std::accumulate(window.begin(), window.end(), 0.0);
    for (std::size_t i = 0; i < signal.size(); ++i) {
        windowed[i] = signal[i] * window[i];
    }
    const auto transformed = rfft(windowed);
    Spectrum out;
    out.frequencies = frequencies(static_cast<int>(signal.size()), fs);
    out.values.resize(transformed.size());
    for (std::size_t i = 0; i < transformed.size(); ++i) {
        out.values[i] = std::abs(transformed[i]) * 2.0 / windowSum;
    }
    return out;
}

Spectrum computeWelchPsd(const std::vector<double>& signal, double fs, int nperseg, bool asDb) {
    validateSignal(signal, fs);
    const int n = normalizedSegmentLength(signal.size(), fs, nperseg);
    const int step = std::max(1, n / 2);
    const std::vector<double> window = periodicHann(n);
    std::vector<double> averaged(static_cast<std::size_t>(n / 2 + 1), 0.0);
    int segments = 0;
    for (int start = 0; start + n <= static_cast<int>(signal.size()); start += step) {
        const std::vector<double> part(signal.begin() + start, signal.begin() + start + n);
        const std::vector<double> density = oneSidedDensity(part, window, fs);
        for (std::size_t i = 0; i < density.size(); ++i) {
            averaged[i] += density[i];
        }
        ++segments;
    }
    if (segments == 0) {
        throw std::invalid_argument("analysis: no Welch segments");
    }
    Spectrum out;
    out.frequencies = frequencies(n, fs);
    out.values = std::move(averaged);
    for (double& value : out.values) {
        value /= segments;
        if (asDb) {
            value = 10.0 * std::log10(std::max(value, kDbFloor));
        }
    }
    return out;
}

Spectrum longestFiniteSegmentPsd(const std::vector<double>& trace, double fs, double fmax) {
    auto begin = trace.begin();
    auto end = trace.begin();
    for (auto it = trace.begin(); it != trace.end();) {
        if (!std::isfinite(*it)) {
            ++it;
            continue;
        }
        const auto first = it;
        while (it != trace.end() && std::isfinite(*it)) {
            ++it;
        }
        // `>=` keeps the later run when both runs are equally long.
        if (it - first >= end - begin) {
            begin = first;
            end = it;
        }
    }
    if (end - begin < 4) {
        return Spectrum{};
    }
    Spectrum psd = computeWelchPsd(std::vector<double>(begin, end), fs, 0, false);
    const double boundedFmax = std::min(std::max(0.0, fmax), fs / 2.0);
    const auto limit = std::upper_bound(psd.frequencies.begin(), psd.frequencies.end(), boundedFmax);
    const auto keep = static_cast<std::size_t>(limit - psd.frequencies.begin());
    psd.frequencies.resize(keep);
    psd.values.resize(keep);
    return psd;
}

Spectrogram computeSpectrogram(
    const std::vector<double>& signal,
    double fs,
    int nperseg,
    int noverlap,
    double tStart,
    double fmax) {
    validateSignal(signal, fs);
    const int n = normalizedSegmentLength(signal.size(), fs, nperseg);
    if (noverlap < 0 || noverlap >= n) {
        throw std::invalid_argument("analysis: invalid STFT overlap");
    }
    const int step = n - noverlap;
    const std::vector<double> window = periodicHann(n);
    const double boundedFmax = std::min(std::max(0.0, fmax), fs / 2.0);
    Spectrogram out;
    out.frequencies = frequencies(n, fs);
    const auto maxIt = std::upper_bound(out.frequencies.begin(), out.frequencies.end(), boundedFmax);
    out.frequencies.erase(maxIt, out.frequencies.end());
    out.values.assign(out.frequencies.size(), {});
    for (int start = 0; start + n <= static_cast<int>(signal.size()); start += step) {
        const std::vector<double> part(signal.begin() + start, signal.begin() + start + n);
        const std::vector<double> density = oneSidedDensity(part, window, fs);
        for (std::size_t i = 0; i < out.values.size(); ++i) {
            out.values[i].push_back(10.0 * std::log10(std::max(density[i], kDbFloor)));
        }
        out.times.push_back(tStart + (static_cast<double>(start) + n / 2.0) / fs);
    }
    return out;
}

Spectrum computeCoherence(
    const std::vector<double>& a,
    const std::vector<double>& b,
    double fs,
    int nperseg,
    double fmax) {
    validateSignal(a, fs);
    validateSignal(b, fs);
    if (a.size() != b.size()) {
        throw std::invalid_argument("analysis: coherence signals have different lengths");
    }
    const int n = normalizedSegmentLength(a.size(), fs, nperseg);
    const int step = std::max(1, n / 2);
    const std::vector<double> window = periodicHann(n);
    const std::size_t bins = static_cast<std::size_t>(n / 2 + 1);
    std::vector<double> pxx(bins, 0.0);
    std::vector<double> pyy(bins, 0.0);
    std::vector<std::complex<double>> pxy(bins, {0.0, 0.0});
    int segments = 0;
    for (int start = 0; start + n <= static_cast<int>(a.size()); start += step) {
        std::vector<double> partA(a.begin() + start, a.begin() + start + n);
        std::vector<double> partB(b.begin() + start, b.begin() + start + n);
        const double meanA = std::accumulate(partA.begin(), partA.end(), 0.0) / n;
        const double meanB = std::accumulate(partB.begin(), partB.end(), 0.0) / n;
        double windowEnergy = 0.0;
        for (int i = 0; i < n; ++i) {
            partA[static_cast<std::size_t>(i)] = (partA[static_cast<std::size_t>(i)] - meanA) * window[static_cast<std::size_t>(i)];
            partB[static_cast<std::size_t>(i)] = (partB[static_cast<std::size_t>(i)] - meanB) * window[static_cast<std::size_t>(i)];
            windowEnergy += window[static_cast<std::size_t>(i)] * window[static_cast<std::size_t>(i)];
        }
        const auto fftA = rfft(partA);
        const auto fftB = rfft(partB);
        const double scale = 1.0 / (fs * windowEnergy);
        for (std::size_t i = 0; i < bins; ++i) {
            double sideFactor = 1.0;
            if (i > 0 && !(n % 2 == 0 && i == bins - 1)) {
                sideFactor = 2.0;
            }
            pxx[i] += std::norm(fftA[i]) * scale * sideFactor;
            pyy[i] += std::norm(fftB[i]) * scale * sideFactor;
            pxy[i] += fftA[i] * std::conj(fftB[i]) * (scale * sideFactor);
        }
        ++segments;
    }
    Spectrum out;
    const std::vector<double> allFrequencies = frequencies(n, fs);
    const double boundedFmax = std::min(std::max(0.0, fmax), fs / 2.0);
    for (std::size_t i = 0; i < bins && allFrequencies[i] <= boundedFmax; ++i) {
        const double denom = pxx[i] * pyy[i];
        out.frequencies.push_back(allFrequencies[i]);
        out.values.push_back(denom > 0.0 ? std::min(1.0, std::norm(pxy[i]) / denom) : 0.0);
    }
    return out;
}

double bandPower(const Spectrum& psd, double low, double high) {
    if (psd.frequencies.size() < 2 || psd.frequencies.size() != psd.values.size()) {
        return 0.0;
    }
    const double df = psd.frequencies[1] - psd.frequencies[0];
    double power = 0.0;
    for (std::size_t i = 0; i < psd.frequencies.size(); ++i) {
        if (psd.frequencies[i] >= low && psd.frequencies[i] <= high) {
            power += psd.values[i] * df;
        }
    }
    return power;
}

}  // namespace bf::dsp
