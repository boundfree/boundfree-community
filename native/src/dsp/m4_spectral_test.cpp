// M4 spectral-analysis numerical parity test.
//
// Calls the native/src/dsp/spectral.h public API and compares it with
// SciPy-generated golden .npy outputs, reporting pass/fail and max absolute error.
//
// Usage: m4_spectral_test [tests/golden/m4]
//
// Parity target (meta.json records all parameters):
// - FFT amplitude: np.hanning symmetric window, abs*2/window.sum(), no detrending.
// - Welch PSD: scipy.signal.welch window='hann' (periodic), detrend='constant', scaling='density',
//              noverlap=nperseg//2, average='mean'。
// - STFT: scipy.signal.spectrogram window='hann', scaling='density', mode='psd',
//              boundary=None, padded=False; time axis is times+t_start.
// - MSC coherence: scipy.signal.coherence, Cxy=|Pxy|^2/(Pxx*Pyy), inclusive frequency maximum.
// - Band power: inclusive [low, high] bin sum * df from linear Welch PSD.

#include "dsp/npy_io.h"
#include "dsp/spectral.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Tolerances, matching meta.json.
struct Tolerances {
    double fftAmpAtol = 1e-9;
    double welchPsdAtol = 1e-10;
    double welchPsdDbAtol = 1e-9;
    double stftSxxDbAtol = 1e-9;
    double stftFreqAtol = 1e-12;
    double stftTimeAtol = 1e-12;
    double coherenceAtol = 1e-9;
    double bandPowerAtol = 1e-10;
    double freqAtol = 1e-12;
};

// Helpers interpreting Matrix (rows x cols, C-order) as one-dimensional or two-dimensional data.
// loadNpyFloat64 returns 1D (n,) as rows=n, cols=1.

std::vector<double> asVector(const bf::dsp::Matrix& m) {
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(m.size()) * (m.empty() ? 0 : m[0].size()));
    for (const auto& row : m) {
        out.insert(out.end(), row.begin(), row.end());
    }
    return out;
}

// Reads a (rows, cols) matrix in row-major layout.
std::vector<std::vector<double>> asMatrix2d(const bf::dsp::Matrix& m) {
    return m;  // Matrix is already rows x cols.
}

// Compares vector lengths and elementwise absolute error, returning the maximum.
bool vectorsClose(
    const std::vector<double>& got,
    const std::vector<double>& expected,
    double atol,
    double* maxAbsErr) {
    if (got.size() != expected.size()) {
        if (maxAbsErr) {
            *maxAbsErr = std::numeric_limits<double>::infinity();
        }
        return false;
    }
    double maxErr = 0.0;
    for (std::size_t i = 0; i < got.size(); ++i) {
        const double err = std::abs(got[i] - expected[i]);
        if (err > maxErr) {
            maxErr = err;
        }
        if (err > atol) {
            if (maxAbsErr) {
                *maxAbsErr = maxErr;
            }
            return false;
        }
    }
    if (maxAbsErr) {
        *maxAbsErr = maxErr;
    }
    return true;
}

// Reports one check: PASS/FAIL <name> max_abs_err=... [shape]
bool checkVector(
    const std::string& name,
    const std::vector<double>& got,
    const std::vector<double>& expected,
    double atol) {
    double maxErr = 0.0;
    const bool ok = vectorsClose(got, expected, atol, &maxErr);
    if (!ok) {
        std::cerr << "FAIL " << name << " max_abs_err=" << maxErr
                  << " len_got=" << got.size()
                  << " len_exp=" << expected.size() << "\n";
        return false;
    }
    std::cout << "PASS " << name << " max_abs_err=" << maxErr
              << " len=" << got.size() << "\n";
    return true;
}

// Compares two-dimensional matrices: rows, columns and values.
bool matrices2dClose(
    const std::vector<std::vector<double>>& got,
    const std::vector<std::vector<double>>& expected,
    double atol,
    double* maxAbsErr) {
    if (got.size() != expected.size()) {
        if (maxAbsErr) {
            *maxAbsErr = std::numeric_limits<double>::infinity();
        }
        return false;
    }
    double maxErr = 0.0;
    for (std::size_t r = 0; r < got.size(); ++r) {
        if (got[r].size() != expected[r].size()) {
            if (maxAbsErr) {
                *maxAbsErr = std::numeric_limits<double>::infinity();
            }
            return false;
        }
        for (std::size_t c = 0; c < got[r].size(); ++c) {
            const double err = std::abs(got[r][c] - expected[r][c]);
            if (err > maxErr) {
                maxErr = err;
            }
            if (err > atol) {
                if (maxAbsErr) {
                    *maxAbsErr = maxErr;
                }
                return false;
            }
        }
    }
    if (maxAbsErr) {
        *maxAbsErr = maxErr;
    }
    return true;
}

