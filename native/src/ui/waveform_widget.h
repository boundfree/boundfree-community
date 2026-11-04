#pragma once

#include "render/waveform_slice.h"
#include "controller/marker_store.h"

#include <QLocale>
#include <QWidget>
#include <QScrollBar>
#include <QElapsedTimer>

#include <optional>
#include <utility>

namespace bf {

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setAutoScaleEnabled(bool enabled);
    bool autoScaleEnabled() const { return autoScale_; }
    std::pair<double, double> channelAmplitudeRange(int displayChannel) const;
    int autoScaleBuildCount() const { return autoScaleBuildCount_; }
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
    void setViewInteractionsEnabled(bool enabled);
    void setViewPanningEnabled(bool enabled);
    void setAmplitudeZoom(double zoom);
    double amplitudeZoom() const { return amplitudeZoom_; }
    const WaveformDisplayModel& cachedDisplayModel(int maxVisibleChannels = 0) const;
    int displayModelBuildCount() const { return displayModelBuildCount_; }

signals:
    void markerRequested(double time);
    void timeZoomRequested(int steps, double anchorRatio);
    void viewPanRequested(double seconds);
    void pauseViewRequested();
    void resetViewRequested();
    void amplitudeZoomChanged(double zoom);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void updateChannelScroll();
    void ensureAmplitudeRanges(int requestedChannel = -1) const;
    QRectF waveformPlotArea() const;
    void invalidateDisplayModelCache();
    void ensureDisplayModel(int maxVisibleChannels) const;

    QScrollBar* channelScroll_ = nullptr;
    bool autoScale_ = true;
    mutable QElapsedTimer autoScaleClock_;
    mutable bool amplitudeRangesDirty_ = true;
    mutable QVector<std::pair<double, double>> amplitudeRanges_;
    mutable QVector<bool> amplitudeRangeValid_;
    mutable int autoScaleBuildCount_ = 0;
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
    bool viewInteractions_ = false, viewPanning_ = false, draggingView_ = false;
    QPointF lastDragPosition_;
    double amplitudeZoom_ = 1.0;
    mutable int displayModelBuildCount_ = 0;
};

}  // namespace bf
