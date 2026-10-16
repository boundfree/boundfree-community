#pragma once

#include "render/waveform_slice.h"
#include "controller/marker_store.h"

#include <QLocale>
#include <QWidget>

#include <optional>
#include <utility>

namespace bf {

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setSlice(const WaveformSlice& slice);
    void setTimeRange(double t0, double t1);
    void clearTimeRange();
    std::pair<double, double> timeRange() const;
    void setViewState(const WaveformViewState& state);
    void setMarkers(const QVector<Marker>& markers);
    void setCursorTime(double time);
    void setMarkerModeEnabled(bool enabled);
    void setDarkTheme(bool dark);
    void setAxisLabels(const QString& timeLabel, const QString& amplitudeLabel);
    void setNumberLocale(const QLocale& locale);
    const WaveformDisplayModel& cachedDisplayModel(int maxVisibleChannels = 32) const;
    int displayModelBuildCount() const { return displayModelBuildCount_; }

signals:
    void markerRequested(double time);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void invalidateDisplayModelCache();
    void ensureDisplayModel(int maxVisibleChannels) const;

    WaveformSlice slice_;
    std::optional<std::pair<double, double>> timeRange_;
    WaveformViewState viewState_;
    mutable std::optional<WaveformDisplayModel> cachedModel_;
    QVector<Marker> markers_;
    double cursorTime_ = 0.0;
    QString timeAxisLabel_ = QStringLiteral("Time (s)");
    QString amplitudeAxisLabel_ = QStringLiteral("Amplitude (uV)");
    QLocale numberLocale_;
    mutable int cachedMaxVisibleChannels_ = -1;
    bool markerModeEnabled_ = false;
    bool darkTheme_ = false;
    mutable int displayModelBuildCount_ = 0;
};

}  // namespace bf
