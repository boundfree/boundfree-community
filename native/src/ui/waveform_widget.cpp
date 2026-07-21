#include "ui/waveform_widget.h"

#include <QPainter>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace bf {

WaveformWidget::WaveformWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(640, 320);
}

void WaveformWidget::setSlice(const WaveformSlice& slice) {
    slice_ = slice;
    invalidateDisplayModelCache();
    update();
}

void WaveformWidget::setViewState(const WaveformViewState& state) {
    viewState_ = state;
    invalidateDisplayModelCache();
    update();
}

void WaveformWidget::setMarkers(const QVector<Marker>& markers) {
    markers_ = markers;
    update();
}

void WaveformWidget::setCursorTime(double time) {
    cursorTime_ = time;
    update();
}

void WaveformWidget::setMarkerModeEnabled(bool enabled) {
    markerModeEnabled_ = enabled;
    setCursor(markerModeEnabled_ ? Qt::CrossCursor : Qt::ArrowCursor);
}

const WaveformDisplayModel& WaveformWidget::cachedDisplayModel(int maxVisibleChannels) const {
    ensureDisplayModel(maxVisibleChannels);
    return *cachedModel_;
}

void WaveformWidget::invalidateDisplayModelCache() {
    cachedModel_.reset();
    cachedMaxVisibleChannels_ = -1;
}

void WaveformWidget::ensureDisplayModel(int maxVisibleChannels) const {
    if (cachedModel_.has_value() && cachedMaxVisibleChannels_ == maxVisibleChannels) {
        return;
    }
    cachedModel_ = waveformDisplayModel(slice_, viewState_, maxVisibleChannels);
    cachedMaxVisibleChannels_ = maxVisibleChannels;
    ++displayModelBuildCount_;
}

void WaveformWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#111827"));
    painter.setRenderHint(QPainter::Antialiasing, false);

    const WaveformDisplayModel& model = cachedDisplayModel(32);
    const int nChannels = static_cast<int>(model.data.size());
    if (nChannels <= 0) {
        return;
    }

    const double rowHeight = static_cast<double>(height()) / nChannels;
    for (int ch = 0; ch < nChannels; ++ch) {
        const std::vector<float>& samples = model.data[ch];
        if (samples.size() < 2) {
            continue;
        }

        painter.setPen(QPen(QColor("#CBD5E1"), 1));
        if (ch < model.channelNames.size()) {
            painter.drawText(8, static_cast<int>(rowHeight * ch + 16), model.channelNames[ch]);
        }

        painter.setPen(QPen(QColor("#6EE7B7"), 1));
        const double amplitude = std::max(1.0, model.amplitudeUv);
        const double centerY = rowHeight * (ch + 0.5);
        QPointF previous(0.0, centerY - samples[0] / amplitude * rowHeight * 0.45);
        for (int i = 1; i < static_cast<int>(samples.size()); ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(samples.size() - 1) * width();
            const double y = centerY - samples[i] / amplitude * rowHeight * 0.45;
            const QPointF current(x, y);
            painter.drawLine(previous, current);
            previous = current;
        }
    }
    const double span = std::max(1e-12, model.t1 - model.t0);
    for (const Marker& marker : markers_) {
        if (marker.time < model.t0 || marker.time > model.t1) {
            continue;
        }
        const int x = static_cast<int>(width() * (marker.time - model.t0) / span);
        painter.setPen(QPen(marker.color, 1));
        painter.drawLine(x, 0, x, height());
        painter.drawText(x + 3, 15, marker.label);
    }
    if (cursorTime_ >= model.t0 && cursorTime_ <= model.t1) {
        const int x = static_cast<int>(width() * (cursorTime_ - model.t0) / span);
        painter.setPen(QPen(QColor("#F8FAFC"), 1));
        painter.drawLine(x, 0, x, height());
    }
}

void WaveformWidget::mousePressEvent(QMouseEvent* event) {
    if (markerModeEnabled_ && event->button() == Qt::LeftButton && !slice_.data.empty() && slice_.t1 > slice_.t0) {
        const double ratio = std::clamp(static_cast<double>(event->position().x()) / std::max(1, width()), 0.0, 1.0);
        emit markerRequested(slice_.t0 + ratio * (slice_.t1 - slice_.t0));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

}  // namespace bf
