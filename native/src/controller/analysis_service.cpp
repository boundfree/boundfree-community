#include "controller/analysis_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>

namespace bf {
namespace {

AnalysisResult calculate(const AnalysisRequest& request, const std::shared_ptr<std::atomic<int>>& current, int generation) {
    if (request.data.empty() || request.fs <= 0.0) {
        throw std::invalid_argument("analysis.invalid_input");
    }
    const auto cancelled = [&]() { return current->load() != generation; };
    const auto channel = [&](int value) -> const std::vector<double>& {
        if (value < 0 || value >= static_cast<int>(request.data.size())) {
            throw std::invalid_argument("analysis.invalid_channel");
        }
        return request.data[static_cast<std::size_t>(value)];
    };

    AnalysisResult result;
    result.kind = request.kind;
    result.channel = request.channel;
    result.channelB = request.channelB;
    switch (request.kind) {
    case AnalysisKind::Psd:
        result.spectrum = dsp::computeWelchPsd(channel(request.channel), request.fs, request.nperseg);
        result.bandPowers.reserve(dsp::eegBands().size());
        for (const dsp::Band& band : dsp::eegBands()) {
            result.bandPowers.push_back(dsp::bandPower(result.spectrum, band.low, band.high));
        }
        break;
    case AnalysisKind::Fft:
        result.spectrum = dsp::computeFftAmplitude(channel(request.channel), request.fs);
        break;
    case AnalysisKind::Stft: {
        const int requested = request.nperseg > 0 ? request.nperseg : std::max(16, static_cast<int>(request.fs / 2.0));
        const int nperseg = std::min(requested, static_cast<int>(channel(request.channel).size()));
        if (nperseg < 2) {
            throw std::invalid_argument("analysis.invalid_input");
        }
        result.spectrogram = dsp::computeSpectrogram(
            channel(request.channel), request.fs, nperseg, nperseg * 3 / 4, request.tStart, request.fmax);
        break;
    }
    case AnalysisKind::Coherence:
        if (request.channel == request.channelB) {
            throw std::invalid_argument("analysis.same_channel");
        }
        result.spectrum = dsp::computeCoherence(
            channel(request.channel), channel(request.channelB), request.fs, request.nperseg, request.fmax);
        break;
    case AnalysisKind::BandTrend: {
        const std::vector<double>& signal = channel(request.channel);
        const int epochSamples = request.epochSamples > 0 ? request.epochSamples : static_cast<int>(request.fs);
        if (epochSamples < 2) {
            throw std::invalid_argument("analysis.invalid_input");
        }
        result.trend.powers.assign(dsp::eegBands().size(), {});
        for (int start = 0; start + 2 <= static_cast<int>(signal.size()); start += epochSamples) {
            if (cancelled()) {
                return result;
            }
            const int count = std::min(epochSamples, static_cast<int>(signal.size()) - start);
            if (count < 2) {
                break;
            }
            std::vector<double> segment(signal.begin() + start, signal.begin() + start + count);
            const dsp::Spectrum psd = dsp::computeWelchPsd(segment, request.fs);
            result.trend.times.push_back(request.tStart + (start + count / 2.0) / request.fs);
            for (std::size_t band = 0; band < dsp::eegBands().size(); ++band) {
                const dsp::Band& definition = dsp::eegBands()[band];
                result.trend.powers[band].push_back(dsp::bandPower(psd, definition.low, definition.high));
            }
        }
        break;
    }
    }
    return result;
}

}  // namespace

class AnalysisService::State {
public:
    std::shared_ptr<std::atomic<int>> current = std::make_shared<std::atomic<int>>(0);
    std::atomic<int> next{0};
};

AnalysisService::AnalysisService(QObject* parent) : QObject(parent), state_(std::make_shared<State>()) {
    qRegisterMetaType<bf::AnalysisResult>();
}

AnalysisService::~AnalysisService() {
    cancel();
}

int AnalysisService::request(const AnalysisRequest& request) {
    const int generation = state_->next.fetch_add(1) + 1;
    state_->current->store(generation);
    const std::shared_ptr<State> state = state_;
    QPointer<AnalysisService> service(this);
    QThreadPool::globalInstance()->start([service, state, request, generation]() {
        try {
            AnalysisResult result = calculate(request, state->current, generation);
            if (state->current->load() != generation || service.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(service.data(), [service, state, generation, result = std::move(result)]() {
                if (!service.isNull() && state->current->load() == generation) {
                    emit service->completed(generation, result);
                }
            }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            if (state->current->load() != generation || service.isNull()) {
                return;
            }
            const QString detail = QString::fromUtf8(ex.what());
            const QString code = detail.startsWith("analysis.") ? detail : QStringLiteral("analysis.failed");
            QMetaObject::invokeMethod(service.data(), [service, state, generation, code, detail]() {
                if (!service.isNull() && state->current->load() == generation) {
                    emit service->failed(generation, code, detail);
                }
            }, Qt::QueuedConnection);
        }
    });
    return generation;
}

void AnalysisService::cancel() {
    state_->current->store(state_->next.fetch_add(1) + 1);
}

bool AnalysisService::isCurrent(int generation) const {
    return state_->current->load() == generation;
}

}  // namespace bf
