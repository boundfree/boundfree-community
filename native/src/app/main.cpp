#include "ui/unified_signal_smoke.h"
#include <QFileDialog>
#include "ui/live_ica_smoke.h"
#include "ui/simulator_ica_smoke.h"
#include "controller/file_open_controller.h"
#include "controller/export_controller.h"
#include "controller/export_data_modes_smoke.h"
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
#include "ui/activation_dialog.h"
#include "render/minmax_decimator.h"
#include "render/waveform_slice.h"
#include "ui/main_window.h"
#include "ui/fluent_controls.h"
#include "ui/device_waveform_smoke.h"
#include "ui/capture_replay_smoke.h"
#include "ui/empty_controls_smoke.h"
#include "ui/scrub_file_smoke.h"
#include "ui/analysis_text_smoke.h"
#include "ui/analysis_refresh_smoke.h"
#include "ui/reference_labels_smoke.h"
#include "ui/waveform_data_modes_smoke.h"
#include "ui/analysis_widgets.h"
#include "ui/plot_style.h"
#include "ui/waveform_widget.h"
#include "ui/video_panel.h"

#include <QApplication>
#include <QScrollBar>
#include <QMenuBar>
#include <QMenu>
#include <QStyleFactory>
#include <QAction>
#include <QDir>
#include <QDialog>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDockWidget>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QFile>
#include <QFontMetrics>
#include <QImage>
#include <QKeySequence>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QMessageBox>
#include <QPalette>
#include <QStatusBar>
#include <QSplitter>
#include <QScrollArea>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <QEventLoop>
#include <limits>
#include <vector>

#ifdef BF_REQUIRE_LICENSE
namespace {

bool ensureOfflineLicense(const bf::VersionInfo& version, const QString& repoRoot, const bf::I18nService& i18n, bool smokeGate) {
    // Automated smoke gate verifies the gate path without waiting for online network or interactive dialogs.
    if (smokeGate) {
        return false;
    }

    bf::LicenseManager manager(version.version, repoRoot);

    // Level 1: check saved license.dat exactly once (no network, instant).
    // The result — including structured clock-watermark failures — is kept
    // and handed to the activation dialog so the startup failure is shown
    // in the status bar instead of being lost.
    const bf::LicenseResult startupCheck = manager.checkSavedLicense();
    if (startupCheck.ok()) {
        // Legacy keys are offline-compatible: do not let an absent or stale
        // server record invalidate a locally verified offline authorization.
        if (!bf::ActivationDialog::requiresOnlineStatusCheck(startupCheck)) {
            return true;
        }
        // Server-managed keys retain remote revocation checks and fail open on
        // a network error or timeout.
        if (bf::ActivationDialog::checkAndHandleRevocation(
                &manager, version, const_cast<bf::I18nService*>(&i18n))) {
            return true;
        }
    }

    {
        bf::ActivationDialog dlg(&manager, version, const_cast<bf::I18nService*>(&i18n),
                                 startupCheck);
        if (dlg.exec() == QDialog::Accepted) {
            return true;
        }
    }

    return false;
}

}  // namespace
#endif

