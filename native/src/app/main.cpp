#include "controller/file_open_controller.h"
#include "controller/export_controller.h"
#include "controller/file_session.h"
#include "controller/request_generation.h"
#include "controller/processing_view_state.h"
#include "controller/waveform_view_state.h"
#include "controller/analysis_service.h"
#include "controller/marker_store.h"
#include "controller/playback_state.h"
#include "controller/record_controller.h"
#include "controller/video_sync_state.h"
#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ipc/sidecar_client.h"
#include "license/machine_id.h"
#include "license/license_manager.h"
#include "render/minmax_decimator.h"
#include "render/waveform_slice.h"
#include "ui/main_window.h"
#include "ui/analysis_widgets.h"
#include "ui/waveform_widget.h"
#include "ui/video_panel.h"

#include <QApplication>
#include <QMenuBar>
#include <QStyleFactory>
#include <QAction>
#include <QDir>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDockWidget>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QMessageBox>
#include <QPalette>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <QEventLoop>
#include <limits>
#include <vector>

#ifdef BF_REQUIRE_LICENSE
namespace {

bool ensureOfflineLicense(const bf::VersionInfo& version, const QString& repoRoot, const bf::I18nService& i18n, bool smokeGate) {
    bf::LicenseManager manager(version.version, repoRoot);
    bf::LicenseResult result = manager.checkSavedLicense();
    if (result.ok()) {
        return true;
    }

    while (true) {
        const QString errorText = i18n.text(QString("error.%1").arg(result.error.code), result.error.params);
        bool accepted = false;
        if (smokeGate) {
            QTimer::singleShot(0, []() {
                if (QWidget* modal = QApplication::activeModalWidget()) {
                    modal->close();
                }
            });
        }
        const QString key = QInputDialog::getMultiLineText(
            nullptr,
            i18n.text("app.license.title"),
            i18n.text("app.license.prompt", {{"machine_id", bf::currentMachineId()}, {"error", errorText}}),
            QString(),
            &accepted);
        if (!accepted) {
            return false;
        }

        result = manager.verifyKey(key);
        if (!result.ok()) {
            continue;
        }
        if (!manager.saveLicense(key)) {
            QMessageBox::warning(nullptr, i18n.text("app.license.title"), i18n.text("app.license.save_failed"));
            continue;
        }
        QMessageBox::information(nullptr, i18n.text("app.license.title"), i18n.text("app.license.activated"));
        return true;
    }
}

}  // namespace
#endif

