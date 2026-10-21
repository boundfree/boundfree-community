// BoundFree-authored preprocessing/scoring migrated from Pro eeganalysis.cpp.
// Pure numerical operations only; all recording-format I/O belongs to the sidecar.
#include "eeg_separation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>

namespace bf::separation {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::uint64_t maxInputBytes = 256ULL * 1024 * 1024;
void check(const Cancel& cancel) { if (cancel && cancel()) throw Error(Failure::Cancelled); }
bool observed(Role role) {
    return role == Role::Observation || role == Role::EogObservation || role == Role::EmgObservation;
}
bool selected(Role role) { return role != Role::Unconfirmed && role != Role::Excluded; }
void validateResultMatrix(const Matrix& matrix, std::size_t channels, std::size_t samples) {
    if (matrix.size() != channels) throw Error(Failure::InvalidInput);
    for (const auto& row : matrix) {
        if (row.size() != samples) throw Error(Failure::InvalidInput);
        for (double value : row) if (!std::isfinite(value)) throw Error(Failure::NonFinite);
    }
}
} // namespace

int resamplingHalfWidth(double sourceRate, double outputRate)
{
    if (!std::isfinite(sourceRate) || !std::isfinite(outputRate) ||
        (outputRate != 250 && outputRate != 500) || sourceRate < outputRate)
        throw Error(Failure::InvalidInput);
    if (sourceRate == outputRate) return 0;
    const double half = std::ceil(12 * sourceRate / outputRate);
    if (!std::isfinite(half) || half > std::numeric_limits<int>::max() / 2)
        throw Error(Failure::InputTooLarge);
    return int(half);
}