bool checkMatrix2d(
    const std::string& name,
    const std::vector<std::vector<double>>& got,
    const std::vector<std::vector<double>>& expected,
    double atol) {
    double maxErr = 0.0;
    const bool ok = matrices2dClose(got, expected, atol, &maxErr);
    if (!ok) {
        std::cerr << "FAIL " << name << " max_abs_err=" << maxErr
                  << " shape_got=" << got.size() << "x"
                  << (got.empty() ? 0 : got.front().size())
                  << " shape_exp=" << expected.size() << "x"
                  << (expected.empty() ? 0 : expected.front().size()) << "\n";
        return false;
    }
    std::cout << "PASS " << name << " max_abs_err=" << maxErr
              << " shape=" << got.size() << "x"
              << (got.empty() ? 0 : got.front().size()) << "\n";
    return true;
}

// STFT golden .npy shape is (n_freq, n_time) row-major; C++ Spectrogram.values is
// frequency-major: values[frequency][time]. The layouts match directly.
// SciPy returns Sxx[freq][time], matching C++ values[freq][time].
// npy_io maps (rows,cols) to Matrix[rows][cols], therefore frequency-major.
std::vector<std::vector<double>> loadMatrix2d(const std::string& path) {
    return asMatrix2d(bf::dsp::loadNpyFloat64(path));
}

// Verbatim copy of the pre-refactor EegSeparationWorkspace::refreshPlots loop.
// Kept here so the extracted helper is proven bit-identical, not merely close.
bf::dsp::Spectrum legacyWorkspaceSpectrum(const std::vector<double>& trace, double fs) {
    auto begin = trace.begin(), end = trace.begin();
    for (auto it = trace.begin(); it != trace.end();) {
        if (!std::isfinite(*it)) {
            ++it;
            continue;
        }
        const auto first = it;
        while (it != trace.end() && std::isfinite(*it)) ++it;
        if (it - first >= end - begin) {
            begin = first;
            end = it;
        }
    }
    const auto psd = end - begin >= 4
        ? bf::dsp::computeWelchPsd(std::vector<double>(begin, end), fs, 0, false) : bf::dsp::Spectrum{};
    bf::dsp::Spectrum out;
    for (std::size_t i = 0; i < psd.frequencies.size(); ++i)
        if (psd.frequencies[i] <= std::min(100., fs / 2)) {
            out.values.push_back(psd.values[i]);
            out.frequencies.push_back(psd.frequencies[i]);
        }
    return out;
}

// Bit-exact comparison: same lengths and every element equal, no tolerance.
bool bitIdentical(const bf::dsp::Spectrum& a, const bf::dsp::Spectrum& b) {
    return a.frequencies == b.frequencies && a.values == b.values;
}

bool report(const std::string& name, bool ok, const std::string& detail = {}) {
    if (!ok) {
        std::cerr << "FAIL " << name << (detail.empty() ? std::string() : " " + detail) << "\n";
        return false;
    }
    std::cout << "PASS " << name << "\n";
    return true;
}

