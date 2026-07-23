#pragma once

#include "dsp/spectral.h"

#include <QWidget>

namespace bf {

class SpectrumWidget final : public QWidget {
public:
    explicit SpectrumWidget(QWidget* parent = nullptr);
    void setSpectrum(const dsp::Spectrum& spectrum, const QString& title, const QString& yLabel, bool coherence = false);
    void setBandsVisible(bool visible);
    void setLabels(const QString& xLabel, const QString& emptyLabel);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::Spectrum spectrum_;
    QString title_;
    QString yLabel_;
    QString xLabel_ = "Hz";
    QString emptyLabel_ = "No analysis data";
    bool coherence_ = false;
    bool showBands_ = true;
    bool darkTheme_ = true;
};

class SpectrogramWidget final : public QWidget {
public:
    explicit SpectrogramWidget(QWidget* parent = nullptr);
    void setSpectrogram(const dsp::Spectrogram& spectrogram);
    void setLabels(const QString& title, const QString& xLabel, const QString& yLabel, const QString& emptyLabel);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::Spectrogram spectrogram_;
    QString title_ = "STFT";
    QString xLabel_ = "Time (s)";
    QString yLabel_ = "Frequency (Hz)";
    QString emptyLabel_ = "No analysis data";
    bool darkTheme_ = true;
};

class BandTrendWidget final : public QWidget {
public:
    explicit BandTrendWidget(QWidget* parent = nullptr);
    void setTrend(const std::vector<double>& times, const std::vector<std::vector<double>>& powers);
    void setLabels(const QString& title, const QString& xLabel, const QString& emptyLabel);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<double> times_;
    std::vector<std::vector<double>> powers_;
    QString title_ = "Band power trend";
    QString xLabel_ = "Time (s)";
    QString emptyLabel_ = "No analysis data";
    bool darkTheme_ = true;
};

}  // namespace bf
