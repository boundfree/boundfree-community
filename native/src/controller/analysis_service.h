#pragma once

#include "dsp/spectral.h"
#include "dsp/types.h"

#include <QObject>
#include <QString>
#include <memory>

namespace bf {

enum class AnalysisKind {
    Fft,
    Psd,
    Stft,
    BandTrend,
    Coherence,
};

struct AnalysisRequest {
    AnalysisKind kind = AnalysisKind::Psd;
    dsp::Matrix data;
    double fs = 0.0;
    int channel = 0;
    int channelB = 1;
    double tStart = 0.0;
    double fmax = 100.0;
    int nperseg = 0;
    int epochSamples = 0;
};

struct BandTrendResult {
    std::vector<double> times;
    std::vector<std::vector<double>> powers;
};

struct AnalysisResult {
    AnalysisKind kind = AnalysisKind::Psd;
    dsp::Spectrum spectrum;
    dsp::Spectrogram spectrogram;
    BandTrendResult trend;
    int channel = 0;
    int channelB = 0;
};

class AnalysisService final : public QObject {
    Q_OBJECT

public:
    explicit AnalysisService(QObject* parent = nullptr);
    ~AnalysisService() override;

    int request(const AnalysisRequest& request);
    void cancel();
    bool isCurrent(int generation) const;

signals:
    void completed(int generation, const bf::AnalysisResult& result);
    void failed(int generation, const QString& code, const QString& detail);

private:
    class State;
    std::shared_ptr<State> state_;
};

}  // namespace bf

Q_DECLARE_METATYPE(bf::AnalysisResult)
