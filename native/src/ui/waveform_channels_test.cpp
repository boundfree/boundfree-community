#include "ui/waveform_widget.h"
#include "ui/plot_style.h"
#include <QApplication>
#include <QImage>
#include <QWheelEvent>
#include <cmath>
#include <iostream>
#include <limits>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bf::WaveformWidget widget;
    widget.resize(800, 400);
    bf::WaveformSlice slice;
    slice.fs = 100;
    slice.t0 = 0;
    slice.t1 = 1;
    for (int channel = 0; channel < 40; ++channel) {
        slice.channelNames.append(QString("channel-%1").arg(channel));
        slice.data.push_back(std::vector<float>(100, static_cast<float>(channel)));
    }
    for (int i = 0; i < 100; ++i) {
        slice.data[0][i] = i % 2 ? 1 : -1;
        slice.data[1][i] = i % 2 ? 1100 : 900;
        slice.data[2][i] = i % 2 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
        slice.data[3][i] = i < 50 ? 10 : 1000;
    }
    widget.setSlice(slice);
    auto check = [](bool ok, const char* label) {
        if (!ok) std::cerr << "waveform-channels-failed " << label << '\n';
        return ok;
    };
    auto near = [](double a, double b) { return std::abs(a-b) < 1e-6; };
    const auto small = widget.channelAmplitudeRange(0);
    const auto large = widget.channelAmplitudeRange(1);
    const auto invalid = widget.channelAmplitudeRange(2);
    if (!check(near(small.first, -5) && near(small.second, 5), "small_signal_floor")) return 1;
    if (!check(near(large.first, 885) && near(large.second, 1115), "independent_dc_range")) return 1;
    if (!check(near(invalid.first, -5) && near(invalid.second, 5), "nonfinite_safe")) return 1;
    widget.setTimeRange(0, .5);
    widget.setSlice(slice); // New block/seek recomputes immediately for the visible interval.
    const auto cropped = widget.channelAmplitudeRange(3);
    if (!check(near(cropped.first, 5) && near(cropped.second, 15), "viewport_not_prefetch_range")) return 1;
    const int rangeBuilds = widget.autoScaleBuildCount();
    const int modelBuilds = widget.displayModelBuildCount();
    for (int i = 1; i <= 40; ++i) {
        widget.setTimeRange(i * .001, .5 + i * .001);
        widget.channelAmplitudeRange(3);
    }
    if (!check(widget.autoScaleBuildCount() == rangeBuilds && widget.displayModelBuildCount() == modelBuilds, "frame_updates_do_not_rescan")) return 1;
    widget.show();
    app.processEvents();
    auto* scroll = widget.findChild<QScrollBar*>("eegWaveformChannelScroll");
    if (!check(scroll && scroll->maximum() > 0 && widget.cachedDisplayModel().data.size() == 40, "all_channels_scrollable")) return 1;
    QImage first(widget.size(), QImage::Format_ARGB32);
    widget.render(&first);
    const int beforeScroll = widget.displayModelBuildCount();
    scroll->setValue(scroll->maximum());
    QImage last(widget.size(), QImage::Format_ARGB32);
    widget.render(&last);
    if (!check(first != last && widget.displayModelBuildCount() == beforeScroll, "scroll_changes_render_without_rebuild")) return 1;
    QWheelEvent wheel(QPointF(300,200), QPointF(300,200), QPoint(), QPoint(0,120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &wheel);
    if (!check(scroll->value() < scroll->maximum(), "wheel_scroll")) return 1;
    const auto colorPixels = [](const QImage& image, const QColor& color) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            count += image.pixel(x, y) == color.rgba();
        return count;
    };
    if (!check(colorPixels(first, bf::plot::eegChannelColor(0, false)) > 50
        && colorPixels(first, bf::plot::eegChannelColor(1, false)) > 50, "light_channel_colors")) return 1;
    scroll->setValue(0);
    widget.setDarkTheme(true);
    QImage dark(widget.size(), QImage::Format_ARGB32); widget.render(&dark);
    if (!check(colorPixels(dark, bf::plot::eegChannelColor(0, true)) > 50
        && colorPixels(dark, bf::plot::eegChannelColor(1, true)) > 50, "existing_widget_dark_colors")) return 1;
    bf::WaveformViewState hiddenFirst;
    hiddenFirst.setChannelVisible(0, false); widget.setViewState(hiddenFirst);
    QImage hidden(widget.size(), QImage::Format_ARGB32); widget.render(&hidden);
    if (!check(colorPixels(hidden, bf::plot::eegChannelColor(1, true)) > 50,
        "channel_color_survives_hidden_predecessor")) return 1;
    widget.setDarkTheme(false);
    bf::WaveformViewState state;
    state.setAmplitudeUv(200);
    widget.setViewState(state);
    widget.setAutoScaleEnabled(false);
    if (!check(widget.channelAmplitudeRange(1) == std::make_pair(-200.0, 200.0), "manual_range")) return 1;
    for (int i = 1; i < 40; ++i) state.setChannelVisible(i, false);
    widget.setViewState(state);
    if (!check(scroll->maximum() == 0 && scroll->value() == 0, "hidden_channels_clamp_scroll")) return 1;
    widget.setSlice(bf::WaveformSlice{});
    widget.setAutoScaleEnabled(true);
    if (!check(widget.channelAmplitudeRange(0) == std::make_pair(-5.0, 5.0), "empty_safe")) return 1;
    std::cout << "waveform-channels-ok auto=true viewport=true finite=true scroll=true cache=true manual=true\n";
}
