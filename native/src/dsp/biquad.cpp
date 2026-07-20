#include "dsp/biquad.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bf {
namespace dsp {
namespace {

void validateMatrix(const Matrix& data) {
    if (data.empty()) {
        return;
    }
    const std::size_t nSamples = data.front().size();
    for (const auto& row : data) {
        if (row.size() != nSamples) {
            throw std::invalid_argument("sos filter: jagged matrix");
        }
    }
}

void validateSos(const SosMatrix& sos) {
    for (const SosSection& sec : sos) {
        if (std::abs(sec[3]) < 1e-18) {
            throw std::invalid_argument("sos filter: a0 is zero");
        }
    }
}

inline void processSample(double x, const SosSection& sec, BiquadState& st, double& y) {
    const double a0 = sec[3];
    const double b0 = sec[0] / a0;
    const double b1 = sec[1] / a0;
    const double b2 = sec[2] / a0;
    const double a1 = sec[4] / a0;
    const double a2 = sec[5] / a0;

    y = b0 * x + st[0];
    st[0] = b1 * x - a1 * y + st[1];
    st[1] = b2 * x - a2 * y;
}

// scipy.signal.lfilter_zi for order-2 DF2T-compatible state.
BiquadState sosFiltZiOne(const SosSection& sec) {
    const double a0 = sec[3];
    const double b0 = sec[0] / a0;
    const double b1 = sec[1] / a0;
    const double b2 = sec[2] / a0;
    const double a1 = sec[4] / a0;
    const double a2 = sec[5] / a0;

    // companion C = [[-a1, -a2], [1, 0]]; solve (I - C^T) zi = b[1:] - a[1:]*b[0]
    const double rhs0 = b1 - a1 * b0;
    const double rhs1 = b2 - a2 * b0;
    const double det = (1.0 + a1) * 1.0 - (-1.0) * a2;
    if (std::abs(det) < 1e-18) {
        return BiquadState{0.0, 0.0};
    }
    const double z0 = (rhs0 * 1.0 - (-1.0) * rhs1) / det;
    const double z1 = ((1.0 + a1) * rhs1 - a2 * rhs0) / det;
    return BiquadState{z0, z1};
}

int defaultPadLen(const SosMatrix& sos) {
    int zeroB2 = 0;
    int zeroA2 = 0;
    for (const SosSection& sec : sos) {
        if (std::abs(sec[2]) < 1e-15) {
            ++zeroB2;
        }
        if (std::abs(sec[5]) < 1e-15) {
            ++zeroA2;
        }
    }
    const int nSos = static_cast<int>(sos.size());
    return 3 * (2 * nSos + 1 - std::min(zeroB2, zeroA2));
}

std::vector<double> oddExtend(const std::vector<double>& x, int padlen) {
    if (padlen <= 0) {
        return x;
    }
    if (static_cast<int>(x.size()) <= padlen) {
        throw std::invalid_argument("sosfiltfilt: padlen too large for signal");
    }
    std::vector<double> ext(x.size() + static_cast<std::size_t>(2 * padlen));
    // left: 2*x[0] - x[padlen], ..., 2*x[0]-x[1]
    for (int i = 0; i < padlen; ++i) {
        ext[static_cast<std::size_t>(i)] = 2.0 * x[0] - x[static_cast<std::size_t>(padlen - i)];
    }
    std::copy(x.begin(), x.end(), ext.begin() + padlen);
    const std::size_t n = x.size();
    for (int i = 0; i < padlen; ++i) {
        ext[padlen + n + static_cast<std::size_t>(i)] =
            2.0 * x[n - 1] - x[n - 2 - static_cast<std::size_t>(i)];
    }
    return ext;
}

std::vector<double> reverse(const std::vector<double>& x) {
    return std::vector<double>(x.rbegin(), x.rend());
}

std::vector<double> sosFilt1d(
    const SosMatrix& sos,
    const std::vector<double>& x,
    std::vector<BiquadState>* stateOrNull) {
    std::vector<double> y = x;
    std::vector<BiquadState> localState;
    std::vector<BiquadState>* state = stateOrNull;
    if (state == nullptr) {
        localState.assign(sos.size(), BiquadState{0.0, 0.0});
        state = &localState;
    }
    if (state->size() != sos.size()) {
        throw std::invalid_argument("sosFilt1d: state size mismatch");
    }
    for (std::size_t secIdx = 0; secIdx < sos.size(); ++secIdx) {
        for (double& sample : y) {
            double out = 0.0;
            processSample(sample, sos[secIdx], (*state)[secIdx], out);
            sample = out;
        }
    }
    return y;
}

}  // namespace

SosState makeSosState(const SosMatrix& sos, int nChannels) {
    return SosState(
        sos.size(),
        std::vector<BiquadState>(static_cast<std::size_t>(nChannels), BiquadState{0.0, 0.0}));
}

SosState makeSteadyStateSosState(const SosMatrix& sos, int nChannels) {
    SosState state = makeSosState(sos, nChannels);
    // scipy.sosfilt_zi: zi[section] = scale * lfilter_zi; scale *= H(1)
    double scale = 1.0;
    for (std::size_t sec = 0; sec < sos.size(); ++sec) {
        const BiquadState ziOne = sosFiltZiOne(sos[sec]);
        for (int ch = 0; ch < nChannels; ++ch) {
            state[sec][static_cast<std::size_t>(ch)] =
                BiquadState{ziOne[0] * scale, ziOne[1] * scale};
        }
        const SosSection& s = sos[sec];
        const double bSum = s[0] + s[1] + s[2];
        const double aSum = s[3] + s[4] + s[5];
        if (std::abs(aSum) > 1e-18) {
            scale *= bSum / aSum;
        }
    }
    return state;
}

Matrix sosFilt(const SosMatrix& sos, const Matrix& data, SosState* state) {
    validateMatrix(data);
    validateSos(sos);
    if (data.empty()) {
        return {};
    }
    const int nChannels = static_cast<int>(data.size());
    const std::size_t nSamples = data.front().size();

    SosState local;
    if (state == nullptr) {
        local = makeSosState(sos, nChannels);
        state = &local;
    }
    if (state->size() != sos.size()
        || static_cast<int>(state->empty() ? 0 : state->front().size()) != nChannels) {
        throw std::invalid_argument("sosFilt: state shape mismatch");
    }

    Matrix out = makeMatrix(static_cast<std::size_t>(nChannels), nSamples);
    for (int ch = 0; ch < nChannels; ++ch) {
        std::vector<BiquadState> chState(sos.size());
        for (std::size_t sec = 0; sec < sos.size(); ++sec) {
            chState[sec] = (*state)[sec][static_cast<std::size_t>(ch)];
        }
        out[static_cast<std::size_t>(ch)] =
            sosFilt1d(sos, data[static_cast<std::size_t>(ch)], &chState);
        for (std::size_t sec = 0; sec < sos.size(); ++sec) {
            (*state)[sec][static_cast<std::size_t>(ch)] = chState[sec];
        }
    }
    return out;
}

Matrix sosFiltFilt(const SosMatrix& sos, const Matrix& data) {
    validateMatrix(data);
    validateSos(sos);
    if (data.empty()) {
        return {};
    }
    const int padlen = defaultPadLen(sos);
    Matrix out = makeMatrix(data.size(), data.front().size());

    for (std::size_t ch = 0; ch < data.size(); ++ch) {
        std::vector<double> ext = oddExtend(data[ch], padlen);
        SosState zi = makeSteadyStateSosState(sos, 1);
        for (std::size_t sec = 0; sec < sos.size(); ++sec) {
            zi[sec][0][0] *= ext.front();
            zi[sec][0][1] *= ext.front();
        }
        Matrix extMat(1, ext);
        Matrix yMat = sosFilt(sos, extMat, &zi);
        std::vector<double> y = reverse(yMat[0]);

        zi = makeSteadyStateSosState(sos, 1);
        for (std::size_t sec = 0; sec < sos.size(); ++sec) {
            zi[sec][0][0] *= y.front();
            zi[sec][0][1] *= y.front();
        }
        extMat[0] = y;
        yMat = sosFilt(sos, extMat, &zi);
        y = reverse(yMat[0]);
        out[ch].assign(y.begin() + padlen, y.end() - padlen);
    }
    return out;
}

Matrix offlineFilter(const Matrix& data, const std::vector<SosMatrix>& sosList) {
    Matrix out = cloneMatrix(data);
    for (const SosMatrix& sos : sosList) {
        out = sosFiltFilt(sos, out);
    }
    return out;
}

}  // namespace dsp
}  // namespace bf
