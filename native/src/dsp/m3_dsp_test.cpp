#include "dsp/biquad.h"
#include "dsp/filter_design.h"
#include "dsp/npy_io.h"
#include "dsp/pipeline.h"
#include "dsp/reference.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool almostEqual(double a, double b, double rtol, double atol) {
    return std::abs(a - b) <= atol + rtol * std::abs(b);
}

bool matricesClose(
    const bf::dsp::Matrix& got,
    const bf::dsp::Matrix& expected,
    double rtol,
    double atol,
    double* maxAbsErr) {
    if (got.size() != expected.size()) {
        return false;
    }
    double maxErr = 0.0;
    for (std::size_t r = 0; r < got.size(); ++r) {
        if (got[r].size() != expected[r].size()) {
            return false;
        }
        for (std::size_t c = 0; c < got[r].size(); ++c) {
            const double err = std::abs(got[r][c] - expected[r][c]);
            if (err > maxErr) {
                maxErr = err;
            }
            if (!almostEqual(got[r][c], expected[r][c], rtol, atol)) {
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

bool checkNamed(
    const std::string& name,
    const bf::dsp::Matrix& got,
    const bf::dsp::Matrix& expected,
    double rtol,
    double atol) {
    double maxErr = 0.0;
    if (!matricesClose(got, expected, rtol, atol, &maxErr)) {
        std::cerr << "FAIL " << name << " max_abs_err=" << maxErr
                  << " shape_got=" << got.size() << "x" << (got.empty() ? 0 : got.front().size())
                  << " shape_exp=" << expected.size() << "x"
                  << (expected.empty() ? 0 : expected.front().size()) << "\n";
        return false;
    }
    std::cout << "PASS " << name << " max_abs_err=" << maxErr << "\n";
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string goldenDir = (argc > 1) ? argv[1] : std::string("tests/golden/m3");
    int failures = 0;

    try {
        const bf::dsp::Matrix input = bf::dsp::loadNpyFloat64(goldenDir + "/input.npy");
        const bf::dsp::Matrix sosBp = bf::dsp::loadNpyFloat64(goldenDir + "/sos_bandpass_0_5_40.npy");
        const bf::dsp::Matrix sosNotch = bf::dsp::loadNpyFloat64(goldenDir + "/sos_notch_50.npy");
        const bf::dsp::Matrix refRaw = bf::dsp::loadNpyFloat64(goldenDir + "/out_ref_raw.npy");
        const bf::dsp::Matrix refCar = bf::dsp::loadNpyFloat64(goldenDir + "/out_ref_car.npy");
        const bf::dsp::Matrix refBipolar = bf::dsp::loadNpyFloat64(goldenDir + "/out_ref_bipolar.npy");
        const bf::dsp::Matrix outOffline = bf::dsp::loadNpyFloat64(goldenDir + "/out_e2e_offline.npy");
        const bf::dsp::Matrix outRtChunked =
            bf::dsp::loadNpyFloat64(goldenDir + "/out_e2e_realtime_chunked.npy");
        const bf::dsp::Matrix outRtFull = bf::dsp::loadNpyFloat64(goldenDir + "/out_e2e_realtime_full.npy");

        const bf::dsp::SosMatrix designedBp = bf::dsp::designBandpass(0.5, 40.0, 1000.0, 4);
        bf::dsp::Matrix designedBpMat = bf::dsp::makeMatrix(designedBp.size(), 6);
        for (std::size_t i = 0; i < designedBp.size(); ++i) {
            for (int j = 0; j < 6; ++j) {
                designedBpMat[i][static_cast<std::size_t>(j)] = designedBp[i][static_cast<std::size_t>(j)];
            }
        }
        if (!checkNamed("design_bandpass", designedBpMat, sosBp, 1e-9, 1e-12)) {
            ++failures;
        }

        const bf::dsp::SosMatrix designedNotch = bf::dsp::designNotch(50.0, 1000.0, 30.0);
        bf::dsp::Matrix designedNotchMat = bf::dsp::makeMatrix(designedNotch.size(), 6);
        for (std::size_t i = 0; i < designedNotch.size(); ++i) {
            for (int j = 0; j < 6; ++j) {
                designedNotchMat[i][static_cast<std::size_t>(j)] =
                    designedNotch[i][static_cast<std::size_t>(j)];
            }
        }
        if (!checkNamed("design_notch", designedNotchMat, sosNotch, 1e-9, 1e-12)) {
            ++failures;
        }

        if (!checkNamed(
                "reference_raw",
                bf::dsp::applyReference(input, bf::dsp::ReferenceMode::Raw),
                refRaw,
                1e-12,
                1e-15)) {
            ++failures;
        }
        if (!checkNamed(
                "reference_car",
                bf::dsp::applyReference(input, bf::dsp::ReferenceMode::Car),
                refCar,
                1e-12,
                1e-15)) {
            ++failures;
        }
        if (!checkNamed(
                "reference_bipolar",
                bf::dsp::applyReference(input, bf::dsp::ReferenceMode::Bipolar),
                refBipolar,
                1e-12,
                1e-15)) {
            ++failures;
        }

        bf::dsp::ProcessingConfig cfg;
        cfg.fs = 1000.0;
        cfg.nChannels = 4;
        cfg.referenceMode = bf::dsp::ReferenceMode::Car;
        cfg.notchFreqs = {50.0, 100.0, 150.0};
        cfg.bandpass = {
            {true, 0.5, 40.0},
            {false, 1.0, 30.0},
            {true, 1.0, 70.0},
            {true, 5.0, 20.0},
        };

        bf::dsp::ProcessingPipeline offlinePipe;
        offlinePipe.rebuild(cfg);
        if (!checkNamed("e2e_offline", offlinePipe.processOffline(input), outOffline, 1e-6, 1e-8)) {
            ++failures;
        }

        bf::dsp::ProcessingPipeline rtPipe;
        rtPipe.rebuild(cfg);
        bf::dsp::Matrix rtChunked = bf::dsp::makeMatrix(input.size(), 0);
        const std::size_t nSamples = input.front().size();
        for (std::size_t start = 0; start < nSamples; start += 64) {
            const std::size_t end = std::min(start + 64, nSamples);
            bf::dsp::Matrix chunk = bf::dsp::makeMatrix(input.size(), end - start);
            for (std::size_t ch = 0; ch < input.size(); ++ch) {
                chunk[ch].assign(input[ch].begin() + static_cast<std::ptrdiff_t>(start),
                                 input[ch].begin() + static_cast<std::ptrdiff_t>(end));
            }
            bf::dsp::Matrix part = rtPipe.processRealtime(chunk);
            for (std::size_t ch = 0; ch < part.size(); ++ch) {
                rtChunked[ch].insert(rtChunked[ch].end(), part[ch].begin(), part[ch].end());
            }
        }
        if (!checkNamed("e2e_realtime_chunked", rtChunked, outRtChunked, 1e-5, 1e-7)) {
            ++failures;
        }

        bf::dsp::ProcessingPipeline rtFullPipe;
        rtFullPipe.rebuild(cfg);
        if (!checkNamed("e2e_realtime_full", rtFullPipe.processRealtime(input), outRtFull, 1e-5, 1e-7)) {
            ++failures;
        }
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION " << ex.what() << "\n";
        return 2;
    }

    if (failures == 0) {
        std::cout << "m3-dsp-test-ok failures=0\n";
        return 0;
    }
    std::cerr << "m3-dsp-test-failed failures=" << failures << "\n";
    return 1;
}
