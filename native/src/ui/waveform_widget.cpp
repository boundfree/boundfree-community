#include "ui/waveform_widget.h"

#include <QPainter>

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
}

}  // namespace bf