int main(int argc, char** argv) {
    const QString repoRoot = bf::VersionService::detectRepoRoot(QString::fromLocal8Bit(argv[0]));
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
        if (arg == "--smoke-m8-system-ui") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("en_US", &error)) {
                std::cerr << "smoke-m8-system-ui-failed i18n\n";
                return 1;
            }
            bf::MainWindow window(version, &i18n, repoRoot);
            window.show();
            app.processEvents();

            QAction* light = nullptr;
            QAction* dark = nullptr;
            QAction* channels = nullptr;
            bool hasOpen = false;
            bool hasVideo = false;
            bool hasQuit = false;
            for (QAction* action : window.findChildren<QAction*>()) {
                if (action->text() == i18n.text("app.theme.light")) light = action;
                if (action->text() == i18n.text("app.theme.dark")) dark = action;
                if (action->text() == i18n.text("app.view.channels")) channels = action;
                hasOpen = hasOpen || action->shortcut() == QKeySequence::Open;
                hasVideo = hasVideo || action->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O);
                hasQuit = hasQuit || action->shortcut() == QKeySequence::Quit;
            }
            // All four top-level menus must be present with non-empty titles.
            bool hasFileMenu = false;
            bool hasLanguageMenu = false;
            bool hasViewMenu = false;
            bool hasThemeMenu = false;
            bool menuGeometryOk = true;
            for (QAction* menuAction : window.menuBar()->actions()) {
                const QString title = menuAction->text();
                const QRect geom = window.menuBar()->actionGeometry(menuAction);
                const bool visibleWidth = geom.width() >= 24;
                hasFileMenu = hasFileMenu || (title == i18n.text("app.menu.file") && visibleWidth);
                hasLanguageMenu = hasLanguageMenu || (title == i18n.text("app.menu.language") && visibleWidth);
                hasViewMenu = hasViewMenu || (title == i18n.text("app.menu.view") && visibleWidth);
                hasThemeMenu = hasThemeMenu || (title == i18n.text("app.menu.theme") && visibleWidth);
                menuGeometryOk = menuGeometryOk && !title.trimmed().isEmpty() && visibleWidth;
            }
            if (light == nullptr || dark == nullptr || channels == nullptr || !hasOpen || !hasVideo || !hasQuit
                || !hasFileMenu || !hasLanguageMenu || !hasViewMenu || !hasThemeMenu || !menuGeometryOk) {
                std::cerr << "smoke-m8-system-ui-failed actions"
                          << " file=" << hasFileMenu
                          << " language=" << hasLanguageMenu
                          << " view=" << hasViewMenu
                          << " theme=" << hasThemeMenu
                          << " geometry=" << menuGeometryOk
                          << "\n";
                return 1;
            }

            // Default is light lavender; re-apply light then dark to verify both.
            light->trigger();
            app.processEvents();
            const bool lightApplied = window.palette().color(QPalette::Window) == QColor("#F7F3FF")
                && window.palette().color(QPalette::WindowText) == QColor("#1F1235");
            dark->trigger();
            app.processEvents();
            const bool darkApplied = window.palette().color(QPalette::Window) == QColor("#1E1B2E")
                && window.palette().color(QPalette::Highlight) == QColor("#A78BFA");
            const bool beforeToggle = channels->isChecked();
            channels->trigger();
            app.processEvents();
            const bool dockToggled = channels->isChecked() != beforeToggle;

            bool hasIdleStatus = false;
            const QList<QLabel*> statusLabels = window.statusBar()->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
            for (QLabel* label : statusLabels) {
                hasIdleStatus = hasIdleStatus || label->text() == i18n.text("app.status.mode.idle");
            }
            if (!lightApplied || !darkApplied || !dockToggled || statusLabels.size() < 2 || !hasIdleStatus) {
                std::cerr << "smoke-m8-system-ui-failed behavior\n";
                return 1;
            }
            std::cout << "smoke-m8-system-ui-ok\n";
            return 0;
        }
        if (arg == "--smoke-m5-playback") {
            QCoreApplication app(argc, argv);
            bf::PlaybackState state;
            state.setDuration(10.0);
            state.seek(-1.0);
            if (state.position() != 0.0 || state.isPlaying()) {
                std::cerr << "smoke-m5-playback-failed initial_state\n";
                return 1;
            }
            state.setSpeed(2.0);
            state.play();
            state.advance(1.5);
            if (!state.isPlaying() || std::abs(state.position() - 3.0) > 1e-12) {
                std::cerr << "smoke-m5-playback-failed speed\n";
                return 1;
            }
            state.setSpeed(3.0);
            state.advance(1.0);
            if (std::abs(state.position() - 5.0) > 1e-12) {
                std::cerr << "smoke-m5-playback-failed unsupported_speed\n";
                return 1;
            }
            state.seek(9.5);
            state.advance(1.0);
            if (state.isPlaying() || state.position() != 0.0) {
                std::cerr << "smoke-m5-playback-failed tail_reset\n";
                return 1;
            }
            state.setDuration(0.0);
            state.play();
            std::cout << "smoke-m5-playback-ok speeds=5 tail_reset=true\n";
            return state.isPlaying() ? 1 : 0;
        }
        if (arg == "--smoke-m5-markers" && i + 1 < argc) {
            QCoreApplication app(argc, argv);
            bf::MarkerStore store;
            store.replaceFromEvents({{0.07, "late"}, {0.02, "early"}});
            store.add(0.05, "middle");
            if (store.markers().size() != 3 || store.markers()[0].label != "early" || store.markers()[1].label != "middle" || store.markers()[2].label != "late" || store.markers()[0].color == store.markers()[1].color) {
                std::cerr << "smoke-m5-markers-failed ordering_or_colors\n";
                return 1;
            }
            if (!store.updateLabel(1, "edited") || !store.remove(0) || store.remove(9) || store.markers().size() != 2 || store.markers()[0].label != "edited") {
                std::cerr << "smoke-m5-markers-failed edit_or_remove\n";
                return 1;
            }
            bf::SidecarClient sidecar(repoRoot);
            const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
            if (!hello.ok) {
                std::cerr << "smoke-m5-markers-failed code=" << hello.error.code.toStdString() << "\n";
                return 1;
            }
            const bf::SidecarOpenResult opened = sidecar.openFile(QString::fromLocal8Bit(argv[i + 1]));
            if (!opened.ok) {
                sidecar.stop();
                std::cerr << "smoke-m5-markers-failed code=" << opened.error.code.toStdString() << "\n";
                return 1;
            }
            sidecar.stop();
            std::cout << "smoke-m5-markers-ok events=" << opened.events.size() << " markers=" << store.markers().size() << "\n";
            return 0;
        }
        if (arg == "--smoke-m6-sync") {
            QCoreApplication app(argc, argv);
            bf::VideoSyncState sync;
            sync.setOffsetSeconds(2.0);
            if (sync.videoPositionMs(3.0) != 1000) {
                std::cerr << "smoke-m6-sync-failed offset_mapping\n";
                return 1;
            }
            const bf::VideoSyncCommand unknownDuration = sync.sync(100.0, false, 0, false);
            if (unknownDuration.blank || !unknownDuration.seek || unknownDuration.seekPositionMs != 98000) {
                std::cerr << "smoke-m6-sync-failed unknown_duration\n";
                return 1;
            }
            sync.setDurationMs(10000);
            const bf::VideoSyncCommand beforeStart = sync.sync(1.0, false, 0, true);
            const bf::VideoSyncCommand repeatedBlank = sync.sync(1.0, false, 0, false);
            if (!beforeStart.blank || !beforeStart.pause || !beforeStart.seek || beforeStart.seekPositionMs != 0 || repeatedBlank.seek) {
                std::cerr << "smoke-m6-sync-failed blank_entry\n";
                return 1;
            }
            const bf::VideoSyncCommand playingResync = sync.sync(3.0, true, 1500, false);
            const bf::VideoSyncCommand playingWithinThreshold = sync.sync(3.0, true, 1400, true);
            if (playingResync.blank || !playingResync.play || !playingResync.seek || playingResync.seekPositionMs != 1000 || playingWithinThreshold.seek) {
                std::cerr << "smoke-m6-sync-failed playing_threshold\n";
                return 1;
            }
            const bf::VideoSyncCommand pausedResync = sync.sync(3.0, false, 1051, true);
            const bf::VideoSyncCommand pausedWithinThreshold = sync.sync(3.0, false, 1050, false);
            if (!pausedResync.pause || !pausedResync.seek || pausedResync.seekPositionMs != 1000 || pausedWithinThreshold.seek) {
                std::cerr << "smoke-m6-sync-failed paused_threshold\n";
                return 1;
            }
            const bf::VideoSyncCommand afterEnd = sync.sync(12.0, true, 9000, true);
            sync.setOffsetSeconds(std::numeric_limits<double>::quiet_NaN());
            if (!afterEnd.blank || !afterEnd.pause || !afterEnd.seek || sync.offsetSeconds() != 0.0) {
                std::cerr << "smoke-m6-sync-failed end_or_nonfinite\n";
                return 1;
            }
            bf::PlaybackState playback;
            playback.setSpeed(4.0);
            playback.setSpeed(3.0);
            if (playback.speed() != 4.0) {
                std::cerr << "smoke-m6-sync-failed playback_speed\n";
                return 1;
            }
            std::cout << "smoke-m6-sync-ok playing_threshold_ms=400 paused_threshold_ms=50 blank_idempotent=true speeds=5\n";
            return 0;
        }
        if (arg == "--smoke-m6-video" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "smoke-m6-video-failed i18n=" << i18nError.toStdString() << "\n";
                return 1;
            }
            bf::VideoPanel panel(&i18n);
            panel.resize(480, 280);
            panel.show();
            QString mediaError;
            QObject::connect(&panel, &bf::VideoPanel::loadFailed, &app, [&](const QString& detail) {
                mediaError = detail;
            });
            if (!panel.load(QString::fromLocal8Bit(argv[i + 1]))) {
                std::cerr << "smoke-m6-video-failed load\n";
                return 1;
            }
            panel.setRate(2.0);
            panel.syncTo(0.2, true);
            QEventLoop mediaLoop;
            QTimer mediaTimeout;
            QTimer mediaProbe;
            mediaTimeout.setSingleShot(true);
            mediaProbe.setInterval(25);
            QObject::connect(&mediaTimeout, &QTimer::timeout, &mediaLoop, &QEventLoop::quit);
            QObject::connect(&mediaProbe, &QTimer::timeout, &mediaLoop, [&]() {
                panel.syncTo(0.2, true);
                if (panel.videoDurationSeconds() > 0.0
                    && panel.renderedFrameCount() >= 2
                    && panel.droppedFrameCount() > 0) {
                    mediaLoop.quit();
                }
            });
            mediaTimeout.start(8000);
            mediaProbe.start();
            mediaLoop.exec();
            mediaProbe.stop();
            panel.syncTo(0.2, false);
            const bool pausedFrameReady = panel.videoDurationSeconds() > 0.0
                && panel.renderedFrameCount() >= 2
                && panel.droppedFrameCount() > 0;
            panel.syncTo(-1.0, false);
            const bool beforeBlank = panel.isBlank();
            panel.syncTo(panel.videoDurationSeconds() + 1.0, false);
            const bool afterBlank = panel.isBlank();
            if (!mediaError.isEmpty() || !pausedFrameReady || !beforeBlank || !afterBlank || std::abs(panel.playbackRate() - 2.0) > 1e-12) {
                std::cerr << "smoke-m6-video-failed error=" << mediaError.toStdString()
                          << " summary=" << panel.statusSummary().toStdString() << "\n";
                return 1;
            }
            std::cout << "smoke-m6-video-ok " << panel.statusSummary().toStdString()
                      << " before_blank=true after_blank=true\n";
            return 0;
        }
        if (arg == "--smoke-m4-analysis") {
            QCoreApplication app(argc, argv);
            bf::dsp::Matrix data = bf::dsp::makeMatrix(2, 2048);
            for (int sample = 0; sample < 2048; ++sample) {
                const double t = static_cast<double>(sample) / 250.0;
                data[0][static_cast<std::size_t>(sample)] = std::sin(2.0 * 3.14159265358979323846 * 10.0 * t);
                data[1][static_cast<std::size_t>(sample)] = 0.8 * std::sin(2.0 * 3.14159265358979323846 * 10.0 * t + 0.2);
            }
            bf::AnalysisService service;
            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            bool completed = false;
            QString failure;
            QObject::connect(&service, &bf::AnalysisService::completed, &loop, [&](int, const bf::AnalysisResult& result) {
                completed = result.kind == bf::AnalysisKind::Psd && !result.spectrum.values.empty();
                loop.quit();
            });
            QObject::connect(&service, &bf::AnalysisService::failed, &loop, [&](int, const QString& code, const QString&) {
                failure = code;
                loop.quit();
            });
            QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
            bf::AnalysisRequest psd;
            psd.kind = bf::AnalysisKind::Psd;
            psd.data = data;
            psd.fs = 250.0;
            psd.channel = 0;
            service.request(psd);
            timeout.start(3000);
            loop.exec();
            if (!completed) {
                std::cerr << "smoke-m4-analysis-failed code=" << failure.toStdString() << "\n";
                return 1;
            }
            QEventLoop generationLoop;
            QTimer generationTimeout;
            generationTimeout.setSingleShot(true);
            int completedGeneration = 0;
            QObject::connect(&service, &bf::AnalysisService::completed, &generationLoop, [&](int generation, const bf::AnalysisResult&) {
                completedGeneration = generation;
                generationLoop.quit();
            });
            QObject::connect(&generationTimeout, &QTimer::timeout, &generationLoop, &QEventLoop::quit);
            bf::AnalysisRequest first = psd;
            first.kind = bf::AnalysisKind::BandTrend;
            first.epochSamples = 2;
            const int stale = service.request(first);
            const int latest = service.request(psd);
            generationTimeout.start(3000);
            generationLoop.exec();
            if (completedGeneration != latest || stale == latest) {
                std::cerr << "smoke-m4-analysis-failed latest_generation=" << completedGeneration << "\n";
                return 1;
            }
            bool cancelledCallback = false;
            QEventLoop cancelLoop;
            QTimer cancelTimeout;
            cancelTimeout.setSingleShot(true);
            QObject::connect(&service, &bf::AnalysisService::completed, &cancelLoop, [&](int, const bf::AnalysisResult&) { cancelledCallback = true; });
            QObject::connect(&service, &bf::AnalysisService::failed, &cancelLoop, [&](int, const QString&, const QString&) { cancelledCallback = true; });
            QObject::connect(&cancelTimeout, &QTimer::timeout, &cancelLoop, &QEventLoop::quit);
            service.request(first);
            service.cancel();
            cancelTimeout.start(100);
            cancelLoop.exec();
            if (cancelledCallback) {
                std::cerr << "smoke-m4-analysis-failed cancel_callback=true\n";
                return 1;
            }
            QEventLoop errorLoop;
            QTimer errorTimeout;
            errorTimeout.setSingleShot(true);
            QString errorCode;
            QObject::connect(&service, &bf::AnalysisService::failed, &errorLoop, [&](int, const QString& code, const QString&) {
                errorCode = code;
                errorLoop.quit();
            });
            QObject::connect(&errorTimeout, &QTimer::timeout, &errorLoop, &QEventLoop::quit);
            bf::AnalysisRequest invalid = psd;
            invalid.kind = bf::AnalysisKind::Coherence;
            invalid.channelB = 0;
            service.request(invalid);
            errorTimeout.start(3000);
            errorLoop.exec();
            if (errorCode != "analysis.same_channel") {
                std::cerr << "smoke-m4-analysis-failed same_channel=" << errorCode.toStdString() << "\n";
                return 1;
            }
            std::cout << "smoke-m4-analysis-ok psd=true latest_request=true cancel=true same_channel=" << errorCode.toStdString() << "\n";
            return 0;
        }
        if (arg == "--smoke-m4-render") {
            QApplication app(argc, argv);
            bf::dsp::Spectrum spectrum;
            spectrum.frequencies = {0.0, 5.0, 10.0, 20.0, 40.0};
            spectrum.values = {0.1, 0.5, 1.0, 0.4, 0.2};
            bf::SpectrumWidget spectrumWidget;
            spectrumWidget.resize(480, 260);
            spectrumWidget.setSpectrum(spectrum, "PSD", "uV^2/Hz");
            bf::dsp::Spectrogram spectrogram;
            spectrogram.frequencies = {0.0, 10.0, 20.0};
            spectrogram.times = {0.0, 1.0, 2.0};
            spectrogram.values = {{-30.0, -20.0, -10.0}, {-20.0, -10.0, 0.0}, {-10.0, 0.0, 10.0}};
            bf::SpectrogramWidget spectrogramWidget;
            spectrogramWidget.resize(480, 260);
            spectrogramWidget.setSpectrogram(spectrogram);
            bf::BandTrendWidget trendWidget;
            trendWidget.resize(480, 260);
            trendWidget.setTrend({0.5, 1.5, 2.5}, {{1.0, 2.0, 3.0}, {2.0, 1.0, 2.0}, {1.0, 3.0, 1.0}, {0.5, 1.0, 1.5}, {0.2, 0.4, 0.6}});
            const auto nonBackground = [](QWidget& widget) {
                QImage image(widget.size(), QImage::Format_ARGB32);
                image.fill(QColor("#00FF00"));
                widget.render(&image);
                int count = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        if (image.pixelColor(x, y) != QColor("#00FF00")) {
                            ++count;
                        }
                    }
                }
                return count;
            };
            const int spectrumPixels = nonBackground(spectrumWidget);
            const int spectrogramPixels = nonBackground(spectrogramWidget);
            const int trendPixels = nonBackground(trendWidget);
            std::cout << "smoke-m4-render-ok spectrum=" << spectrumPixels
                      << " spectrogram=" << spectrogramPixels
                      << " trend=" << trendPixels << "\n";
            return (spectrumPixels > 0 && spectrogramPixels > 0 && trendPixels > 0) ? 0 : 1;
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

            bf::MainWindow window(version, &i18n, repoRoot);
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

            bf::MainWindow window(version, &i18n, repoRoot);
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
            bf::MainWindow window(version, &i18n, repoRoot);
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
            bf::MainWindow window(version, &i18n, repoRoot);
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
        if (arg == "--smoke-processing-offline") {
            QCoreApplication app(argc, argv);
            bf::WaveformSlice raw;
            raw.fs = 1000.0;
            raw.t0 = 0.0;
            raw.t1 = 0.256;
            raw.channelNames = {"ch1", "ch2", "ch3", "ch4"};
            raw.data.assign(4, std::vector<float>(256, 0.0f));
            for (int ch = 0; ch < 4; ++ch) {
                for (int i = 0; i < 256; ++i) {
                    const double t = static_cast<double>(i) / raw.fs;
                    raw.data[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] = static_cast<float>(
                        0.5 * std::sin(2.0 * 3.14159265358979323846 * 50.0 * t + 0.1 * ch)
                        + 0.1 * ch);
                }
            }
            bf::ProcessingViewState processing;
            processing.referenceMode = bf::dsp::ReferenceMode::Car;
            processing.notchEnabled = true;
            processing.notchBands = {{true, 50.0}, {false, 100.0}, {false, 150.0}};
            processing.globalBandpassEnabled = true;
            processing.bandpassLow = 0.5;
            processing.bandpassHigh = 40.0;
            const bf::WaveformSlice filtered = bf::applyProcessingToSlice(raw, processing);
            double mean = 0.0;
            int count = 0;
            for (const auto& row : filtered.data) {
                for (float v : row) {
                    mean += static_cast<double>(v);
                    ++count;
                }
            }
            if (count > 0) {
                mean /= static_cast<double>(count);
            }
            std::cout << "smoke-processing-offline-ok channels=" << filtered.data.size()
                      << " names=" << filtered.channelNames.join(",").toStdString()
                      << " mean=" << mean
                      << " summary=" << processing.summary().toStdString() << "\n";
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
                image.fill(QColor("#00FF00"));
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
            bf::MainWindow window(version, &i18n, repoRoot);
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
            bf::MainWindow window(version, &i18n, repoRoot);
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
            bf::MainWindow window(version, &i18n, repoRoot);
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
            image.fill(QColor("#00FF00"));
            widget.render(&image);

            int nonBackground = 0;
            const QRgb background = QColor("#00FF00").rgba();
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
        if (arg == "--smoke-m7-record" && i + 2 < argc) {
            QCoreApplication app(argc, argv);
            const QString sourcePath = QString::fromLocal8Bit(argv[i + 1]);
            const QString outputDir = QString::fromLocal8Bit(argv[i + 2]);
            bf::SidecarClient metadataClient(repoRoot);
            const bf::SidecarHelloResult hello = metadataClient.startAndHello(version, 5000);
            if (!hello.ok) {
                std::cerr << "smoke-m7-record-failed code=" << hello.error.code.toStdString() << "\n";
                return 1;
            }
            const bf::SidecarOpenResult source = metadataClient.openFile(sourcePath, 30000);
            metadataClient.stop();
            if (!source.ok) {
                std::cerr << "smoke-m7-record-failed code=" << source.error.code.toStdString() << "\n";
                return 1;
            }
            bf::RecordController controller(repoRoot, version);
            int exitCode = 1;
            QObject::connect(&controller, &bf::RecordController::started, &app, [&](const bf::RecordSummary&) {
                controller.appendEvent(0.02, "m7-smoke");
            });
            QObject::connect(&controller, &bf::RecordController::stopped, &app, [&](const bf::RecordSummary& summary) {
                const QString edfPath = summary.paths.value("edf").toString();
                const QString h5Path = summary.paths.value("hdf5").toString();
                bf::SidecarClient verify(repoRoot);
                const bf::SidecarHelloResult verifyHello = verify.startAndHello(version, 5000);
                const bf::SidecarOpenResult edf = verifyHello.ok ? verify.openFile(edfPath, 30000) : bf::SidecarOpenResult{};
                const bf::SidecarOpenResult h5 = verifyHello.ok ? verify.openFile(h5Path, 30000) : bf::SidecarOpenResult{};
                verify.stop();
                if (verifyHello.ok && edf.ok && h5.ok && h5.events.size() == 1 && summary.samplesWritten > 0) {
                    std::cout << "smoke-m7-record-ok samples=" << summary.samplesWritten
                              << " source_dropped=" << summary.sourceDroppedChunks
                              << " writer_dropped=" << summary.droppedChunks
                              << " events=" << h5.events.size() << "\n";
                    exitCode = 0;
                } else {
                    std::cerr << "smoke-m7-record-failed code=roundtrip_failed\n";
                }
                app.quit();
            });
            QObject::connect(&controller, &bf::RecordController::failed, &app, [&](const bf::AppError& error) {
                std::cerr << "smoke-m7-record-failed code=" << error.code.toStdString() << "\n";
                app.quit();
            });
            if (!controller.startFromFile(sourcePath, source.fs, source.chNames, source.duration, 0.0, outputDir, {{"animal_id", "smoke"}, {"exp_type", "file_replay"}, {"notes", "M7 smoke"}}, std::max(1, source.nSamples / 2), 2)) {
                std::cerr << "smoke-m7-record-failed code=start_rejected\n";
                return 1;
            }
            QTimer::singleShot(60000, &app, [&]() {
                std::cerr << "smoke-m7-record-failed code=timeout\n";
                controller.stop();
                app.exit(1);
            });
            app.exec();
            return exitCode;
        }
        if (arg == "--smoke-m7-export" && i + 2 < argc) {
            QCoreApplication app(argc, argv);
            const QString sourcePath = QString::fromLocal8Bit(argv[i + 1]);
            const QDir outputDir(QString::fromLocal8Bit(argv[i + 2]));
            bf::SidecarClient metadataClient(repoRoot);
            const bf::SidecarHelloResult hello = metadataClient.startAndHello(version, 5000);
            const bf::SidecarOpenResult source = hello.ok ? metadataClient.openFile(sourcePath, 30000) : bf::SidecarOpenResult{};
            metadataClient.stop();
            if (!hello.ok || !source.ok) {
                std::cerr << "smoke-m7-export-failed code=source_open_failed\n";
                return 1;
            }
            bf::ExportController controller(repoRoot, version);
            const QStringList outputs{
                outputDir.filePath("m7_export.csv"),
                outputDir.filePath("m7_export.h5"),
                outputDir.filePath("m7_export.edf"),
            };
            QVector<bf::SidecarEvent> events{{std::min(0.02, source.duration), "m7-smoke"}};
            int outputIndex = 0;
            int timerTicks = 0;
            int exitCode = 1;
            QTimer activityTimer;
            activityTimer.setInterval(1);
            QObject::connect(&activityTimer, &QTimer::timeout, [&]() { ++timerTicks; });
            std::function<void()> startNext;
            startNext = [&]() {
                if (outputIndex >= outputs.size()) {
                    std::cout << "smoke-m7-export-ok formats=3 timer_ticks=" << timerTicks << "\n";
                    exitCode = timerTicks > 0 ? 0 : 1;
                    app.quit();
                    return;
                }
                const double t1 = source.duration;
                if (!controller.exportAsync(sourcePath, 0.0, t1, source.duration, outputs[outputIndex], events)) {
                    std::cerr << "smoke-m7-export-failed code=start_rejected\n";
                    app.quit();
                }
            };
            QObject::connect(&controller, &bf::ExportController::completed, &app, [&](const bf::SidecarExportResult& result) {
                bf::SidecarClient verify(repoRoot);
                const bf::SidecarHelloResult verifyHello = verify.startAndHello(version, 5000);
                const bf::SidecarOpenResult reopened = verifyHello.ok ? verify.openFile(result.path, 30000) : bf::SidecarOpenResult{};
                verify.stop();
                if (!verifyHello.ok || !reopened.ok || result.nSamples <= 0 || result.markerCount != 1) {
                    std::cerr << "smoke-m7-export-failed code=roundtrip_failed\n";
                    app.quit();
                    return;
                }
                ++outputIndex;
                QTimer::singleShot(0, &app, startNext);
            });
            QObject::connect(&controller, &bf::ExportController::failed, &app, [&](const bf::AppError& error) {
                std::cerr << "smoke-m7-export-failed code=" << error.code.toStdString() << "\n";
                app.quit();
            });
            activityTimer.start();
            QTimer::singleShot(0, &app, startNext);
            QTimer::singleShot(60000, &app, [&]() {
                std::cerr << "smoke-m7-export-failed code=timeout\n";
                app.exit(1);
            });
            app.exec();
            return exitCode;
        }
    }

    QApplication app(argc, argv);
    // Fusion makes menu/button colors honor QPalette + stylesheets on Windows.
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"))) {
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("BoundFree EEG Native");
    parser.addHelpOption();
    const QCommandLineOption versionOpt("version", "Print product version.");
    const QCommandLineOption smokeSidecarOpt("smoke-sidecar", "Start sidecar, run hello, and exit.");
    const QCommandLineOption smokeSidecarOpenOpt("smoke-sidecar-open", "Start sidecar, open a file, and exit.", "path");
    const QCommandLineOption smokeOpt("smoke-ui", "Start UI and exit automatically.");
    const QCommandLineOption smokeLicenseGateOpt("smoke-license-gate", "Open the release license gate and cancel automatically.");
    const QCommandLineOption languageOpt("language", "Set startup locale.", "locale", "zh_CN");
    const QCommandLineOption openFileOpt("open-file", "Open an EEG file after startup.", "path");
    const QCommandLineOption openVideoOpt("open-video", "Open a synchronized video after startup.", "path");
    parser.addOptions({versionOpt, smokeSidecarOpt, smokeSidecarOpenOpt, smokeOpt, smokeLicenseGateOpt, languageOpt, openFileOpt, openVideoOpt});
    parser.process(app);

    bf::I18nService i18n(repoRoot);
    QString i18nError;
    if (!i18n.loadLocale(parser.value(languageOpt), &i18nError)) {
        std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
        return 1;
    }
