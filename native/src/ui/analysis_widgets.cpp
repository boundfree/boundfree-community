#include "ui/analysis_widgets.h"

#include "dsp/spectral.h"
#include "ui/plot_style.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bf {
namespace {

QColor bandColor(const dsp::Band& band) {
    return QColor(band.rgba[0], band.rgba[1], band.rgba[2], band.rgba[3]);
}

QColor jet(double normalized) {
    const double x = std::clamp(normalized, 0.0, 1.0);
    const auto curve = [x](double offset) { return std::clamp(1.5 - std::abs(4.0 * x - offset), 0.0, 1.0); };
    return QColor::fromRgbF(curve(3.0), curve(2.0), curve(1.0));
}

QString axisNumber(const QLocale& locale, double value, double span) {
    const double magnitude = std::abs(value);
    if (magnitude > 0.0 && (magnitude < 0.001 || magnitude >= 10000.0)) {
        return locale.toString(value, 'g', 2);
    }
    return plot::formatAxisNumber(locale, value, plot::tickDecimals(plot::niceTickStep(span, 4)));
}

// Titles, summaries and legends belong to the page layout. Only axis labels,
// values and the data grid are painted here, matching the old analysis views.
void drawAxes(QPainter& painter, const QRect& area, const plot::Palette& theme,
              const QLocale& locale, const QString& xLabel, const QString& yLabel,
              double xMin, double xMax, double yMin, double yMax, bool grid = true) {
    painter.save();
    QColor gridColor = theme.grid;
    gridColor.setAlpha(75);
    const int xCount = std::max(2, area.width() / 90);
    const int yCount = std::max(2, area.height() / 50);
    for (const double value : plot::ticks(xMin, xMax, xCount)) {
        const double x = area.left() + area.width() * ((value - xMin) / (xMax - xMin));
        if (grid) {
            painter.setPen(QPen(gridColor, 1));
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
        painter.setPen(theme.label);
        painter.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 4));
        // Keep endpoint values inside the widget and prevent wide localized
        // numbers from painting over neighboring pages.
        const QString text = axisNumber(locale, value, xMax - xMin);
        const int width = painter.fontMetrics().horizontalAdvance(text) + 6;
        const double left = std::clamp(x - width / 2.0, 0.0,
                                       static_cast<double>(std::max(0, painter.device()->width() - width)));
        painter.drawText(QRectF(left, area.bottom() + 5, width, 18), Qt::AlignCenter, text);
    }
    QVector<double> labelRows;
    for (const double value : plot::ticks(yMin, yMax, yCount)) {
        const double y = area.bottom() - area.height() * ((value - yMin) / (yMax - yMin));
        if (grid) {
            painter.setPen(QPen(gridColor, 1));
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }
        painter.setPen(theme.label);
        painter.drawLine(QPointF(area.left() - 4, y), QPointF(area.left(), y));
        if (plot::labelFits(labelRows, y, 18)) {
            labelRows.push_back(y);
            const QString text = axisNumber(locale, value, yMax - yMin);
            const QRectF labelRect(22, y - 9, std::max(1, area.left() - 28), 18);
            painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(labelRect.width())));
        }
    }
    painter.setPen(theme.grid);
    painter.drawRect(area);
    painter.setPen(theme.label);
    painter.drawText(QRect(area.left(), painter.device()->height() - 20, area.width(), 18), Qt::AlignCenter, xLabel);
    painter.translate(2, area.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-area.height() / 2, 0, area.height(), 18), Qt::AlignCenter, yLabel);
    painter.restore();
}

void drawEmptyAxes(QPainter& painter, const QRect& area, const plot::Palette& theme,
                   const QLocale& locale, const QString& xLabel, const QString& yLabel,
                   const QString& emptyLabel) {
    drawAxes(painter, area, theme, locale, xLabel, yLabel, 0.0, 1.0, 0.0, 1.0);
    painter.setPen(theme.empty);
    painter.drawText(area.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap, emptyLabel);
}

