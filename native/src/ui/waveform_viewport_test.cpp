#include "ui/waveform_widget.h"
#include "ui/plot_style.h"
#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <cmath>
#include <iostream>
#include <limits>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bf::WaveformWidget widget;
    widget.setAutoScaleEnabled(false);
    widget.resize(800, 400);
    bf::WaveformSlice slice;
    slice.fs = 100;
    slice.t0 = 10;
    slice.t1 = 20;
    slice.channelNames = {"pulse"};
    slice.data = {std::vector<float>(1000, 0.0f)};
    slice.data[0][300] = 150; // Absolute time 13 seconds.
    widget.setSlice(slice);
    widget.setCursorTime(-1);
    const auto area = bf::plot::plotArea(widget, bf::plot::kWaveformMargins);
    auto render = [&]() {
        QImage image(widget.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        widget.render(&image);
        return image;
    };
    auto peakX = [&](const QImage& image) {
        const QRgb color = bf::plot::eegPalette(false).spectrumLine.rgba();
        int sum = 0, count = 0;
        for (int y = static_cast<int>(area.top()) + 1; y < area.center().y() - 25; ++y)
            for (int x = static_cast<int>(area.left()) + 1; x < area.right(); ++x)
                if (image.pixel(x, y) == color) { sum += x; ++count; }
        return count ? static_cast<double>(sum) / count : -1.0;
    };
    auto check = [](bool valid, const char* label) {
        if (!valid) std::cerr << "waveform-viewport-failed " << label << '\n';
        return valid;
    };
    if (!check(widget.timeRange() == std::make_pair(10.0, 20.0), "static_range")) return 1;
    const double staticPeak = peakX(render());
    if (!check(std::abs(staticPeak - (area.left() + area.width() * .3)) < 3, "absolute_sample_time")) return 1;
    const int builds = widget.displayModelBuildCount();
    widget.setTimeRange(10, 15);
    const double firstPeak = peakX(render());
    widget.setTimeRange(11, 16);
    const double secondPeak = peakX(render());
    if (!check(std::abs(firstPeak - secondPeak - area.width() / 5) < 3, "continuous_shift")) return 1;
    widget.setTimeRange(14, 19);
    const auto clipped = render();
    if (!check(peakX(clipped) < 0, "offscreen_pulse_clipped")) return 1;
    double clicked = -1;
    widget.setMarkerModeEnabled(true);
    QObject::connect(&widget, &bf::WaveformWidget::markerRequested, [&](double time) { clicked = time; });
    const QPointF point(area.left() + area.width() / 2, area.center().y());
    QMouseEvent click(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &click);
    if (!check(std::abs(clicked - 16.5) < 1e-6, "marker_time")) return 1;
    widget.setTimeRange(std::numeric_limits<double>::quiet_NaN(), 20);
    if (!check(widget.timeRange() == std::make_pair(14.0, 19.0), "invalid_range_ignored")) return 1;
    widget.clearTimeRange();
    if (!check(std::abs(peakX(render()) - staticPeak) < 1 && widget.displayModelBuildCount() == builds, "cache_and_static_restore")) return 1;
    widget.setTimeRange(12, 17);
    widget.setSlice(slice);
    if (!check(widget.timeRange() == std::make_pair(12.0, 17.0), "slice_preserves_viewport")) return 1;
    std::cout << "waveform-viewport-ok shift=true clip=true marker=true cache=true static=true\n";
    return 0;
}
