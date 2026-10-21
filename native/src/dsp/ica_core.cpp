// BoundFree-authored ICA algorithm migrated from Pro eegicacore.cpp.
// No RHX reader, Qt, licensing code or third-party implementation is included.
#include "ica_core.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace bf::separation {
const char* failureCode(Failure reason) noexcept
{
    switch (reason) {
    case Failure::InvalidInput: return "analysis.invalid_input";
    case Failure::InvalidRange: return "analysis.invalid_range";
    case Failure::MissingContext: return "analysis.missing_context";
    case Failure::InputTooLarge: return "analysis.input_too_large";
    case Failure::FlatChannel: return "analysis.flat_channel";
    case Failure::LowRank: return "analysis.low_rank";
    case Failure::NotConverged: return "analysis.not_converged";
    case Failure::NonFinite: return "analysis.non_finite";
    case Failure::Reconstruction: return "analysis.reconstruction_failed";
    case Failure::Cancelled: return "analysis.cancelled";
    }
    return "analysis.invalid_input";
}
namespace ica {
namespace {
constexpr double pi = 3.14159265358979323846;
Matrix zeros(int rows, int cols) { return Matrix(rows, std::vector<double>(cols)); }
void check(const Cancel& cancel) { if (cancel && cancel()) throw Error(Failure::Cancelled); }

// Jacobi rotations for small symmetric matrices (at most 32 channels).
void eigen(Matrix a, Matrix& vectors, std::vector<double>& values, const Cancel& cancel)
{
    const int n = int(a.size());
    vectors = zeros(n, n);
    for (int i = 0; i < n; ++i) vectors[i][i] = 1;
    for (int step = 0; step < 100 * n * n; ++step) {
        if (step % 32 == 0) check(cancel);
        int p = 0, q = 0;
        double largest = 0, scale = 0;
        for (int i = 0; i < n; ++i) {
            scale = std::max(scale, std::abs(a[i][i]));
            for (int j = i + 1; j < n; ++j) {
                if (std::abs(a[i][j]) > largest) { largest = std::abs(a[i][j]); p = i; q = j; }
            }
        }
        if (largest <= std::max(1e-30, scale * 1e-13)) break;
        const double angle = 0.5 * std::atan2(2 * a[p][q], a[q][q] - a[p][p]);
        const double c = std::cos(angle), s = std::sin(angle);
        const double app = a[p][p], aqq = a[q][q], apq = a[p][q];
        for (int k = 0; k < n; ++k) {
            if (k != p && k != q) {
                const double akp = a[k][p], akq = a[k][q];
                a[k][p] = a[p][k] = c * akp - s * akq;
                a[k][q] = a[q][k] = s * akp + c * akq;
            }
            const double vkp = vectors[k][p], vkq = vectors[k][q];
            vectors[k][p] = c * vkp - s * vkq;
            vectors[k][q] = s * vkp + c * vkq;
        }
        a[p][p] = c*c*app - 2*s*c*apq + s*s*aqq;
        a[q][q] = s*s*app + 2*s*c*apq + c*c*aqq;
        a[p][q] = a[q][p] = 0;
    }
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a_, int b) { return a[a_][a_] > a[b][b]; });
    Matrix sorted = vectors;
    values.resize(n);
    for (int j = 0; j < n; ++j) {
        values[j] = a[order[j]][order[j]];
        for (int i = 0; i < n; ++i) sorted[i][j] = vectors[i][order[j]];
    }
    vectors.swap(sorted);
}

Matrix decorrelate(const Matrix& w, const Cancel& cancel)
{
    const int n = int(w.size());
    Matrix gram = zeros(n, n), u;
    std::vector<double> d;
    for (int i = 0; i < n; ++i) for (int j = i; j < n; ++j) {
        gram[i][j] = gram[j][i] = std::inner_product(w[i].begin(), w[i].end(), w[j].begin(), 0.0);
    }
    eigen(gram, u, d, cancel);
    Matrix out = zeros(n, n);
    for (int k = 0; k < n; ++k) {
        if (d[k] < 1e-20 || !std::isfinite(d[k])) throw Error(Failure::NotConverged);
        for (int j = 0; j < n; ++j) {
            double tmp = 0;
            for (int l = 0; l < n; ++l) tmp += u[l][k] * w[l][j];
            for (int i = 0; i < n; ++i) out[i][j] += u[i][k] * tmp / std::sqrt(d[k]);
        }
    }
    return out;
}

