#include "ui/analysis_widgets.h"

#include "dsp/spectral.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bf {
namespace {

constexpr int kLeft = 54;
constexpr int kRight = 16;
constexpr int kTop = 28;
constexpr int kBottom = 36;

QColor bandColor(const dsp::Band& band) {
    return QColor(band.rgba[0], band.rgba[1], band.rgba[2], band.rgba[3]);
}

QColor jet(double normalized) {
    const double x = std::clamp(normalized, 0.0, 1.0);
    const auto curve = [x](double offset) { return std::clamp(1.5 - std::abs(4.0 * x - offset), 0.0, 1.0); };
    return QColor::fromRgbF(curve(3.0), curve(2.0), curve(1.0));
}

QRect plotRect(const QWidget& widget) {
    return widget.rect().adjusted(kLeft, kTop, -kRight, -kBottom);
}

struct ThemePalette {
    QColor background;
    QColor grid;
    QColor label;
    QColor empty;
    QColor coherenceLine;
    QColor spectrumLine;
};

ThemePalette themePalette(bool dark) {
    if (dark) {
        return {
            QColor("#1E1B2E"),
            QColor("#6B7280"),
            QColor("#F5F3FF"),
            QColor("#C4B5FD"),
            QColor("#FBBF24"),
            QColor("#A78BFA"),
        };
    }
    return {
        QColor("#FFFFFF"),
        QColor("#C4B5FD"),
        QColor("#1F1235"),
        QColor("#6B5B95"),
        QColor("#7C3AED"),
        QColor("#6D28D9"),
    };
}

}  // namespace

SpectrumWidget::SpectrumWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 260);
}

void SpectrumWidget::setSpectrum(const dsp::Spectrum& spectrum, const QString& title, const QString& yLabel, bool coherence) {
    spectrum_ = spectrum;
    title_ = title;
    yLabel_ = yLabel;
    coherence_ = coherence;
    update();
}

void SpectrumWidget::setLabels(const QString& xLabel, const QString& emptyLabel) {
    xLabel_ = xLabel;
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
    const ThemePalette t = themePalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect plot = plotRect(*this);
    painter.setPen(QPen(t.grid, 1));
    painter.drawRect(plot);
    painter.setPen(t.label);
    painter.drawText(8, 18, title_);
    painter.save();
    painter.translate(14, height() / 2);
    painter.rotate(-90);
    painter.drawText(QRect(-height() / 2, 0, height(), 18), Qt::AlignCenter, yLabel_);
    painter.restore();
    painter.drawText(QRect(plot.left(), height() - 28, plot.width(), 20), Qt::AlignCenter, xLabel_);
    if (spectrum_.frequencies.empty() || spectrum_.values.empty()) {
        painter.setPen(t.empty);
        painter.drawText(plot, Qt::AlignCenter, emptyLabel_);
        return;
    }
    const double fMax = std::max(1.0, spectrum_.frequencies.back());
    if (showBands_) {
        for (const dsp::Band& band : dsp::eegBands()) {
            const double left = std::clamp(band.low / fMax, 0.0, 1.0);
            const double right = std::clamp(band.high / fMax, 0.0, 1.0);
            painter.fillRect(QRectF(plot.left() + plot.width() * left, plot.top(), plot.width() * (right - left), plot.height()), bandColor(band));
        }
    }
    const auto minmax = std::minmax_element(spectrum_.values.begin(), spectrum_.values.end());
    const double yMin = coherence_ ? 0.0 : *minmax.first;
    const double yMax = coherence_ ? 1.0 : std::max(*minmax.second, yMin + 1e-12);
    if (coherence_) {
        painter.setPen(QPen(t.coherenceLine, 1, Qt::DashLine));
        const double y = plot.bottom() - 0.5 * plot.height();
        painter.drawLine(plot.left(), static_cast<int>(y), plot.right(), static_cast<int>(y));
    }
    QPainterPath path;
    for (std::size_t i = 0; i < spectrum_.values.size(); ++i) {
        const double x = plot.left() + plot.width() * spectrum_.frequencies[i] / fMax;
        const double value = std::clamp(spectrum_.values[i], yMin, yMax);
        const double y = plot.bottom() - plot.height() * (value - yMin) / (yMax - yMin);
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    painter.setPen(QPen(t.spectrumLine, 1.5));
}

SpectrogramWidget::SpectrogramWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 260);
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
    const ThemePalette t = themePalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    const QRect plot = plotRect(*this);
    painter.setPen(QPen(t.grid, 1));
    painter.drawRect(plot);
    painter.setPen(t.label);
    painter.drawText(8, 18, title_);
    painter.drawText(QRect(plot.left(), height() - 28, plot.width(), 20), Qt::AlignCenter, xLabel_);
    painter.save();
    painter.translate(14, height() / 2);
    painter.rotate(-90);
    painter.drawText(QRect(-height() / 2, 0, height(), 18), Qt::AlignCenter, yLabel_);
    painter.restore();
    if (spectrogram_.values.empty() || spectrogram_.times.empty()) {
        painter.setPen(t.empty);
        painter.drawText(plot, Qt::AlignCenter, emptyLabel_);
        return;
    }
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const auto& row : spectrogram_.values) {
        for (double value : row) {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    maximum = std::max(maximum, minimum + 1e-12);
    const int rows = static_cast<int>(spectrogram_.values.size());
    const int cols = static_cast<int>(spectrogram_.times.size());
    const double cellWidth = static_cast<double>(plot.width()) / cols;
    const double cellHeight = static_cast<double>(plot.height()) / rows;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const double ratio = (spectrogram_.values[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] - minimum) / (maximum - minimum);
            painter.fillRect(QRectF(plot.left() + col * cellWidth, plot.bottom() - (row + 1) * cellHeight, cellWidth + 1.0, cellHeight + 1.0), jet(ratio));
        }
    }
    const QRect colorbar(plot.right() + 5, plot.top(), 8, plot.height());
    for (int y = 0; y < colorbar.height(); ++y) {
        painter.setPen(jet(1.0 - static_cast<double>(y) / std::max(1, colorbar.height() - 1)));
        painter.drawLine(colorbar.left(), colorbar.top() + y, colorbar.right(), colorbar.top() + y);
    }
    painter.setPen(t.label);
    const double fmax = std::max(1.0, spectrogram_.frequencies.back());
    for (const dsp::Band& band : dsp::eegBands()) {
        if (band.low <= fmax) {
            const double x = plot.left() + plot.width() * std::min(band.low, fmax) / fmax;
            painter.drawText(static_cast<int>(x) + 2, plot.top() + 14, QString::fromUtf8(band.key));
        }
    }
}

