// plot_style self-test.
// Style references collapsible_panel_test.cpp: custom main returning non-zero on failure.
// Runs headless via QT_QPA_PLATFORM=offscreen; needs a QApplication to build a QWidget
// for the plot-area geometry checks.
//
// Covers:
//   (a) margin spec: the documented alignment between analysis and waveform margins
//   (b) plotArea/plotRect geometry, including the degenerate tiny-widget guard
//   (c) niceTickStep 1/2/5 rounding and its invalid-input fallback
//   (d) tickDecimals bounds
//   (e) tick generation: absolute endpoints, 1/2/5 interior steps, degenerate inputs
//   (f) axis number formatting: locale decimal point, trailing-zero trimming, zero snap
//   (g) labelFits minimum-gap predicate used to thin dense row labels

#include "ui/plot_style.h"

#include <QApplication>
#include <QLocale>
#include <QWidget>

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) {
        std::cout << "  ok: " << label << "\n";
    } else {
        std::cerr << "  FAIL: " << label << "\n";
        ++failures;
    }
}

void checkNear(double actual, double expected, double tolerance, const char* label) {
    check(std::abs(actual - expected) <= tolerance, label);
}

const QLocale kEnUs(QLocale::English, QLocale::UnitedStates);
const QLocale kDeDe(QLocale::German, QLocale::Germany);

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // (a) margin spec: waveform/analysis share the top-right-bottom style grid, and
    // the waveform gutter is the wider one because it carries channel names.
    check(bf::plot::kWaveformMargins.right == bf::plot::kAnalysisMargins.right, "waveform right margin == analysis right margin");
    check(bf::plot::kWaveformMargins.left > bf::plot::kAnalysisMargins.left, "waveform left gutter is wider than analysis");
    check(bf::plot::kWaveformMargins.bottom > bf::plot::kAnalysisMargins.bottom, "waveform bottom band fits tick text + axis title");

    // (b) plot geometry.
    QWidget widget;
    widget.resize(640, 320);
    const QRectF waveformArea = bf::plot::plotArea(widget, bf::plot::kWaveformMargins);
    checkNear(waveformArea.left(), bf::plot::kWaveformMargins.left, 0.0, "plotArea left == margins.left");
    checkNear(waveformArea.top(), bf::plot::kWaveformMargins.top, 0.0, "plotArea top == margins.top");
    checkNear(waveformArea.width(),
              640.0 - bf::plot::kWaveformMargins.left - bf::plot::kWaveformMargins.right,
              0.0,
              "plotArea width == w - left - right");
    checkNear(waveformArea.height(),
              320.0 - bf::plot::kWaveformMargins.top - bf::plot::kWaveformMargins.bottom,
              0.0,
              "plotArea height == h - top - bottom");
    const QRect analysisPlot = bf::plot::plotRect(widget);
    check(analysisPlot.left() == 54 && analysisPlot.top() == 28, "plotRect defaults to analysis margins");
    check(analysisPlot.width() == 640 - 54 - 16 && analysisPlot.height() == 320 - 28 - 36, "plotRect keeps analysis width/height");

    QWidget tiny;
    tiny.resize(1, 1);
    const QRectF tinyArea = bf::plot::plotArea(tiny, bf::plot::kWaveformMargins);
    check(tinyArea.width() >= 1.0 && tinyArea.height() >= 1.0, "plotArea stays positive for a degenerate widget");

    // (c) 1/2/5 tick step.
    checkNear(bf::plot::niceTickStep(0.06), 0.01, 1e-12, "niceTickStep(0.06) == 0.01");
    checkNear(bf::plot::niceTickStep(0.12), 0.02, 1e-12, "niceTickStep(0.12) == 0.02");
    checkNear(bf::plot::niceTickStep(1.0), 0.2, 1e-12, "niceTickStep(1.0) == 0.2");
    checkNear(bf::plot::niceTickStep(10.0), 2.0, 1e-12, "niceTickStep(10.0) == 2");
    checkNear(bf::plot::niceTickStep(120.0), 20.0, 1e-12, "niceTickStep(120.0) == 20 (max toolbar window)");
    checkNear(bf::plot::niceTickStep(0.0), 1.0, 0.0, "niceTickStep(0) falls back to 1");
    checkNear(bf::plot::niceTickStep(std::nan("")), 1.0, 0.0, "niceTickStep(NaN) falls back to 1");

    // (d) decimals.
    check(bf::plot::tickDecimals(0.2) == 2, "tickDecimals(0.2) == 2");
    check(bf::plot::tickDecimals(0.01) == 3, "tickDecimals(0.01) == 3");
    check(bf::plot::tickDecimals(0.0) == 1, "tickDecimals(0) falls back to 1");
    check(bf::plot::tickDecimals(-1.0) == 1, "tickDecimals(negative) falls back to 1");
    check(bf::plot::tickDecimals(1e-12) == 4, "tickDecimals clamps to 4");

    // (e) tick generation: endpoints are the absolute window bounds.
    const QVector<double> windowTicks = bf::plot::ticks(12.5, 12.62);
    check(windowTicks.size() == 7, "ticks(12.5, 12.62) yields 7 ticks");
    if (windowTicks.size() == 7) {
        checkNear(windowTicks.first(), 12.5, 1e-9, "first tick == t0");
        checkNear(windowTicks.last(), 12.62, 1e-9, "last tick == t1");
        checkNear(windowTicks[1], 12.52, 1e-9, "interior tick uses the 0.02 step");
    }
    const QVector<double> zeroBased = bf::plot::ticks(0.0, 0.06);
    check(zeroBased.size() == 7, "ticks(0, 0.06) yields 7 ticks");
    if (!zeroBased.isEmpty()) {
        checkNear(zeroBased.first(), 0.0, 1e-12, "ticks start at t0 == 0");
        checkNear(zeroBased.last(), 0.06, 1e-9, "ticks end at t1 == 0.06");
    }
    check(bf::plot::ticks(1.0, 1.0).isEmpty(), "ticks(1, 1) is empty");
    check(bf::plot::ticks(1.0, 0.0).isEmpty(), "ticks(1, 0) is empty");
    check(bf::plot::ticks(std::nan(""), 1.0).isEmpty(), "ticks(NaN, 1) is empty");
    check(bf::plot::ticks(0.0, std::nan("")).isEmpty(), "ticks(0, NaN) is empty");

    // (f) axis number formatting follows the passed locale, not the system locale.
    check(bf::plot::formatAxisNumber(kEnUs, 12.5, 2) == QStringLiteral("12.5"), "en_US 12.5 with 2 decimals trims to \"12.5\"");
    check(bf::plot::formatAxisNumber(kEnUs, 12.0, 4) == QStringLiteral("12"), "en_US 12 with 4 decimals trims to \"12\"");
    check(bf::plot::formatAxisNumber(kEnUs, -200.0, 0) == QStringLiteral("-200"), "en_US -200 with 0 decimals stays \"-200\"");
    check(bf::plot::formatAxisNumber(kEnUs, 0.0, 2) == QStringLiteral("0"), "exact zero renders as \"0\"");
    check(bf::plot::formatAxisNumber(kEnUs, -1e-9, 2) == QStringLiteral("0"), "sub-resolution negative snaps to \"0\"");
    check(bf::plot::formatAxisNumber(kDeDe, 12.5, 2) == QStringLiteral("12,5"), "de_DE uses the locale decimal comma");

    const auto adc = bf::plot::waveformAmplitudeTicks(kEnUs, -9587882.6, 8800994.6, "count", true);
    check(adc.unit == QStringLiteral("count ×10⁶") && adc.lower == "-9.6" && adc.upper == "8.8",
          "large ADC axis has an explicit multiplier and short ticks");
    const auto ripple = bf::plot::waveformAmplitudeTicks(kEnUs, 4999995.4, 5000004.6, "count", true);
    check(ripple.unit == "count" && ripple.lower != ripple.upper &&
          kEnUs.toDouble(ripple.lower) < 5000000 && kEnUs.toDouble(ripple.upper) > 5000000,
          "compact ticks preserve small variation on a large DC baseline");
    const auto volts = bf::plot::waveformAmplitudeTicks(kDeDe, .0008, .0012, "V", true);
    check(volts.unit == QStringLiteral("V ×10⁻³") && volts.lower == "0,8" && volts.upper == "1,2",
          "small voltage ticks preserve units and locale");
    const auto offline = bf::plot::waveformAmplitudeTicks(kEnUs, -9587882.6, 8800994.6, "uV", false);
    check(offline.unit == "uV" && offline.upper == "8,800,994.6", "offline axis format remains unchanged");

    // (g) minimum-gap predicate used to thin dense row labels.
    const QVector<double> placed{16.0, 262.0};
    check(!bf::plot::labelFits(placed, 12.2, 17.0), "label too close to the top boundary label is rejected");
    check(!bf::plot::labelFits(placed, 260.0, 17.0), "label too close to the bottom boundary label is rejected");
    check(bf::plot::labelFits(placed, 37.4, 17.0), "label clear of both boundary labels is accepted");
    check(bf::plot::labelFits({}, 0.0, 17.0), "first label is always accepted");

    if (failures == 0) {
        std::cout << "plot_style_test: PASS\n";
    } else {
        std::cerr << "plot_style_test: FAIL (" << failures << ")\n";
    }
    return failures == 0 ? 0 : 1;
}