#ifdef BF_REQUIRE_LICENSE
    if (!ensureOfflineLicense(version, repoRoot, i18n, parser.isSet(smokeLicenseGateOpt))) {
        if (parser.isSet(smokeLicenseGateOpt)) {
            std::cout << "smoke-license-gate-ok\n";
        }
        return 0;
    }
#endif

    bf::MainWindow window(version, &i18n, repoRoot);
    bf::SidecarClient sidecar(repoRoot);
    bf::FileOpenController fileController(repoRoot, version);
    window.setOpenFileHandler([&](const QString& path) {
        fileController.openInitialWindow(path);
    });
    window.setWindowRequestHandler([&](const QString& path, double t0, double windowSeconds) {
        fileController.openWindow(path, t0, windowSeconds);
    });
    window.setExportMarkersHandler([&](const QString& path, const QVector<bf::SidecarEvent>& events) {
        return sidecar.exportMarkersCsv(path, events, 5000);
    });
    QObject::connect(&fileController, &bf::FileOpenController::loaded, &window, [&](const bf::FileOpenResult& result) {
        window.showFileOpenResult(result);
        if (parser.isSet(openVideoOpt) && !window.loadVideoForBrowser(parser.value(openVideoOpt))) {
            std::cerr << "smoke-ui-file-flow-failed code=video_load_failed\n";
            QTimer::singleShot(0, &app, [&]() { app.exit(1); });
            return;
        }
        if (parser.isSet(smokeOpt) && parser.isSet(openFileOpt)) {
            std::cout << "smoke-ui-file-flow-ok format=" << result.session.format.toStdString()
                      << " n_channels=" << result.session.nChannels
                      << " t0=" << result.waveform.t0
                      << " t1=" << result.waveform.t1;
            if (parser.isSet(openVideoOpt)) {
                std::cout << " video=" << window.videoStatusSummary().toStdString();
            }
            std::cout << "\n";
            QTimer::singleShot(parser.isSet(openVideoOpt) ? 500 : 0, &app, &QCoreApplication::quit);
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
