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
    update();
}

void WaveformWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#111827"));
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor("#6EE7B7"), 1));

    const int nChannels = static_cast<int>(slice_.data.size());
    if (nChannels <= 0) {
        return;
    }

    const double rowHeight = static_cast<double>(height()) / nChannels;
    for (int ch = 0; ch < nChannels; ++ch) {
        const std::vector<float>& samples = slice_.data[ch];
        if (samples.size() < 2) {
            continue;
        }

        float maxAbs = 1.0f;
        for (const float value : samples) {
            maxAbs = std::max(maxAbs, std::abs(value));
        }

        const double centerY = rowHeight * (ch + 0.5);
        QPointF previous(0.0, centerY - samples[0] / maxAbs * rowHeight * 0.35);
        for (int i = 1; i < static_cast<int>(samples.size()); ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(samples.size() - 1) * width();
            const double y = centerY - samples[i] / maxAbs * rowHeight * 0.35;
            const QPointF current(x, y);
            painter.drawLine(previous, current);
            previous = current;
        }
    }
}

}  // namespace bf
