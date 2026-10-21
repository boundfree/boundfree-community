#include "eeg_separation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace bf::separation;
namespace {
constexpr double pi = 3.14159265358979323846;
int checks = 0;
void require(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template <class F> void rejects(F operation, Failure reason) {
    try { operation(); }
    catch (const Error& error) { require(error.reason == reason, "unexpected failure reason"); return; }
    require(false, "invalid request was accepted");
}
Request fixture() {
    Request request;
    request.sampleRate = 2000;
    request.outputRate = 500;
    request.firstSample = 256;
    request.sampleCount = 24000;
    request.totalSamples = 25000;
    request.readFirstSample = 160;
    request.samples = Matrix(6, std::vector<double>(24192));
    request.roles = {Role::Observation, Role::Observation, Role::Observation, Role::EogReference, Role::EmgReference, Role::Excluded};
    request.channelIds = {"A-000", "A-002", "A-003", "A-004", "A-005", "A-006"};
    const double mixing[3][3] = {{1,.5,.3},{.3,1.2,.1},{1,-.2,1.1}};
    std::mt19937 rng(12138);
    for (std::size_t t = 0; t < request.samples[0].size(); ++t) {
        const double seconds = (t + request.readFirstSample) / request.sampleRate;
        double eye = 18 * std::sin(2 * pi * .18 * seconds);
        for (double blink : {2.2,5.1,9.3,11.6}) eye += 100 * std::exp(-std::pow((seconds-blink)/.12,2));
        const std::array<double, 3> sources{12 * std::sin(2 * pi * 10 * seconds) + 4 * std::sin(2 * pi * 63 * seconds),
            eye, (double(rng()) / 4294967295.0 - .5) * (8 + 65 * std::exp(-std::pow((seconds-7)/.5,2)))};
        for (int c = 0; c < 3; ++c) {
            request.samples[c][t] = 13+c;
            for (int k = 0; k < 3; ++k) request.samples[c][t] += mixing[c][k] * sources[k];
        }
        request.samples[0][t] += 100 * std::sin(2 * pi * 800 * seconds);
        request.samples[3][t] = sources[1]; request.samples[4][t] = sources[2];
        request.samples[5][t] = std::numeric_limits<double>::quiet_NaN(); // excluded values are not analyzed
    }
    return request;
}
} // namespace
int main() {
    try {
        auto request = fixture();
        auto result = analyze(request);
        require(result.original.size() == 3 && result.original[0].size() == 6000, "exact channel and sample counts");
        require(result.firstSample == 256 && result.actualStart == .128 && result.resampleHalfWidth == 48, "sample alignment and context");
        require(result.channelIds == std::vector<std::string>({"A-000", "A-002", "A-003"}), "stable observation channel order");
        require(result.ica.rank == 3 && result.scores.size() == 3, "reference-only channels excluded from fit");
        double sine = 0, cosine = 0;
        for (int t = 100; t < 5900; ++t) {
            sine += result.original[0][t] * std::sin(2 * pi * 200 * t / 500.0);
            cosine += result.original[0][t] * std::cos(2 * pi * 200 * t / 500.0);
        }
        const double alias = 2 * std::hypot(sine, cosine) / 5800;
        std::cout << "800 Hz alias amplitude at 200 Hz = " << alias << " uV\n";
        require(alias < .1, "out-of-band source rejected before decimation");
        double eog = 0, emg = 0;
        for (const auto& score : result.scores) { eog = std::max(eog, score.eogCorrelation); emg = std::max(emg, score.emgCorrelation); }
        require(eog > .9 && emg > .8, "reference clues correlate with controlled eye/muscle sources");
        const auto unknown = reconstruct(result, std::vector<int>(result.ica.rank, 3));
        require(unknown.eeg[0][200] == 0 && unknown.eog[0][200] == 0 && unknown.emg[0][200] == 0, "unclassified components stay unresolved");
        auto grouped = reconstruct(result, {0,1,2});
        std::cout << "Five-group reconstruction max error = " << grouped.maxError << " uV\n";
        require(grouped.maxError < 1e-9 && unknown.maxError < 1e-9, "single mean reconstruction identity");
        auto repeat = analyze(request);
        require(repeat.ica.sources == result.ica.sources, "fixed seed deterministic");
        const auto before = result.ica.sources;
        reconstruct(result, {3,2,1});
        require(result.ica.sources == before, "label changes leave decomposition intact");
        rejects([&] { analyze(request, [] { return true; }); }, Failure::Cancelled);
        int polls = 0;
        rejects([&] { analyze(request, [&] { return ++polls > 5; }); }, Failure::Cancelled);
        rejects([&] { reconstruct(result, {0,1,2}, [] { return true; }); }, Failure::Cancelled);
        rejects([&] { reconstruct(result, {0,1,4}); }, Failure::InvalidInput);
        auto invalid = request; invalid.roles[5] = Role::Unconfirmed;
        rejects([&] { analyze(invalid); }, Failure::InvalidInput);
        invalid = request; invalid.roles[5] = Role::EogReference;
        rejects([&] { analyze(invalid); }, Failure::NonFinite);
        invalid = request; std::fill(invalid.samples[3].begin(), invalid.samples[3].end(), 0);
        rejects([&] { analyze(invalid); }, Failure::FlatChannel);
        invalid = request; invalid.samples[1] = invalid.samples[0]; invalid.samples[2] = invalid.samples[0];
        rejects([&] { analyze(invalid); }, Failure::LowRank);
        invalid = request; invalid.sampleCount = 26000;
        rejects([&] { analyze(invalid); }, Failure::InvalidRange);
        invalid = request; invalid.readFirstSample = 209;
        rejects([&] { analyze(invalid); }, Failure::MissingContext);
        invalid = request; invalid.channelIds[1] = invalid.channelIds[0];
        rejects([&] { analyze(invalid); }, Failure::InvalidInput);
        invalid = request; invalid.sampleRate = 250;
        rejects([&] { analyze(invalid); }, Failure::InvalidInput);
        invalid = request; invalid.maxIterations = 1;
        rejects([&] { analyze(invalid); }, Failure::NotConverged);
        invalid = request; invalid.outputRate = 250;
        auto lower = analyze(invalid);
        require(lower.original[0].size() == 3000 && lower.sampleRate == 250, "250 Hz supported without upsampling");
        // Exact-rate, full-recording boundary path; no missing-context requirement.
        Request exact;
        exact.samples = result.original;
        exact.roles = std::vector<Role>(3, Role::Observation);
        exact.sampleRate = exact.outputRate = 500;
        exact.sampleCount = exact.totalSamples = 6000;
        for (std::size_t t = 0; t < 6000; ++t) exact.samples[0][t] += 25 * std::sin(2 * pi * 50 * t / 500.0);
        exact.notchHz = 50;
        const auto notched = analyze(exact);
        require(notched.original == exact.samples && notched.resampleHalfWidth == 0, "original remains before notch at exact source rate");
        double before50 = 0, after50 = 0;
        for (std::size_t t = 500; t < 5500; ++t) {
            const double phase = std::sin(2 * pi * 50 * t / 500.0);
            before50 += notched.original[0][t] * phase;
            after50 += notched.ica.analysis[0][t] * phase;
        }
        require(std::abs(after50 / before50) < .01, "optional notch removes 50 Hz without replacing original");
        auto malformed = request; malformed.samples[1].pop_back();
        rejects([&] { analyze(malformed); }, Failure::InvalidInput);
        malformed = request; malformed.notchHz = 55;
        rejects([&] { analyze(malformed); }, Failure::InvalidInput);
        malformed = request; malformed.firstSample = std::numeric_limits<std::uint64_t>::max();
        rejects([&] { analyze(malformed); }, Failure::InvalidRange);
        auto damaged = result; damaged.ica.residual[0][0] += .001;
        try { reconstruct(damaged, {0,1,2}); require(false, "damaged reconstruction was accepted"); }
        catch (const Error& error) {
            require(error.reason == Failure::Reconstruction && std::isfinite(error.diagnosticErrorUv) && error.diagnosticErrorUv > .0009,
                    "reconstruction error includes measured microvolt diagnostic");
        }
        damaged = result; damaged.ica.mixing[0].clear();
        rejects([&] { reconstruct(damaged, {0,1,2}); }, Failure::InvalidInput);
        require(resamplingHalfWidth(500,500) == 0, "equal source/output rate uses no resampling");
        rejects([] { resamplingHalfWidth(1e300, 250); }, Failure::InputTooLarge);
        std::cout << "PASS " << checks << " separation checks (synthetic data only)\n";
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
