#pragma once

#include "render/waveform_slice.h"
#include "controller/marker_store.h"

#include <QWidget>

#include <optional>

namespace bf {

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setSlice(const WaveformSlice& slice);
    void setViewState(const WaveformViewState& state);
    void setMarkers(const QVector<Marker>& markers);
    void setCursorTime(double time);
    void setMarkerModeEnabled(bool enabled);
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
    WaveformViewState viewState_;
    mutable std::optional<WaveformDisplayModel> cachedModel_;
    QVector<Marker> markers_;
    double cursorTime_ = 0.0;
    mutable int cachedMaxVisibleChannels_ = -1;
    bool markerModeEnabled_ = false;
    mutable int displayModelBuildCount_ = 0;
};

}  // namespace bf
