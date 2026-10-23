#pragma once

#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

namespace bf::separation {
using Matrix = std::vector<std::vector<double>>;
using Cancel = std::function<bool()>;
using Progress = std::function<void(int)>;
enum class Failure {
    InvalidInput, InvalidRange, MissingContext, InputTooLarge, FlatChannel,
    LowRank, NotConverged, NonFinite, Reconstruction, Cancelled
};
const char* failureCode(Failure reason) noexcept;
class Error : public std::runtime_error {
public:
    explicit Error(Failure reason, double diagnosticErrorUv = std::numeric_limits<double>::quiet_NaN())
        : std::runtime_error(failureCode(reason)), reason(reason), diagnosticErrorUv(diagnosticErrorUv) {}
    Failure reason;
    double diagnosticErrorUv;
};
namespace ica {
struct Options {
    double sampleRate = 500.0;
    int notchHz = 0;
    unsigned seed = 42;
    int maxIterations = 600;
    double tolerance = 1e-6;
};
struct Result {
    Matrix analysis;          // uV after optional notch, without output high-pass
    Matrix sources;           // arbitrary units, not uV
    Matrix mixing;            // observations x rank
    Matrix unmixing;          // rank x observations
    Matrix residual;          // uV, without the channel mean
    std::vector<double> mean; // uV, retained once for the reconstruction identity
    int rank = 0;
    int iterations = 0;
    double convergence = 0.0;
};
Result fit(const Matrix& input, const Options& options, Cancel cancel = {}, Progress progress = {});
// Labels are 0 EEG candidate, 1 EOG estimate, 2 EMG contribution, 3 unresolved.
Matrix project(const Result& result, const std::vector<int>& groups, int group, Cancel cancel = {});
double correlation(const std::vector<double>& x, const std::vector<double>& y);
// Zero-phase Butterworth, applied only to fitting/scoring copies.
std::vector<double> highpass(const std::vector<double>& x, double sampleRate, double cutoff);
} // namespace ica
} // namespace bf::separation
