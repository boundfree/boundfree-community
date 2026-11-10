#pragma once

// 绘图区共享口径：波形浏览器、频谱、时频等绘图组件共用同一套边距、主题
// 配色与坐标轴刻度算法，避免各组件各自复制常量导致同一窗口内的画面不成套。
//
// 约定：
//   - 所有坐标轴文字（轴标题、刻度数字、通道名）统一使用 Palette::label 单色；
//   - 边框与网格线使用 Palette::grid，需要更淡的内部线条时就地对 grid setAlpha()，
//     不新增配色角色（保持既有绘图组件的做法一致）；
//   - 刻度数值必须按调用方传入的 QLocale 渲染（UI 侧取 I18nService::qlocale()），
//     不使用系统 locale，否则轴数字与状态栏/标注时间等其它数值口径不一致。

#include <QColor>
#include <QLocale>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace bf {
namespace plot {

// 绘图区边距（像素）。
struct Margins {
    int left;
    int top;
    int right;
    int bottom;
};

// 频谱 / 时频图：保持既有画面不变。
inline constexpr Margins kAnalysisMargins{54, 28, 16, 36};

// 波形浏览器：左栏要容纳通道名与 ±幅度刻度数字（超长通道名按可用宽度省略），底部
// 要容纳时间刻度文字与轴标题，因此左/下边距大于分析图；上/右边距与 kAnalysisMargins 对齐。
inline constexpr Margins kWaveformMargins{168, 8, 16, 42};

// 亮 / 暗主题配色。
struct Palette {
    QColor background;
    QColor grid;
    QColor label;
    QColor empty;
    QColor coherenceLine;
    QColor spectrumLine;
};

inline Palette palette(bool dark) {
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

// EEG uses the archived PySide6 theme. The generic palette above stays as a
// reusable base for other plot consumers.
inline Palette eegPalette(bool dark) {
    if (dark) return {QColor("#181828"), QColor("#555577"), QColor("#DDE0FF"),
                      QColor("#AAAAC0"), QColor("#FF5252"), QColor("#00CFFF")};
    return {QColor("#FFFFFF"), QColor("#BBBBCC"), QColor("#333333"),
            QColor("#555555"), QColor("#888888"), QColor("#1565C0")};
}

inline QColor eegChannelColor(int channel, bool dark) {
    static const char* light[] = {"#1565C0", "#2E7D32", "#E65100", "#C62828",
                                  "#6A1B9A", "#00695C", "#4E342E", "#AD1457"};
    static const char* night[] = {"#00CFFF", "#00E676", "#FFD740", "#FF5252",
                                  "#E040FB", "#00E5FF", "#CCFF90", "#FF6E40"};
    const int index = channel < 0 ? 0 : channel % 8;
    const char* color = dark ? night[index] : light[index];
    return QColor(QString::fromLatin1(color));
}

// 浮点绘图区：供按行细分（波形）使用。
inline QRectF plotArea(const QWidget& widget, Margins margins) {
    return QRectF(margins.left,
                  margins.top,
                  std::max(1, widget.width() - margins.left - margins.right),
                  std::max(1, widget.height() - margins.top - margins.bottom));
}

// 整数绘图区：供直接 drawRect（频谱 / 时频）使用。
inline QRect plotRect(const QWidget& widget, Margins margins = kAnalysisMargins) {
    return plotArea(widget, margins).toAlignedRect();
}

// 1/2/5 × 10ⁿ 步长：把 span 分成约 targetTicks 段，保证刻度值是整齐的整数倍。
inline double niceTickStep(double span, int targetTicks = 6) {
    if (!(span > 0.0) || !std::isfinite(span) || targetTicks <= 0) {
        return 1.0;
    }
    const double raw = span / targetTicks;
    const double exponent = std::floor(std::log10(raw));
    const double magnitude = std::pow(10.0, exponent);
    const double normalized = raw / magnitude;
    const double multiple = normalized <= 1.0 ? 1.0 : normalized <= 2.0 ? 2.0 : normalized <= 5.0 ? 5.0 : 10.0;
    return multiple * magnitude;
}

// 刻度文字小数位数：按步长推导并留一位余量，上限 4 位避免超长标签。
inline int tickDecimals(double step) {
    if (!(step > 0.0) || !std::isfinite(step)) {
        return 1;
    }
    return std::clamp(static_cast<int>(std::ceil(-std::log10(step))) + 1, 0, 4);
}

// 刻度序列：首末取窗口绝对时间，中间取步长整数倍；非法输入返回空。
inline QVector<double> ticks(double t0, double t1, int targetTicks = 6) {
    QVector<double> values;
    if (!std::isfinite(t0) || !std::isfinite(t1) || t1 <= t0) {
        return values;
    }

    const double span = t1 - t0;
    const double step = niceTickStep(span, targetTicks);
    const double epsilon = std::max(1e-9, span * 1e-9);
    values.push_back(t0);
    const double firstInterior = std::ceil((t0 + epsilon) / step) * step;
    for (double tick = firstInterior; tick < t1 - epsilon; tick += step) {
        if (std::isfinite(tick)) {
            values.push_back(tick);
        }
        // 防御异常步长在极端时间范围下退化成死循环。
        if (values.size() > 16) {
            break;
        }
    }
    if (std::abs(values.constLast() - t1) > epsilon) {
        values.push_back(t1);
    }
    return values;
}

// 轴数值文本：按传入 locale 渲染，并去掉多余的尾随零。
inline QString formatAxisNumber(const QLocale& locale, double value, int decimals) {
    if (std::abs(value) < std::pow(10.0, -decimals) * 0.5) {
        value = 0.0;
    }
    QString text = locale.toString(value, 'f', decimals);
    if (decimals > 0) {
        const QString decimalPoint = locale.decimalPoint();
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(decimalPoint)) {
            text.chop(decimalPoint.size());
        }
    }
    return text;
}

struct WaveformAmplitudeTicks {
    QString lower, upper, unit;
};

// Scale only the annotation, never signal values. A narrow ripple on a large
// DC offset keeps its precision instead of rounding both limits to one label.
inline WaveformAmplitudeTicks waveformAmplitudeTicks(const QLocale& locale, double low, double high,
                                                      const QString& unit, bool compact) {
    double divisor = 1.0;
    int exponent = 0;
    const double largest = std::max(std::abs(low), std::abs(high));
    const double span = high - low;
    if (compact && std::isfinite(largest) && largest > 0 && span >= largest * .001 &&
        (largest >= 10000 || largest < .01)) {
        exponent = std::clamp(3 * static_cast<int>(std::floor(std::log10(largest) / 3)), -36, 36);
        divisor = std::pow(10., exponent);
    }
    WaveformAmplitudeTicks result;
    result.unit = unit;
    if (exponent) {
        const QString digits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
        QString superscript;
        for (const auto c : QString::number(exponent))
            superscript += c == '-' ? QStringLiteral("⁻") : QString(digits[c.digitValue()]);
        result.unit += QStringLiteral(" ×10") + superscript;
    }
    int decimals = 1;
    if (compact && span > 0 && std::isfinite(span))
        decimals = std::clamp(static_cast<int>(std::ceil(-std::log10(span / divisor / 4))) + 1, 1, 8);
    result.lower = formatAxisNumber(locale, low / divisor, decimals);
    result.upper = formatAxisNumber(locale, high / divisor, decimals);
    if (low != high && result.lower == result.upper) {
        result.lower = locale.toString(low / divisor, 'g', 9);
        result.upper = locale.toString(high / divisor, 'g', 9);
    }
    return result;
}

// 文字标签抽稀：kept 是已放置标签的中心 y，判断候选中心 y 是否满足最小间距。
// 用于多通道密集排布时按行抽稀文字，避免标签互相压盖。
inline bool labelFits(const QVector<double>& kept, double y, double minGap) {
    for (const double taken : kept) {
        if (std::abs(taken - y) < minGap) {
            return false;
        }
    }
    return true;
}

}  // namespace plot
}  // namespace bf
