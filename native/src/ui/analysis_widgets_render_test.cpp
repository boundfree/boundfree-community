#include "ui/analysis_widgets.h"
#include "ui/plot_style.h"

#include <QApplication>
#include <QImage>

#include <iostream>
#include <limits>

namespace {
QImage render(QWidget& widget) {
    QImage result(widget.size(), QImage::Format_ARGB32);
    result.fill(Qt::transparent);
    widget.render(&result);
    return result;
}

int changes(const QImage& a, const QImage& b, const QRect& area) {
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            count += a.pixel(x, y) != b.pixel(x, y);
        }
    }
    return count;
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bool passed = true;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const bool dark : {false, true}) {
        bf::SpectrumWidget spectrum;
        bf::SpectrogramWidget spectrogram;
        bf::BandTrendWidget trend;
        for (QWidget* widget : {static_cast<QWidget*>(&spectrum), static_cast<QWidget*>(&spectrogram), static_cast<QWidget*>(&trend)}) {
            widget->resize(600, 300);
            widget->setLocale(QLocale(QLocale::English));
        }
        spectrum.setDarkTheme(dark);
        spectrogram.setDarkTheme(dark);
        trend.setDarkTheme(dark);
        const bf::dsp::Spectrum signal{{0.0, 0.5, 1.0}, {0.1, 0.7, 0.2}};
        spectrum.setSpectrum(signal, "A title", "Power");
        const QImage spectrumA = render(spectrum);
        spectrum.setSpectrum(signal, "A much longer different title", "Power");
        passed &= expect(render(spectrum) == spectrumA, "Spectrum title leaked into plot");
        spectrum.setAxisLabels("Frequency", "Changed axis", "empty");
        passed &= expect(changes(spectrumA, render(spectrum), QRect(0, 12, 21, 246)) > 10,
                         "Spectrum y-axis translation did not update without new data");
        spectrum.setAxisLabels("Hz", "Power", "empty");
        const QImage english = render(spectrum);
        spectrum.setLocale(QLocale(QLocale::German));
        const QImage german = render(spectrum);
        passed &= expect(changes(english, german, QRect(24, 12, 50, 247)) > 0,
                         "Numeric y-axis did not follow widget locale");

        const bf::dsp::Spectrogram heatmap{{0.0, 10.0, 20.0}, {0.0, 0.5, 1.0},
                                          {{-30.0, -20.0, -10.0}, {-15.0, -5.0, 0.0}, {-25.0, -15.0, -5.0}}};
        spectrogram.setSpectrogram(heatmap);
        spectrogram.setLabels("STFT one", "Time", "Frequency", "empty");
        const QImage stftA = render(spectrogram);
        spectrogram.setLabels("STFT title two", "Time", "Frequency", "empty");
        passed &= expect(render(spectrogram) == stftA, "STFT title leaked into plot");
        // A heatmap pixel high in the data rectangle must be its cell color,
        // with no incorrectly positioned frequency-name text over time.
        const QRect heatArea = bf::plot::plotRect(spectrogram, {76, 12, 70, 42});
        const QRgb cell = stftA.pixel(heatArea.left() + 15, heatArea.top() + 8);
        bool uniform = true;
        for (int y = heatArea.top() + 4; y < heatArea.top() + 20; ++y) {
            for (int x = heatArea.left() + 5; x < heatArea.left() + 60; ++x) {
                uniform &= stftA.pixel(x, y) == cell;
            }
        }
        passed &= expect(uniform, "Frequency legend obscures STFT data");

        trend.setTrend({0.0, 0.5, 1.0}, {{0.1, 0.7, 0.2}, {0.2, 0.5, 1.0}});
        trend.setLabels("Trend title one", "Time", "empty");
        const QImage trendA = render(trend);
        trend.setLabels("Trend title two", "Time", "empty");
        passed &= expect(render(trend) == trendA, "Trend title leaked into plot");
        // No in-plot colored legend remains: a low, constant band has no curve
        // near the top. A second, move-only point defines the same y range.
        trend.setTrend({0.0, 0.5, 1.0}, {{0.1, 0.1, 0.1}, {1.0}});
        const QImage lowTrend = render(trend);
        const QRect trendArea = bf::plot::plotRect(trend, {76, 12, 16, 42});
        int saturatedLegendPixels = 0;
        for (int y = trendArea.top() + 4; y < trendArea.top() + 20; ++y) {
            for (int x = trendArea.left() + 4; x < trendArea.left() + 120; ++x) {
                const QColor c = lowTrend.pixelColor(x, y);
                saturatedLegendPixels += c.saturation() > 160 && c.value() > 100;
            }
        }
        passed &= expect(saturatedLegendPixels < 8, "Trend legend still paints over data");

        // Unequally spaced center times use midpoint bin edges [9,11,14,18].
        // At t=14.5 the third (blue) cell has begun; an index-spaced heatmap
        // would still show the second (red) bin at that axis coordinate.
        spectrogram.setSpectrogram({{0.0, 10.0}, {10.0, 12.0, 16.0},
                                    {{0.0, 1.0, 0.0}, {0.0, 1.0, 0.0}}});
        const QImage uneven = render(spectrogram);
        const int sampleX = heatArea.left() + static_cast<int>(heatArea.width() * (14.5 - 9.0) / 9.0);
        const int sampleY = heatArea.bottom() - static_cast<int>(heatArea.height() * 10.0 / 15.0);
        const QColor expectedLow = QColor::fromRgbF(0.0, 0.0, 0.5);
        passed &= expect(uneven.pixel(sampleX, sampleY) == expectedLow.rgba(),
                         "STFT colors disagree with center-time bin boundaries");
        const int peakX = heatArea.left() + static_cast<int>(heatArea.width() * (12.0 - 9.0) / 9.0);
        passed &= expect(uneven.pixel(peakX, sampleY) == QColor::fromRgbF(0.5, 0.0, 0.0).rgba(),
                         "STFT peak is not at its reported time coordinate");
        // A single center uses a one-second exterior extent. Both the center
        // and off-center sample are visible instead of an almost-zero axis.
        spectrogram.setSpectrogram({{5.0}, {12.0}, {{0.0}}});
        const QImage singleBin = render(spectrogram);
        passed &= expect(singleBin.pixel(heatArea.center()) == expectedLow.rgba() &&
                             singleBin.pixel(heatArea.left() + 10, heatArea.center().y()) == expectedLow.rgba(),
                         "Single STFT bin failed to fill a finite axis extent");
        trend.setTrend({12.0}, {{0.5}});
        const QImage singleEpoch = render(trend);
        const auto& band = bf::dsp::eegBands().front();
        const QColor pointColor(band.rgba[0], band.rgba[1], band.rgba[2]);
        bool hasPoint = false;
        for (int y = trendArea.top(); y < trendArea.top() + 4; ++y) {
            for (int x = trendArea.center().x() - 3; x <= trendArea.center().x() + 3; ++x) {
                hasPoint |= singleEpoch.pixelColor(x, y) == pointColor;
            }
        }
        passed &= expect(hasPoint, "Single trend epoch has no visible marker");

        // Missing axes, ragged rows, nonfinite samples and mismatched vector
        // lengths are rendered as gaps/empty state, never indexed unchecked.
        spectrum.setSpectrum({{0.0, 1.0, 2.0}, {nan}}, "", "");
        render(spectrum);
        spectrum.setSpectrum({{0.0, nan, 2.0}, {1.0, 2.0, 3.0}}, "", "");
        render(spectrum);
        spectrogram.setSpectrogram({{}, {0.0}, {{1.0}}});
        render(spectrogram);
        spectrogram.setSpectrogram({{0.0, 10.0}, {0.0, 1.0}, {{1.0}, {nan, 2.0}}});
        render(spectrogram);
        trend.setTrend({0.0, nan, 2.0}, {{1.0}, {nan, 2.0, 3.0, 4.0}});
        render(trend);
        for (QWidget* widget : {static_cast<QWidget*>(&spectrum), static_cast<QWidget*>(&spectrogram), static_cast<QWidget*>(&trend)}) {
            widget->resize(240, 160);
            passed &= expect(!render(*widget).isNull(), "Small plot failed to render");
        }
    }
    if (passed) {
        std::cout << "Analysis exterior metadata, axes, locale, and malformed-model checks passed\n";
    }
    return passed ? 0 : 1;
}
