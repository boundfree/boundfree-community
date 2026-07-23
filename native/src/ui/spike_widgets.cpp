#include "ui/spike_widgets.h"

#include <QPainter>
#include <QPainterPath>
#include <QRect>

#include <algorithm>
#include <cmath>
#include <vector>

namespace bf {
namespace {

// 与 analysis_widgets.cpp 保持一致的边距/主题配色。
constexpr int kLeft = 54;
constexpr int kRight = 16;
constexpr int kTop = 28;
constexpr int kBottom = 36;

// PSTH / ISI 默认 bin（可调，内部常量；bin 数由 duration / 范围推导，禁止硬编码时长）。
constexpr double kPsthBinMs = 50.0;
constexpr double kIsiBinMs = 1.0;
constexpr double kIsiRangeMs = 50.0;
constexpr double kRefractoryMs = 1.0;  // 不应期违例高亮上限

struct ThemePalette {
    QColor background;
    QColor grid;
    QColor label;
    QColor empty;
};

ThemePalette themePalette(bool dark) {
    if (dark) {
        return {
            QColor("#111827"),
            QColor("#64748B"),
            QColor("#CBD5E1"),
            QColor("#94A3B8"),
        };
    }
    return {
        QColor("#FFFFFF"),
        QColor("#94A3B8"),
        QColor("#0F172A"),
        QColor("#64748B"),
    };
}

QRect plotRect(const QWidget& widget) {
    return widget.rect().adjusted(kLeft, kTop, -kRight, -kBottom);
}

// 稳定的簇颜色轮换：按 cluster.id 取色，确保跨重绘/重排颜色一致。
QColor clusterColor(int id) {
    static const QColor palette[] = {
        QColor("#60A5FA"),  // blue
        QColor("#F87171"),  // red
        QColor("#34D399"),  // green
        QColor("#FBBF24"),  // amber
        QColor("#A78BFA"),  // violet
        QColor("#22D3EE"),  // cyan
        QColor("#FB923C"),  // orange
        QColor("#E879F9"),  // pink
    };
    constexpr int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    return palette[((id % n) + n) % n];
}

void drawEmpty(QPainter& painter, const ThemePalette& t, const QRect& plot, const QString& title, const QString& emptyLabel) {
    painter.setPen(QPen(t.grid, 1));
    painter.drawRect(plot);
    painter.setPen(t.label);
    painter.drawText(8, 18, title);
    painter.setPen(t.empty);
    painter.drawText(plot, Qt::AlignCenter, emptyLabel);
}

}  // namespace

// ---------------------------------------------------------------------------
// SpikeClusterWidget
// ---------------------------------------------------------------------------

SpikeClusterWidget::SpikeClusterWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 260);
}

void SpikeClusterWidget::setAnalysis(const dsp::SpikeAnalysis& analysis) {
    analysis_ = analysis;
    update();
}

void SpikeClusterWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void SpikeClusterWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette t = themePalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect plot = plotRect(*this);

    const QString title = "Spike waveforms";
    const QString xLabel = "Time (ms)";
    const QString yLabel = "Amplitude (uV)";

    if (analysis_.clusters.empty() || analysis_.snippetLen <= 0) {
        drawEmpty(painter, t, plot, title, emptyLabel_);
        return;
    }

    // 波形快照时间轴：preMs .. preMs + postMs，由 snippetLen 样本 / fs 映射。
    // 禁止硬编码采样率：从 analysis_.fs 推导 ms/样本。
    const double fs = analysis_.fs > 0 ? analysis_.fs : 1.0;
    const double preMs = analysis_.preMs;
    const double postMs = analysis_.postMs;
    const int snippetLen = analysis_.snippetLen;
    const double totalMs = (snippetLen > 0) ? (snippetLen / fs * 1000.0) : (preMs + postMs);
    // 以 preMs..preMs+postMs 为轴，若 snippet 与窗不一致则按 snippet 实际长度。
    const double tStartMs = (snippetLen > 0) ? 0.0 : preMs;  // 快照索引 0 对齐到 0，更直观
    const double tSpanMs = std::max(1e-9, totalMs);

    // Y 自适应：扫描所有簇所有样本。
    double yMin = std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const auto& c : analysis_.clusters) {
        for (const auto& w : c.waveforms) {
            for (double v : w) {
                yMin = std::min(yMin, v);
                yMax = std::max(yMax, v);
            }
        }
        for (double v : c.meanWaveform) {
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
        }
    }
    if (!std::isfinite(yMin) || !std::isfinite(yMax)) {
        drawEmpty(painter, t, plot, title, emptyLabel_);
        return;
    }
    // 对称留白。
    if (yMin > 0) yMin = 0.0;
    if (yMax < 0) yMax = 0.0;
    double pad = (yMax - yMin) * 0.1;
    if (pad < 1e-9) pad = 1.0;
    yMin -= pad;
    yMax += pad;

    // 轴。
    painter.setPen(QPen(t.grid, 1));
    painter.drawRect(plot);
    painter.setPen(t.label);
    painter.drawText(8, 18, title);
    painter.save();
    painter.translate(14, plot.top() + plot.height() / 2);
    painter.rotate(-90);
    painter.drawText(QRect(-plot.height() / 2, 0, plot.height(), 18), Qt::AlignCenter, yLabel);
    painter.restore();
    painter.drawText(QRect(plot.left(), height() - 28, plot.width(), 20), Qt::AlignCenter, xLabel);

    const auto xMs = [&](int i) -> double {
        // 样本 i -> ms，索引 0 对齐 tStartMs。
        return tStartMs + (snippetLen > 0 ? (i / fs * 1000.0) : (i * (tSpanMs / std::max(1, snippetLen))));
    };
    const auto mapX = [&](int i) -> double {
        return plot.left() + plot.width() * (xMs(i) - tStartMs) / tSpanMs;
    };
    const auto mapY = [&](double v) -> double {
        return plot.bottom() - plot.height() * (v - yMin) / (yMax - yMin);
    };

    // 0 基线。
    painter.setPen(QPen(t.grid, 1, Qt::DashLine));
    const double y0 = mapY(0.0);
    painter.drawLine(QPointF(plot.left(), y0), QPointF(plot.right(), y0));

    for (const auto& cluster : analysis_.clusters) {
        const QColor color = clusterColor(cluster.id);
        // 半透明细线：每条 waveform。
        QColor faint = color; faint.setAlpha(90);
        painter.setPen(QPen(faint, 1.0));
        for (const auto& w : cluster.waveforms) {
            QPainterPath path;
            for (int i = 0; i < static_cast<int>(w.size()) && i < snippetLen; ++i) {
                const double x = mapX(i);
                const double y = mapY(w[i]);
                if (i == 0) path.moveTo(x, y);
                else path.lineTo(x, y);
            }
            painter.drawPath(path);
        }
        // meanWaveform：粗线。
        painter.setPen(QPen(color, 2.0));
        QPainterPath meanPath;
        for (int i = 0; i < static_cast<int>(cluster.meanWaveform.size()) && i < snippetLen; ++i) {
            const double x = mapX(i);
            const double y = mapY(cluster.meanWaveform[i]);
            if (i == 0) meanPath.moveTo(x, y);
            else meanPath.lineTo(x, y);
        }
        painter.drawPath(meanPath);
    }
}

// ---------------------------------------------------------------------------
// SpikeRasterWidget
// ---------------------------------------------------------------------------

SpikeRasterWidget::SpikeRasterWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 260);
}

void SpikeRasterWidget::setAnalysis(const dsp::SpikeAnalysis& analysis) {
    analysis_ = analysis;
    update();
}

void SpikeRasterWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void SpikeRasterWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette t = themePalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect plot = plotRect(*this);

    const QString title = "Spike raster";
    const QString xLabel = "Time (s)";
    const QString yLabel = "Cluster";

    if (analysis_.clusters.empty()) {
        drawEmpty(painter, t, plot, title, emptyLabel_);
        return;
    }
    const double duration = analysis_.duration > 0 ? analysis_.duration : 1.0;
    // 没有真实 duration 时从 timestamps 末尾兜底（不硬编码时长）。
    double tmax = duration;
    for (const auto& c : analysis_.clusters) {
        if (!c.timestamps.empty()) tmax = std::max(tmax, c.timestamps.back());
    }
    tmax = std::max(tmax, 1e-9);

    const int nClusters = static_cast<int>(analysis_.clusters.size());
    const double rowH = plot.height() / static_cast<double>(nClusters);

    painter.setPen(QPen(t.grid, 1));
    painter.drawRect(plot);
    painter.setPen(t.label);
    painter.drawText(8, 18, title);
    painter.save();
    painter.translate(14, plot.top() + plot.height() / 2);
    painter.rotate(-90);
    painter.drawText(QRect(-plot.height() / 2, 0, plot.height(), 18), Qt::AlignCenter, yLabel);
    painter.restore();
    painter.drawText(QRect(plot.left(), height() - 28, plot.width(), 20), Qt::AlignCenter, xLabel);

    const auto mapX = [&](double ts) -> double {
        return plot.left() + plot.width() * (ts / tmax);
    };

    for (int ci = 0; ci < nClusters; ++ci) {
        const auto& cluster = analysis_.clusters[ci];
        const QColor color = clusterColor(cluster.id);
        // 行中线。
        const double rowMid = plot.top() + rowH * (ci + 0.5);
        const double tickHalf = std::min(rowH * 0.4, 8.0);

        // 行分隔（除最后一行）。
        if (ci > 0) {
            painter.setPen(QPen(t.grid, 1, Qt::DotLine));
            painter.drawLine(QPointF(plot.left(), plot.top() + rowH * ci), QPointF(plot.right(), plot.top() + rowH * ci));
        }

        // 簇 id 标签（左侧 plot 外）。
        painter.setPen(t.label);
        painter.drawText(QRect(0, static_cast<int>(plot.top() + rowH * ci), kLeft - 4, static_cast<int>(rowH)),
                         Qt::AlignRight | Qt::AlignVCenter, QString::number(cluster.id));

        // tick。
        painter.setPen(QPen(color, 1));
        for (double ts : cluster.timestamps) {
            const double x = mapX(ts);
            painter.drawLine(QPointF(x, rowMid - tickHalf), QPointF(x, rowMid + tickHalf));
        }
    }

    // X 轴刻度。
    painter.setPen(t.label);
    painter.setPen(QPen(t.grid, 1));
    for (int i = 0; i <= 5; ++i) {
        const double x = plot.left() + plot.width() * i / 5.0;
        painter.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
        painter.drawText(QRectF(x - 30, plot.bottom() + 5, 60, 16), Qt::AlignCenter,
                         QString::number(tmax * i / 5.0, 'f', 2));
    }
}