std::vector<double> zeroPhase(const std::vector<double>& x, double fs, double frequency, bool notch)
{
    const double omega = 2*pi*frequency/fs, cosine = std::cos(omega);
    const double alpha = std::sin(omega) / (2 * (notch ? 30.0 : std::sqrt(0.5)));
    const double a0 = 1 + alpha, a1 = -2*cosine/a0, a2 = (1-alpha)/a0;
    const double b0 = (notch ? 1 : (1+cosine)/2)/a0;
    const double b1 = (notch ? -2*cosine : -(1+cosine))/a0, b2 = b0;
    const size_t pad = std::min(x.size()-1, size_t(std::ceil(3*fs)));
    std::vector<double> y(x.size()+2*pad);
    for (size_t i = 0; i < pad; ++i) y[i] = 2*x.front()-x[pad-i];
    std::copy(x.begin(), x.end(), y.begin()+pad);
    for (size_t i = 0; i < pad; ++i) y[pad+x.size()+i] = 2*x.back()-x[x.size()-2-i];
    for (int pass = 0; pass < 2; ++pass) {
        double x1 = y.front(), x2 = x1, y1 = notch ? x1 : 0, y2 = y1;
        for (double& v : y) {
            const double out = b0*v+b1*x1+b2*x2-a1*y1-a2*y2;
            x2 = x1; x1 = v; y2 = y1; y1 = out; v = out;
        }
        std::reverse(y.begin(), y.end());
    }
    return std::vector<double>(y.begin()+pad, y.begin()+pad+x.size());
}
}

std::vector<double> highpass(const std::vector<double>& x, double fs, double cutoff)
{
    if (x.size() < 2 || !std::isfinite(fs) || !std::isfinite(cutoff) || fs <= 2*cutoff || cutoff <= 0) throw Error(Failure::InvalidInput);
    for (double value : x) if (!std::isfinite(value)) throw Error(Failure::NonFinite);
    return zeroPhase(x, fs, cutoff, false);
}

Result fit(const Matrix& input, const Options& options, Cancel cancel, Progress progress)
{
    check(cancel);
    const int channels = int(input.size());
    if (channels < 2 || channels > 32 || input[0].size() < 5*options.sampleRate ||
        input[0].size() > 60000 || input[0].size() > 120*options.sampleRate || !std::isfinite(options.sampleRate) || options.sampleRate < 100 ||
        options.sampleRate > 500 || (options.notchHz != 0 && options.notchHz != 50 && options.notchHz != 60) ||
        options.notchHz >= options.sampleRate/2 || options.maxIterations < 1 ||
        !std::isfinite(options.tolerance) || options.tolerance <= 0) throw Error(Failure::InvalidInput);
    const int samples = int(input[0].size());
    Result result;
    result.analysis = input;
    Matrix fitData;
    // Distribution subsampling, after temporal filtering: not a resampled time series.
    const int stride = std::max(1, (samples+11999)/12000);
    const int fitSamples = (samples+stride-1)/stride;
    if (fitSamples < channels*20) throw Error(Failure::InvalidInput);
    for (int c = 0; c < channels; ++c) {
        check(cancel);
        if (int(input[c].size()) != samples) throw Error(Failure::InvalidInput);
        for (double v : input[c]) if (!std::isfinite(v)) throw Error(Failure::NonFinite);
        auto [lo, hi] = std::minmax_element(input[c].begin(), input[c].end());
        if (*hi-*lo < 0.1) throw Error(Failure::FlatChannel);
        if (options.notchHz) result.analysis[c] = zeroPhase(input[c], options.sampleRate, options.notchHz, true);
        auto filtered = highpass(result.analysis[c], options.sampleRate, 1.0);
        std::vector<double> row;
        for (int i = 0; i < samples; i += stride) row.push_back(filtered[i]);
        const double mean = std::accumulate(row.begin(), row.end(), 0.0)/fitSamples;
        for (double& v : row) v -= mean;
        fitData.push_back(std::move(row));
    }
    Matrix cov = zeros(channels, channels), eigenvectors;
    std::vector<double> eigenvalues;
    for (int i = 0; i < channels; ++i) for (int j = i; j < channels; ++j) {
        cov[i][j] = cov[j][i] = std::inner_product(fitData[i].begin(), fitData[i].end(), fitData[j].begin(), 0.0)/fitSamples;
    }
    for (const auto& row : cov) for (double value : row)
        if (!std::isfinite(value)) throw Error(Failure::NonFinite);
    check(cancel);
    eigen(cov, eigenvectors, eigenvalues, cancel);
    const double threshold = std::max(1e-12, eigenvalues[0]*1e-8);
    for (double v : eigenvalues) if (v > threshold) ++result.rank;
    const int rank = result.rank;
    if (rank < 2) throw Error(Failure::LowRank);
    Matrix whitened = zeros(rank, fitSamples), whitening = zeros(rank, channels);
    for (int k = 0; k < rank; ++k) for (int c = 0; c < channels; ++c) {
        whitening[k][c] = eigenvectors[c][k]/std::sqrt(eigenvalues[k]);
        for (int t = 0; t < fitSamples; ++t) whitened[k][t] += whitening[k][c]*fitData[c][t];
    }
    fitData.clear();
    std::mt19937 rng(options.seed);
    // Uniform integer-to-double conversion fixes initialization across standard libraries.
    Matrix w = zeros(rank, rank);
    for (auto& row : w) for (double& v : row) v = double(rng())/4294967295.0-0.5;
    w = decorrelate(w, cancel);
    for (int iteration = 1; iteration <= options.maxIterations; ++iteration) {
        check(cancel);
        Matrix next = zeros(rank, rank);
        std::vector<double> nonlinear(fitSamples);
        for (int i = 0; i < rank; ++i) {
            check(cancel);
            std::fill(nonlinear.begin(), nonlinear.end(), 0);
            for (int k = 0; k < rank; ++k) for (int t = 0; t < fitSamples; ++t) nonlinear[t] += w[i][k]*whitened[k][t];
            double derivative = 0;
            for (double& v : nonlinear) { v = std::tanh(v); derivative += 1-v*v; }
            for (int k = 0; k < rank; ++k) {
                next[i][k] = (std::inner_product(whitened[k].begin(), whitened[k].end(), nonlinear.begin(), 0.0)-derivative*w[i][k])/fitSamples;
            }
        }
        next = decorrelate(next, cancel);
        double delta = 0;
        for (int i = 0; i < rank; ++i) delta = std::max(delta, std::abs(std::abs(std::inner_product(w[i].begin(), w[i].end(), next[i].begin(), 0.0))-1));
        w.swap(next);
        result.iterations = iteration;
        result.convergence = delta;
        if (progress) progress(40+int(45.0*iteration/options.maxIterations));
        if (delta < options.tolerance) break;
        if (iteration == options.maxIterations) throw Error(Failure::NotConverged);
    }
    result.mixing = zeros(channels, rank);
    result.unmixing = zeros(rank, channels);
    result.sources = zeros(rank, samples);
    result.mean.resize(channels);
    result.residual = result.analysis;
    for (int c = 0; c < channels; ++c) {
        check(cancel);
        result.mean[c] = std::accumulate(result.analysis[c].begin(), result.analysis[c].end(), 0.0)/samples;
        for (int i = 0; i < rank; ++i) for (int k = 0; k < rank; ++k) {
            result.mixing[c][i] += eigenvectors[c][k]*std::sqrt(eigenvalues[k])*w[i][k];
            result.unmixing[i][c] += w[i][k]*whitening[k][c];
        }
        for (int i = 0; i < rank; ++i) for (int t = 0; t < samples; ++t) {
            result.sources[i][t] += result.unmixing[i][c]*(result.analysis[c][t]-result.mean[c]);
        }
    }
    for (int c = 0; c < channels; ++c) {
        check(cancel);
        for (int t = 0; t < samples; ++t) result.residual[c][t] -= result.mean[c];
        for (int i = 0; i < rank; ++i) for (int t = 0; t < samples; ++t) result.residual[c][t] -= result.mixing[c][i]*result.sources[i][t];
    }
    for (const auto* matrix : {&result.analysis, &result.sources, &result.mixing, &result.unmixing, &result.residual})
        for (const auto& row : *matrix) for (double value : row)
            if (!std::isfinite(value)) throw Error(Failure::NonFinite);
    for (double value : result.mean) if (!std::isfinite(value)) throw Error(Failure::NonFinite);
    check(cancel);
    return result;
}