// Display PSD helper: longest finite run, gap boundaries, inclusion rules,
// truncation, and bit parity with the code it replaced in the UI.
int checkLongestFiniteSegmentPsd() {
    int failures = 0;
    const double fs = 250.0;
    const auto trace = [](std::size_t n) {
        std::vector<double> out(n);
        for (std::size_t i = 0; i < n; ++i) out[i] = std::sin(0.07 * static_cast<double>(i)) + 0.3 * std::cos(0.31 * static_cast<double>(i));
        return out;
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    // No finite sample at all: NaN, +Inf and -Inf runs all yield an empty result.
    for (double fill : {nan, inf, -inf}) {
        const bf::dsp::Spectrum empty = bf::dsp::longestFiniteSegmentPsd(std::vector<double>(64, fill), fs, 100.0);
        failures += !report("longest_finite_segment_all_nonfinite",
                            empty.frequencies.empty() && empty.values.empty());
    }
    failures += !report("longest_finite_segment_empty_trace",
                        bf::dsp::longestFiniteSegmentPsd({}, fs, 100.0).values.empty());

    // Shortest usable run: 3 finite samples are rejected, 4 are enough for Welch.
    failures += !report("longest_finite_segment_short_run",
                        bf::dsp::longestFiniteSegmentPsd(trace(3), fs, 100.0).values.empty());
    failures += !report("longest_finite_segment_four_samples",
                        !bf::dsp::longestFiniteSegmentPsd(trace(4), fs, 100.0).values.empty());
    {   // Finite samples hidden behind NaN edges still count as one run.
        std::vector<double> padded(2, nan);
        const auto body = trace(8);
        padded.insert(padded.end(), body.begin(), body.end());
        padded.push_back(nan);
        failures += !report("longest_finite_segment_nan_edges",
                            !bf::dsp::longestFiniteSegmentPsd(padded, fs, 100.0).values.empty());
    }

    // An internal gap is a hard boundary: the 96-sample run wins over the 24-sample run.
    {
        std::vector<double> gapped = trace(96);
        gapped.insert(gapped.end(), 5, nan);
        const auto tail = trace(24);
        gapped.insert(gapped.end(), tail.begin(), tail.end());
        const bf::dsp::Spectrum got = bf::dsp::longestFiniteSegmentPsd(gapped, fs, 100.0);
        failures += !report("longest_finite_segment_internal_gap",
                            bitIdentical(got, bf::dsp::longestFiniteSegmentPsd(trace(96), fs, 100.0)) &&
                                !got.values.empty());
    }

    // Equal-length runs: the later run is selected, so a stale earlier run is not reused.
    {
        std::vector<double> first(32, 1.0);
        std::vector<double> later = trace(32);
        std::vector<double> tied = first;
        tied.push_back(nan);
        tied.insert(tied.end(), later.begin(), later.end());
        const bf::dsp::Spectrum got = bf::dsp::longestFiniteSegmentPsd(tied, fs, 100.0);
        const bf::dsp::Spectrum expected = bf::dsp::longestFiniteSegmentPsd(later, fs, 100.0);
        const bf::dsp::Spectrum stale = bf::dsp::longestFiniteSegmentPsd(first, fs, 100.0);
        failures += !report("longest_finite_segment_tie_takes_later_run",
                            bitIdentical(got, expected) && !bitIdentical(got, stale));
    }

    // Frequency truncation keeps frequencies and values in step and is inclusive.
    {
        const auto signal = trace(512);
        const bf::dsp::Spectrum full = bf::dsp::computeWelchPsd(signal, fs, 0, false);
        const bf::dsp::Spectrum limited = bf::dsp::longestFiniteSegmentPsd(signal, fs, 20.0);
        std::size_t keep = 0;
        while (keep < full.frequencies.size() && full.frequencies[keep] <= 20.0) ++keep;
        bool synced = limited.frequencies.size() == limited.values.size() && keep == limited.frequencies.size();
        for (std::size_t i = 0; synced && i < keep; ++i) {
            synced = limited.frequencies[i] == full.frequencies[i] && limited.values[i] == full.values[i];
        }
        failures += !report("longest_finite_segment_truncation",
                            synced && keep > 0 && limited.frequencies.back() <= 20.0 &&
                                (keep == full.frequencies.size() || full.frequencies[keep] > 20.0));
        failures += !report("longest_finite_segment_above_nyquist",
                            bitIdentical(bf::dsp::longestFiniteSegmentPsd(signal, fs, 1000.0),
                                         bf::dsp::computeWelchPsd(signal, fs, 0, false)));
        failures += !report("longest_finite_segment_clamps_fmax",
                            bf::dsp::longestFiniteSegmentPsd(signal, fs, -1.0).frequencies.size() == 1 &&
                                bf::dsp::longestFiniteSegmentPsd(signal, fs, -1.0).frequencies.front() == 0.0);
    }

    // Bit parity with the previous in-UI implementation for every shape above.
    {
        std::vector<std::vector<double>> inputs;
        inputs.push_back(trace(512));
        inputs.push_back(trace(4));
        inputs.push_back(trace(3));
        inputs.push_back({});
        {   std::vector<double> v(40, nan); v[10] = 1.0; v[11] = 2.0; v[12] = 3.0; inputs.push_back(v); }
        {   std::vector<double> v = trace(50); v[20] = nan; inputs.push_back(v); }
        {   std::vector<double> v = trace(200); std::fill(v.begin() + 60, v.begin() + 66, inf); inputs.push_back(v); }
        {   std::vector<double> v = trace(120); v[0] = nan; v[119] = -inf; inputs.push_back(v); }
        {   std::vector<double> v(64, 0.0); inputs.push_back(v); }
        bool identical = true;
        for (const auto& input : inputs) {
            const bf::dsp::Spectrum got = bf::dsp::longestFiniteSegmentPsd(input, fs, 100.0);
            const bf::dsp::Spectrum expected = legacyWorkspaceSpectrum(input, fs);
            identical = identical && bitIdentical(got, expected);
        }
        failures += !report("longest_finite_segment_matches_legacy_ui_bits", identical);
    }
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string goldenDir = (argc > 1) ? argv[1] : std::string("tests/golden/m4");
    int failures = checkLongestFiniteSegmentPsd();
    Tolerances tol;

    try {
        // Input signals
        const bf::dsp::Matrix signalMat = bf::dsp::loadNpyFloat64(goldenDir + "/signal.npy");
        const std::vector<double> signal = asVector(signalMat);
        const bf::dsp::Matrix signalBMat = bf::dsp::loadNpyFloat64(goldenDir + "/signal_b.npy");
        const std::vector<double> signalB = asVector(signalBMat);

        // Fixed generator parameters from meta.json, hard-coded to avoid a JSON dependency.
        const double fs = 250.0;
        const int nperseg = 256;
        const int noverlapStft = 192;  // nperseg * 3 / 4
        const double tStart = 12.5;
        const double stftFmax = 60.0;
        const double cohFmax = 80.0;

        // ---- FFT amplitude ----
        const bf::dsp::Spectrum fftResult = bf::dsp::computeFftAmplitude(signal, fs);
        const std::vector<double> fftFreqExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/fft_freq.npy"));
        const std::vector<double> fftAmpExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/fft_amp.npy"));
        if (!checkVector("fft_freq", fftResult.frequencies, fftFreqExp, tol.freqAtol)) {
            ++failures;
        }
        if (!checkVector("fft_amp", fftResult.values, fftAmpExp, tol.fftAmpAtol)) {
            ++failures;
        }

        // ---- Linear Welch PSD ----
        const bf::dsp::Spectrum welchResult = bf::dsp::computeWelchPsd(signal, fs, nperseg, /*asDb=*/false);
        const std::vector<double> welchFreqExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/welch_freq.npy"));
        const std::vector<double> welchPsdExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/welch_psd.npy"));
        if (!checkVector("welch_freq", welchResult.frequencies, welchFreqExp, tol.freqAtol)) {
            ++failures;
        }
        if (!checkVector("welch_psd", welchResult.values, welchPsdExp, tol.welchPsdAtol)) {
            ++failures;
        }

        // ---- Welch PSD dB ----
        const bf::dsp::Spectrum welchDbResult = bf::dsp::computeWelchPsd(signal, fs, nperseg, /*asDb=*/true);
        const std::vector<double> welchPsdDbExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/welch_psd_db.npy"));
        if (!checkVector("welch_psd_db", welchDbResult.values, welchPsdDbExp, tol.welchPsdDbAtol)) {
            ++failures;
        }

        // ---- STFT ----
        const bf::dsp::Spectrogram stftResult =
            bf::dsp::computeSpectrogram(signal, fs, nperseg, noverlapStft, tStart, stftFmax);
        const std::vector<double> stftFreqExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/stft_freq.npy"));
        const std::vector<double> stftTimeExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/stft_time.npy"));
        const std::vector<std::vector<double>> stftSxxDbExp = loadMatrix2d(goldenDir + "/stft_sxx_db.npy");
        if (!checkVector("stft_freq", stftResult.frequencies, stftFreqExp, tol.stftFreqAtol)) {
            ++failures;
        }
        if (!checkVector("stft_time", stftResult.times, stftTimeExp, tol.stftTimeAtol)) {
            ++failures;
        }
        // C++ Spectrogram.values[freq][time] vs scipy Sxx[freq,time]
        if (!checkMatrix2d("stft_sxx_db", stftResult.values, stftSxxDbExp, tol.stftSxxDbAtol)) {
            ++failures;
        }

        // ---- MSC coherence ----
        const bf::dsp::Spectrum cohResult =
            bf::dsp::computeCoherence(signal, signalB, fs, nperseg, cohFmax);
        const std::vector<double> cohFreqExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/coh_freq.npy"));
        const std::vector<double> cohExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/coh.npy"));
        if (!checkVector("coh_freq", cohResult.frequencies, cohFreqExp, tol.freqAtol)) {
            ++failures;
        }
        if (!checkVector("coh", cohResult.values, cohExp, tol.coherenceAtol)) {
            ++failures;
        }

        // ---- Five-band power from linear Welch PSD ----
        const std::vector<double> bandPowerExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/band_power.npy"));
        // Matches eegBands() order: delta/theta/alpha/beta/gamma.
        const std::array<bf::dsp::Band, 5>& bands = bf::dsp::eegBands();
        std::vector<double> bandPowerGot;
        bandPowerGot.reserve(bands.size());
        for (const auto& band : bands) {
            bandPowerGot.push_back(bf::dsp::bandPower(welchResult, band.low, band.high));
        }
        if (!checkVector("band_power", bandPowerGot, bandPowerExp, tol.bandPowerAtol)) {
            ++failures;
        }
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION " << ex.what() << "\n";
        return 2;
    }

    if (failures == 0) {
        std::cout << "m4-spectral-test-ok failures=0\n";
        return 0;
    }
    std::cerr << "m4-spectral-test-failed failures=" << failures << "\n";
    return 1;
}