// ---------------------------------------------------------------------------
// SpikePsthWidget
// ---------------------------------------------------------------------------

SpikePsthWidget::SpikePsthWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 260);
}

void SpikePsthWidget::setAnalysis(const dsp::SpikeAnalysis& analysis) {
    analysis_ = analysis;
    update();
}

void SpikePsthWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void SpikePsthWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette t = themePalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect plot = plotRect(*this);

    const QString title = "PSTH";
    const QString xLabel = "Time (s)";
    const QString yLabel = "Rate (Hz)";

    if (analysis_.clusters.empty()) {
        drawEmpty(painter, t, plot, title, emptyLabel_);
        return;
    }
    const double duration = analysis_.duration > 0 ? analysis_.duration : 1.0;
    double tmax = duration;
    for (const auto& c : analysis_.clusters) {
        if (!c.timestamps.empty()) tmax = std::max(tmax, c.timestamps.back());
    }
    tmax = std::max(tmax, 1e-9);

    // bin 数由 duration / tmax 与 kPsthBinMs 推导（不硬编码 bin 数）。
    const double binSec = kPsthBinMs / 1000.0;
    const int nBins = std::max(1, static_cast<int>(std::ceil(tmax / binSec)));
    const double effSpan = nBins * binSec;

    // 每簇 per-bin 计数 + 总发放率（Hz = count / binSec）。
    std::vector<std::vector<int>> counts(analysis_.clusters.size(), std::vector<int>(nBins, 0));
    double maxRate = 0.0;
    for (std::size_t ci = 0; ci < analysis_.clusters.size(); ++ci) {
        for (double ts : analysis_.clusters[ci].timestamps) {
            int b = static_cast<int>(ts / binSec);
            if (b < 0) b = 0;
            if (b >= nBins) b = nBins - 1;
            counts[ci][b]++;
        }
        for (int c : counts[ci]) {
            maxRate = std::max(maxRate, c / binSec);
        }
    }
    maxRate = std::max(maxRate, 1e-9);

    painter.setPen(QPen(t.grid, 1));
    painter.drawRect(plot);
    painter.setPen(t.label);
    painter.drawText(8, 18, title);
    painter.save();
    painter.translate(14, plot.top() + plot.height() / 2);
    painter.rotate(-90);
    painter.drawText(QRect(-plot.height() / 2, 0, plot.height(), 18), Qt::AlignCenter, yLabel);
    painter.restore();
    painter.drawText(QRect(plot.left(), height() - 28, plot.width(), 20), Qt::AlignCenter, xLabel);

    const double barW = plot.width() / static_cast<double>(nBins);
    for (std::size_t ci = 0; ci < analysis_.clusters.size(); ++ci) {
        const QColor color = clusterColor(analysis_.clusters[ci].id);
        QColor fill = color; fill.setAlpha(160);
        painter.setBrush(fill);
        painter.setPen(QPen(color, 1));
        for (int b = 0; b < nBins; ++b) {
            const double rate = counts[ci][b] / binSec;
            const double h = plot.height() * (rate / maxRate);
            const double x = plot.left() + b * barW;
            // 多簇叠加：每簇画一次（半透明），可叠加观察。
            painter.drawRect(QRectF(x, plot.bottom() - h, std::max(1.0, barW - 0.5), h));
        }
    }

    // X 轴刻度。
    painter.setPen(QPen(t.grid, 1));
    painter.setBrush(Qt::NoBrush);
    for (int i = 0; i <= 5; ++i) {
        const double x = plot.left() + plot.width() * i / 5.0;
        painter.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
        painter.setPen(t.label);
        painter.drawText(QRectF(x - 30, plot.bottom() + 5, 60, 16), Qt::AlignCenter,
                         QString::number(effSpan * i / 5.0, 'f', 2));
        painter.setPen(QPen(t.grid, 1));
    }
}