Matrix project(const Result& result, const std::vector<int>& groups, int group, Cancel cancel)
{
    check(cancel);
    const auto channels = result.analysis.size();
    if (result.rank < 2 || result.rank > 32 || int(groups.size()) != result.rank ||
        group < 0 || group > 3 || channels < 2 || channels > 32 ||
        result.sources.size() != size_t(result.rank) || result.mixing.size() != channels ||
        result.analysis[0].empty() || result.analysis[0].size() > 60000) throw Error(Failure::InvalidInput);
    const auto samples = result.analysis[0].size();
    for (const auto& row : result.analysis) if (row.size() != samples) throw Error(Failure::InvalidInput);
    for (const auto& row : result.sources) if (row.size() != samples) throw Error(Failure::InvalidInput);
    for (const auto& row : result.mixing) if (row.size() != size_t(result.rank)) throw Error(Failure::InvalidInput);
    Matrix out = zeros(int(channels), int(samples));
    for (int i = 0; i < result.rank; ++i) {
        if (groups[i] < 0 || groups[i] > 3) throw Error(Failure::InvalidInput);
        if (groups[i] != group) continue;
        for (size_t c = 0; c < channels; ++c) {
            check(cancel);
            for (size_t t = 0; t < samples; ++t) {
                out[c][t] += result.mixing[c][i]*result.sources[i][t];
                if (!std::isfinite(out[c][t])) throw Error(Failure::NonFinite);
            }
        }
    }
    return out;
}

double correlation(const std::vector<double>& x, const std::vector<double>& y)
{
    if (x.size() != y.size() || x.empty()) return 0;
    const double mx = std::accumulate(x.begin(), x.end(), 0.0)/x.size();
    const double my = std::accumulate(y.begin(), y.end(), 0.0)/y.size();
    double cross = 0, vx = 0, vy = 0;
    for (size_t i = 0; i < x.size(); ++i) { cross += (x[i]-mx)*(y[i]-my); vx += (x[i]-mx)*(x[i]-mx); vy += (y[i]-my)*(y[i]-my); }
    return vx > 0 && vy > 0 ? cross/std::sqrt(vx*vy) : 0;
}
}

} // namespace bf::separation
