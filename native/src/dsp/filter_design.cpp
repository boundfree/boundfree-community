#include "dsp/filter_design.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace bf {
namespace dsp {
namespace {

constexpr double kPi = 3.14159265358979323846;
using Complex = std::complex<double>;

struct Zpk {
    std::vector<Complex> z;
    std::vector<Complex> p;
    double k = 1.0;
};

int relativeDegree(const std::vector<Complex>& z, const std::vector<Complex>& p) {
    return static_cast<int>(p.size()) - static_cast<int>(z.size());
}

Zpk buttap(int order) {
    if (order < 0) {
        throw std::invalid_argument("buttap: order must be non-negative");
    }
    Zpk out;
    out.k = 1.0;
    for (int m = -order + 1; m < order; m += 2) {
        out.p.push_back(-std::exp(Complex(0.0, kPi * static_cast<double>(m) / (2.0 * order))));
    }
    return out;
}

Zpk lp2bpZpk(const Zpk& in, double wo, double bw) {
    Zpk out;
    const int degree = relativeDegree(in.z, in.p);
    auto transform = [&](const std::vector<Complex>& src, std::vector<Complex>& dst) {
        for (const Complex& value : src) {
            const Complex lp = value * (bw / 2.0);
            const Complex disc = std::sqrt(lp * lp - wo * wo);
            dst.push_back(lp + disc);
            dst.push_back(lp - disc);
        }
    };
    transform(in.z, out.z);
    transform(in.p, out.p);
    for (int i = 0; i < degree; ++i) {
        out.z.emplace_back(0.0, 0.0);
    }
    out.k = in.k * std::pow(bw, degree);
    return out;
}

Zpk bilinearZpk(const Zpk& in, double fs) {
    Zpk out;
    const int degree = relativeDegree(in.z, in.p);
    const double fs2 = 2.0 * fs;
    Complex num = 1.0;
    Complex den = 1.0;
    for (const Complex& z : in.z) {
        out.z.push_back((fs2 + z) / (fs2 - z));
        num *= (fs2 - z);
    }
    for (const Complex& p : in.p) {
        out.p.push_back((fs2 + p) / (fs2 - p));
        den *= (fs2 - p);
    }
    for (int i = 0; i < degree; ++i) {
        out.z.emplace_back(-1.0, 0.0);
    }
    out.k = in.k * std::real(num / den);
    return out;
}

std::vector<double> polyFromRoots(const std::vector<Complex>& roots) {
    std::vector<Complex> coeffs{1.0};
    for (const Complex& root : roots) {
        std::vector<Complex> next(coeffs.size() + 1, Complex(0.0, 0.0));
        for (std::size_t i = 0; i < coeffs.size(); ++i) {
            next[i] += coeffs[i];
            next[i + 1] -= coeffs[i] * root;
        }
        coeffs.swap(next);
    }
    std::vector<double> out(coeffs.size());
    for (std::size_t i = 0; i < coeffs.size(); ++i) {
        out[i] = std::real(coeffs[i]);
    }
    return out;
}

// Conjugate-aware pairing sufficient for Butterworth bandpass SOS.
// Validated against scipy butter(..., output='sos') for order=4 bandpass.
SosMatrix zpkToSosSimple(Zpk zpk) {
    std::vector<Complex> zeros = zpk.z;
    std::vector<Complex> poles = zpk.p;
    while (zeros.size() < poles.size()) {
        zeros.emplace_back(0.0, 0.0);
    }
    while (poles.size() < zeros.size()) {
        poles.emplace_back(0.0, 0.0);
    }

    auto worstIndex = [](const std::vector<Complex>& values) {
        std::size_t idx = 0;
        double best = 1e300;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double score = std::abs(1.0 - std::abs(values[i]));
            if (score < best) {
                best = score;
                idx = i;
            }
        }
        return idx;
    };

    auto eraseAt = [](std::vector<Complex>& values, std::size_t index) {
        values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
    };