// DSP returns bin centers. The heatmap and its ticks must use the same
// midpoint boundaries, including unequal spacing. A lone bin has no measured
// spacing, so use a one-unit extent rather than an almost-zero axis range.
std::vector<double> binEdges(const std::vector<double>& centers, std::size_t count, bool nonnegative) {
    if (count == 0 || count > centers.size()) {
        return {};
    }
    std::vector<double> edges(count + 1);
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(centers[i]) || (i > 0 && !(centers[i] > centers[i - 1]))) {
            return {};
        }
    }
    if (count == 1) {
        edges.front() = centers.front() - 0.5;
        edges.back() = centers.front() + 0.5;
    } else {
        for (std::size_t i = 1; i < count; ++i) {
            const double spacing = centers[i] - centers[i - 1];
            if (!std::isfinite(spacing)) {
                return {};
            }
            edges[i] = centers[i - 1] + spacing * 0.5;
        }
        edges.front() = centers.front() - (edges[1] - centers.front());
        edges.back() = centers[count - 1] + (centers[count - 1] - edges[count - 1]);
    }
    if (nonnegative) {
        edges.front() = std::max(0.0, edges.front());
    }
    if (!std::isfinite(edges.front()) || !std::isfinite(edges.back()) ||
        !(edges.back() > edges.front()) || !std::isfinite(edges.back() - edges.front())) {
        return {};
    }
    return edges;
}

bool usableRange(double low, double high) {
    return std::isfinite(low) && std::isfinite(high) && high > low && std::isfinite(high - low);
}

}  // namespace

SpectrumWidget::SpectrumWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(240, 160);
}

void SpectrumWidget::setSpectrum(const dsp::Spectrum& spectrum, const QString& title, const QString& yLabel, bool coherence) {
    spectrum_ = spectrum;
    title_ = title;
    yLabel_ = yLabel;
    coherence_ = coherence;
    update();
}

void SpectrumWidget::setLabels(const QString& xLabel, const QString& emptyLabel) {
    setAxisLabels(xLabel, yLabel_, emptyLabel);
}

void SpectrumWidget::setAxisLabels(const QString& xLabel, const QString& yLabel, const QString& emptyLabel) {
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    emptyLabel_ = emptyLabel;
    update();
}

void SpectrumWidget::setBandsVisible(bool visible) {
    showBands_ = visible;
    update();
}

void SpectrumWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void SpectrumWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const plot::Palette t = plot::eegPalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect area = plot::plotRect(*this, {76, 12, 16, 42});
    const std::size_t count = std::min(spectrum_.frequencies.size(), spectrum_.values.size());
    double fMax = 1.0;
    double yMin = std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(spectrum_.frequencies[i]) || spectrum_.frequencies[i] < 0.0 || !std::isfinite(spectrum_.values[i])) {
            continue;
        }
        fMax = std::max(fMax, spectrum_.frequencies[i]);
        yMin = std::min(yMin, spectrum_.values[i]);
        yMax = std::max(yMax, spectrum_.values[i]);
    }
    if (!std::isfinite(yMin)) {
        drawEmptyAxes(painter, area, t, locale(), xLabel_, yLabel_, emptyLabel_);
        return;
    }
    yMax = coherence_ ? 1.0 : std::max(yMax, yMin + std::max(1e-12, std::abs(yMin) * 1e-12));
    yMin = coherence_ || yMin >= 0.0 ? 0.0 : yMin;
    if (!usableRange(yMin, yMax)) {
        drawEmptyAxes(painter, area, t, locale(), xLabel_, yLabel_, emptyLabel_);
        return;
    }
    // Shade first so grid and tick labels retain contrast in either theme.
    if (showBands_) {
        for (const dsp::Band& band : dsp::eegBands()) {
            const double left = std::clamp(band.low / fMax, 0.0, 1.0);
            const double right = std::clamp(band.high / fMax, 0.0, 1.0);
            painter.fillRect(QRectF(area.left() + area.width() * left, area.top(), area.width() * (right - left), area.height()), bandColor(band));
        }
    }
    drawAxes(painter, area, t, locale(), xLabel_, yLabel_, 0.0, fMax, yMin, yMax);
    painter.save();
    painter.setClipRect(area.adjusted(0, -1, 1, 1));
    if (coherence_) {
        painter.setPen(QPen(t.coherenceLine, 1, Qt::DashLine));
        const double y = area.bottom() - 0.5 * area.height();
        painter.drawLine(area.left(), static_cast<int>(y), area.right(), static_cast<int>(y));
    }
    QPainterPath path;
    bool connected = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(spectrum_.frequencies[i]) || spectrum_.frequencies[i] < 0.0 || !std::isfinite(spectrum_.values[i])) {
            connected = false;
            continue;
        }
        const double x = area.left() + area.width() * (spectrum_.frequencies[i] / fMax);
        const double value = std::clamp(spectrum_.values[i], yMin, yMax);
        const double y = area.bottom() - area.height() * ((value - yMin) / (yMax - yMin));
        if (!connected) {
            path.moveTo(x, y);
            connected = true;
        } else {
            path.lineTo(x, y);
        }
    }
    painter.setPen(QPen(t.spectrumLine, 1.5));
    painter.drawPath(path);
    painter.restore();
}

