#include "ui/waveform_widget.h"

#include "ui/plot_style.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <limits>

#include <algorithm>
#include <cmath>

namespace bf {

WaveformWidget::WaveformWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(640, 320);
    channelScroll_ = new QScrollBar(Qt::Vertical, this);
    channelScroll_->setObjectName("eegWaveformChannelScroll");
    channelScroll_->setSingleStep(110);
    channelScroll_->setRange(0, 0);
    channelScroll_->hide();
    connect(channelScroll_, &QScrollBar::valueChanged, this, [this]() { update(); });
}

QRectF WaveformWidget::waveformPlotArea() const {
    QRectF area = plot::plotArea(*this, plot::kWaveformMargins);
    if (channelScroll_->maximum() > 0) area.setRight(area.right() - channelScroll_->sizeHint().width());
    return area;
}

void WaveformWidget::setAutoScaleEnabled(bool enabled) {
    if (autoScale_ == enabled) return;
    autoScale_ = enabled;
    amplitudeRangesDirty_ = true;
    update();
}

void WaveformWidget::updateChannelScroll() {
    int count = 0;
    for (int ch = std::max(0, viewState_.firstVisibleChannel); ch < static_cast<int>(slice_.data.size()); ++ch)
        if (viewState_.isChannelVisible(ch)) ++count;
    const QRectF area = plot::plotArea(*this, plot::kWaveformMargins);
    const int available = std::max(1, static_cast<int>(area.height()));
    channelScroll_->setRange(0, std::max(0, count * 110 - available));
    channelScroll_->setPageStep(available);
    channelScroll_->setVisible(channelScroll_->maximum() > 0);
    const int scrollWidth = channelScroll_->sizeHint().width();
    channelScroll_->setGeometry(width() - scrollWidth, static_cast<int>(area.top()), scrollWidth, available);
}

void WaveformWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateChannelScroll();
}