Result analyze(const Request& request, Cancel cancel, Progress progress)
{
    check(cancel);
    const int half = resamplingHalfWidth(request.sampleRate, request.outputRate);
    if ((request.notchHz != 0 && request.notchHz != 50 && request.notchHz != 60) ||
        request.maxIterations < 1 || !std::isfinite(request.tolerance) || request.tolerance <= 0)
        throw Error(Failure::InvalidInput);
    if (request.samples.empty() || request.roles.size() != request.samples.size() ||
        (!request.channelIds.empty() && request.channelIds.size() != request.samples.size()) ||
        request.samples.size() > 40 || request.sampleCount == 0 || request.totalSamples == 0)
        throw Error(Failure::InvalidInput);
    const auto readCount = request.samples[0].size();
    if (readCount == 0 || readCount > maxInputBytes / sizeof(double) / request.samples.size())
        throw Error(Failure::InputTooLarge);
    if (request.firstSample > request.totalSamples || request.sampleCount > request.totalSamples - request.firstSample ||
        request.readFirstSample > request.totalSamples || readCount > request.totalSamples - request.readFirstSample)
        throw Error(Failure::InvalidRange);
    const double duration = double(request.sampleCount) / request.sampleRate;
    if (!std::isfinite(duration) || duration < 5 || duration > 120) throw Error(Failure::InvalidRange);
    const auto requestedEnd = request.firstSample + request.sampleCount;
    const auto requiredFirst = request.firstSample > std::uint64_t(half) ? request.firstSample - half : 0;
    const auto requiredEnd = requestedEnd + std::min(std::uint64_t(half), request.totalSamples - requestedEnd);
    if (request.readFirstSample > requiredFirst || request.readFirstSample + readCount < requiredEnd)
        throw Error(Failure::MissingContext);
    const auto outputCount = std::uint64_t(std::floor((request.sampleCount - 1) * request.outputRate / request.sampleRate)) + 1;
    if (outputCount > 60000 || outputCount < 5 * request.outputRate) throw Error(Failure::InvalidRange);
    std::set<std::string> uniqueIds;
    std::vector<std::size_t> selectedRows, observationRows;
    for (std::size_t c = 0; c < request.samples.size(); ++c) {
        if (request.samples[c].size() != readCount) throw Error(Failure::InvalidInput);
        const Role role = request.roles[c];
        if (int(role) < int(Role::Unconfirmed) || int(role) > int(Role::Excluded) || role == Role::Unconfirmed)
            throw Error(Failure::InvalidInput);
        if (!request.channelIds.empty() &&
            (request.channelIds[c].empty() || !uniqueIds.insert(request.channelIds[c]).second))
            throw Error(Failure::InvalidInput);
        if (!selected(role)) continue;
        check(cancel);
        for (double value : request.samples[c]) if (!std::isfinite(value)) throw Error(Failure::NonFinite);
        if (observed(role)) observationRows.push_back(selectedRows.size());
        selectedRows.push_back(c);
    }
    if (observationRows.size() < 2 || observationRows.size() > 32) throw Error(Failure::InvalidInput);

    // Symmetric Blackman-windowed sinc, cutoff 0.4 * output rate, exactly as Pro.
    // Context is mandatory away from recording edges; actual edges renormalize.
    Matrix resampled(selectedRows.size(), std::vector<double>(std::size_t(outputCount)));
    const double cutoff = 0.4 * request.outputRate / request.sampleRate;
    for (std::size_t t = 0; t < outputCount; ++t) {
        if (t % 128 == 0) {
            check(cancel);
            if (progress) progress(int(40.0 * t / outputCount));
        }
        const double pos = double(request.firstSample - request.readFirstSample) + t * request.sampleRate / request.outputRate;
        if (!half) {
            const auto index = std::size_t(std::llround(pos));
            if (index >= readCount) throw Error(Failure::InvalidRange);
            for (std::size_t c = 0; c < selectedRows.size(); ++c) resampled[c][t] = request.samples[selectedRows[c]][index];
            continue;
        }
        const auto center = std::int64_t(std::floor(pos));
        double sum = 0;
        for (auto j = std::max(std::int64_t(0), center - half); j <= std::min(std::int64_t(readCount - 1), center + half); ++j) {
            const double d = j - pos, r = d / (half + 1.0);
            const double window = 0.42 + 0.5 * std::cos(pi * r) + 0.08 * std::cos(2 * pi * r);
            const double weight = (std::abs(d) < 1e-12 ? 2 * cutoff : std::sin(2 * pi * cutoff * d) / (pi * d)) * window;
            sum += weight;
            for (std::size_t c = 0; c < selectedRows.size(); ++c) resampled[c][t] += weight * request.samples[selectedRows[c]][std::size_t(j)];
        }
        if (!std::isfinite(sum) || std::abs(sum) < 1e-12) throw Error(Failure::InvalidInput);
        for (auto& row : resampled) {
            row[t] /= sum;
            if (!std::isfinite(row[t])) throw Error(Failure::NonFinite);
        }
    }
    for (const auto& row : resampled) {
        const auto range = std::minmax_element(row.begin(), row.end());
        if (*range.second - *range.first < 0.1) throw Error(Failure::FlatChannel);
    }
    Result result;
    result.sampleRate = request.outputRate;
    result.firstSample = request.firstSample;
    result.actualStart = double(request.firstSample) / request.sampleRate;
    result.resampleHalfWidth = half;
    for (auto row : observationRows) {
        result.original.push_back(resampled[row]);
        result.channelIds.push_back(request.channelIds.empty() ? std::to_string(selectedRows[row]) : request.channelIds[selectedRows[row]]);
    }
    ica::Options options;
    options.sampleRate = request.outputRate;
    options.notchHz = request.notchHz;
    options.seed = request.seed;
    options.maxIterations = request.maxIterations;
    options.tolerance = request.tolerance;
    result.ica = ica::fit(result.original, options, cancel, progress);
    Matrix eogReferences, emgReferences;
    for (std::size_t row = 0; row < selectedRows.size(); ++row) {
        check(cancel);
        const auto role = request.roles[selectedRows[row]];
        if (role == Role::EogObservation || role == Role::EogReference) eogReferences.push_back(ica::highpass(resampled[row], request.outputRate, 1));
        if (role == Role::EmgObservation || role == Role::EmgReference) emgReferences.push_back(ica::highpass(resampled[row], request.outputRate, 1));
    }
    if (progress) progress(90);
    for (const auto& source : result.ica.sources) {
        check(cancel);
        const auto low = ica::highpass(source, request.outputRate, 1);
        const auto high = ica::highpass(source, request.outputRate, 30);
        ComponentScore score;
        for (const auto& ref : eogReferences) score.eogCorrelation = std::max(score.eogCorrelation, std::abs(ica::correlation(low, ref)));
        for (const auto& ref : emgReferences) score.emgCorrelation = std::max(score.emgCorrelation, std::abs(ica::correlation(low, ref)));
        const double power = std::inner_product(low.begin(), low.end(), low.begin(), 0.0);
        score.highFrequencyRatio = power > 0 ? std::inner_product(high.begin(), high.end(), high.begin(), 0.0) / power : 0;
        if (!std::isfinite(score.eogCorrelation) || !std::isfinite(score.emgCorrelation) || !std::isfinite(score.highFrequencyRatio))
            throw Error(Failure::NonFinite);
        result.scores.push_back(score);
    }
    check(cancel);
    if (progress) progress(100);
    return result;
}

Reconstruction reconstruct(const Result& result, const std::vector<int>& groups, Cancel cancel)
{
    check(cancel);
    Reconstruction output;
    output.eeg = ica::project(result.ica, groups, 0, cancel);
    output.eog = ica::project(result.ica, groups, 1, cancel);
    output.emg = ica::project(result.ica, groups, 2, cancel);
    output.unresolved = ica::project(result.ica, groups, 3, cancel);
    output.residual = result.ica.residual;
    const auto channels = result.ica.analysis.size(), samples = result.ica.analysis[0].size();
    validateResultMatrix(output.residual, channels, samples);
    if (result.ica.mean.size() != channels) throw Error(Failure::InvalidInput);
    for (std::size_t c = 0; c < channels; ++c) {
        check(cancel);
        if (!std::isfinite(result.ica.mean[c])) throw Error(Failure::NonFinite);
        for (std::size_t t = 0; t < samples; ++t) {
            const double sum = result.ica.mean[c] + output.eeg[c][t] + output.eog[c][t] + output.emg[c][t] + output.unresolved[c][t] + output.residual[c][t];
            if (!std::isfinite(sum) || !std::isfinite(result.ica.analysis[c][t])) throw Error(Failure::NonFinite);
            output.maxError = std::max(output.maxError, std::abs(sum - result.ica.analysis[c][t]));
        }
    }
    if (output.maxError > 1e-6) throw Error(Failure::Reconstruction, output.maxError);
    return output;
}
} // namespace bf::separation