SpectrogramWidget::SpectrogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(240, 160);
}

void SpectrogramWidget::setSpectrogram(const dsp::Spectrogram& spectrogram) {
    spectrogram_ = spectrogram;
    update();
}

void SpectrogramWidget::setLabels(const QString& title, const QString& xLabel, const QString& yLabel, const QString& emptyLabel) {
    title_ = title;
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    emptyLabel_ = emptyLabel;
    update();
}

void SpectrogramWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void SpectrogramWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const plot::Palette t = plot::eegPalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    // The color scale has its own exterior column, like the old view.
    const QRect area = plot::plotRect(*this, {76, 12, 70, 42});
    const std::size_t rows = std::min(spectrogram_.values.size(), spectrogram_.frequencies.size());
    const std::size_t cols = spectrogram_.times.size();
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t row = 0; row < rows; ++row) {
        if (!std::isfinite(spectrogram_.frequencies[row])) {
            continue;
        }
        for (std::size_t col = 0; col < std::min(cols, spectrogram_.values[row].size()); ++col) {
            const double value = spectrogram_.values[row][col];
            if (std::isfinite(value) && std::isfinite(spectrogram_.times[col])) {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
    }
    const auto timeEdges = binEdges(spectrogram_.times, cols, false);
    const auto frequencyEdges = binEdges(spectrogram_.frequencies, rows, true);
    if (timeEdges.empty() || frequencyEdges.empty() || !std::isfinite(minimum)) {
        drawEmptyAxes(painter, area, t, locale(), xLabel_, yLabel_, emptyLabel_);
        return;
    }
    maximum = std::max(maximum, minimum + std::max(1e-12, std::abs(minimum) * 1e-12));
    const double tMin = timeEdges.front();
    const double tMax = timeEdges.back();
    const double fMin = frequencyEdges.front();
    const double fMax = frequencyEdges.back();
    if (!usableRange(minimum, maximum)) {
        drawEmptyAxes(painter, area, t, locale(), xLabel_, yLabel_, emptyLabel_);
        return;
    }
    painter.save();
    painter.setClipRect(area);
    for (std::size_t row = 0; row < rows; ++row) {
        if (!std::isfinite(spectrogram_.frequencies[row])) {
            continue;
        }
        for (std::size_t col = 0; col < std::min(cols, spectrogram_.values[row].size()); ++col) {
            const double value = spectrogram_.values[row][col];
            if (!std::isfinite(value) || !std::isfinite(spectrogram_.times[col])) {
                continue;
            }
            const double ratio = (value - minimum) / (maximum - minimum);
            const double left = area.left() + area.width() * ((timeEdges[col] - tMin) / (tMax - tMin));
            const double right = area.left() + area.width() * ((timeEdges[col + 1] - tMin) / (tMax - tMin));
            const double top = area.bottom() - area.height() * ((frequencyEdges[row + 1] - fMin) / (fMax - fMin));
            const double bottom = area.bottom() - area.height() * ((frequencyEdges[row] - fMin) / (fMax - fMin));
            painter.fillRect(QRectF(left, top, right - left + 1.0, bottom - top + 1.0), jet(ratio));
        }
    }
    painter.restore();
    drawAxes(painter, area, t, locale(), xLabel_, yLabel_, tMin, tMax, fMin, fMax, false);
    const QRect colorbar(area.right() + 8, area.top(), 10, area.height());
    for (int y = 0; y < colorbar.height(); ++y) {
        painter.setPen(jet(1.0 - static_cast<double>(y) / std::max(1, colorbar.height() - 1)));
        painter.drawLine(colorbar.left(), colorbar.top() + y, colorbar.right(), colorbar.top() + y);
    }
    painter.setPen(t.label);
    painter.drawText(QRect(colorbar.right() + 4, colorbar.top() - 4, 46, 18),
                     Qt::AlignLeft | Qt::AlignVCenter, axisNumber(locale(), maximum, maximum - minimum));
    painter.drawText(QRect(colorbar.right() + 4, colorbar.bottom() - 14, 46, 18),
                     Qt::AlignLeft | Qt::AlignVCenter, axisNumber(locale(), minimum, maximum - minimum));
    painter.drawText(QRect(colorbar.left() - 4, height() - 20, 40, 18), Qt::AlignCenter, QStringLiteral("dB"));
}

BandTrendWidget::BandTrendWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(240, 160);
}

