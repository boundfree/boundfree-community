// M4 频谱分析数值对齐测试。
//
// 调用 native/src/dsp/spectral.h 的公共 API,与 scripts/generate_m4_golden.py
// 用 SciPy 生成的黄金 .npy 结果逐项比对,输出每个检查项的通过/失败与最大绝对误差。
//
// 用法: m4_spectral_test [tests/golden/m4]
//
// 对齐目标 (meta.json 记录完整参数):
// - FFT 幅度:   np.hanning 对称窗, abs*2/window.sum(), 不去趋势。
// - Welch PSD: scipy.signal.welch window='hann'(周期), detrend='constant', scaling='density',
//              noverlap=nperseg//2, average='mean'。
// - STFT:      scipy.signal.spectrogram window='hann', scaling='density', mode='psd',
//              boundary=None, padded=False; 时间轴 times+t_start。
// - MSC 相干:  scipy.signal.coherence, Cxy=|Pxy|^2/(Pxx*Pyy), 频率上限闭区间。
// - 频段功率:   闭区间 [low,high] 频点求和 * df, 基于 Welch 线性 PSD。

#include "dsp/npy_io.h"
#include "dsp/spectral.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

// 容差 (与 meta.json 一致)。
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

// 将 Matrix (rows x cols, C-order) 解释为一维向量或二维布局的辅助函数。
// loadNpyFloat64 对 1D (n,) 返回 rows=n, cols=1 的矩阵。

std::vector<double> asVector(const bf::dsp::Matrix& m) {
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(m.size()) * (m.empty() ? 0 : m[0].size()));
    for (const auto& row : m) {
        out.insert(out.end(), row.begin(), row.end());
    }
    return out;
}

// 读取形状为 (rows, cols) 的二维矩阵,按行优先展开。
std::vector<std::vector<double>> asMatrix2d(const bf::dsp::Matrix& m) {
    return m;  // Matrix 已是 rows x cols。
}

// 比较两个一维向量:长度、逐元素绝对误差,返回最大绝对误差。
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

// 报告单个检查项,统一格式: PASS/FAIL <name> max_abs_err=... [shape]
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

// 比较二维矩阵 (行数、列数、逐元素)。
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

// STFT golden .npy 形状为 (n_freq, n_time) 行优先;C++ Spectrogram.values 是
// frequency-major: values[frequency][time]。两者布局一致,直接比对。
// 但 scipy spectrogram 返回 Sxx[freq][time],C++ values 也是 [freq][time] -> 一致。
// 注意 npy_io 读 (rows,cols) -> Matrix[rows][cols],正好 freq-major。
std::vector<std::vector<double>> loadMatrix2d(const std::string& path) {
    return asMatrix2d(bf::dsp::loadNpyFloat64(path));
}

}  // namespace

int main(int argc, char** argv) {
    const std::string goldenDir = (argc > 1) ? argv[1] : std::string("tests/golden/m4");
    int failures = 0;
    Tolerances tol;

    try {
        // 输入信号
        const bf::dsp::Matrix signalMat = bf::dsp::loadNpyFloat64(goldenDir + "/signal.npy");
        const std::vector<double> signal = asVector(signalMat);
        const bf::dsp::Matrix signalBMat = bf::dsp::loadNpyFloat64(goldenDir + "/signal_b.npy");
        const std::vector<double> signalB = asVector(signalBMat);

        // 从 meta.json 读取的固定参数 (与生成器一致,这里硬编码以避免引入 JSON 依赖)。
        const double fs = 250.0;
        const int nperseg = 256;
        const int noverlapStft = 192;  // nperseg * 3 / 4
        const double tStart = 12.5;
        const double stftFmax = 60.0;
        const double cohFmax = 80.0;

        // ---- FFT 幅度 ----
        const bf::dsp::Spectrum fftResult = bf::dsp::computeFftAmplitude(signal, fs);
        const std::vector<double> fftFreqExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/fft_freq.npy"));
        const std::vector<double> fftAmpExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/fft_amp.npy"));
        if (!checkVector("fft_freq", fftResult.frequencies, fftFreqExp, tol.freqAtol)) {
            ++failures;
        }
        if (!checkVector("fft_amp", fftResult.values, fftAmpExp, tol.fftAmpAtol)) {
            ++failures;
        }

        // ---- Welch PSD 线性 ----
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

        // ---- MSC 相干 ----
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

        // ---- 五频段功率 (基于 Welch 线性 PSD) ----
        const std::vector<double> bandPowerExp = asVector(bf::dsp::loadNpyFloat64(goldenDir + "/band_power.npy"));
        // 与 eegBands() 顺序一致: delta/theta/alpha/beta/gamma。
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