// ---------------------------------------------------------------------------
// SpikeIsiWidget
// ---------------------------------------------------------------------------

SpikeIsiWidget::SpikeIsiWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 260);
}

void SpikeIsiWidget::setAnalysis(const dsp::SpikeAnalysis& analysis) {
    analysis_ = analysis;
    update();
}

void SpikeIsiWidget::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    update();
}

void SpikeIsiWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const ThemePalette t = themePalette(darkTheme_);
    painter.fillRect(rect(), t.background);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect plot = plotRect(*this);

    const QString title = "ISI histogram";
    const QString xLabel = "ISI (ms)";
    const QString yLabel = "Count";

    if (analysis_.clusters.empty()) {
        drawEmpty(painter, t, plot, title, emptyLabel_);
        return;
    }

    // bin 数由 kIsiRangeMs / kIsiBinMs 推导。
    const int nBins = std::max(1, static_cast<int>(std::ceil(kIsiRangeMs / kIsiBinMs)));
    std::vector<std::vector<int>> counts(analysis_.clusters.size(), std::vector<int>(nBins, 0));
    int maxCount = 0;
    for (std::size_t ci = 0; ci < analysis_.clusters.size(); ++ci) {
        const auto& ts = analysis_.clusters[ci].timestamps;
        for (std::size_t i = 1; i < ts.size(); ++i) {
            // timestamps 单位秒 -> ISI ms。
            double isiMs = (ts[i] - ts[i - 1]) * 1000.0;
            if (isiMs < 0) isiMs = 0;
            int b = static_cast<int>(isiMs / kIsiBinMs);
            if (b < 0) b = 0;
            if (b >= nBins) b = nBins - 1;
            counts[ci][b]++;
            maxCount = std::max(maxCount, counts[ci][b]);
        }
    }
    maxCount = std::max(maxCount, 1);

    painter.setPen(QPen(t.grid, 1));
    painter.drawRect(plot);
    painter.setPen(t.label);
    painter.drawText(8, 18, title);
    painter.save();
    painter.translate(14, plot.top() + plot.height() / 2);
    painter.rotate(-90);
    painter.drawText(QRect(-plot.height() / 2, 0, plot.height(), 18), Qt::AlignCenter, yLabel);
    painter.restore();
    painter.drawText(QRect(plot.left(), height() - 28, plot.width(), 20), Qt::AlignCenter, xLabel);

    const double barW = plot.width() / static_cast<double>(nBins);

    // 不应期违例区高亮 (< kRefractoryMs)。
    const int refractoryBins = std::max(1, static_cast<int>(std::ceil(kRefractoryMs / kIsiBinMs)));
    painter.setBrush(QColor(248, 113, 113, 40));  // 浅红
    painter.setPen(Qt::NoPen);
    painter.drawRect(QRectF(plot.left(), plot.top(), barW * refractoryBins, plot.height()));

    for (std::size_t ci = 0; ci < analysis_.clusters.size(); ++ci) {
        const QColor color = clusterColor(analysis_.clusters[ci].id);
        QColor fill = color; fill.setAlpha(170);
        painter.setBrush(fill);
        painter.setPen(QPen(color, 1));
        for (int b = 0; b < nBins; ++b) {
            const double h = plot.height() * (counts[ci][b] / static_cast<double>(maxCount));
            const double x = plot.left() + b * barW;
            painter.drawRect(QRectF(x, plot.bottom() - h, std::max(1.0, barW - 0.5), h));
        }
    }

    // X 轴刻度。
    painter.setPen(QPen(t.grid, 1));
    painter.setBrush(Qt::NoBrush);
    for (int i = 0; i <= 5; ++i) {
        const double x = plot.left() + plot.width() * i / 5.0;
        painter.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 4));
        painter.setPen(t.label);
        painter.drawText(QRectF(x - 30, plot.bottom() + 5, 60, 16), Qt::AlignCenter,
                         QString::number(kIsiRangeMs * i / 5.0, 'f', 0));
        painter.setPen(QPen(t.grid, 1));
    }
}

}  // namespace bf