void WaveformWidget::wheelEvent(QWheelEvent* event) {
    if (viewInteractions_ && waveformPlotArea().contains(event->position()) &&
        (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        double steps = event->angleDelta().y() / 120.0;
        if (!steps && event->pixelDelta().y()) steps = event->pixelDelta().y() > 0 ? 1. : -1.;
        steps = std::clamp(steps, -4., 4.);
        setFocus(Qt::MouseFocusReason);
        if (steps) {
            if (event->modifiers() & Qt::ControlModifier)
                emit timeZoomRequested(steps > 0 ? static_cast<int>(std::ceil(steps)) : static_cast<int>(std::floor(steps)),
                    std::clamp((event->position().x() - waveformPlotArea().left()) / waveformPlotArea().width(), 0., 1.));
            else setAmplitudeZoom(amplitudeZoom_ * std::pow(1.25, steps));
        }
        event->accept(); return;
    }
    if (channelScroll_->maximum() <= 0) { QWidget::wheelEvent(event); return; }
    const int delta = !event->pixelDelta().isNull() ? event->pixelDelta().y() : event->angleDelta().y() * 110 / 120;
    channelScroll_->setValue(channelScroll_->value() - delta);
    event->accept();
}

void WaveformWidget::ensureAmplitudeRanges(int requestedChannel) const {
    if (!autoScale_) return;
    const auto& model = cachedDisplayModel(0);
    const auto [start, end] = timeRange();
    const int count = static_cast<int>(model.data.size());
    const bool reset = amplitudeRangesDirty_ || amplitudeRanges_.size() != count;
    if (reset || !autoScaleClock_.isValid() || autoScaleClock_.elapsed() >= 2000) {
        amplitudeRanges_.resize(count);
        amplitudeRangeValid_.fill(false, count);
        amplitudeRangesDirty_ = false;
        autoScaleClock_.restart();
    }
    const double rowHeight = count ? std::max(110.0, waveformPlotArea().height() / count) : 110.0;
    const int first = requestedChannel >= 0 ? requestedChannel : static_cast<int>(channelScroll_->value() / rowHeight);
    const int last = requestedChannel >= 0 ? requestedChannel + 1 : static_cast<int>(std::ceil((channelScroll_->value() + waveformPlotArea().height()) / rowHeight));
    bool computed = false;
    for (int ch = std::max(0, first); ch < std::min(last, count); ++ch) {
        if (amplitudeRangeValid_[ch]) continue;
        amplitudeRangeValid_[ch] = true;
        computed = true;
        const auto& values = model.data[ch];
        double low = std::numeric_limits<double>::infinity(), high = -low;
        if (ch < static_cast<int>(model.sampleTimes.size()) && model.sampleTimes[ch].size() == values.size()) {
            for (std::size_t i = 0; i < values.size(); ++i) if (model.sampleTimes[ch][i] >= start &&
                model.sampleTimes[ch][i] <= end && std::isfinite(values[i])) {
                low = std::min(low, static_cast<double>(values[i])); high = std::max(high, static_cast<double>(values[i]));
            }
        } else if (model.sampleTimes.empty() && model.fs > 0 && std::isfinite(model.fs)) {
            const auto first = static_cast<std::size_t>(std::clamp(std::ceil((start - model.t0) * model.fs), 0.0, static_cast<double>(values.size())));
            const auto last = static_cast<std::size_t>(std::clamp(std::ceil((end - model.t0) * model.fs), 0.0, static_cast<double>(values.size())));
            for (auto i = first; i < last; ++i) if (std::isfinite(values[i])) {
                low = std::min(low, static_cast<double>(values[i]));
                high = std::max(high, static_cast<double>(values[i]));
            }
        }
        const double mid = std::isfinite(low) ? (low + high) * .5 : 0.0;
        // Live ADC data may have a million-count DC offset with only a few
        // counts of variation. Base the range on that variation, never on a
        // percentage of the offset. Only guard numeric precision/one count.
        const QString unit = model.channelUnits.value(ch, model.unit);
        const double liveFloor = unit == "count" ? 1.0 :
            std::max(1e-12, std::abs(mid) * std::numeric_limits<float>::epsilon());
        const double floor = model.sampleTimes.empty() ? 5.0 : liveFloor;
        const double half = std::isfinite(low) ? std::max((high - low) * .575, floor) : floor;
        const std::pair<double, double> next{mid - half, mid + half};
        auto& old = amplitudeRanges_[ch];
        const double span = std::max(2.0 * floor, old.second - old.first);
        if (reset || std::abs(next.first - old.first) / span > .05 || std::abs(next.second - old.second) / span > .05) old = next;
    }
    if (computed) ++autoScaleBuildCount_;
}

std::pair<double, double> WaveformWidget::channelAmplitudeRange(int displayChannel) const {
    if (autoScale_) ensureAmplitudeRanges(displayChannel);
    const auto range = !autoScale_ ? std::make_pair(-std::max(1e-9, viewState_.amplitudeUv), std::max(1e-9, viewState_.amplitudeUv)) :
        displayChannel >= 0 && displayChannel < amplitudeRanges_.size() ? amplitudeRanges_[displayChannel] : std::make_pair(-5.0, 5.0);
    const double middle = (range.first + range.second) * .5, half = (range.second - range.first) * .5 / amplitudeZoom_;
    return {middle - half, middle + half};
}

void WaveformWidget::setViewInteractionsEnabled(bool enabled) {
    viewInteractions_ = enabled; setFocusPolicy(enabled ? Qt::StrongFocus : Qt::NoFocus);
    if (!enabled) { setViewPanningEnabled(false); setAmplitudeZoom(1); }
}
void WaveformWidget::setViewPanningEnabled(bool enabled) {
    enabled = enabled && viewInteractions_;
    if (viewPanning_ == enabled) return;
    viewPanning_ = enabled; draggingView_ = false;
    setCursor(viewPanning_ ? Qt::OpenHandCursor : markerModeEnabled_ ? Qt::CrossCursor : Qt::ArrowCursor);
}
void WaveformWidget::setAmplitudeZoom(double zoom) {
    if (!std::isfinite(zoom)) return;
    zoom = std::clamp(zoom, .25, 16.);
    if (std::abs(zoom - 1) < 1e-9) zoom = 1;
    if (amplitudeZoom_ == zoom) return;
    amplitudeZoom_ = zoom; update(); emit amplitudeZoomChanged(zoom);
}

void WaveformWidget::setSlice(const WaveformSlice& slice) {
    const bool sameLiveLayout = !slice.sampleTimes.empty() && !slice_.sampleTimes.empty() &&
        slice.channelNames == slice_.channelNames && slice.channelUnits == slice_.channelUnits;
    slice_ = slice;
    invalidateDisplayModelCache();
    if (!sameLiveLayout) amplitudeRangesDirty_ = true;
    updateChannelScroll();
    update();
}

void WaveformWidget::setTimeRange(double t0, double t1) {
    if (!std::isfinite(t0) || !std::isfinite(t1) || t1 <= t0) return;
    const auto old = timeRange();
    if (std::abs((t1 - t0) - (old.second - old.first)) > 1e-6) amplitudeRangesDirty_ = true;
    timeRange_ = std::make_pair(t0, t1);
    update();
}

void WaveformWidget::clearTimeRange() {
    timeRange_.reset();
    amplitudeRangesDirty_ = true;
    update();
}

std::pair<double, double> WaveformWidget::timeRange() const {
    return timeRange_.value_or(std::make_pair(slice_.t0, slice_.t1));
}

void WaveformWidget::setViewState(const WaveformViewState& state) {
    viewState_ = state;
    invalidateDisplayModelCache();
    amplitudeRangesDirty_ = true;
    updateChannelScroll();
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
    if (markerModeEnabled_ == enabled) return;
    markerModeEnabled_ = enabled;
    setCursor(markerModeEnabled_ ? Qt::CrossCursor : viewPanning_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
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
    if (maxVisibleChannels >= static_cast<int>(slice_.data.size())) maxVisibleChannels = 0;
    if (cachedModel_.has_value() && cachedMaxVisibleChannels_ == maxVisibleChannels) {
        return;
    }
    cachedModel_ = waveformDisplayModel(slice_, viewState_, maxVisibleChannels);
    cachedMaxVisibleChannels_ = maxVisibleChannels;
    ++displayModelBuildCount_;
}

void WaveformWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const plot::Palette theme = plot::eegPalette(darkTheme_);
    const QRectF plotArea = waveformPlotArea();
    const QFontMetrics metrics(font());
    const int textHeight = metrics.height();

    painter.fillRect(rect(), theme.background);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const WaveformDisplayModel& model = cachedDisplayModel(0);
    const int nChannels = static_cast<int>(model.data.size());
    const double rowHeight = nChannels > 0 ? std::max(110.0, plotArea.height() / nChannels) : 0.0;
    const double scrollOffset = channelScroll_->value();
    const int firstRow = rowHeight > 0 ? static_cast<int>(scrollOffset / rowHeight) : 0;
    const int lastRow = rowHeight > 0 ? std::min(nChannels, static_cast<int>(std::ceil((scrollOffset + plotArea.height()) / rowHeight))) : 0;
    ensureAmplitudeRanges();
    const auto [visibleStart, visibleEnd] = timeRange();
    const double span = visibleEnd - visibleStart;
    const double safeSpan = span > 0.0 && std::isfinite(span) ? span : 1.0;

    // Shared time axis: the endpoints preserve the absolute EEG time of the
    // requested window, while interior ticks use a readable 1/2/5 step.
    const QVector<double> xTicks = plot::ticks(visibleStart, visibleEnd);
    const int xDecimals = plot::tickDecimals(plot::niceTickStep(safeSpan));

    // Inner grid lines (time ticks, per-row zero lines and separators) use a
    // softened grid color; the plot frame uses the solid grid color, matching
    // the spectrum/spectrogram plots. Axes are drawn even when no channel
    // is visible so an empty plot still shows its coordinate system.
    QColor innerGrid = theme.grid;
    innerGrid.setAlpha(70);
    painter.setPen(QPen(innerGrid, 1));
    for (const double tick : xTicks) {
        const double ratio = std::clamp((tick - visibleStart) / safeSpan, 0.0, 1.0);
        const double x = plotArea.left() + ratio * plotArea.width();
        painter.drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
    }

    painter.save();
    painter.setClipRect(plotArea);
    // Each row has a center line; in automatic mode it follows the data offset.
    painter.setPen(QPen(innerGrid, 1));
    for (int ch = firstRow; ch < lastRow; ++ch) {
        const double rowTop = plotArea.top() + rowHeight * ch - scrollOffset;
        const double rowMid = rowTop + rowHeight * 0.5;
        painter.drawLine(QPointF(plotArea.left(), rowMid), QPointF(plotArea.right(), rowMid));
        painter.drawLine(QPointF(plotArea.left(), rowTop + rowHeight), QPointF(plotArea.right(), rowTop + rowHeight));
    }

    painter.restore();
    painter.setPen(QPen(theme.grid, 1));
    painter.drawRect(plotArea);

    // Independent channel names and range limits stay outside the waveform.
    painter.setPen(theme.label);
    const double numberRight = plotArea.left() - 8.0;
    const double nameLeft = 28.0;
    const double nameWidth = std::max(24.0, numberRight - nameLeft - 60.0);
    painter.save();
    painter.setClipRect(QRectF(0, plotArea.top(), plotArea.left(), plotArea.height()));
    for (int ch = firstRow; ch < lastRow; ++ch) {
        const double rowTop = plotArea.top() + rowHeight * ch - scrollOffset;
        const auto range = channelAmplitudeRange(ch);
        const auto annotation = plot::waveformAmplitudeTicks(numberLocale_, range.first, range.second,
            model.channelUnits.value(ch, model.unit), !model.sampleTimes.empty());
        const QString name = ch < model.channelNames.size() ? model.channelNames[ch] : QString();
        painter.drawText(QRectF(nameLeft, rowTop + rowHeight / 2 - textHeight / 2, nameWidth, textHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(name, Qt::ElideRight, static_cast<int>(nameWidth)));
        if (!model.channelUnits.isEmpty())
            painter.drawText(QRectF(nameLeft, rowTop + rowHeight / 2 + textHeight / 2, numberRight - nameLeft, textHeight),
                Qt::AlignLeft | Qt::AlignVCenter, annotation.unit);
        for (const auto& tick : {std::make_pair(annotation.upper, rowTop + 4.0), std::make_pair(annotation.lower, rowTop + rowHeight - textHeight - 4.0)}) {
            painter.drawText(QRectF(nameLeft, tick.second, numberRight - nameLeft, textHeight), Qt::AlignRight | Qt::AlignVCenter,
                             tick.first);
        }
    }
    painter.restore();

    // Vertical axis title. Channel names remain in the left gutter so they do
    // not cover the signal itself.
    painter.save();
    painter.translate(12.0, plotArea.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRectF(-plotArea.height() * 0.5, -8.0, plotArea.height(), 16.0),
                     Qt::AlignCenter, amplitudeAxisLabel_);
    painter.restore();

    // Signal rows use independent ranges in automatic mode and the shared
    // manual range otherwise. Each trace is clipped to its own row.
    painter.save();
    painter.setClipRect(plotArea);
    if (nChannels > 0) {
        for (int ch = firstRow; ch < lastRow; ++ch) {
            const std::vector<float>& samples = model.data[ch];
            const auto* times = ch < static_cast<int>(model.sampleTimes.size()) ? &model.sampleTimes[ch] : nullptr;
            const bool explicitTimes = times && times->size() == samples.size();
            if (samples.size() < 2 || (!model.sampleTimes.empty() && !explicitTimes) ||
                (!explicitTimes && (!std::isfinite(model.fs) || model.fs <= 0.0))) {
                continue;
            }

            // Preserve source/output index colors when hiding or scrolling rows.
            int colorChannel = std::max(0, viewState_.firstVisibleChannel);
            int remaining = ch;
            for (; colorChannel < static_cast<int>(slice_.data.size()); ++colorChannel) {
                if (!viewState_.isChannelVisible(colorChannel)) continue;
                if (remaining-- == 0) break;
            }
            painter.setPen(QPen(plot::eegChannelColor(colorChannel, darkTheme_), 1));
            const double centerY = plotArea.top() + rowHeight * (ch + 0.5) - scrollOffset;
            const double drawWidth = std::max(1.0, plotArea.width());
            const double drawHeight = rowHeight * 0.45;
            const auto range = channelAmplitudeRange(ch);
            const double centerValue = (range.first + range.second) * .5;
            const double halfRange = std::max(explicitTimes ? 1e-12 : 1.0, (range.second - range.first) * .5);
            const auto pointFor = [&](int sampleIndex) {
                const double sampleTime = explicitTimes ? (*times)[sampleIndex] : model.t0 + static_cast<double>(sampleIndex) / model.fs;
                const double ratio = (sampleTime - visibleStart) / safeSpan;
                const double x = plotArea.left() + ratio * drawWidth;
                const double y = centerY - (samples[static_cast<std::size_t>(sampleIndex)] - centerValue) / halfRange * drawHeight;
                return QPointF(x, y);
            };
            // Include one sample outside each edge to retain crossing segments.
            const int lastIndex = static_cast<int>(samples.size()) - 1;
            const int first = explicitTimes ? std::clamp(static_cast<int>(std::lower_bound(times->begin(), times->end(), visibleStart) - times->begin()) - 1, 0, lastIndex) :
                static_cast<int>(std::clamp(std::floor((visibleStart - model.t0) * model.fs), 0.0, static_cast<double>(lastIndex)));
            const int last = explicitTimes ? std::clamp(static_cast<int>(std::upper_bound(times->begin(), times->end(), visibleEnd) - times->begin()), 0, lastIndex) :
                static_cast<int>(std::clamp(std::ceil((visibleEnd - model.t0) * model.fs), 0.0, static_cast<double>(lastIndex)));
            painter.save();
            painter.setClipRect(QRectF(plotArea.left(), centerY - rowHeight * .5, plotArea.width(), rowHeight), Qt::IntersectClip);
            QPointF previous = pointFor(first);
            for (int i = first + 1; i <= last; ++i) {
                const QPointF current = pointFor(i);
                if (std::isfinite(previous.y()) && std::isfinite(current.y())) painter.drawLine(previous, current);
                previous = current;
            }
            painter.restore();
        }
        for (const Marker& marker : markers_) {
            if (marker.time < visibleStart || marker.time > visibleEnd) {
                continue;
            }
            const int x = static_cast<int>(plotArea.left() + plotArea.width() * (marker.time - visibleStart) / safeSpan);
            painter.setPen(QPen(marker.color, 1));
            painter.drawLine(x, plotArea.top(), x, plotArea.bottom());
            painter.drawText(x + 3, plotArea.top() + 15, marker.label);
        }
        if (cursorTime_ >= visibleStart && cursorTime_ <= visibleEnd) {
            const int x = static_cast<int>(plotArea.left() + plotArea.width() * (cursorTime_ - visibleStart) / safeSpan);
            painter.setPen(QPen(theme.label, 1));
            painter.drawLine(x, plotArea.top(), x, plotArea.bottom());
        }
    }

    painter.restore();

    // Bottom time axis is shared by all visible channels. Tick labels are
    // clamped inside the widget (the outermost one would otherwise be clipped
    // at the right edge) and the axis title never overlaps the label band.
    painter.setPen(theme.label);
    const auto timeLabelRect = [&](double tick) {
        const double ratio = std::clamp((tick - visibleStart) / safeSpan, 0.0, 1.0);
        const double x = plotArea.left() + ratio * plotArea.width();
        const QString text = plot::formatAxisNumber(numberLocale_, tick, xDecimals);
        const double labelWidth = std::max(36.0, static_cast<double>(metrics.horizontalAdvance(text) + 6));
        const double labelLeft = std::clamp(x - labelWidth * 0.5, 0.0, std::max(0.0, width() - labelWidth));
        return QRectF(labelLeft, plotArea.bottom() + 7.0, labelWidth, textHeight);
    };
    double previousLabelRight = -std::numeric_limits<double>::infinity();
    const double lastLabelLeft = xTicks.isEmpty() ? 0.0 : timeLabelRect(xTicks.back()).left();
    for (int index = 0; index < xTicks.size(); ++index) {
        const double tick = xTicks[index];
        const double ratio = std::clamp((tick - visibleStart) / safeSpan, 0.0, 1.0);
        const double x = plotArea.left() + ratio * plotArea.width();
        painter.drawLine(QPointF(x, plotArea.bottom()), QPointF(x, plotArea.bottom() + 5.0));
        const auto label = timeLabelRect(tick);
        // Preserve endpoint times while thinning labels that collide with them.
        if (label.left() < previousLabelRight + 4.0 ||
            (index > 0 && index + 1 < xTicks.size() && label.right() + 4.0 > lastLabelLeft))
            continue;
        painter.drawText(label, Qt::AlignHCenter | Qt::AlignTop,
                         plot::formatAxisNumber(numberLocale_, tick, xDecimals));
        previousLabelRight = label.right();
    }
    const double titleTop = std::max(plotArea.bottom() + 7.0 + textHeight + 2.0, height() - textHeight - 2.0);
    painter.drawText(QRectF(plotArea.left(), titleTop, plotArea.width(), std::max(0.0, height() - titleTop)),
                     Qt::AlignHCenter | Qt::AlignTop, timeAxisLabel_);
}

void WaveformWidget::mousePressEvent(QMouseEvent* event) {
    const QRectF plotArea = waveformPlotArea();
    const auto [visibleStart, visibleEnd] = timeRange();
    if (viewInteractions_ && event->button() == Qt::LeftButton && plotArea.contains(event->position())) {
        setFocus(Qt::MouseFocusReason);
        if (viewPanning_) {
            draggingView_ = true; lastDragPosition_ = event->position(); setCursor(Qt::ClosedHandCursor);
            event->accept(); return;
        }
    }
    if (markerModeEnabled_ && event->button() == Qt::LeftButton && plotArea.contains(event->position()) &&
        !slice_.data.empty() && visibleEnd > visibleStart) {
        const double ratio = std::clamp((static_cast<double>(event->position().x()) - plotArea.left()) /
                                           std::max(1.0, plotArea.width()),
                                       0.0,
                                       1.0);
        emit markerRequested(visibleStart + ratio * (visibleEnd - visibleStart));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event) {
    if (draggingView_ && viewPanning_) {
        const auto range = timeRange();
        const double seconds = (event->position().x() - lastDragPosition_.x()) / waveformPlotArea().width() * (range.second - range.first);
        lastDragPosition_ = event->position(); emit viewPanRequested(seconds); event->accept(); return;
    }
    QWidget::mouseMoveEvent(event);
}
void WaveformWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (draggingView_ && event->button() == Qt::LeftButton) {
        draggingView_ = false; setCursor(viewPanning_ ? Qt::OpenHandCursor : Qt::ArrowCursor); event->accept(); return;
    }
    QWidget::mouseReleaseEvent(event);
}
void WaveformWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (viewInteractions_ && event->button() == Qt::LeftButton && waveformPlotArea().contains(event->position())) {
        draggingView_ = false; setCursor(viewPanning_ ? Qt::OpenHandCursor : Qt::ArrowCursor);
        emit resetViewRequested(); event->accept(); return;
    }
    QWidget::mouseDoubleClickEvent(event);
}
void WaveformWidget::keyPressEvent(QKeyEvent* event) {
    if (viewInteractions_) {
        switch (event->key()) {
        case Qt::Key_Space: if (!event->isAutoRepeat()) emit pauseViewRequested(); break;
        case Qt::Key_Plus: case Qt::Key_Equal: emit timeZoomRequested(1, 1.); break;
        case Qt::Key_Minus: emit timeZoomRequested(-1, 1.); break;
        case Qt::Key_0: emit resetViewRequested(); break;
        case Qt::Key_Left: if (!viewPanning_) { QWidget::keyPressEvent(event); return; } emit viewPanRequested((timeRange().second - timeRange().first) * .2); break;
        case Qt::Key_Right: if (!viewPanning_) { QWidget::keyPressEvent(event); return; } emit viewPanRequested(-(timeRange().second - timeRange().first) * .2); break;
        default: QWidget::keyPressEvent(event); return;
        }
        event->accept(); return;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace bf