namespace {

// smoke 失败诊断：打印错误码与线协议参数（含 reason），无需改跑 UI 就能定位失败原因。
QString formatErrorDetail(const bf::AppError& error) {
    const QByteArray params = QJsonDocument(error.params).toJson(QJsonDocument::Compact);
    return QString("%1 params=%2").arg(error.code, QString::fromUtf8(params));
}

}  // namespace

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
                const bool hitTestOk = window.menuBar()->actionAt(geom.center()) == menuAction;
                const QPoint windowPoint = window.menuBar()->mapTo(&window, geom.center());
                QWidget* localTarget = window.childAt(windowPoint);
                const bool localHitOk = (localTarget == window.menuBar())
                    || (localTarget != nullptr && window.menuBar()->isAncestorOf(localTarget));

                const QPoint globalCenter = window.menuBar()->mapToGlobal(geom.center());
                QWidget* screenTarget = QApplication::widgetAt(globalCenter);
                // When running under an active interactive desktop, screenTarget checks the real
                // top-level window. In headless, locked-screen, or background runner sessions,
                // the platform WindowFromPoint returns null; in that case, localHitOk guarantees
                // no rogue overlapping child widgets (such as orphan panels) occlude the menu bar.
                const bool screenHitOk = (screenTarget != nullptr)
                    ? (screenTarget == window.menuBar() || window.menuBar()->isAncestorOf(screenTarget))
                    : localHitOk;
                if (!screenHitOk) {
                    std::cerr << "menu-hit-debug title=" << title.toStdString()
                              << " geom=" << geom.x() << "," << geom.y() << "," << geom.width() << "x" << geom.height()
                              << " global=" << globalCenter.x() << "," << globalCenter.y()
                              << " localTarget=" << (localTarget != nullptr ? localTarget->metaObject()->className() : "null")
                              << " target=" << (screenTarget != nullptr ? screenTarget->metaObject()->className() : "null")
                              << " targetObject=" << (screenTarget != nullptr ? screenTarget->objectName().toStdString() : std::string())
                              << " targetGeom=";
                    if (screenTarget != nullptr) {
                        const QRect targetGeom = screenTarget->geometry();
                        std::cerr << targetGeom.x() << "," << targetGeom.y() << "," << targetGeom.width() << "x" << targetGeom.height();
                    }
                    std::cerr << " menubar=" << window.menuBar()->geometry().x() << "," << window.menuBar()->geometry().y()
                              << "," << window.menuBar()->geometry().width() << "x" << window.menuBar()->geometry().height()
                              << "\n";
                }
                const bool popupBound = menuAction->menu() != nullptr;
                const bool interactive = menuAction->isEnabled() && menuAction->isVisible()
                    && visibleWidth && hitTestOk && screenHitOk && popupBound;
                hasFileMenu = hasFileMenu || (title == i18n.text("app.menu.file") && interactive);
                hasLanguageMenu = hasLanguageMenu || (title == i18n.text("app.menu.language") && interactive);
                hasViewMenu = hasViewMenu || (title == i18n.text("app.menu.view") && interactive);
                hasThemeMenu = hasThemeMenu || (title == i18n.text("app.menu.theme") && interactive);
                menuGeometryOk = menuGeometryOk && !title.trimmed().isEmpty() && interactive;
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

            // Re-apply the archived light then dark theme and verify both.
            light->trigger();
            app.processEvents();
            const auto lightColors = bf::surfaceColors();
            const bool lightApplied = window.palette().color(QPalette::Window) == lightColors.bgCanvas
                && window.palette().color(QPalette::WindowText) == lightColors.textPrimary;
            dark->trigger();
            app.processEvents();
            const auto darkColors = bf::surfaceColors();
            const bool darkApplied = window.palette().color(QPalette::Window) == darkColors.bgCanvas
                && window.palette().color(QPalette::Highlight) == darkColors.accentDefault
                && darkColors.bgCanvas != lightColors.bgCanvas;
            const bool emptyViewDisabled = !channels->isEnabled();

            bool hasIdleStatus = false;
            const QList<QLabel*> statusLabels = window.statusBar()->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly);
            for (QLabel* label : statusLabels) {
                hasIdleStatus = hasIdleStatus || label->text() == i18n.text("app.status.mode.idle");
            }
            bf::FileOpenResult data;
            data.session.ok = true; data.session.path = "system-ui.csv";
            data.session.fs = 100; data.session.duration = 10;
            data.session.nChannels = 1; data.session.chNames = {"ch1"};
            data.waveform.fs = 100; data.waveform.t0 = 0; data.waveform.t1 = 10;
            data.waveform.channelNames = data.session.chNames;
            data.waveform.data = {std::vector<float>(1000)};
            window.showFileOpenResult(data);
            const bool beforeToggle = channels->isChecked();
            channels->trigger(); app.processEvents();
            auto* tabs = window.findChild<QTabWidget*>("eegParameterTabs");
            const bool dockToggled = channels->isEnabled() && channels->isChecked() != beforeToggle
                && tabs && !tabs->isTabVisible(0);
            if (!lightApplied || !darkApplied || !emptyViewDisabled || !dockToggled || statusLabels.size() < 2 || !hasIdleStatus) {
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
        if (arg == "--smoke-file-session-reuse" && i + 1 < argc) {
            QCoreApplication app(argc, argv);
            const QString fixtureRoot = QString::fromLocal8Bit(argv[i + 1]);
            auto controller = std::make_unique<bf::FileOpenController>(fixtureRoot, version);
            int loaded = 0;
            int failed = 0;
            QString lastErrorCode;
            bf::FileOpenResult last;
            QObject::connect(controller.get(), &bf::FileOpenController::loaded, &app,
                             [&](const bf::FileOpenResult& result) { ++loaded; last = result; });
            QObject::connect(controller.get(), &bf::FileOpenController::failed, &app,
                             [&](const bf::AppError& error) { ++failed; lastErrorCode = error.code; });
            const auto waitFor = [](const std::function<bool()>& condition) {
                QElapsedTimer elapsed;
                elapsed.start();
                while (!condition() && elapsed.elapsed() < 5000) {
                    QEventLoop loop;
                    QTimer::singleShot(5, &loop, &QEventLoop::quit);
                    loop.exec();
                }
                return condition();
            };
            const auto fail = [](const char* reason) {
                std::cerr << "smoke-file-session-reuse-failed " << reason << '\n';
                return 1;
            };
            const auto openAndWait = [&](const QString& path, double t0) {
                const int beforeLoaded = loaded;
                const int beforeFailed = failed;
                controller->openWindow(path, t0, 1.0);
                return waitFor([&]() { return loaded != beforeLoaded || failed != beforeFailed; })
                    && loaded == beforeLoaded + 1 && failed == beforeFailed
                    && last.session.path == path && std::abs(last.waveform.t0 - t0) < 1e-9
                    && last.waveform.data.size() == 1 && last.waveform.data[0].size() == 10
                    && std::abs(last.waveform.data[0][0] - (t0 + 1.0)) < 1e-6;
            };
            if (!openAndWait("a.csv", 0) || !openAndWait("a.csv", 1)
                || !openAndWait("a.csv", 2)) return fail("reuse");
            const int beforeReopen = loaded;
            controller->openInitialWindow("a.csv", 1);
            if (!waitFor([&]() { return loaded != beforeReopen || failed != 0; })
                || loaded != beforeReopen + 1 || failed != 0 || last.waveform.t0 != 0)
                return fail("explicit_reopen");
            if (!openAndWait("b.csv", 0)) return fail("switch");

            const int beforeBurst = loaded;
            controller->openWindow("b.csv", 10, 1);
            if (!waitFor([&]() { return QFile::exists(fixtureRoot + "/burst-started"); })) return fail("burst_start");
            controller->openWindow("b.csv", 11, 1);
            controller->openWindow("b.csv", 12, 1);
            controller->openWindow("b.csv", 13, 1);
            QFile release(fixtureRoot + "/burst-release");
            if (!release.open(QIODevice::WriteOnly)) return fail("burst_release");
            release.close();
            if (!waitFor([&]() { return loaded != beforeBurst || failed != 0; })
                || loaded != beforeBurst + 1 || failed != 0 || last.waveform.t0 != 13)
                return fail("latest_request");

            controller->openWindow("bad.csv", 0, 1);
            if (!waitFor([&]() { return failed != 0; }) || failed != 1
                || lastErrorCode != "reader.file_not_found") return fail("open_error");
            if (!openAndWait("a.csv", 0)) return fail("open_error_recovery");
            controller->openWindow("a.csv", 30, 1);
            if (!waitFor([&]() { return failed != 1; }) || failed != 2
                || !lastErrorCode.startsWith("ipc.")) return fail("peer_crash");
            if (!openAndWait("a.csv", 1)) return fail("peer_crash_recovery");

            const int beforeDestroy = loaded;
            controller->openWindow("a.csv", 20, 1);
            if (!waitFor([&]() { return QFile::exists(fixtureRoot + "/destroy-started"); })) return fail("destroy_start");
            controller->openWindow("a.csv", 21, 1);
            controller.reset(); // Waits for active IPC, drops pending work and queued callbacks.
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
            if (loaded != beforeDestroy || failed != 2) return fail("callback_after_destroy");
            std::cout << "smoke-file-session-reuse-ok reuse=true switch=true latest=true recovery=true destroy=true\n";
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
                std::cout << "smoke-sidecar-open-error " << formatErrorDetail(opened.error).toStdString() << "\n";
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
                std::cerr << "smoke-open-file-failed " << formatErrorDetail(opened.error).toStdString() << "\n";
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
            const bf::SidecarHelloResult hello = sidecar.startAndHello(version, 30000);
            if (!hello.ok) {
                std::cerr << "smoke-open-slice-failed code=" << hello.error.code.toStdString() << "\n";
                return 1;
            }
            const bf::SidecarOpenResult opened = sidecar.openFile(QString::fromLocal8Bit(argv[i + 1]), 30000);
            if (!opened.ok) {
                sidecar.stop();
                std::cerr << "smoke-open-slice-failed " << formatErrorDetail(opened.error).toStdString() << "\n";
                return 1;
            }
            const double t0 = QString::fromLocal8Bit(argv[i + 2]).toDouble();
            const double t1 = QString::fromLocal8Bit(argv[i + 3]).toDouble();
            const bf::SidecarSliceResult slice = sidecar.getSlice(opened.handle, t0, t1, 30000);
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
            result.session.duration = 0.01;
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
        if (arg == "--smoke-ui-analysis-refresh") return smokeAnalysisRefresh(argc, argv, version, repoRoot);
        if (arg == "--smoke-ui-analysis-text") return smokeAnalysisText(argc, argv, version, repoRoot);
        if (arg == "--smoke-ui-reference-labels") return smokeReferenceLabels(argc, argv, version, repoRoot);
        if (arg == "--smoke-ui-waveform-data-modes") return smokeWaveformDataModes(argc, argv, version, repoRoot);
        if (arg == "--smoke-export-data-modes" && i + 2 < argc)
            return smokeExportDataModes(argc, argv, version, repoRoot,
                QString::fromLocal8Bit(argv[i + 1]), QString::fromLocal8Bit(argv[i + 2]));
        if (arg == "--smoke-ui-analysis-presentation") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("en_US", &error)) return 1;
            bf::MainWindow window(version, &i18n, repoRoot);
            window.resize(1280, 800);
            auto* service = window.findChild<bf::AnalysisService*>();
            auto* tabs = window.findChild<QTabWidget*>("eegAnalysisTabs");
            const auto fail = [](const char* reason) { std::cerr << "smoke-ui-analysis-presentation-failed " << reason << '\n'; return 1; };
            if (!service || !tabs) return fail("missing_service");
            const auto wait = [&](const std::function<bool()>& done) {
                QElapsedTimer elapsed; elapsed.start();
                while (!done() && elapsed.elapsed() < 4000) {
                    QEventLoop loop; QTimer::singleShot(5, &loop, &QEventLoop::quit); loop.exec();
                }
                return done();
            };
            auto label = [&](const char* role, int page) {
                return window.findChild<QLabel*>(QString("eegAnalysis%1_%2").arg(role).arg(page));
            };
            auto state = [&](int page) { auto* value = label("Status", page); return value ? value->property("analysisState").toString() : QString(); };
            auto generation = [&](int page) { return label("Status", page)->property("analysisGeneration").toInt(); };
            auto plot = [&](int page) { return window.findChild<QWidget*>(QString("eegAnalysisPlot_%1").arg(page)); };
            bf::FileOpenResult input;
            input.session.ok = true; input.session.path = "presentation.csv"; input.session.format = "CSV";
            input.session.fs = 200; input.session.duration = 10; input.session.nSamples = 2000; input.session.nChannels = 2;
            input.session.chNames = {"alpha", "beta"};
            input.waveform.fs = 200; input.waveform.t0 = 0; input.waveform.t1 = 10;
            input.waveform.channelNames = input.session.chNames; input.waveform.data.resize(2);
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 2000; ++n)
                input.waveform.data[ch].push_back(30 * std::sin(2 * 3.141592653589793 * (8 + ch * 3) * n / 200.)
                    + 10 * std::sin(2 * 3.141592653589793 * 20 * n / 200.));
            window.showFileOpenResult(input); window.show();
            const QString screenshotDir = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
            if (!screenshotDir.isEmpty()) QDir().mkpath(screenshotDir);
            int oldGeneration = 0;
            const std::array<bf::AnalysisKind, 4> kinds{bf::AnalysisKind::Psd, bf::AnalysisKind::Stft, bf::AnalysisKind::BandTrend, bf::AnalysisKind::Coherence};
            const char* titleKeys[] = {"app.analysis.psd", "app.analysis.stft", "app.analysis.trend", "app.analysis.coherence"};
            for (int page = 0; page < 4; ++page) {
                tabs->setCurrentIndex(page);
                if (!wait([&]() { return state(page) == "ready"; })) return fail("real_analysis_not_ready");
                // Text changes schedule height-for-width layout updates.
                QEventLoop textLayout; QTimer::singleShot(40, &textLayout, &QEventLoop::quit); textLayout.exec();
                if (!plot(page) || !label("Title", page) || !label("Legend", page) || !label("Summary", page)) return fail("missing_external_labels");
                for (const char* role : {"Title", "Status", "Legend", "Summary"}) {
                    auto* text = label(role, page);
                    if (text->parentWidget() == plot(page) || plot(page)->isAncestorOf(text)) return fail("attribute_inside_plot");
                    const QRect textBounds(text->mapTo(&window, QPoint()), text->size());
                    const QRect plotBounds(plot(page)->mapTo(&window, QPoint()), plot(page)->size());
                    if (text->isVisible() && textBounds.intersects(plotBounds)) return fail("attribute_overlaps_plot");
                }
                if (label("Title", page)->text() != i18n.text(titleKeys[page])) return fail("wrong_title");
                const int completed = generation(page);
                if (oldGeneration) {
                    service->failed(oldGeneration, "analysis.failed", "stale-internal-detail");
                    if (state(page) != "ready") return fail("stale_failure_applied");
                }
                if (!i18n.loadLocale("zh_CN", &error)) return fail("locale");
                if (label("Title", page)->text() != i18n.text(titleKeys[page]) || !service->isCurrent(completed)) return fail("retranslate_recomputed");
                app.processEvents();
                if (!screenshotDir.isEmpty()) window.grab().save(QDir(screenshotDir).filePath(QString("analysis-page-%1-zh_CN.png").arg(page)));
                if (!i18n.loadLocale("en_US", &error)) return fail("locale_restore");
                const QImage lightPlot = plot(page)->grab().toImage();
                QAction* darkTheme = nullptr; QAction* lightTheme = nullptr;
                for (auto* action : window.findChildren<QAction*>()) {
                    if (action->text() == i18n.text("app.theme.dark")) darkTheme = action;
                    if (action->text() == i18n.text("app.theme.light")) lightTheme = action;
                }
                if (!darkTheme || !lightTheme) return fail("theme_actions");
                darkTheme->trigger(); app.processEvents();
                if (state(page) != "ready" || !service->isCurrent(completed)
                    || plot(page)->grab().toImage() == lightPlot) return fail("theme_recomputes_or_keeps_old_colors");
                if (!screenshotDir.isEmpty()) window.grab().save(QDir(screenshotDir).filePath(QString("analysis-page-%1-dark.png").arg(page)));
                lightTheme->trigger(); app.processEvents();
                const QImage ready = plot(page)->grab().toImage();
                service->failed(completed, "analysis.failed", "not-user-visible");
                if (state(page) != "failed" || !label("Summary", page)->text().isEmpty()
                    || label("Status", page)->text().contains("not-user-visible") || ready == plot(page)->grab().toImage()) return fail("failed_did_not_clear");
                // A normal same-file seek requests fresh data on this page.
                window.showFileOpenResult(input);
                if (state(page) != "pending") return fail("missing_pending");
                const int pending = generation(page);
                service->cancel(); // Deterministic empty response, without a racing real completion.
                bf::AnalysisResult empty; empty.kind = kinds[page];
                service->completed(pending, empty);
                if (state(page) != "empty" || !label("Summary", page)->text().isEmpty()) return fail("empty_marked_ready");
                window.showFileOpenResult(input);
                if (!wait([&]() { return state(page) == "ready"; })) return fail("recovery");
                oldGeneration = generation(page);
            }
            window.setBrowserWindowSecondsForBrowser(1);
            tabs->setCurrentIndex(0);
            const int heldGeneration = generation(0);
            service->cancel(); // Hold a pending UI task while playback keeps moving.
            QAction* play = nullptr;
            for (auto* action : window.findChildren<QAction*>())
                if (action->text() == i18n.text("app.playback.play")) play = action;
            if (!play || state(0) != "pending") return fail("background_setup");
            play->trigger();
            QEventLoop backgroundWait; QTimer::singleShot(750, &backgroundWait, &QEventLoop::quit); backgroundWait.exec();
            if (generation(0) != heldGeneration || state(0) != "pending") return fail("background_restarts_pending");
            auto* position = window.findChild<QDoubleSpinBox*>("eegPlaybackPosition");
            if (!position || position->value() < .2) return fail("background_froze_playback");
            play->trigger(); // Explicit pause may supersede the old pending analysis.
            if (generation(0) == heldGeneration || !wait([&]() { return state(0) == "ready"; })) return fail("explicit_refresh_cannot_supersede");
            oldGeneration = generation(0);
            window.showFileOpenLoading("new-file.csv");
            for (int page = 0; page < 4; ++page)
                if (state(page) != "idle" || !label("Summary", page)->text().isEmpty()) return fail("file_load_keeps_old_result");
            service->failed(oldGeneration, "analysis.failed", "obsolete");
            if (state(3) != "idle") return fail("load_accepts_stale");
            std::cout << "smoke-ui-analysis-presentation-ok pages=4 external=true states=true locale=true stale=true background=true\n";
            return 0;
        }
        if (arg == "--smoke-ui-record-form") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("zh_CN", &error)) return 1;
            bf::MainWindow window(version, &i18n, repoRoot);
            bf::FileOpenResult result;
            result.session.ok = true; result.session.path = "record-form.csv"; result.session.format = "CSV";
            result.session.fs = 100; result.session.duration = 10;
            result.session.nChannels = 1; result.session.chNames = {"a"};
            result.waveform.fs = 100; result.waveform.t0 = 0; result.waveform.t1 = 10;
            result.waveform.channelNames = result.session.chNames;
            result.waveform.data = {std::vector<float>(1000, 1)};
            window.showFileOpenResult(result);
            window.show();
            QAction* record = nullptr;
            for (auto* action : window.findChildren<QAction*>())
                if (action->text() == i18n.text("app.record.start")) record = action;
            if (!record) return 1;
            bool found = false;
            QTimer::singleShot(0, &window, [&]() {
                auto* dialog = window.findChild<QDialog*>("recordMetadataDialog");
                found = dialog && dialog->findChild<QWidget*>("recordAnimalId")
                    && dialog->findChild<QWidget*>("recordExperimentType")
                    && dialog->findChild<QWidget*>("recordNotes")
                    && dialog->findChild<QWidget*>("recordOutputDirectory");
                if (!dialog) { app.exit(1); return; }
                const QString directory = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
                if (!directory.isEmpty() && QDir().mkpath(directory))
                    dialog->grab().save(QDir(directory).filePath("record-form-zh_CN.png"));
                dialog->reject();
            });
            record->trigger();
            if (!found || !window.recordStatusSummary().startsWith("active=0 samples=0")
                || record->text() != i18n.text("app.record.start")) {
                std::cerr << "smoke-ui-record-form-failed cancel\n"; return 1;
            }
            std::cout << "smoke-ui-record-form-ok fields=true cancel=true\n";
            return 0;
        }
        if (arg == "--smoke-ui-channel-parameters") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("en_US", &error)) return 1;
            bf::MainWindow window(version, &i18n, repoRoot);
            bf::FileOpenResult result;
            result.session.ok = true; result.session.path = "channels.csv"; result.session.format = "CSV";
            result.session.fs = 200; result.session.duration = 4;
            result.session.nChannels = 3; result.session.nSamples = 800;
            result.session.chNames = {"a", "b", "c"};
            result.waveform.fs = 200; result.waveform.t0 = 0; result.waveform.t1 = 4;
            result.waveform.channelNames = result.session.chNames;
            result.waveform.data.resize(3);
            for (int ch = 0; ch < 3; ++ch)
                for (int n = 0; n < 800; ++n)
                    result.waveform.data[ch].push_back((ch + 1) * 20 * std::sin(2 * 3.141592653589793 * 10 * n / 200.)
                        + 30 * std::sin(2 * 3.141592653589793 * 60 * n / 200.));
            window.showFileOpenResult(result);
            auto* waveform = window.findChild<bf::WaveformWidget*>();
            auto* automatic = window.findChild<QCheckBox*>("eegAutoAmplitude");
            auto* amplitude = window.findChild<QDoubleSpinBox*>("eegManualAmplitude");
            const auto fail = [](const char* reason) { std::cerr << "smoke-ui-channel-parameters-failed " << reason << '\n'; return 1; };
            if (!waveform || !automatic || !amplitude || !automatic->isChecked() || amplitude->isEnabled()) return fail("auto_default");
            automatic->setChecked(false);
            amplitude->setValue(500);
            if (!amplitude->isEnabled() || waveform->autoScaleEnabled()
                || waveform->channelAmplitudeRange(0) != std::make_pair(-500., 500.)) return fail("manual_amplitude");
            automatic->setChecked(true);
            if (!waveform->autoScaleEnabled() || amplitude->isEnabled()) return fail("auto_restore");
            auto mode = [&](int ch) { return window.findChild<QComboBox*>(QString("eegBandpassMode_%1").arg(ch)); };
            auto low = [&](int ch) { return window.findChild<QDoubleSpinBox*>(QString("eegBandpassLow_%1").arg(ch)); };
            auto high = [&](int ch) { return window.findChild<QDoubleSpinBox*>(QString("eegBandpassHigh_%1").arg(ch)); };
            if (!mode(0) || !low(0) || !high(0)) return fail("row_controls");
            auto* originalMode = mode(0);
            mode(0)->setCurrentIndex(2);
            low(0)->setValue(5); high(0)->setValue(20);
            QMetaObject::invokeMethod(high(0), "editingFinished", Qt::DirectConnection);
            const auto state = window.processingViewState();
            if (mode(0) != originalMode || state.perChannelBandpass.size() != 1
                || state.perChannelBandpass.at(0).low != 5 || state.perChannelBandpass.at(0).high != 20) return fail("override_identity");
            const auto model = waveform->cachedDisplayModel();
            const auto component = [](const std::vector<float>& samples, double frequency) {
                double sum = 0;
                for (int n = 100; n < 700; ++n) sum += samples[n] * std::sin(2 * 3.141592653589793 * frequency * n / 200.);
                return std::abs(sum / 300);
            };
            if (component(model.data[0], 60) > 3 || component(model.data[0], 10) < 12
                || component(model.data[1], 60) < 25) return fail("channel_isolation");
            high(0)->setValue(2);
            QMetaObject::invokeMethod(high(0), "editingFinished", Qt::DirectConnection);
            if (window.processingViewState().perChannelBandpass.at(0).high != 20) return fail("invalid_range_committed");
            high(0)->setValue(20);
            QMetaObject::invokeMethod(high(0), "editingFinished", Qt::DirectConnection);
            mode(0)->setCurrentIndex(0);
            if (!window.processingViewState().perChannelBandpass.empty()) return fail("inherit_keeps_override");
            auto global = window.processingViewState();
            global.globalBandpassEnabled = true; global.bandpassLow = 5; global.bandpassHigh = 20;
            window.setProcessingViewStateForBrowser(global);
            mode(1)->setCurrentIndex(1);
            const auto disabled = waveform->cachedDisplayModel();
            if (component(disabled.data[0], 60) > 3 || component(disabled.data[1], 60) < 25) return fail("disabled_does_not_bypass_global");
            auto bipolar = window.processingViewState();
            bipolar.referenceMode = bf::dsp::ReferenceMode::Bipolar;
            bipolar.setChannelBandpass(0, {true, 5, 20});
            window.setProcessingViewStateForBrowser(bipolar);
            if (waveform->cachedDisplayModel().data.size() != 2 || !mode(0) || mode(0)->currentIndex() != 2) return fail("bipolar_rows");
            const auto expected = bf::applyProcessingToSlice(result.waveform, bipolar);
            if (waveform->cachedDisplayModel().data != expected.data) return fail("bipolar_mapping");
            window.setChannelVisibleForBrowser(0, false);
            if (waveform->cachedDisplayModel().data.size() != 1) return fail("channel_visibility");
            auto other = result;
            other.session.path = "other.csv";
            window.showFileOpenResult(other);
            if (!window.processingViewState().perChannelBandpass.empty()) return fail("new_file_keeps_overrides");
            // A low-rate file can inherit a high cutoff above Nyquist. The
            // user must still be able to edit a valid draft or disable it.
            auto inherited = window.processingViewState();
            inherited.globalBandpassEnabled = false;
            inherited.bandpassHigh = 40;
            window.setProcessingViewStateForBrowser(inherited);
            auto lowRate = result;
            lowRate.session.path = "low-rate.csv";
            lowRate.session.fs = 50; lowRate.waveform.fs = 50;
            window.showFileOpenResult(lowRate);
            mode(0)->setCurrentIndex(2);
            high(0)->setValue(30);
            QMetaObject::invokeMethod(high(0), "editingFinished", Qt::DirectConnection);
            if (window.processingViewState().perChannelBandpass.count(0) &&
                window.processingViewState().perChannelBandpass.at(0).high >= 25) return fail("nyquist_draft_committed");
            if (!low(0)->isEnabled() || !high(0)->isEnabled()) return fail("invalid_draft_locked");
            high(0)->setValue(15);
            QMetaObject::invokeMethod(high(0), "editingFinished", Qt::DirectConnection);
            if (!window.processingViewState().perChannelBandpass.count(0)
                || window.processingViewState().perChannelBandpass.at(0).high != 15) return fail("draft_correction");
            high(0)->setValue(30);
            QMetaObject::invokeMethod(high(0), "editingFinished", Qt::DirectConnection);
            mode(0)->setCurrentIndex(1);
            if (window.processingViewState().perChannelBandpass.at(0).enabled) return fail("invalid_draft_cannot_disable");
            std::cout << "smoke-ui-channel-parameters-ok amplitude=true filters=true identity=true bipolar=true\n";
            return 0;
        }
        if (arg == "--smoke-ui-device-waveform") return bf::smokeDeviceWaveform(argc, argv, version, repoRoot);
        if (arg == "--smoke-ui-unified-tools") return bf::smokeUnifiedSignalTools(argc,argv,version,repoRoot);
        if (arg == "--smoke-ui-live-ica") return bf::smokeLiveIca(argc,argv,version,repoRoot);
        if (arg == "--smoke-ui-simulator-ica") return bf::smokeSimulatorIca(argc,argv,version,repoRoot);
        if (arg == "--smoke-ui-capture-replay" && i + 1 < argc)
            return bf::smokeCaptureReplay(argc, argv, version, repoRoot, QString::fromLocal8Bit(argv[i + 1]));
        if (arg == "--smoke-ui-workspace-navigation") {
            QApplication app(argc, argv);
            // This early smoke path never invokes licensing and isolates UI preferences.
            QTemporaryDir preferences;
            if (!preferences.isValid()) return 1;
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
            QCoreApplication::setOrganizationName("BoundFreeNavigationSmoke");
            QCoreApplication::setApplicationName("BoundFreeNavigationSmoke");
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("en_US", &error)) return 1;
            bf::MainWindow window(version, &i18n, repoRoot);
            window.resize(1280, 900);
            window.show();
            auto* navigation = window.findChild<QTabBar*>("eegWorkspaceNavigation");
            auto* stack = window.findChild<QStackedWidget*>("eegWorkspaceStack");
            auto* action = window.findChild<QAction*>("eegOpenSeparation");
            auto* controller = window.findChild<bf::EegSeparationController*>();
            auto* fileOpen = window.findChild<QAction*>("openCurrentWorkspaceFile");
            auto* teachingOpen = window.findChild<QAction*>("openTeachingRecording");
            const auto fail = [&](const QString& reason) {
                std::cerr << "smoke-ui-workspace-navigation-failed " << reason.toStdString()
                          << " last_error=" << error.toStdString() << '\n';
                return 1;
            };
            const auto wait = [](int ms) {
                QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
            };
            const auto until = [&](const std::function<bool()>& ready, int timeout) {
                QElapsedTimer elapsed; elapsed.start();
                while (!ready() && elapsed.elapsed() < timeout) wait(20);
                return ready();
            };
            if (!navigation || !stack || !action || !controller || navigation->count() != 2
                || stack->count() != 2
                || navigation->currentIndex() != 0 || stack->currentIndex() != 0)
                return fail("initial_controls");
            if (!fileOpen || !teachingOpen || window.findChild<QWidget*>("separationAlternativeInput") ||
                window.findChild<QPushButton*>("separationOpen") || window.findChild<QPushButton*>("separationOpenTeaching"))
                return fail("file_menu_inputs");
            QObject::connect(controller, &bf::EegSeparationController::failed, &window,
                             [&](const bf::AppError& failure) { error = formatErrorDetail(failure); });
            bf::FileOpenResult browser;
            browser.session.ok = true;
            browser.session.path = QDir(repoRoot).filePath("samples/offline-eeg/teaching.h5");
            browser.session.format = "HDF5";
            browser.session.fs = 500; browser.session.duration = 16;
            browser.session.nChannels = 5; browser.session.nSamples = 8000;
            browser.session.chNames = {"Mixed-1", "Mixed-2", "Mixed-3", "Eye-reference", "Muscle-reference"};
            browser.waveform.fs = 500; browser.waveform.t0 = 0; browser.waveform.t1 = 16;
            browser.waveform.channelNames = browser.session.chNames;
            browser.waveform.data.assign(5, std::vector<float>(8000));
            for (int channel = 0; channel < 5; ++channel)
                for (int sample = 0; sample < 8000; ++sample)
                    browser.waveform.data[channel][sample] = 20 * std::sin(sample * .02 * (channel + 1));
            window.showFileOpenResult(browser);
            QAction* browserPlay = nullptr;
            for (auto* candidate : window.findChildren<QAction*>())
                if (candidate->text() == i18n.text("app.playback.play")) browserPlay = candidate;
            if (!browserPlay || !browserPlay->isEnabled()) return fail("browser_play_control");
            browserPlay->trigger();
            wait(80);
            if (browserPlay->text() != i18n.text("app.playback.pause")) return fail("browser_play_start");
            navigation->setCurrentIndex(1);
            if (!action->isChecked() || stack->currentIndex() != 1
                || browserPlay->text() != i18n.text("app.playback.play"))
                return fail("enter_separation_pauses_browser");
            if (!until([&] { return controller->state() == bf::EegSeparationController::State::Ready; }, 60000)
                || controller->source().path == browser.session.path
                || controller->source().metadata.value("signal_provenance").toObject()
                       .value("source_id").toString() != browser.session.path
                || controller->source().channels != browser.session.chNames)
                return fail("inherit_browser_selection_snapshot");
            // The verified teaching fixture remains an explicit advanced file input.
            teachingOpen->trigger();
            if (!until([&] { return !controller->isBusy(); }, 60000)
                || !controller->source().teaching)
                return fail("teaching_file_input");
            controller->start();
            if (fileOpen->isEnabled() || teachingOpen->isEnabled()) return fail("file_actions_enabled_during_fit");
            if (!until([&] { return !controller->isBusy(); }, 60000) || !controller->output())
                return fail("teaching_fit");
            controller->applyTeachingLabels();
            controller->setConfirmed(true);
            const auto output = controller->output();
            QTimer::singleShot(50, &window, [] {
                if (auto* chooser = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) chooser->reject();
            });
            fileOpen->trigger();
            if (controller->output() != output || !controller->isConfirmed() || !fileOpen->isEnabled())
                return fail("file_menu_cancel_loses_analysis");
            const auto labels = controller->labels();
            const auto settings = controller->settings();
            auto* workspace = stack->widget(1);
            auto* previewPlay = workspace->findChild<QPushButton*>("separationPlay");
            if (!previewPlay || !previewPlay->isEnabled()) return fail("preview_play_control");
            previewPlay->click();
            wait(120);
            if (!workspace->property("previewPlaying").toBool()
                || workspace->property("previewPositionSeconds").toDouble() <= 0)
                return fail("preview_play_start");
            action->trigger();
            const double pausedPosition = workspace->property("previewPositionSeconds").toDouble();
            wait(180);
            if (navigation->currentIndex() != 0 || stack->currentIndex() != 0 || action->isChecked()
                || workspace->property("previewPlaying").toBool()
                || workspace->property("previewPositionSeconds").toDouble() != pausedPosition)
                return fail("menu_return_pauses_hidden_preview");
            const QString screenshots = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
            QStringList navigationLocales = i18n.supportedLocales();
            // Pseudo is a development-only locale and is intentionally absent from packages.
            // A source checkout must still load it successfully to catch expansion regressions.
            if (QFile::exists(QDir(repoRoot).filePath("native/i18n/bf_app_pseudo.ts")))
                navigationLocales.append("pseudo");
            for (const QString& locale : navigationLocales) {
                if (!i18n.loadLocale(locale, &error)) return fail("locale_load");
                for (int index : {1, 0}) {
                    navigation->setCurrentIndex(index);
                    wait(40);
                    if (window.size() != QSize(1280, 900) || !navigation->isVisible()
                        || stack->currentIndex() != index || action->isChecked() != (index == 1)
                        || navigation->tabText(0) != i18n.text("app.separation.browser_workspace")
                        || navigation->tabText(1) != i18n.text("app.separation.separation_workspace")
                        || fileOpen->text() != i18n.text("app.action.open")
                        || teachingOpen->text() != i18n.text("app.separation.open_teaching")
                        || !navigation->rect().contains(navigation->tabRect(1))
                        || navigation->accessibleName() != i18n.text("app.separation.workspace_navigation")
                        || navigation->tabToolTip(0) != i18n.text("app.separation.browser_workspace_help"))
                        return fail("locale_navigation_" + locale);
                    const auto& current = controller->settings();
                    if (controller->output() != output || controller->labels() != labels
                        || !controller->isConfirmed() || current.roles != settings.roles
                        || current.start != settings.start || current.duration != settings.duration
                        || current.outputRate != settings.outputRate || current.notchHz != settings.notchHz)
                        return fail("navigation_preserves_analysis_" + locale);
                    if (locale == "zh_CN" && !screenshots.isEmpty() && QDir().mkpath(screenshots)
                        && !window.grab().save(QDir(screenshots).filePath(index == 1
                            ? "navigation-separation-zhCN.png" : "navigation-browser-zhCN.png")))
                        return fail("screenshot_write");
                    if (locale == "zh_CN" && index == 1 && !screenshots.isEmpty()) {
                        auto* menu = qobject_cast<QMenu*>(fileOpen->parent());
                        if (!menu) return fail("file_menu_missing");
                        menu->popup(window.mapToGlobal(QPoint(10, 50)));
                        wait(300);
                        const bool saved = menu->grab().save(QDir(screenshots).filePath("file-menu-zhCN.png"));
                        menu->hide();
                        if (!saved) return fail("file_menu_screenshot_write");
                    }
                }
            }
            action->trigger();
            if (navigation->currentIndex() != 1 || stack->currentIndex() != 1 || !action->isChecked())
                return fail("menu_enter_sync");
            std::cout << "smoke-ui-workspace-navigation-ok locales=" << navigationLocales.size()
                      << " navigation=true menu=true inheritance=true browser_paused=true preview_paused=true retained=true\n";
            return 0;
        }
        if (arg == "--smoke-ui-empty-controls") return smokeEmptyControls(argc, argv, version, repoRoot);
        if (arg == "--smoke-ui-fixed-layout") {
            QApplication app(argc, argv);
            QTemporaryDir settings;
            if (!settings.isValid()) return 1;
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
            QCoreApplication::setOrganizationName("BoundFreeLayoutSmoke");
            QCoreApplication::setApplicationName("BoundFreeLayoutSmoke");
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("en_US", &error)) return 1;
            bf::MainWindow window(version, &i18n, repoRoot);
            bf::FileOpenResult result;
            result.session.ok = true; result.session.path = "layout.csv"; result.session.format = "CSV";
            result.session.fs = 100; result.session.duration = 100;
            result.session.nChannels = 2; result.session.nSamples = 10000;
            result.session.chNames = {"ch1", "ch2"};
            result.waveform.fs = 100; result.waveform.t0 = 0; result.waveform.t1 = 10;
            result.waveform.channelNames = result.session.chNames;
            result.waveform.data = {std::vector<float>(1000), std::vector<float>(1000)};
            for (int sample = 0; sample < 1000; ++sample) {
                result.waveform.data[0][sample] = 50 * std::sin(2 * 3.141592653589793 * 8 * sample / 100.0);
                result.waveform.data[1][sample] = 30 * std::sin(2 * 3.141592653589793 * 13 * sample / 100.0);
            }
            window.showFileOpenResult(result);
            window.show();
            const auto themeKey = qEnvironmentVariableIntValue("BF_LAYOUT_DARK") == 1 ? "app.theme.dark" : "app.theme.light";
            for (auto* action : window.findChildren<QAction*>())
                if (action->text() == i18n.text(themeKey)) { action->trigger(); break; }
            const auto settle = []() { QEventLoop loop; QTimer::singleShot(30, &loop, &QEventLoop::quit); loop.exec(); };
            const auto fail = [&](const QString& detail) {
                std::cerr << "smoke-ui-fixed-layout-failed " << detail.toStdString() << '\n';
                const auto geometry = [](QWidget* widget) {
                    const QSize minimum = widget->minimumSize();
                    const QSize hint = widget->minimumSizeHint();
                    std::cerr << "layout_geometry " << widget->metaObject()->className()
                              << " name=" << widget->objectName().toStdString()
                              << " size=" << widget->width() << 'x' << widget->height()
                              << " min=" << minimum.width() << 'x' << minimum.height()
                              << " min_hint=" << hint.width() << 'x' << hint.height() << '\n';
                };
                geometry(&window);
                for (QWidget* widget : window.findChildren<QWidget*>()) {
                    if (widget->objectName().startsWith("eeg") || qobject_cast<QTabWidget*>(widget)) geometry(widget);
                    if (auto* tabs = qobject_cast<QTabWidget*>(widget)) {
                        for (int tab = 0; tab < tabs->count(); ++tab) geometry(tabs->widget(tab));
                    }
                }
                const QString directory = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
                if (!directory.isEmpty() && QDir().mkpath(directory)) {
                    const QString path = QDir(directory).filePath("layout-failure.png");
                    if (window.grab().save(path)) std::cerr << "layout_screenshot " << path.toStdString() << '\n';
                }
                return 1;
            };
            auto* workspace = window.findChild<QSplitter*>("eegWorkspaceSplitter");
            auto* signal = window.findChild<QSplitter*>("eegSignalSplitter");
            auto* analysis = window.findChild<QSplitter*>("eegAnalysisSplitter");
            auto* top = window.findChild<QWidget*>("eegSignalToolbar");
            auto* bottom = window.findChild<QWidget*>("eegPlaybackBar");
            auto* parameters = window.findChild<QTabWidget*>("eegParameterTabs");
            auto* processingScroll = window.findChild<QScrollArea*>("eegProcessingScroll");
            auto* analysisScroll = window.findChild<QScrollArea*>("signalAnalysisScroll");
            auto* waveform = window.findChild<bf::WaveformWidget*>();
            auto* analysisPanel = window.findChild<QWidget*>("eegAnalysisPanel");
            auto* compactTabs = window.findChild<QTabWidget*>("signalCompactTools");
            auto* signalPage = window.findChild<QWidget*>("eegSignalPage");
            auto* progress = window.findChild<QSlider*>("eegPlaybackProgress");
            if (!workspace || !signal || !analysis || !top || !bottom || !parameters || !waveform
                || !analysisPanel || !compactTabs || !signalPage || !progress || !processingScroll || !analysisScroll
                || parameters->count() != 3) return fail("missing_regions");
            auto* notch = window.findChild<QCheckBox*>("eegNotchEnabled");
            auto* notchSection = window.findChild<QWidget*>("eegNotchSection");
            auto* notchFrequency = window.findChild<QDoubleSpinBox*>("eegNotchFrequency_0");
            auto* reference = window.findChild<QComboBox*>("eegReferenceMode");
            auto* inputSummary = window.findChild<QLabel*>("eegAnalysisInputSummary");
            if (!notch || !notchSection || !notchFrequency || !reference || !inputSummary
                || !parameters->isAncestorOf(reference) || !parameters->isAncestorOf(notch)
                || !notchSection->isAncestorOf(notch) || !notchSection->isAncestorOf(notchFrequency)
                || analysisPanel->isAncestorOf(parameters)) return fail("preprocessing_scope");
            if (!window.windowTitle().contains("layout.csv")) return fail("filename_in_title");
            bool openInMenu = false;
            for (auto* menu : window.findChildren<QMenu*>())
                for (auto* action : menu->actions())
                    if (action->shortcut() == QKeySequence::Open && menu->title() == i18n.text("app.menu.file")) openInMenu = true;
            if (!openInMenu) return fail("open_file_menu");
            // Changing preprocessing must update both the DSP state and the
            // read-only analysis context; changing analysis page keeps it global.
            notchFrequency->setValue(25);
            notch->setChecked(true);
            if (!window.processingViewState().notchEnabled || !inputSummary->text().contains("25"))
                return fail("preprocessing_update");
            notch->setChecked(false);
            const auto bounds = [&](QWidget* widget) { return QRect(widget->mapTo(&window, QPoint()), widget->size()); };
            const auto accessible = [&](QWidget* widget) {
                if (!widget->isVisible() || widget->width() < 8 || widget->height() < 8
                    || !window.rect().contains(bounds(widget))) return false;
                QWidget* hit = window.childAt(widget->mapTo(&window, widget->rect().center()));
                return hit == widget || (hit != nullptr && widget->isAncestorOf(hit));
            };
            const auto analysisReachable = [&](QWidget* page, int index) -> QString {
                if (!page->isVisible()) return "hidden_page";
                QList<QWidget*> targets;
                for (auto* child : page->findChildren<QWidget*>()) {
                    if (child->isVisible() && (qobject_cast<QComboBox*>(child)
                        || qobject_cast<QAbstractSpinBox*>(child) || qobject_cast<QCheckBox*>(child)))
                        targets.push_back(child);
                }
                for (const auto& suffix : {QString("Plot"), QString("Status"), QString("Summary")}) {
                    auto* child = window.findChild<QWidget*>(QString("eegAnalysis%1_%2").arg(suffix).arg(index));
                    if (child && child->isVisible()) targets.push_back(child);
                }
                for (auto* child : targets) {
                    analysisScroll->ensureWidgetVisible(child, 0, 4); settle();
                    if (!accessible(child)) return "unreachable:" + child->objectName();
                }
                analysisScroll->verticalScrollBar()->setValue(0); settle();
                return {};
            };
            const auto regionError = [&]() -> QString {
                if (workspace->orientation() != Qt::Horizontal || signal->orientation() != Qt::Vertical
                    || analysis->orientation() != Qt::Vertical) return "splitter_orientation";
                if (bounds(workspace).bottom() >= bounds(bottom).top()
                    || workspace->widget(0) != parameters) return "region_order";
                if (window.width() < 1000) {
                    // Narrow windows share the right column through two tabs.
                    // Hidden pages retain stale geometry and must be checked
                    // for visibility/ownership rather than as adjacent columns.
                    if (!compactTabs->isVisible() || workspace->count() != 2
                        || workspace->widget(1) != compactTabs || compactTabs->count() != 2
                        || compactTabs->widget(0) != signalPage || compactTabs->widget(1) != analysis
                        || bounds(parameters).right() >= bounds(compactTabs).left()) return "compact_scope";
                    const bool showingWaveform = compactTabs->currentIndex() == 0;
                    if (waveform->isVisible() != showingWaveform || analysisPanel->isVisible() == showingWaveform
                        || !accessible(compactTabs->currentWidget())) return "compact_page_visibility";
                } else {
                    if (compactTabs->isVisible() || compactTabs->count() != 0
                        || workspace->widget(1) != signalPage || workspace->widget(2) != analysis
                        || !waveform->isVisible() || !analysisPanel->isVisible()) return "wide_scope";
                    if (bounds(waveform).right() >= bounds(analysisPanel).left()
                        || bounds(parameters).right() >= bounds(waveform).left()) return "region_order";
                }
                if (waveform->isVisible() && bounds(top).bottom() >= bounds(waveform).top()) return "toolbar_order";
                if (top->isAncestorOf(notch)) return "signal_scope";
                if (!accessible(progress)) return "progress_unreachable";
                for (QWidget* region : {top, bottom}) {
                    if (!region->isVisible()) continue;
                    for (QWidget* child : region->findChildren<QWidget*>()) {
                        if (qobject_cast<QAbstractButton*>(child) || qobject_cast<QAbstractSpinBox*>(child)
                            || qobject_cast<QComboBox*>(child) || qobject_cast<QSlider*>(child)) {
                            if (!accessible(child)) return "control_unreachable:" + child->objectName()
                                + ":" + child->metaObject()->className();
                        }
                    }
                }
                for (auto* panel : window.findChildren<QDockWidget*>()) {
                    if (panel->isFloating() || panel->features() != QDockWidget::NoDockWidgetFeatures)
                        return "floating_panel";
                }
                return {};
            };
            const QString screenshotDir = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
            if (!screenshotDir.isEmpty() && !QDir().mkpath(screenshotDir)) return fail("screenshot_directory");
            for (const QString& locale : {QString("zh_CN"), QString("en_US"), QString("ja_JP"), QString("zh_TW")}) {
                if (!i18n.loadLocale(locale, &error)) return fail(error);
                // Retranslation changes height-for-width hints asynchronously.
                // Let those layout requests settle before requesting a size.
                settle();
                for (const QSize size : {QSize(760, 560), QSize(900, 560), QSize(999, 700), QSize(1000, 700), QSize(1280, 800)}) {
                    window.resize(size);
                    settle();
                    // A width change can invalidate the previous minimum height.
                    // Retry the same exact target after that recalculation.
                    window.resize(size);
                    settle();
                    if (!screenshotDir.isEmpty()) {
                        const QString path = QDir(screenshotDir).filePath(QString("layout-%1-%2x%3.png")
                            .arg(locale).arg(size.width()).arg(size.height()));
                        if (!window.grab().save(path)) return fail("screenshot_save");
                    }
                    // Full analysis attributes may require a taller minimum
                    // window than the old scrolling footer at narrow widths.
                    if (window.width() != size.width() || window.height() < size.height()
                        || window.height() > std::max(size.height(), window.minimumHeight())) return fail(QString("minimum_size:%1:%2x%3")
                        .arg(locale).arg(window.width()).arg(window.height()));
                    const QString problem = regionError();
                    if (!problem.isEmpty()) return fail(locale + ":" + QString::number(size.width()) + ":" + problem);
                    if (compactTabs->isVisible()) {
                        if (compactTabs->tabText(0) != i18n.text("app.workspace.waveform")
                            || compactTabs->tabText(1) != i18n.text("app.analysis.panel")) return fail("compact_tab_translation");
                        compactTabs->setCurrentIndex(1); settle();
                        const QString analysisProblem = regionError();
                        if (!analysisProblem.isEmpty()) return fail(locale + ":compact_analysis:" + analysisProblem);
                        auto* pages = window.findChild<QTabWidget*>("eegAnalysisTabs");
                        if (!pages || pages->count() != 4) return fail("compact_analysis_tabs");
                        for (int tab = 0; tab < pages->count(); ++tab) {
                            pages->setCurrentIndex(tab); settle();
                            const auto problem = analysisReachable(pages->currentWidget(), tab);
                            if (!problem.isEmpty()) return fail("compact_analysis:" + QString::number(tab) + ":" + problem);
                        }
                        pages->setCurrentIndex(0);
                        if (!screenshotDir.isEmpty() && !window.grab().save(QDir(screenshotDir).filePath(
                            QString("layout-analysis-%1-%2.png").arg(locale).arg(size.width())))) return fail("compact_screenshot_save");
                        compactTabs->setCurrentIndex(0); settle();
                        if (!regionError().isEmpty()) return fail("compact_waveform_restore");
                    }
                }
                auto* toggle = window.findChild<QAction*>("eegToggleAnalysis");
                if (!toggle || toggle->text() != i18n.text("app.view.analysis")) return fail("untranslated_action");
            }
            const int oldBoundary = workspace->sizes().value(0);
            workspace->setSizes({280, 560, 420});
            settle();
            if (workspace->sizes().value(0) == oldBoundary) {
                workspace->setSizes({240, 400, 620});
                settle();
            }
            if (workspace->sizes().value(0) == oldBoundary || !regionError().isEmpty()) return fail("splitter_resize");
            auto* analysisTabs = analysisPanel->findChild<QTabWidget*>();
            if (!analysisTabs || analysisTabs->count() != 4) return fail("analysis_tabs");
            for (int tab = 0; tab < 4; ++tab) {
                analysisTabs->setCurrentIndex(tab); settle();
                if (analysisTabs->currentIndex() != tab || !analysisReachable(analysisTabs->currentWidget(), tab).isEmpty()
                    || !regionError().isEmpty()) return fail("analysis_tab_unreachable:" + QString::number(tab));
            }
            analysisTabs->setCurrentIndex(0);
            auto* filters = window.findChild<QWidget*>("eegFilterPanel");
            if (!filters) return fail("filter_panel");
            parameters->setCurrentIndex(1); settle();
            for (int tab = 0; tab < parameters->count(); ++tab) {
                parameters->setCurrentIndex(tab); settle();
                if (!accessible(parameters->currentWidget())) return fail("parameter_tab_unreachable");
                if (!window.findChild<QAction*>("eegToggleChannels")->isChecked()
                    || !window.findChild<QAction*>("eegToggleMarkers")->isChecked())
                    return fail("tab_switch_changes_visibility_preference");
                for (int other = 0; other < parameters->count(); ++other)
                    if (other != tab && parameters->widget(other)->isVisible()) return fail("parameter_tab_overlap");
            }
            if (parameters->tabText(0) != i18n.text("app.browser.channels")
                || parameters->tabText(1) != i18n.text("app.processing.panel")
                || parameters->tabText(2) != i18n.text("app.marker.panel")) return fail("parameter_tab_translation");
            parameters->setCurrentIndex(1); settle();
            if (!screenshotDir.isEmpty()) window.grab().save(QDir(screenshotDir).filePath("layout-processing.png"));
            auto* channelFilters = window.findChild<bf::CollapsiblePanel*>("eegPerChannelFilters");
            if (!channelFilters || channelFilters->isExpanded()) return fail("per_channel_default_expanded");
            channelFilters->setExpanded(true); settle();
            // Expander internals also contain buttons, including deliberately
            // hidden headers. Check the two actual filter actions by identity.
            for (const auto* name : {"eegApplyBandpassToSelected", "eegClearChannelBandpass"}) {
                auto* button = filters->findChild<QPushButton*>(name);
                if (!button) return fail("advanced_filter_button_missing:" + QString::fromLatin1(name));
                processingScroll->ensureWidgetVisible(button, 0, 4); settle();
                if (!accessible(button)) return fail("advanced_filter_unreachable:" + button->objectName());
            }
            const auto panelVisible = [&](const QString& suffix, QWidget* panel) {
                return suffix == "Channels" ? parameters->isTabVisible(0)
                    : suffix == "Markers" ? parameters->isTabVisible(2) : panel->isVisible();
            };
            for (const QString& suffix : {QString("Channels"), QString("Markers"), QString("Analysis"), QString("Video")}) {
                const QString panelName = suffix == "Channels" ? "eegChannelPanel"
                    : suffix == "Markers" ? "eegMarkerPanel" : "eeg" + suffix + "Panel";
                auto* panel = window.findChild<QWidget*>(panelName);
                auto* action = window.findChild<QAction*>("eegToggle" + suffix);
                if (!panel || !action) return fail("missing_toggle:" + suffix);
                const bool initial = panelVisible(suffix, panel);
                const int initialWaveformWidth = waveform->width();
                action->trigger(); settle();
                if (suffix == "Analysis" && !panel->isVisible()
                    && (analysis->isVisible() || waveform->width() <= initialWaveformWidth))
                    return fail("hidden_analysis_keeps_empty_column");
                if (panelVisible(suffix, panel) == initial || action->isChecked() != panelVisible(suffix, panel))
                    return fail("toggle_visibility:" + suffix);
                if (suffix == "Video" && panel->isVisible()
                    && bounds(panel).top() <= bounds(waveform).bottom()) return fail("video_not_below_waveform");
                if (suffix == "Video" && panel->isVisible()) {
                    window.resize(900, 560); settle();
                    window.resize(900, 560); settle();
                    app.processEvents();
                    if (!screenshotDir.isEmpty()) window.grab().save(QDir(screenshotDir).filePath("layout-video-900x560.png"));
                    if (window.width() != 900 || window.height() < 560
                        || window.height() > std::max(560, window.minimumHeight()) || !regionError().isEmpty()
                        || bounds(panel).top() <= bounds(waveform).bottom()) return fail("compact_video_layout");
                    window.resize(1280, 800); settle();
                    if (!regionError().isEmpty()) return fail("wide_layout_after_video");
                }
                action->trigger(); settle();
                if (panelVisible(suffix, panel) != initial || action->isChecked() != initial) return fail("toggle_restore:" + suffix);
            }
            const QStringList hiddenSuffixes{"Channels", "Markers", "Analysis"};
            for (const QString& suffix : hiddenSuffixes) window.findChild<QAction*>("eegToggle" + suffix)->trigger();
            settle();
            window.showFileOpenResult(result); // A seek response must preserve user panel choices.
            settle();
            for (const QString& suffix : hiddenSuffixes) {
                const QString name = suffix == "Channels" ? "eegChannelPanel"
                    : suffix == "Markers" ? "eegMarkerPanel" : "eegAnalysisPanel";
                auto* panel = window.findChild<QWidget*>(name);
                auto* action = window.findChild<QAction*>("eegToggle" + suffix);
                if (panelVisible(suffix, panel) || action->isChecked()) return fail("seek_restores_hidden_panel:" + suffix);
                action->trigger();
            }
            settle();
            if (!regionError().isEmpty()) return fail("layout_after_toggles");
            if (!screenshotDir.isEmpty()) {
                if (!i18n.loadLocale("zh_CN", &error)) return fail(error);
                channelFilters->setExpanded(false);
                parameters->setCurrentIndex(1);
                auto preview = window.processingViewState();
                preview.globalBandpassEnabled = true;
                preview.notchEnabled = true;
                window.setProcessingViewStateForBrowser(preview);
                analysisTabs->setCurrentIndex(3);
                window.resize(1440, 900); settle();
                window.resize(1440, 900); settle();
                processingScroll->verticalScrollBar()->setValue(0);
                settle();
                window.grab().save(QDir(screenshotDir).filePath("workspace-coherence.png"));
                for (auto* menu : window.findChildren<QMenu*>()) {
                    if (menu->title() != i18n.text("app.menu.file")) continue;
                    menu->popup(window.menuBar()->mapToGlobal(QPoint(0, window.menuBar()->height())));
                    settle();
                    menu->grab().save(QDir(screenshotDir).filePath("file-menu.png"));
                    menu->hide(); break;
                }
            }
            std::cout << "smoke-ui-fixed-layout-ok locales=4 widths=5 compact_tabs=true breakpoint=true panels=true splitters=true controls=true\n";
            return 0;
        }
        if (arg == "--smoke-ui-scrub-file" && i + 1 < argc) {
            QApplication app(argc, argv);
            return bf::runScrubFileSmoke(version, repoRoot, QString::fromLocal8Bit(argv[i + 1]));
        }
        if (arg == "--smoke-ui-continuous-file" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("en_US", &error)) return 1;
            bf::MainWindow window(version, &i18n, repoRoot);
            bf::FileOpenController controller(repoRoot, version);
            auto* waveform = window.findChild<bf::WaveformWidget*>();
            QAction* play = nullptr;
            QComboBox* speed = nullptr;
            for (auto* action : window.findChildren<QAction*>())
                if (action->toolTip() == i18n.text("app.playback.play")) play = action;
            for (auto* combo : window.findChildren<QComboBox*>())
                if (combo->count() == 5 && combo->itemData(0).toDouble() == .25
                    && combo->itemData(4).toDouble() == 4) speed = combo;
            if (!waveform || !play || !speed) {
                std::cerr << "smoke-ui-continuous-file-failed controls\n";
                return 1;
            }
            int requests = 0, results = 0, samples = 0, advancingSamples = 0;
            bool cacheJump = false, clockJump = false, completed = false;
            QString failure;
            double previous = 0.0, finalPosition = 0.0;
            qint64 previousMs = 0;
            QElapsedTimer elapsed;
            QTimer sampleTimer;
            QEventLoop loop;
            window.setWindowRequestHandler([&](const QString& path, double start, double length) {
                ++requests;
                controller.openWindow(path, start, length);
            });
            QObject::connect(&sampleTimer, &QTimer::timeout, &window, [&]() {
                const double current = waveform->timeRange().first;
                const qint64 now = elapsed.elapsed();
                const double delta = current - previous;
                if (delta < -1e-6 || delta > 4.0 * (now - previousMs) / 1000.0 + .15) clockJump = true;
                if (delta > .001) ++advancingSamples;
                ++samples;
                previous = current;
                previousMs = now;
            });
            QObject::connect(&controller, &bf::FileOpenController::loaded, &window,
                             [&](const bf::FileOpenResult& result) {
                const auto before = waveform->timeRange();
                window.showFileOpenResult(result);
                ++results;
                if (results > 1) {
                    if (before != waveform->timeRange()) cacheJump = true;
                    return;
                }
                if (result.session.nChannels != 2 || std::abs(result.session.fs - 100.0) > 1e-6
                    || std::abs(result.session.duration - 100.0) > 1e-6) {
                    failure = "fixture_metadata";
                    loop.quit();
                    return;
                }
                speed->setCurrentIndex(4);
                elapsed.start();
                play->trigger();
                sampleTimer.start(20);
                // Import/initial open time is excluded from the playback interval.
                QTimer::singleShot(2000, &window, [&]() {
                    sampleTimer.stop();
                    play->trigger();
                    const auto paused = waveform->timeRange();
                    finalPosition = paused.first;
                    QTimer::singleShot(200, &window, [&, paused]() {
                        if (waveform->timeRange() != paused
                            || play->text() != i18n.text("app.playback.play")) failure = "pause_moves";
                        completed = true;
                        loop.quit();
                    });
                });
            });
            QObject::connect(&controller, &bf::FileOpenController::failed, &window,
                             [&](const bf::AppError& e) { failure = e.code; loop.quit(); });
            QTimer::singleShot(10000, &window, [&]() { failure = "timeout"; loop.quit(); });
            window.resize(1200, 800);
            window.show();
            const QString path = QString::fromLocal8Bit(argv[i + 1]);
            window.showFileOpenLoading(path);
            controller.openInitialWindow(path, 10);
            loop.exec();
            if (!completed || !failure.isEmpty() || cacheJump || clockJump || results < 3
                || requests < 2 || requests * 4 >= samples || advancingSamples < 10 || finalPosition < 3.0) {
                std::cerr << "smoke-ui-continuous-file-failed reason=" << failure.toStdString()
                          << " results=" << results << " requests=" << requests << " samples=" << samples
                          << " advancing=" << advancingSamples << " position=" << finalPosition
                          << " cache_jump=" << cacheJump << " clock_jump=" << clockJump << '\n';
                return 1;
            }
            std::cout << "smoke-ui-continuous-file-ok results=" << results << " requests=" << requests
                      << " samples=" << samples << " advancing=" << advancingSamples
                      << " position=" << finalPosition << " pause=true cache=true\n";
            return 0;
        }
        if (arg == "--smoke-ui-continuous-playback") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("en_US", &error)) return 1;
            bf::MainWindow window(version, &i18n, repoRoot);
            auto data = [](const QString& path, double start, double length) {
                bf::FileOpenResult r;
                r.session.ok = true; r.session.path = path; r.session.format = "CSV";
                r.session.fs = 100; r.session.duration = 100;
                r.session.nSamples = 10000; r.session.nChannels = 2;
                r.session.chNames = {"a", "b"};
                r.waveform.fs = 100; r.waveform.t0 = start;
                r.waveform.t1 = std::min(100.0, start + length);
                r.waveform.channelNames = r.session.chNames;
                const int count = static_cast<int>(std::round((r.waveform.t1 - start) * 100));
                r.waveform.data.resize(2);
                for (int ch = 0; ch < 2; ++ch)
                    for (int sample = 0; sample < count; ++sample)
                        r.waveform.data[ch].push_back(100 * std::sin((start + sample / 100.0) * (ch + 1) * 6.28));
                return r;
            };
            window.showFileOpenResult(data("continuous.csv", 0, 10));
            auto* waveform = window.findChild<bf::WaveformWidget*>();
            auto* position = window.findChild<QDoubleSpinBox*>("eegPlaybackPosition");
            auto* slider = window.findChild<QSlider*>("eegPlaybackProgress");
            QAction* play = nullptr;
            QComboBox* speed = nullptr;
            for (auto* action : window.findChildren<QAction*>())
                if (action->text() == i18n.text("app.playback.play")) play = action;
            for (auto* combo : window.findChildren<QComboBox*>())
                if (combo->count() == 5 && combo->itemData(0).toDouble() == .25 && combo->itemData(4).toDouble() == 4) speed = combo;
            const auto fail = [](const char* reason) {
                std::cerr << "smoke-ui-continuous-playback-failed " << reason << '\n'; return 1;
            };
            if (!waveform || !position || !slider || !play || !speed) return fail("controls");
            const auto wait = [](int ms) {
                QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
            };
            int requests = 0, generation = 0, delay = 300;
            bool cacheJump = false, shortResponse = false;
            window.setWindowRequestHandler([&](const QString& path, double start, double length) {
                ++requests;
                const int id = ++generation;
                QTimer::singleShot(delay, &window, [&, path, start, length, id]() {
                    if (id != generation) return;
                    const auto before = waveform->timeRange();
                    window.showFileOpenResult(data(path, start, shortResponse ? 1.0 : length));
                    if (length > 10 && !shortResponse && before != waveform->timeRange()) cacheJump = true;
                });
            });
            play->trigger(); wait(120);
            if (requests != 1 || position->value() > .02) return fail("slow_read_must_wait");
            play->trigger(); wait(300);
            if (position->value() > .02 || play->text() != i18n.text("app.playback.play")) return fail("buffer_result_autoplays");
            delay = 10;
            play->trigger(); wait(300);
            const double first = waveform->timeRange().first;
            if (first < .10 || first > 1 || std::abs(first - position->value()) > .002 || cacheJump) return fail("continuous_view_or_cache_jump");
            waveform->cachedDisplayModel();
            const int builds = waveform->displayModelBuildCount(), beforeRequests = requests;
            wait(180); waveform->cachedDisplayModel();
            if (waveform->timeRange().first <= first + .05 || waveform->displayModelBuildCount() != builds || requests != beforeRequests)
                return fail("per_frame_rebuild_or_request");
            play->trigger(); const auto paused = waveform->timeRange(); wait(100);
            if (waveform->timeRange() != paused) return fail("pause_moves");
            speed->setCurrentIndex(4); play->trigger(); wait(250);
            const double delta = waveform->timeRange().first - paused.first;
            if (delta < .5 || delta > 2.5) return fail("speed");
            slider->setValue(9900); wait(80);
            if (std::abs(waveform->timeRange().first - 90) > .001 || waveform->timeRange().second != 100) return fail("tail_window");
            wait(400);
            if (position->value() != 0 || play->text() != i18n.text("app.playback.play")) return fail("tail_reset");
            const int beforeLoading = requests;
            window.showFileOpenLoading("next.csv"); play->trigger(); slider->setValue(4000); wait(60);
            if (requests != beforeLoading) return fail("loading_dispatches_old_file");
            window.showFileOpenResult(data("next.csv", 0, 10));
            if (position->value() != 0 || waveform->timeRange() != std::make_pair(0.0, 10.0)) return fail("file_reset");
            shortResponse = true; play->trigger(); wait(100);
            const int afterShort = requests; wait(150);
            if (requests != afterShort || play->text() != i18n.text("app.playback.play")) return fail("short_response_retry_storm");
            std::cout << "smoke-ui-continuous-playback-ok smooth=true cache=true pause=true speed=true tail=true loading=true short_response=true requests=" << requests << '\n';
            return 0;
        }
        if (arg == "--smoke-ui-seek-preview" || arg == "--smoke-ui-scrub-preview") {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString error;
            if (!i18n.loadLocale("en_US", &error)) return 1;
            bf::MainWindow window(version, &i18n, repoRoot);
            bf::FileOpenResult result;
            result.session.ok = true;
            result.session.path = "seek-preview.csv";
            result.session.format = "CSV";
            result.session.fs = 100.0;
            result.session.nChannels = 2;
            result.session.nSamples = 10000;
            result.session.duration = 100.0;
            result.session.chNames = {"ch1", "ch2"};
            result.waveform.fs = 100.0;
            result.waveform.t0 = 0.0;
            // Covers the initial display and the 5-second lookahead.
            result.waveform.t1 = 15.0;
            result.waveform.channelNames = result.session.chNames;
            result.waveform.data = {std::vector<float>(1500, 1.0f), std::vector<float>(1500, 2.0f)};
            window.showFileOpenResult(result);
            auto* slider = window.findChild<QSlider*>("eegPlaybackProgress");
            auto* position = window.findChild<QDoubleSpinBox*>("eegPlaybackPosition");
            QAction* play = nullptr;
            for (auto* action : window.findChildren<QAction*>()) {
                if (action->text() == i18n.text("app.playback.play")) play = action;
            }
            auto fail = [](const char* reason) {
                std::cerr << "smoke-ui-seek-preview-failed " << reason << "\n";
                return 1;
            };
            if (!slider || !position || !play) return fail("controls");
            int requests = 0;
            double requestedTime = -1.0;
            window.setWindowRequestHandler([&](const QString&, double t0, double) {
                ++requests;
                requestedTime = t0;
            });
            auto wait = [](int milliseconds) {
                QEventLoop loop;
                QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
                loop.exec();
            };
            auto slice = [&](double start) {
                auto copy = result;
                copy.waveform.t0 = start;
                copy.waveform.t1 = start + 10.0;
                copy.waveform.data = {std::vector<float>(1000, static_cast<float>(start)), std::vector<float>(1000, 2.0f)};
                return copy;
            };
            auto* waveform = window.findChild<bf::WaveformWidget*>();
            if (!waveform) return fail("waveform");
            play->trigger();
            window.showFileOpenResult(result); // Finish initial lookahead.
            requests = 0;
            slider->setSliderDown(true);
            slider->setSliderPosition(200);
            if (requests != 0 || waveform->timeRange() != std::make_pair(2.0, 12.0)) return fail("cache_preview");
            slider->setSliderPosition(6000);
            slider->setSliderPosition(6100);
            slider->setSliderPosition(6200);
            if (requests != 0 || std::abs(position->value() - 62.0) > 1e-6) return fail("drag_preview");
            wait(120);
            if (requests != 1 || std::abs(requestedTime - 62.0) > 1e-6) return fail("throttled_latest");
            slider->setSliderPosition(8000);
            wait(120);
            if (requests != 1 || slider->sliderPosition() != 8000 || std::abs(position->value() - 80.0) > 1e-6)
                return fail("timer_overwrites_or_prefetches");
            window.showFileOpenResult(slice(62.0));
            if (waveform->timeRange() != std::make_pair(2.0, 12.0)) return fail("stale_preview_overwrites");
            wait(120);
            if (requests != 2 || std::abs(requestedTime - 80.0) > 1e-6) return fail("coalesced_latest");
            window.showFileOpenResult(slice(80.0));
            if (waveform->timeRange() != std::make_pair(80.0, 90.0) || slider->sliderPosition() != 8000
                || std::abs(position->value() - 80.0) > 1e-6) return fail("loaded_preview");
            if (window.findChild<QLabel*>("eegAnalysisStatus_0")->property("analysisGeneration").toInt() != 0)
                return fail("analysis_during_drag");
            slider->setSliderDown(false);
            if (requests != 3 || std::abs(requestedTime - 80.0) > 1e-6) return fail("release_not_single_seek");
            window.showFileOpenResult(slice(80.0));
            if (waveform->timeRange() != std::make_pair(80.0, 90.0) || std::abs(position->value() - 80.0) > 1e-6)
                return fail("final_commit_view");
            play->trigger(); // Stop before checking discrete input paths.
            QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
            QApplication::sendEvent(slider, &right);
            if (requests != 4 || std::abs(requestedTime - 80.01) > 1e-6) return fail("keyboard_seek");
            slider->triggerAction(QAbstractSlider::SliderPageStepAdd);
            const double expected = 100.0 * slider->sliderPosition() / slider->maximum();
            if (requests != 5 || std::abs(requestedTime - expected) > 1e-6) return fail("track_step_seek");
            window.showFileOpenResult(slice(expected));
            window.retranslateUi();
            if (requests != 5) return fail("programmatic_refresh_seeks");
            slider->setSliderDown(true);
            slider->setSliderPosition(4000);
            result.session.path = "seek-preview-next.csv";
            window.showFileOpenResult(result);
            wait(120);
            if (requests != 5 || slider->isSliderDown() || std::abs(position->value()) > 1e-6)
                return fail("new_file_keeps_stale_preview");
            slider->setSliderDown(true);
            slider->setSliderPosition(200);
            slider->setSliderPosition(5000);
            wait(120);
            if (requests != 6) return fail("error_setup");
            window.showFileOpenError(bf::makeError("reader.slice_out_of_range"));
            if (slider->isSliderDown() || slider->value() != 200 || std::abs(position->value() - 2.0) > 1e-6
                || waveform->timeRange() != std::make_pair(2.0, 12.0)) return fail("error_view_inconsistent");
            slider->setSliderDown(true);
            slider->setSliderPosition(400);
            slider->setSliderPosition(5000);
            window.showFileOpenLoading("third.csv");
            window.showFileOpenError(bf::makeError("reader.slice_out_of_range"));
            wait(120);
            if (requests != 6 || slider->isSliderDown() || slider->value() != 400
                || std::abs(position->value() - 4.0) > 1e-6
                || waveform->timeRange() != std::make_pair(4.0, 14.0)) return fail("failed_open_view_inconsistent");
            std::cout << "smoke-ui-seek-preview-ok preview=true timer=true release_once=true keyboard=true track=true refresh=true file_switch=true cache=true coalesced=true stale=true loading=true error=true failed_open=true analysis=true\n";
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
            result.session.duration = 10.0;
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
            widget.setAutoScaleEnabled(false);
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
            result.session.duration = 10.0;
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
            result.session.duration = 10.0;
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
            result.session.duration = 10.0;
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
            widget.setAutoScaleEnabled(false);
            bf::WaveformSlice slice;
            slice.fs = 100.0;
            slice.t0 = 0.0;
            slice.t1 = 0.06;
            slice.channelNames = {"ch1", "ch2"};
            slice.data = {{0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f},
                          {1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}};
            widget.resize(640, 320);
            widget.setSlice(slice);

            const auto renderToImage = [](bf::WaveformWidget& target) {
                QImage rendered(target.size(), QImage::Format_ARGB32);
                rendered.fill(QColor("#00FF00"));
                target.render(&rendered);
                return rendered;
            };
            // Axis regions come from the shared plot margins, and the two text
            // bands deliberately exclude the frame line and the tick marks so
            // the counts can only be produced by axis text, never by a line.
            const bf::plot::Margins margins = bf::plot::kWaveformMargins;
            const QRectF area = bf::plot::plotArea(widget, margins);
            const int textHeight = QFontMetrics(widget.font()).height();
            const QRgb waveformBackground = QColor("#FFFFFF").rgba();
            const auto countTextPixels = [&](const QImage& image, int x0, int x1, int y0, int y1) {
                int count = 0;
                for (int y = std::max(0, y0); y < std::min(image.height(), y1); ++y) {
                    for (int x = std::max(0, x0); x < std::min(image.width(), x1); ++x) {
                        if (image.pixel(x, y) != waveformBackground) {
                            ++count;
                        }
                    }
                }
                return count;
            };

            const QImage image = renderToImage(widget);
            int nonBackground = 0;
            const QRgb background = QColor("#00FF00").rgba();
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (image.pixel(x, y) != background) {
                        ++nonBackground;
                    }
                }
            }
            const int timeTextPixels = countTextPixels(image,
                                                       static_cast<int>(area.left()) - 40,
                                                       static_cast<int>(area.right()) + 1,
                                                       static_cast<int>(area.bottom()) + 6,
                                                       static_cast<int>(area.bottom()) + 7 + textHeight);
            const int gutterTextPixels = countTextPixels(image,
                                                         24,
                                                         static_cast<int>(area.left()) - 12,
                                                         static_cast<int>(area.top()),
                                                         static_cast<int>(area.bottom()));

            // No visible channel: the coordinate system must still render.
            bf::WaveformWidget emptyWidget;
            bf::WaveformSlice emptySlice;
            emptySlice.fs = 100.0;
            emptySlice.t0 = 0.0;
            emptySlice.t1 = 0.06;
            emptyWidget.resize(640, 320);
            emptyWidget.setSlice(emptySlice);
            const QImage emptyImage = renderToImage(emptyWidget);
            const QRectF emptyArea = bf::plot::plotArea(emptyWidget, margins);
            const int emptyTimeTextPixels = countTextPixels(emptyImage,
                                                            static_cast<int>(emptyArea.left()) - 40,
                                                            static_cast<int>(emptyArea.right()) + 1,
                                                            static_cast<int>(emptyArea.bottom()) + 6,
                                                            static_cast<int>(emptyArea.bottom()) + 7 + textHeight);

            std::cout << "smoke-waveform-render-ok image=640x320 non_background_pixels=" << nonBackground
                      << " axis_time_text_pixels=" << timeTextPixels
                      << " axis_gutter_text_pixels=" << gutterTextPixels
                      << " empty_axis_time_text_pixels=" << emptyTimeTextPixels << "\n";
            // The empty case has no channel rows, so its left gutter legitimately
            // stays empty; only the shared time axis is asserted there.
            return nonBackground > 0 && timeTextPixels > 0 && gutterTextPixels > 0 && emptyTimeTextPixels > 0 ? 0 : 1;
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
            if (!controller.startFromFile(sourcePath, source.fs, source.chNames, source.duration, 0.0, outputDir, {{"animal_id", "smoke"}, {"exp_type", "file_replay"}, {"notes", "M7 smoke"}}, static_cast<int>(std::clamp<qint64>(source.nSamples / 2, 1, 1024)), 2)) {
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
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/app_icon.png")));
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
    const QCommandLineOption languageOpt("language", "Set startup locale (overrides saved/system; not persisted).", "locale");
    const QCommandLineOption openFileOpt("open-file", "Open an EEG file after startup.", "path");
    const QCommandLineOption openVideoOpt("open-video", "Open a synchronized video after startup.", "path");
    parser.addOptions({versionOpt, smokeSidecarOpt, smokeSidecarOpenOpt, smokeOpt, smokeLicenseGateOpt, languageOpt, openFileOpt, openVideoOpt});
    parser.process(app);

    // 启动 locale 优先级：显式 --language > 已保存设置 > 系统 locale > zh_CN。
    // CLI 覆盖仅作用于本次运行，不写设置；菜单切换才持久化。
    bf::I18nService i18n(repoRoot);
    const QString cliLocale = parser.value(languageOpt);
    const QString startupLocale = bf::I18nService::resolveStartupLocale(cliLocale);
    QString i18nError;
    if (!i18n.loadLocale(startupLocale, &i18nError)) {
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
    window.setCloseFileHandler([&]() { fileController.closeSession(); });
    window.setOpenFileHandler([&](const QString& path) {
        fileController.openInitialWindow(path);
    });
    window.setOpenCaptureHandler([&](const QString& path, const QString& stream) {
        fileController.openInitialWindow(path, 10.0, stream);
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