BandTrendWidget::BandTrendWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 260);
}

void BandTrendWidget::setTrend(const std::vector<double>& times, const std::vector<std::vector<double>>& powers) {
    times_ = times;
    powers_ = powers;
    update();
}

void BandTrendWidget::setLabels(const QString& title, const QString& xLabel, const QString& emptyLabel) {
    title_ = title;
    xLabel_ = xLabel;
    emptyLabel_ = emptyLabel;
    update();
}

void BandTrendWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void BandTrendWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette t = themePalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect plot = plotRect(*this);
    painter.setPen(QPen(t.grid, 1));
    painter.drawRect(plot);
    painter.setPen(t.label);
    painter.drawText(8, 18, title_);
    painter.drawText(QRect(plot.left(), height() - 28, plot.width(), 20), Qt::AlignCenter, xLabel_);
    if (times_.empty() || powers_.empty()) {
        painter.setPen(t.empty);
        painter.drawText(plot, Qt::AlignCenter, emptyLabel_);
        return;
    }
    double maxValue = 0.0;
    for (const auto& row : powers_) {
        for (double value : row) {
            maxValue = std::max(maxValue, value);
        }
    }
    maxValue = std::max(maxValue, 1e-12);
    const double minTime = times_.front();
    const double timeSpan = std::max(1e-12, times_.back() - minTime);
    int legendX = plot.left();
    for (std::size_t band = 0; band < powers_.size() && band < dsp::eegBands().size(); ++band) {
        const dsp::Band& definition = dsp::eegBands()[band];
        painter.fillRect(QRect(legendX, plot.top() + 6, 8, 8), QColor(definition.rgba[0], definition.rgba[1], definition.rgba[2]));
        painter.setPen(t.label);
        painter.drawText(legendX + 11, plot.top() + 14, QString::fromUtf8(definition.key));
        legendX += 58;
    }
    for (std::size_t band = 0; band < powers_.size() && band < dsp::eegBands().size(); ++band) {
        const dsp::Band& definition = dsp::eegBands()[band];
        QPainterPath path;
        for (std::size_t i = 0; i < powers_[band].size() && i < times_.size(); ++i) {
            const double x = plot.left() + plot.width() * (times_[i] - minTime) / timeSpan;
            const double y = plot.bottom() - plot.height() * powers_[band][i] / maxValue;
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        painter.setPen(QPen(QColor(definition.rgba[0], definition.rgba[1], definition.rgba[2]), 1.5));
        painter.drawPath(path);
    }
}

}  // namespace bf
