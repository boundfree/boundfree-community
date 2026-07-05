#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMainWindow>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <vector>

#include "waveform_gl.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("BoundFree EEG M0 OpenGL waveform spike");
    parser.addHelpOption();
    QCommandLineOption channelsOpt("channels", "Channel count.", "channels", "256");
    QCommandLineOption fsOpt("fs", "Sample rate.", "hz", "1000");
    QCommandLineOption windowOpt("window-sec", "Visible time window in seconds.", "seconds", "4");
    QCommandLineOption durationOpt("duration-ms", "Auto-exit after this many milliseconds.", "ms", "0");
    parser.addOptions({channelsOpt, fsOpt, windowOpt, durationOpt});
    parser.process(app);

    const QString platform = QGuiApplication::platformName();
    if (platform == "offscreen" || platform == "minimal") {
        std::fprintf(stderr, "QOpenGLWidget benchmark requires an X11/Wayland platform; got %s.\n",
                     platform.toUtf8().constData());
        return 2;
    }

    const int nCh = parser.value(channelsOpt).toInt();
    const int fs = parser.value(fsOpt).toInt();
    const int winSec = parser.value(windowOpt).toInt();
    const int durationMs = parser.value(durationOpt).toInt();
    const int nSamp = fs * winSec;

    QMainWindow win;
    win.resize(1280, 800);
    win.setWindowTitle("BoundFree Spike");
    auto* gl = new WaveformGL();
    win.setCentralWidget(gl);
    win.show();

    std::vector<float> buf(static_cast<std::size_t>(nCh) * static_cast<std::size_t>(nSamp));
    double phase = 0.0;
    int produced = 0;
    int rendered = 0;
    QElapsedTimer clock;
    clock.start();
    qint64 lastLog = 0;

    auto* frameTimer = new QTimer(&win);
    QObject::connect(frameTimer, &QTimer::timeout, [&]() {
        for (int c = 0; c < nCh; ++c) {
            for (int s = 0; s < nSamp; ++s) {
                const auto idx = static_cast<std::size_t>(c) * static_cast<std::size_t>(nSamp) +
                                 static_cast<std::size_t>(s);
                buf[idx] = 50.0f * std::sin(phase + 0.02 * static_cast<double>(s) +
                                            0.1 * static_cast<double>(c));
            }
        }
        phase += 0.05;
        ++produced;
        gl->setData(nCh, nSamp, buf.data());
    });
    QObject::connect(gl, &WaveformGL::frameRendered, [&]() {
        ++rendered;
        const qint64 elapsed = clock.elapsed();
        if (elapsed - lastLog >= 1000) {
            const QString title = QString("Spike %1ch@%2Hz window=%3s produced=%4 rendered=%5")
                                      .arg(nCh)
                                      .arg(fs)
                                      .arg(winSec)
                                      .arg(produced)
                                      .arg(rendered);
            win.setWindowTitle(title);
            std::printf("%lld ms: produced=%d rendered=%d\n",
                        static_cast<long long>(elapsed), produced, rendered);
            std::fflush(stdout);
            produced = 0;
            rendered = 0;
            lastLog = elapsed;
        }
    });

    if (durationMs > 0) {
        QTimer::singleShot(durationMs, &app, &QCoreApplication::quit);
    }
    frameTimer->start(0);

    return app.exec();
}