void BandTrendWidget::setTrend(const std::vector<double>& times, const std::vector<std::vector<double>>& powers) {
    times_ = times;
    powers_ = powers;
    update();
}

void BandTrendWidget::setLabels(const QString& title, const QString& xLabel, const QString& emptyLabel) {
    title_ = title;
    setAxisLabels(xLabel, yLabel_, emptyLabel);
}

void BandTrendWidget::setAxisLabels(const QString& xLabel, const QString& yLabel, const QString& emptyLabel) {
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    emptyLabel_ = emptyLabel;
    update();
}

void BandTrendWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void BandTrendWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const plot::Palette t = plot::eegPalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect area = plot::plotRect(*this, {76, 12, 16, 42});
    if (times_.empty() || !std::isfinite(times_.front()) || !std::isfinite(times_.back())) {
        drawEmptyAxes(painter, area, t, locale(), xLabel_, yLabel_, emptyLabel_);
        return;
    }
    double maxValue = 0.0;
    bool hasData = false;
    const std::size_t bands = std::min(powers_.size(), dsp::eegBands().size());
    for (std::size_t band = 0; band < bands; ++band) {
        for (std::size_t i = 0; i < std::min(powers_[band].size(), times_.size()); ++i) {
            if (std::isfinite(powers_[band][i]) && std::isfinite(times_[i])) {
                maxValue = std::max(maxValue, powers_[band][i]);
                hasData = true;
            }
        }
    }
    maxValue = std::max(maxValue, 1e-12);
    const double minTime = times_.size() == 1 ? times_.front() - 0.5 : times_.front();
    const double maxTime = times_.size() == 1 ? times_.front() + 0.5 : times_.back();
    if (!hasData || !usableRange(minTime, maxTime)) {
        drawEmptyAxes(painter, area, t, locale(), xLabel_, yLabel_, emptyLabel_);
        return;
    }
    drawAxes(painter, area, t, locale(), xLabel_, yLabel_, minTime, maxTime, 0.0, maxValue);
    painter.save();
    painter.setClipRect(area.adjusted(0, -1, 1, 1));
    for (std::size_t band = 0; band < bands; ++band) {
        const dsp::Band& definition = dsp::eegBands()[band];
        QPainterPath path;
        std::vector<QPointF> points;
        bool connected = false;
        for (std::size_t i = 0; i < powers_[band].size() && i < times_.size(); ++i) {
            if (!std::isfinite(times_[i]) || !std::isfinite(powers_[band][i]) ||
                times_[i] < minTime || times_[i] > maxTime) {
                connected = false;
                continue;
            }
            const double x = area.left() + area.width() * ((times_[i] - minTime) / (maxTime - minTime));
            const double y = area.bottom() - area.height() * (std::clamp(powers_[band][i], 0.0, maxValue) / maxValue);
            points.emplace_back(x, y);
            if (!connected) {
                path.moveTo(x, y);
                connected = true;
            } else {
                path.lineTo(x, y);
            }
        }
        painter.setPen(QPen(QColor(definition.rgba[0], definition.rgba[1], definition.rgba[2]), 1.5));
        painter.drawPath(path);
        if (points.size() == 1) {
            painter.setBrush(QColor(definition.rgba[0], definition.rgba[1], definition.rgba[2]));
            painter.drawEllipse(points.front(), 2.5, 2.5);
            painter.setBrush(Qt::NoBrush);
        }
    }
    painter.restore();
}

}  // namespace bf
