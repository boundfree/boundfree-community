#include "ui/waveform_widget.h"

#include "ui/plot_style.h"

#include <QFontMetrics>
#include <QMouseEvent>
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
void WaveformWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void WaveformWidget::setAxisLabels(const QString& timeLabel, const QString& amplitudeLabel) {
    timeAxisLabel_ = timeLabel;
    amplitudeAxisLabel_ = amplitudeLabel;
    update();
}

void WaveformWidget::setNumberLocale(const QLocale& locale) {
    numberLocale_ = locale;
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
    const plot::Palette theme = plot::palette(darkTheme_);
    const QRectF plotArea = plot::plotArea(*this, plot::kWaveformMargins);
    const QFontMetrics metrics(font());
    const int textHeight = metrics.height();

    painter.fillRect(rect(), theme.background);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const WaveformDisplayModel& model = cachedDisplayModel(32);
    const int nChannels = static_cast<int>(model.data.size());
    const double rowHeight = nChannels > 0 ? plotArea.height() / nChannels : 0.0;
    const double span = model.t1 - model.t0;
    const double safeSpan = span > 0.0 && std::isfinite(span) ? span : 1.0;
    const double amplitude = std::max(1.0, model.amplitudeUv);

    // Shared time axis: the endpoints preserve the absolute EEG time of the
    // requested window, while interior ticks use a readable 1/2/5 step.
    const QVector<double> xTicks = plot::ticks(model.t0, model.t1);
    const int xDecimals = plot::tickDecimals(plot::niceTickStep(safeSpan));

    // Inner grid lines (time ticks, per-row zero lines and separators) use a
    // softened grid color; the plot frame uses the solid grid color, matching
    // the spectrum/spectrogram/spike plots. Axes are drawn even when no channel
    // is visible so an empty plot still shows its coordinate system.
    QColor innerGrid = theme.grid;
    innerGrid.setAlpha(70);
    painter.setPen(QPen(innerGrid, 1));
    for (const double tick : xTicks) {
        const double ratio = std::clamp((tick - model.t0) / safeSpan, 0.0, 1.0);
        const double x = plotArea.left() + ratio * plotArea.width();
        painter.drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
    }

    // Row separators and per-row zero lines. The zero line is drawn but not
    // labelled: the amplitude range is shared by every row, so a "0" per row
    // would overlap itself as soon as rows get denser than the text height.
    painter.setPen(QPen(innerGrid, 1));
    for (int ch = 0; ch < nChannels; ++ch) {
        const double rowTop = plotArea.top() + rowHeight * ch;
        const double rowMid = rowTop + rowHeight * 0.5;
        painter.drawLine(QPointF(plotArea.left(), rowMid), QPointF(plotArea.right(), rowMid));
        painter.drawLine(QPointF(plotArea.left(), rowTop + rowHeight), QPointF(plotArea.right(), rowTop + rowHeight));
    }

    painter.setPen(QPen(theme.grid, 1));
    painter.drawRect(plotArea);

    // Left gutter: channel names plus the shared +/- amplitude scale. The two
    // amplitude numbers are clamped inside the plot area so they can never run
    // into the bottom time axis band, and names are thinned by text height once
    // rows get dense instead of piling up on each other. The name column is
    // bounded by the measured width of the amplitude numbers.
    painter.setPen(theme.label);
    const double numberRight = plotArea.left() - 8.0;
    const QString plusText = plot::formatAxisNumber(numberLocale_, amplitude, 0);
    const QString minusText = plot::formatAxisNumber(numberLocale_, -amplitude, 0);
    const double numberColumn = std::max(static_cast<double>(metrics.horizontalAdvance(plusText)),
                                         static_cast<double>(metrics.horizontalAdvance(minusText)));
    const double nameLeft = 28.0;
    const double nameWidth = std::max(24.0, numberRight - numberColumn - 8.0 - nameLeft);

    if (nChannels > 0) {
        const auto drawAmplitudeNumber = [&](double boundaryY, const QString& text) {
            const double centerY = std::clamp(boundaryY,
                                              plotArea.top() + textHeight * 0.5,
                                              plotArea.bottom() - textHeight * 0.5);
            painter.drawLine(QPointF(numberRight, centerY), QPointF(plotArea.left(), centerY));
            painter.drawText(QRectF(nameLeft, centerY - textHeight * 0.5, numberRight - nameLeft, textHeight),
                             Qt::AlignRight | Qt::AlignVCenter,
                             text);
        };
        drawAmplitudeNumber(plotArea.top(), plusText);
        drawAmplitudeNumber(plotArea.bottom(), minusText);

        const bool namesFit = rowHeight >= textHeight;
        const double nameGap = textHeight + 1.0;
        QVector<double> keptNames;
        for (int ch = 0; ch < nChannels; ++ch) {
            const double centerY = plotArea.top() + rowHeight * (ch + 0.5);
            if (!namesFit && !plot::labelFits(keptNames, centerY, nameGap)) {
                continue;
            }
            keptNames.append(centerY);
            const QString name = ch < model.channelNames.size() ? model.channelNames[ch] : QString();
            if (name.isEmpty()) {
                continue;
            }
            painter.drawText(QRectF(nameLeft, centerY - textHeight * 0.5, nameWidth, textHeight),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(name, Qt::ElideRight, static_cast<int>(nameWidth)));
        }
    }

    // Vertical axis title. Channel names remain in the left gutter so they do
    // not cover the signal itself.
    painter.save();
    painter.translate(12.0, plotArea.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-plotArea.height() * 0.5, -8.0, plotArea.height(), 16.0),
                     Qt::AlignCenter, amplitudeAxisLabel_);
    painter.restore();

    // Signal rows: every row shares the toolbar's +/- range and keeps its own
    // zero line. Markers and the playback cursor are clipped to the plot area.
    if (nChannels > 0) {
        for (int ch = 0; ch < nChannels; ++ch) {
            const std::vector<float>& samples = model.data[ch];
            if (samples.size() < 2) {
                continue;
            }

            painter.setPen(QPen(theme.spectrumLine, 1));
            const double centerY = plotArea.top() + rowHeight * (ch + 0.5);
            const double drawWidth = std::max(1.0, plotArea.width());
            const double drawHeight = rowHeight * 0.45;
            const auto pointFor = [&](int sampleIndex) {
                const double ratio = static_cast<double>(sampleIndex) / static_cast<double>(samples.size() - 1);
                const double x = plotArea.left() + ratio * drawWidth;
                const double y = centerY - samples[static_cast<std::size_t>(sampleIndex)] / amplitude * drawHeight;
                return QPointF(x, y);
            };
            QPointF previous = pointFor(0);
            for (int i = 1; i < static_cast<int>(samples.size()); ++i) {
                const QPointF current = pointFor(i);
                painter.drawLine(previous, current);
                previous = current;
            }
        }
        for (const Marker& marker : markers_) {
            if (marker.time < model.t0 || marker.time > model.t1) {
                continue;
            }
            const int x = static_cast<int>(plotArea.left() + plotArea.width() * (marker.time - model.t0) / safeSpan);
            painter.setPen(QPen(marker.color, 1));
            painter.drawLine(x, plotArea.top(), x, plotArea.bottom());
            painter.drawText(x + 3, plotArea.top() + 15, marker.label);
        }
        if (cursorTime_ >= model.t0 && cursorTime_ <= model.t1) {
            const int x = static_cast<int>(plotArea.left() + plotArea.width() * (cursorTime_ - model.t0) / safeSpan);
            painter.setPen(QPen(theme.label, 1));
            painter.drawLine(x, plotArea.top(), x, plotArea.bottom());
        }
    }

    // Bottom time axis is shared by all visible channels. Tick labels are
    // clamped inside the widget (the outermost one would otherwise be clipped
    // at the right edge) and the axis title never overlaps the label band.
    painter.setPen(theme.label);
    for (const double tick : xTicks) {
        const double ratio = std::clamp((tick - model.t0) / safeSpan, 0.0, 1.0);
        const double x = plotArea.left() + ratio * plotArea.width();
        painter.drawLine(QPointF(x, plotArea.bottom()), QPointF(x, plotArea.bottom() + 5.0));
        const QString text = plot::formatAxisNumber(numberLocale_, tick, xDecimals);
        const double labelWidth = std::max(36.0, static_cast<double>(metrics.horizontalAdvance(text) + 6));
        const double labelLeft = std::clamp(x - labelWidth * 0.5, 0.0, std::max(0.0, width() - labelWidth));
        painter.drawText(QRectF(labelLeft, plotArea.bottom() + 7.0, labelWidth, textHeight),
                         Qt::AlignHCenter | Qt::AlignTop, text);
    }
    const double titleTop = std::max(plotArea.bottom() + 7.0 + textHeight + 2.0, height() - textHeight - 2.0);
    painter.drawText(QRectF(plotArea.left(), titleTop, plotArea.width(), std::max(0.0, height() - titleTop)),
                     Qt::AlignHCenter | Qt::AlignTop, timeAxisLabel_);
}

void WaveformWidget::mousePressEvent(QMouseEvent* event) {
    const QRectF plotArea = plot::plotArea(*this, plot::kWaveformMargins);
    if (markerModeEnabled_ && event->button() == Qt::LeftButton && plotArea.contains(event->position()) &&
        !slice_.data.empty() && slice_.t1 > slice_.t0) {
        const double ratio = std::clamp((static_cast<double>(event->position().x()) - plotArea.left()) /
                                           std::max(1.0, plotArea.width()),
                                       0.0,
                                       1.0);
        emit markerRequested(slice_.t0 + ratio * (slice_.t1 - slice_.t0));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

}  // namespace bf
