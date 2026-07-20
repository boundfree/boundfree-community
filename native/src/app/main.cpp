#include "controller/file_open_controller.h"
#include "controller/file_session.h"
#include "controller/request_generation.h"
#include "controller/waveform_view_state.h"
#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ipc/sidecar_client.h"
#include "license/machine_id.h"
#include "render/minmax_decimator.h"
#include "render/waveform_slice.h"
#include "ui/main_window.h"
#include "ui/waveform_widget.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    const QString repoRoot = bf::VersionService::detectRepoRoot();
    bf::VersionService versionService(repoRoot);
    bf::VersionInfo version;
    QString versionError;
    if (!versionService.load(&version, &versionError)) {
        std::cerr << "failed to load version.json: " << versionError.toStdString() << "\n";
        return 1;
    }

    auto valueAfter = [&](int start, const QString& key, const QString& fallback) {
        for (int j = start; j + 1 < argc; ++j) {
            if (QString::fromLocal8Bit(argv[j]) == key) {
                return QString::fromLocal8Bit(argv[j + 1]);
            }
        }
        return fallback;
    };

    auto hasFlag = [&](int start, const QString& key) {
        for (int j = start; j < argc; ++j) {
            if (QString::fromLocal8Bit(argv[j]) == key) {
                return true;
            }
        }
        return false;
    };

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--version") {
            QCoreApplication app(argc, argv);
            std::cout << version.displayString().toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-sidecar") {
            QCoreApplication app(argc, argv);
            bf::SidecarClient sidecar(repoRoot);
            const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
            if (!hello.ok) {
                std::cerr << "smoke-sidecar-failed code=" << hello.error.code.toStdString() << "\n";
                return 1;
            }
            sidecar.stop();
            std::cout << "smoke-sidecar-ok product_version=" << hello.productVersion.toStdString()
                      << " ipc_protocol_version=" << hello.ipcProtocolVersion << "\n";
            return 0;
        }
        if (arg == "--smoke-sidecar-open" && i + 1 < argc) {
            QCoreApplication app(argc, argv);
            bf::SidecarClient sidecar(repoRoot);
            const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
            if (!hello.ok) {
                std::cerr << "smoke-sidecar-open-failed code=" << hello.error.code.toStdString() << "\n";
                return 1;
            }
            const QString path = QString::fromLocal8Bit(argv[i + 1]);
            const bf::SidecarOpenResult opened = sidecar.openFile(path);
            sidecar.stop();
            if (!opened.ok) {
                std::cout << "smoke-sidecar-open-error code=" << opened.error.code.toStdString()
                          << " path=" << opened.error.params.value("path").toString().toStdString() << "\n";
                return 0;
            }
            std::cout << "smoke-sidecar-open-ok handle=" << opened.handle.toStdString()
                      << " fs=" << opened.fs << " n_channels=" << opened.nChannels << "\n";
            return 0;
        }
        if (arg == "--smoke-machine-id" && i + 4 < argc) {
            QCoreApplication app(argc, argv);
            QString machineGuid;
            QString mac;
            for (int j = i + 1; j + 1 < argc; j += 2) {
                const QString key = QString::fromLocal8Bit(argv[j]);
                const QString value = QString::fromLocal8Bit(argv[j + 1]);
                if (key == "--machine-guid") {
                    machineGuid = value;
                } else if (key == "--mac") {
                    mac = value;
                }
            }
            std::cout << "smoke-machine-id " << bf::machineIdFromInputs(machineGuid, mac).toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-open-file" && i + 1 < argc) {
            QCoreApplication app(argc, argv);
            bf::SidecarClient sidecar(repoRoot);
            const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
            if (!hello.ok) {
                std::cerr << "smoke-open-file-failed code=" << hello.error.code.toStdString() << "\n";
                return 1;
            }
            const bf::SidecarOpenResult opened = sidecar.openFile(QString::fromLocal8Bit(argv[i + 1]));
            sidecar.stop();
            if (!opened.ok) {
                std::cerr << "smoke-open-file-failed code=" << opened.error.code.toStdString() << "\n";
                return 1;
            }
            std::cout << "smoke-open-file-ok format=" << opened.format.toStdString()
                      << " fs=" << opened.fs
                      << " n_channels=" << opened.nChannels
                      << " n_samples=" << opened.nSamples
                      << " duration=" << opened.duration
                      << " channels=" << bf::formatChannelList(opened.chNames).toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-open-slice" && i + 3 < argc) {
            QCoreApplication app(argc, argv);
            bf::SidecarClient sidecar(repoRoot);
            const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
            if (!hello.ok) {
                std::cerr << "smoke-open-slice-failed code=" << hello.error.code.toStdString() << "\n";
                return 1;
            }
            const bf::SidecarOpenResult opened = sidecar.openFile(QString::fromLocal8Bit(argv[i + 1]));
            if (!opened.ok) {
                sidecar.stop();
                std::cerr << "smoke-open-slice-failed code=" << opened.error.code.toStdString() << "\n";
                return 1;
            }
            const double t0 = QString::fromLocal8Bit(argv[i + 2]).toDouble();
            const double t1 = QString::fromLocal8Bit(argv[i + 3]).toDouble();
            const bf::SidecarSliceResult slice = sidecar.getSlice(opened.handle, t0, t1);
            sidecar.stop();
            if (!slice.ok || slice.data.empty() || slice.data.front().empty()) {
                std::cerr << "smoke-open-slice-failed code=" << slice.error.code.toStdString() << "\n";
                return 1;
            }
            const float first = slice.data.front().front();
            const float last = slice.data.back().back();
            std::cout << "smoke-open-slice-ok shape=" << slice.nChannels << "x" << slice.nSamples
                      << " first=" << first << " last=" << last
                      << " unit=" << slice.unit.toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-ui-open-file" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }

            bf::MainWindow window(version, &i18n);
            bf::FileOpenController controller(repoRoot, version);
            int exitCode = 1;

            QObject::connect(&controller, &bf::FileOpenController::loaded, &window, [&](const bf::FileOpenResult& result) {
                window.showFileOpenResult(result);
                std::cout << "smoke-ui-open-file-ok format=" << result.session.format.toStdString()
                          << " n_channels=" << result.session.nChannels
                          << " n_samples=" << result.session.nSamples << "\n";
                exitCode = 0;
                app.quit();
            });
            QObject::connect(&controller, &bf::FileOpenController::failed, &window, [&](const bf::AppError& error) {
                window.showFileOpenError(error);
                std::cerr << "smoke-ui-open-file-failed code=" << error.code.toStdString() << "\n";
                exitCode = 1;
                app.quit();
            });

            window.show();
            window.showFileOpenLoading(QString::fromLocal8Bit(argv[i + 1]));
            controller.openInitialWindow(QString::fromLocal8Bit(argv[i + 1]));
            QTimer::singleShot(5000, &app, [&]() {
                std::cerr << "smoke-ui-open-file-failed code=timeout\n";
                exitCode = 1;
                app.quit();
            });
            app.exec();
            return exitCode;
        }
        if (arg == "--smoke-ui-open-file-dispose" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::FileOpenController controller(repoRoot, version);
            controller.openInitialWindow(QString::fromLocal8Bit(argv[i + 1]));
            std::cout << "smoke-ui-open-file-dispose-ok\n";
            return 0;
        }
        if (arg == "--smoke-ui-file-open-status") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }

            bf::MainWindow window(version, &i18n);
            window.showFileOpenLoading("demo.csv");
            const QString loadingEn = window.statusBar()->currentMessage();
            if (!i18n.loadLocale("pseudo", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            window.retranslateUi();
            const QString loadingPseudo = window.statusBar()->currentMessage();

            bf::FileOpenResult result;
            result.session.ok = true;
            result.session.format = "CSV";
            result.session.fs = 100.0;
            result.session.nChannels = 2;
            result.waveform.fs = 100.0;
            result.waveform.t0 = 0.0;
            result.waveform.t1 = 0.01;
            result.waveform.channelNames = {"ch1", "ch2"};
            result.waveform.data = {{0.0f, 1.0f}, {1.0f, 0.0f}};
            window.showFileOpenResult(result);
            const QString openedPseudo = window.statusBar()->currentMessage();
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            window.retranslateUi();
            const QString openedEn = window.statusBar()->currentMessage();

            std::cout << "smoke-ui-file-open-status-ok"
                      << " loading_en=" << loadingEn.toStdString()
                      << " loading_pseudo=" << loadingPseudo.toStdString()
                      << " opened_pseudo=" << openedPseudo.toStdString()
                      << " opened_en=" << openedEn.toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-ui-browser-controls") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            bf::MainWindow window(version, &i18n);
            bf::FileOpenResult result;
            result.session.ok = true;
            result.session.format = "CSV";
            result.session.fs = 100.0;
            result.session.nChannels = 2;
            result.session.nSamples = 1000;
            result.waveform.fs = 100.0;
            result.waveform.t0 = 0.0;
            result.waveform.t1 = 10.0;
            result.waveform.channelNames = {"ch1", "ch2"};
            result.waveform.data = {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, -1.0f}};
            window.showFileOpenResult(result);
            std::cout << "smoke-ui-browser-controls-ok status=" << window.browserStatusText().toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-ui-open-latest" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            bf::MainWindow window(version, &i18n);
            bf::FileOpenController controller(repoRoot, version);
            int exitCode = 1;
            QObject::connect(&controller, &bf::FileOpenController::loaded, &window, [&](const bf::FileOpenResult& result) {
                window.showFileOpenResult(result);
                if (result.waveform.t0 == 0.02 && result.waveform.t1 == 0.05) {
                    std::cout << "smoke-ui-open-latest-ok t0=" << result.waveform.t0
                              << " t1=" << result.waveform.t1 << "\n";
                    exitCode = 0;
                    app.quit();
                }
            });
            QObject::connect(&controller, &bf::FileOpenController::failed, &window, [&](const bf::AppError& error) {
                std::cerr << "smoke-ui-open-latest-failed code=" << error.code.toStdString() << "\n";
                exitCode = 1;
                app.quit();
            });
            const QString path = QString::fromLocal8Bit(argv[i + 1]);
            controller.openWindow(path, 0.0, 0.03);
            controller.openWindow(path, 0.02, 0.03);
            QTimer::singleShot(5000, &app, [&]() {
                std::cerr << "smoke-ui-open-latest-failed code=timeout\n";
                exitCode = 1;
                app.quit();
            });
            app.exec();
            return exitCode;
        }
        if (arg == "--smoke-parse-slice-payload" && i + 1 < argc) {
            QCoreApplication app(argc, argv);
            QFile file(QString::fromLocal8Bit(argv[i + 1]));
            if (!file.open(QIODevice::ReadOnly)) {
                std::cout << "smoke-parse-slice-payload-error code=protocol.invalid_params field=path reason=unreadable\n";
                return 1;
            }
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                std::cout << "smoke-parse-slice-payload-error code=protocol.invalid_json\n";
                return 1;
            }
            const bf::SidecarSliceResult slice = bf::parseSlicePayload(doc.object());
            if (!slice.ok) {
                std::cout << "smoke-parse-slice-payload-error code=" << slice.error.code.toStdString()
                          << " field=" << slice.error.params.value("field").toString().toStdString()
                          << " reason=" << slice.error.params.value("reason").toString().toStdString() << "\n";
                return 1;
            }
            std::cout << "smoke-parse-slice-payload-ok shape=" << slice.nChannels << "x" << slice.nSamples << "\n";
            return 0;
        }
        if (arg == "--smoke-minmax-decimator") {
            QCoreApplication app(argc, argv);
            const std::vector<float> samples{0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
            std::cout << "smoke-minmax-decimator "
                      << bf::formatMinMaxBuckets(bf::minMaxDecimate(samples, 2)).toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-request-generation") {
            QCoreApplication app(argc, argv);
            bf::RequestGeneration generation;
            const int first = generation.next();
            const int second = generation.next();
            const bool firstCurrent = generation.isCurrent(first);
            const bool secondCurrent = generation.isCurrent(second);
            generation.invalidate();
            const bool afterInvalidate = generation.isCurrent(second);
            std::cout << "smoke-request-generation-ok first=" << first
                      << " second=" << second
                      << " first_current=" << (firstCurrent ? "true" : "false")
                      << " second_current=" << (secondCurrent ? "true" : "false")
                      << " after_invalidate=" << (afterInvalidate ? "true" : "false") << "\n";
            return 0;
        }
        if (arg == "--smoke-waveform-view-state") {
            QCoreApplication app(argc, argv);
            bf::WaveformViewState state;
            state.setWindowSeconds(500.0);
            state.setAmplitudeUv(10.0);
            state.setFirstVisibleChannel(3);
            state.setChannelVisible(1, false);
            std::cout << "smoke-waveform-view-state-ok "
                      << bf::formatWaveformViewState(state, 4).toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-waveform-display-model") {
            QCoreApplication app(argc, argv);
            bf::WaveformSlice slice;
            slice.fs = 100.0;
            slice.t0 = 0.0;
            slice.t1 = 0.04;
            slice.unit = "uV";
            slice.channelNames = {"ch1", "ch2", "ch3"};
            slice.data = {
                {0.0f, 1.0f, 2.0f, 3.0f},
                {10.0f, 11.0f, 12.0f, 13.0f},
                {20.0f, 21.0f, 22.0f, 23.0f},
            };
            bf::WaveformViewState state;
            state.setFirstVisibleChannel(1);
            state.setChannelVisible(1, false);
            state.setAmplitudeUv(200.0);
            const bf::WaveformDisplayModel model = bf::waveformDisplayModel(slice, state, 4);
            std::cout << "smoke-waveform-display-model-ok channels="
                      << model.channelNames.join(",").toStdString()
                      << " samples=" << (model.data.empty() ? 0 : model.data.front().size())
                      << " window=" << model.t0 << "-" << model.t1
                      << " amplitude=" << model.amplitudeUv << "\n";
            return 0;
        }
        if (arg == "--smoke-waveform-display-model-cache") {
            QApplication app(argc, argv);
            bf::WaveformWidget widget;
            bf::WaveformSlice slice;
            slice.fs = 100.0;
            slice.t0 = 0.0;
            slice.t1 = 0.04;
            slice.unit = "uV";
            slice.channelNames = {"ch1", "ch2"};
            slice.data = {{0.0f, 1.0f, 2.0f, 3.0f}, {10.0f, 11.0f, 12.0f, 13.0f}};
            bf::WaveformViewState state;
            state.setAmplitudeUv(200.0);
            widget.setSlice(slice);
            widget.setViewState(state);
            const int before = widget.displayModelBuildCount();
            const bf::WaveformDisplayModel first = widget.cachedDisplayModel(32);
            const int afterFirst = widget.displayModelBuildCount();
            const bf::WaveformDisplayModel second = widget.cachedDisplayModel(32);
            const int afterSecond = widget.displayModelBuildCount();
            QImage image(widget.size(), QImage::Format_ARGB32);
            widget.render(&image);
            widget.render(&image);
            const int afterPaint = widget.displayModelBuildCount();
            std::cout << "smoke-waveform-display-model-cache-ok"
                      << " builds_before=" << before
                      << " builds_after_first=" << afterFirst
                      << " builds_after_second=" << afterSecond
                      << " builds_after_paint=" << afterPaint
                      << " channels=" << first.channelNames.join(",").toStdString()
                      << " same_channels=" << (first.channelNames == second.channelNames ? "true" : "false")
                      << "\n";
            return 0;
        }
        if (arg == "--smoke-async-open-nonblocking" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::FileOpenController controller(repoRoot, version);
            int exitCode = 1;
            int timerTicks = 0;
            bool loadedSeen = false;
            QTimer ticker;
            ticker.setInterval(5);
            QObject::connect(&ticker, &QTimer::timeout, &app, [&]() {
                ++timerTicks;
            });
            QObject::connect(&controller, &bf::FileOpenController::loaded, &app, [&](const bf::FileOpenResult&) {
                loadedSeen = true;
                std::cout << "smoke-async-open-nonblocking-ok timer_ticks=" << timerTicks << "\n";
                exitCode = (timerTicks > 0) ? 0 : 1;
                if (timerTicks <= 0) {
                    std::cerr << "smoke-async-open-nonblocking-failed code=no_event_loop_progress\n";
                }
                app.quit();
            });
            QObject::connect(&controller, &bf::FileOpenController::failed, &app, [&](const bf::AppError& error) {
                std::cerr << "smoke-async-open-nonblocking-failed code=" << error.code.toStdString() << "\n";
                exitCode = 1;
                app.quit();
            });
            ticker.start();
            controller.openInitialWindow(QString::fromLocal8Bit(argv[i + 1]), 0.05);
            QTimer::singleShot(10000, &app, [&]() {
                std::cerr << "smoke-async-open-nonblocking-failed code=timeout loaded="
                          << (loadedSeen ? "true" : "false") << " ticks=" << timerTicks << "\n";
                exitCode = 1;
                app.quit();
            });
            app.exec();
            return exitCode;
        }
        if (arg == "--smoke-waveform-benchmark") {
            QApplication app(argc, argv);
            const int channels = valueAfter(i + 1, "--channels", "256").toInt();
            const int fs = valueAfter(i + 1, "--fs", "1000").toInt();
            const int seconds = valueAfter(i + 1, "--seconds", "4").toInt();
            const int frames = valueAfter(i + 1, "--frames", "30").toInt();
            const bool jsonOutput = hasFlag(i + 1, "--json");
            const QString platformName = QApplication::platformName().trimmed().toLower();
            const QString gate =
                (platformName == "offscreen" || platformName == "minimal")
                    ? "development-signal-only"
                    : "display-candidate";
            bf::WaveformWidget widget;
            bf::WaveformSlice slice;
            slice.fs = fs;
            slice.t0 = 0.0;
            slice.t1 = seconds;
            for (int ch = 0; ch < channels; ++ch) {
                slice.channelNames.push_back(QString("Ch%1").arg(ch + 1));
                std::vector<float> samples;
                samples.reserve(fs * seconds);
                for (int sample = 0; sample < fs * seconds; ++sample) {
                    samples.push_back(static_cast<float>(((sample + ch) % 200) - 100));
                }
                slice.data.push_back(std::move(samples));
            }
            bf::WaveformViewState state;
            state.setWindowSeconds(seconds);
            state.setAmplitudeUv(200.0);
            widget.resize(1280, 720);
            widget.setViewState(state);
            widget.setSlice(slice);

            QImage image(widget.size(), QImage::Format_ARGB32);
            QElapsedTimer timer;
            timer.start();
            for (int frame = 0; frame < frames; ++frame) {
                image.fill(QColor("#111827"));
                widget.render(&image);
            }
            const double elapsedSeconds = std::max(0.001, timer.elapsed() / 1000.0);
            const double fps = frames / elapsedSeconds;
            if (jsonOutput) {
                const QJsonObject payload{
                    {"channels", channels},
                    {"fs", fs},
                    {"seconds", seconds},
                    {"frames", frames},
                    {"fps", fps},
                    {"gate", gate},
                };
                std::cout << QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString() << "\n";
            } else {
                std::cout << "smoke-waveform-benchmark-ok channels=" << channels
                          << " fs=" << fs
                          << " seconds=" << seconds
                          << " frames=" << frames
                          << " fps=" << fps
                          << " gate=" << gate.toStdString() << "\n";
            }
            return 0;
        }
        if (arg == "--smoke-ui-channel-browser") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            bf::MainWindow window(version, &i18n);
            bf::FileOpenResult result;
            result.session.ok = true;
            result.session.format = "CSV";
            result.session.fs = 100.0;
            result.session.nChannels = 4;
            result.session.nSamples = 1000;
            result.waveform.fs = 100.0;
            result.waveform.t0 = 0.0;
            result.waveform.t1 = 10.0;
            result.waveform.channelNames = {"ch1", "ch2", "ch3", "ch4"};
            result.waveform.data = {
                {0.0f, 1.0f, 0.0f},
                {1.0f, 0.0f, -1.0f},
                {2.0f, 1.0f, 0.0f},
                {3.0f, 2.0f, 1.0f},
            };
            window.showFileOpenResult(result);
            window.setChannelVisibleForBrowser(1, false);
            window.setFirstVisibleChannelForBrowser(2);
            std::cout << "smoke-ui-channel-browser-ok summary="
                      << window.browserControlSummary().toStdString()
                      << " status=" << window.browserStatusText().toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-ui-window-request" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            const QString path = QString::fromLocal8Bit(argv[i + 1]);
            bf::MainWindow window(version, &i18n);
            QString requestedPath;
            double requestedT0 = -1.0;
            double requestedWindow = -1.0;
            window.setWindowRequestHandler([&](const QString& activePath, double t0, double windowSeconds) {
                requestedPath = activePath;
                requestedT0 = t0;
                requestedWindow = windowSeconds;
            });
            bf::FileOpenResult result;
            result.session.ok = true;
            result.session.path = path;
            result.session.format = "CSV";
            result.session.fs = 100.0;
            result.session.nChannels = 2;
            result.session.nSamples = 1000;
            result.waveform.fs = 100.0;
            result.waveform.t0 = 0.0;
            result.waveform.t1 = 10.0;
            result.waveform.channelNames = {"ch1", "ch2"};
            result.waveform.data = {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, -1.0f}};
            window.showFileOpenResult(result);
            window.setBrowserWindowSecondsForBrowser(4.0);
            std::cout << "smoke-ui-window-request-ok path=" << requestedPath.toStdString()
                      << " t0=" << requestedT0
                      << " window=" << requestedWindow << "\n";
            return requestedPath == path && requestedT0 == 0.0 && requestedWindow == 4.0 ? 0 : 1;
        }
        if (arg == "--smoke-ui-window-latest" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            const QString path = QString::fromLocal8Bit(argv[i + 1]);
            bf::MainWindow window(version, &i18n);
            bf::FileOpenController controller(repoRoot, version);
            int exitCode = 1;
            window.setWindowRequestHandler([&](const QString& activePath, double t0, double windowSeconds) {
                controller.openWindow(activePath, t0, windowSeconds);
            });
            QObject::connect(&controller, &bf::FileOpenController::loaded, &window, [&](const bf::FileOpenResult& result) {
                window.showFileOpenResult(result);
                if (result.waveform.t0 == 0.02 && result.waveform.t1 == 0.1) {
                    std::cout << "smoke-ui-window-latest-ok t0=" << result.waveform.t0
                              << " t1=" << result.waveform.t1
                              << " status=" << window.browserStatusText().toStdString() << "\n";
                    exitCode = 0;
                    app.quit();
                }
            });
            QObject::connect(&controller, &bf::FileOpenController::failed, &window, [&](const bf::AppError& error) {
                std::cerr << "smoke-ui-window-latest-failed code=" << error.code.toStdString() << "\n";
                exitCode = 1;
                app.quit();
            });
            controller.openWindow(path, 0.0, 0.03);
            bf::FileOpenResult result;
            result.session.ok = true;
            result.session.path = path;
            result.session.format = "CSV";
            result.session.fs = 100.0;
            result.session.nChannels = 2;
            result.session.nSamples = 1000;
            result.waveform.fs = 100.0;
            result.waveform.t0 = 0.02;
            result.waveform.t1 = 0.05;
            result.waveform.channelNames = {"ch1", "ch2"};
            result.waveform.data = {{2.0f, 3.0f, 4.0f}, {12.0f, 13.0f, 14.0f}};
            window.showFileOpenResult(result);
            window.setBrowserWindowSecondsForBrowser(4.0);
            QTimer::singleShot(5000, &app, [&]() {
                std::cerr << "smoke-ui-window-latest-failed code=timeout\n";
                exitCode = 1;
                app.quit();
            });
            app.exec();
            return exitCode;
        }
        if (arg == "--smoke-waveform-render") {
            QApplication app(argc, argv);
            bf::WaveformWidget widget;
            bf::WaveformSlice slice;
            slice.fs = 100.0;
            slice.t0 = 0.0;
            slice.t1 = 0.06;
            slice.channelNames = {"ch1", "ch2"};
            slice.data = {{0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f},
                          {1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}};
            widget.resize(640, 320);
            widget.setSlice(slice);

            QImage image(widget.size(), QImage::Format_ARGB32);
            image.fill(QColor("#111827"));
            widget.render(&image);

            int nonBackground = 0;
            const QRgb background = QColor("#111827").rgba();
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (image.pixel(x, y) != background) {
                        ++nonBackground;
                    }
                }
            }

            std::cout << "smoke-waveform-render-ok image=640x320 non_background_pixels=" << nonBackground << "\n";
            return nonBackground > 0 ? 0 : 1;
        }
    }

    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("BoundFree EEG Native");
    parser.addHelpOption();
    const QCommandLineOption versionOpt("version", "Print product version.");
    const QCommandLineOption smokeSidecarOpt("smoke-sidecar", "Start sidecar, run hello, and exit.");
    const QCommandLineOption smokeSidecarOpenOpt("smoke-sidecar-open", "Start sidecar, open a file, and exit.", "path");
    const QCommandLineOption smokeOpt("smoke-ui", "Start UI and exit automatically.");
    const QCommandLineOption languageOpt("language", "Set startup locale.", "locale", "zh_CN");
    const QCommandLineOption openFileOpt("open-file", "Open an EEG file after startup.", "path");
    parser.addOptions({versionOpt, smokeSidecarOpt, smokeSidecarOpenOpt, smokeOpt, languageOpt, openFileOpt});
    parser.process(app);

    bf::I18nService i18n(repoRoot);
    QString i18nError;
    if (!i18n.loadLocale(parser.value(languageOpt), &i18nError)) {
        std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
        return 1;
    }

    bf::MainWindow window(version, &i18n);
    bf::SidecarClient sidecar(repoRoot);
    bf::FileOpenController fileController(repoRoot, version);
    window.setOpenFileHandler([&](const QString& path) {
        fileController.openInitialWindow(path);
    });
    window.setWindowRequestHandler([&](const QString& path, double t0, double windowSeconds) {
        fileController.openWindow(path, t0, windowSeconds);
    });
    QObject::connect(&fileController, &bf::FileOpenController::loaded, &window, [&](const bf::FileOpenResult& result) {
        window.showFileOpenResult(result);
        if (parser.isSet(smokeOpt) && parser.isSet(openFileOpt)) {
            std::cout << "smoke-ui-file-flow-ok format=" << result.session.format.toStdString()
                      << " n_channels=" << result.session.nChannels
                      << " t0=" << result.waveform.t0
                      << " t1=" << result.waveform.t1 << "\n";
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
        }
    });
    QObject::connect(&fileController, &bf::FileOpenController::failed, &window, [&](const bf::AppError& error) {
        window.showFileOpenError(error);
        if (parser.isSet(smokeOpt) && parser.isSet(openFileOpt)) {
            std::cerr << "smoke-ui-file-flow-failed code=" << error.code.toStdString() << "\n";
            QTimer::singleShot(0, &app, [&]() { app.exit(1); });
        }
    });
    window.show();
    window.setSidecarConnecting();

    QTimer::singleShot(0, &window, [&]() {
        const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
        if (hello.ok) {
            window.setSidecarConnected();
            if (parser.isSet(openFileOpt)) {
                window.openPathForBrowser(parser.value(openFileOpt));
            }
        } else {
            window.setSidecarFailed(hello.error);
        }
    });

    if (parser.isSet(smokeOpt)) {
        const int smokeTimeoutMs = parser.isSet(openFileOpt) ? 30000 : 500;
        QTimer::singleShot(smokeTimeoutMs, &app, [&]() {
            sidecar.stop();
            if (parser.isSet(openFileOpt)) {
                std::cerr << "smoke-ui-file-flow-failed code=timeout\n";
                app.exit(1);
                return;
            }
            std::cout << "smoke-ui-ok\n";
            app.quit();
        });
    }

    const int rc = app.exec();
    sidecar.stop();
    return rc;
}
