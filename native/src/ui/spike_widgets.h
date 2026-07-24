#pragma once

#include "dsp/spike_types.h"

#include <QWidget>

namespace bf {

// Four self-drawn (QPainter) Spike views consuming bf::dsp::SpikeAnalysis.
// Consistent with ui/analysis_widgets.{h,cpp}: final QWidget subclasses, no Q_OBJECT,
// setDarkTheme + paintEvent, emptyLabel empty state, kLeft/kTop/... margins and plotRect mapping.
// All time/amplitude mapping is derived from SpikeAnalysis::fs/duration/snippetLen/preMs/postMs;
// sampling rate and duration are never hardcoded.

class SpikeClusterWidget final : public QWidget {
public:
    explicit SpikeClusterWidget(QWidget* parent = nullptr);
    void setAnalysis(const dsp::SpikeAnalysis& analysis);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::SpikeAnalysis analysis_;
    QString emptyLabel_ = "No spike data";
    bool darkTheme_ = false;
};

class SpikeRasterWidget final : public QWidget {
public:
    explicit SpikeRasterWidget(QWidget* parent = nullptr);
    void setAnalysis(const dsp::SpikeAnalysis& analysis);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::SpikeAnalysis analysis_;
    QString emptyLabel_ = "No spike data";
    bool darkTheme_ = false;
};

class SpikePsthWidget final : public QWidget {
public:
    explicit SpikePsthWidget(QWidget* parent = nullptr);
    void setAnalysis(const dsp::SpikeAnalysis& analysis);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::SpikeAnalysis analysis_;
    QString emptyLabel_ = "No spike data";
    bool darkTheme_ = false;
};

class SpikeIsiWidget final : public QWidget {
public:
    explicit SpikeIsiWidget(QWidget* parent = nullptr);
    void setAnalysis(const dsp::SpikeAnalysis& analysis);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::SpikeAnalysis analysis_;
    QString emptyLabel_ = "No spike data";
    bool darkTheme_ = false;
};

}  // namespace bf