    auto takeConjugateOrNextReal = [&](std::vector<Complex>& values, Complex first) {
        if (std::abs(first.imag()) >= 1e-12) {
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (std::abs(values[i] - std::conj(first)) < 1e-8) {
                    eraseAt(values, i);
                    return std::conj(first);
                }
            }
            return std::conj(first);
        }
        std::size_t best = 0;
        double bestScore = 1e300;
        bool found = false;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (std::abs(values[i].imag()) >= 1e-12) {
                continue;
            }
            const double score = std::abs(1.0 - std::abs(values[i]));
            if (!found || score < bestScore) {
                found = true;
                bestScore = score;
                best = i;
            }
        }
        if (!found) {
            return Complex(0.0, 0.0);
        }
        Complex second = values[best];
        eraseAt(values, best);
        return second;
    };

    std::vector<SosSection> sections;
    while (!poles.empty()) {
        const std::size_t p1Idx = worstIndex(poles);
        Complex p1 = poles[p1Idx];
        eraseAt(poles, p1Idx);
        Complex p2 = takeConjugateOrNextReal(poles, p1);

        Complex z1 = 0.0;
        Complex z2 = 0.0;
        if (!zeros.empty()) {
            std::size_t z1Idx = 0;
            double bestDist = 1e300;
            for (std::size_t i = 0; i < zeros.size(); ++i) {
                const double dist = std::abs(zeros[i] - p1);
                if (dist < bestDist) {
                    bestDist = dist;
                    z1Idx = i;
                }
            }
            z1 = zeros[z1Idx];
            eraseAt(zeros, z1Idx);
            if (std::abs(z1.imag()) >= 1e-12) {
                z2 = takeConjugateOrNextReal(zeros, z1);
            } else {
                std::size_t z2Idx = 0;
                double best = 1e300;
                bool found = false;
                for (std::size_t i = 0; i < zeros.size(); ++i) {
                    if (std::abs(zeros[i].imag()) >= 1e-12) {
                        continue;
                    }
                    const double dist = std::abs(zeros[i] - p1);
                    if (!found || dist < best) {
                        found = true;
                        best = dist;
                        z2Idx = i;
                    }
                }
                if (found) {
                    z2 = zeros[z2Idx];
                    eraseAt(zeros, z2Idx);
                }
            }
        }

        const std::vector<double> b = polyFromRoots({z1, z2});
        const std::vector<double> a = polyFromRoots({p1, p2});
        SosSection sec{};
        sec[0] = b[0];
        sec[1] = b[1];
        sec[2] = b[2];
        sec[3] = a[0];
        sec[4] = a[1];
        sec[5] = a[2];
        sections.push_back(sec);
    }

    // Built worst-first; reverse so worst ends last (scipy order).
    std::reverse(sections.begin(), sections.end());
    if (!sections.empty()) {
        sections.front()[0] *= zpk.k;
        sections.front()[1] *= zpk.k;
        sections.front()[2] *= zpk.k;
    }
    return sections;
}

SosMatrix baToOneSection(const std::vector<double>& b, const std::vector<double>& a) {
    SosSection sec{};
    for (std::size_t i = 0; i < b.size() && i < 3; ++i) {
        sec[3 - b.size() + i] = b[i];
    }
    for (std::size_t i = 0; i < a.size() && i < 3; ++i) {
        sec[6 - a.size() + i] = a[i];
    }
    const double a0 = sec[3];
    if (std::abs(a0) < 1e-18) {
        throw std::invalid_argument("baToOneSection: a0 is zero");
    }
    for (double& c : sec) {
        c /= a0;
    }
    return SosMatrix{sec};
}

}  // namespace

SosMatrix designBandpass(double low, double high, double fs, int order) {
    if (!(fs > 0.0) || order <= 0) {
        throw std::invalid_argument("designBandpass: invalid fs/order");
    }
    const double nyq = fs / 2.0;
    const double lo = std::max(low / nyq, 1e-4);
    const double hi = std::min(high / nyq, 0.9999);
    if (!(lo < hi)) {
        throw std::invalid_argument("designBandpass: low must be < high after clamping");
    }

    const double fsDes = 2.0;
    const double w0 = 2.0 * fsDes * std::tan(kPi * lo / fsDes);
    const double w1 = 2.0 * fsDes * std::tan(kPi * hi / fsDes);
    const double bw = w1 - w0;
    const double wo = std::sqrt(w0 * w1);

    Zpk proto = buttap(order);
    Zpk band = lp2bpZpk(proto, wo, bw);
    Zpk digital = bilinearZpk(band, fsDes);
    return zpkToSosSimple(digital);
}

SosMatrix designNotch(double freq, double fs, double q) {
    if (!(fs > 0.0) || !(q > 0.0)) {
        throw std::invalid_argument("designNotch: invalid fs/q");
    }
    // scipy: iirnotch(w0, Q) with w0 = freq/(fs/2) and default fs=2 inside helper.
    double w0 = freq / (fs / 2.0);
    if (!(w0 > 0.0 && w0 < 1.0)) {
        throw std::invalid_argument("designNotch: frequency outside (0, nyquist)");
    }
    w0 = 2.0 * w0 / 2.0;
    double bw = w0 / q;
    bw *= kPi;
    w0 *= kPi;
    const double beta = std::tan(bw / 2.0);
    const double gain = 1.0 / (1.0 + beta);
    const double cosw = std::cos(w0);
    const std::vector<double> b{gain, -2.0 * gain * cosw, gain};
    const std::vector<double> a{1.0, -2.0 * gain * cosw, 2.0 * gain - 1.0};
    return baToOneSection(b, a);
}

std::vector<double> expandNotchFrequencies(
    const std::vector<double>& fundamentals,
    double fs,
    bool includeHarmonics,
    int maxHarmonicMultiple) {
    const double nyquist = fs / 2.0;
    std::vector<double> out;
    for (double fundamental : fundamentals) {
        if (!(fundamental > 0.0) || !(fundamental < nyquist)) {
            continue;
        }
        const int maxMultiple = includeHarmonics ? std::max(1, maxHarmonicMultiple) : 1;
        for (int multiple = 1; multiple <= maxMultiple; ++multiple) {
            const double freq = fundamental * static_cast<double>(multiple);
            if (freq >= nyquist) {
                break;
            }
            out.push_back(freq);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(), [](double a, double b) {
                  return std::abs(a - b) < 1e-9;
              }),
              out.end());
    return out;
}

}  // namespace dsp
}  // namespace bf
