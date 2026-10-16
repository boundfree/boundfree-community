#include "ui/analysis_widgets.h"
#include "ui/plot_style.h"

#include <QApplication>
#include <QImage>

#include <iostream>

namespace {
QImage render(bf::SpectrumWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    widget.render(&image);
    return image;
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bool passed = true;
    for (const bool dark : {false, true}) {
        for (const bool bands : {false, true}) {
            for (const char* mode : {"PSD", "FFT", "Coherence"}) {
                const bool coherence = QString::fromLatin1(mode) == "Coherence";
                bf::SpectrumWidget widget;
                widget.resize(480, 260);
                widget.setDarkTheme(dark);
                widget.setBandsVisible(bands);

                // One sample makes a move-only path: the same axes, frequency
                // bands and coherence reference line, with no connecting curve.
                // An empty spectrum would instead introduce placeholder text
                // and omit the bands, allowing a missing curve to pass.
                bf::dsp::Spectrum baseline;
                baseline.frequencies = {40.0};
                baseline.values = {0.5};
                widget.setSpectrum(baseline, mode, "Power", coherence);
                const QImage withoutCurve = render(widget);

                bf::dsp::Spectrum signal;
                signal.frequencies = {0.0, 5.0, 10.0, 20.0, 40.0};
                signal.values = {0.1, 0.5, 1.0, 0.4, 0.2};
                widget.setSpectrum(signal, mode, "Power", coherence);
                const QImage withCurve = render(widget);

                const QRect interior = bf::plot::plotRect(widget).adjusted(2, 2, -2, -2);
                int changed = 0;
                for (int y = interior.top(); y <= interior.bottom(); ++y) {
                    for (int x = interior.left(); x <= interior.right(); ++x) {
                        if (withCurve.pixel(x, y) == bf::plot::palette(dark).spectrumLine.rgba()
                            && withoutCurve.pixel(x, y) != bf::plot::palette(dark).spectrumLine.rgba()) {
                            ++changed;
                        }
                    }
                }
                std::cout << mode << " dark=" << dark << " bands=" << bands
                          << " curve_pixels=" << changed << '\n';
                if (changed < 100) {
                    std::cerr << "Spectrum curve was not rendered\n";
                    passed = false;
                }
            }
        }
    }
    return passed ? 0 : 1;
}
