#include "app/smoke/smoke_entries.h"
#include <QFileDialog>
#include "controller/file_open_controller.h"
#include "controller/export_controller.h"
#include "controller/file_session.h"
#include "controller/request_generation.h"
#include "render/processing_view_state.h"
#include "render/waveform_view_state.h"
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

bool ensureOfflineLicense(bf::LicenseManager& manager, const bf::VersionInfo& version,
                          bf::I18nService& i18n, bool smokeGate, bool& checkOnline) {
    // Automated smoke gate verifies the gate path without waiting for online network or interactive dialogs.
    if (smokeGate) {
        return false;
    }

    // Level 1: check saved license.dat exactly once (no network, instant).
    // The result — including structured clock-watermark failures — is kept
    // and handed to the activation dialog so the startup failure is shown
    // in the status bar instead of being lost.
    const bf::LicenseResult startupCheck = manager.checkSavedLicense();
    if (startupCheck.ok()) {
        checkOnline = bf::ActivationDialog::requiresOnlineStatusCheck(startupCheck);
        return true;
    }

    {
        bf::ActivationDialog dlg(&manager, version, &i18n, startupCheck);
        if (dlg.exec() == QDialog::Accepted) {
            return true;
        }
    }

    return false;
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

    if (const std::optional<int> entry = bf::runCommandLineEntry(argc, argv, version, repoRoot)) {
        return *entry;
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
    bf::LicenseManager licenseManager(version.version, repoRoot);
    bool checkOnlineLicense = false;
    if (!ensureOfflineLicense(licenseManager, version, i18n, parser.isSet(smokeLicenseGateOpt),
                              checkOnlineLicense)) {
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
#ifdef BF_REQUIRE_LICENSE
    if (checkOnlineLicense) {
        QTimer::singleShot(0, &window, [&] {
            bf::ActivationDialog::checkAndHandleRevocation(&licenseManager, version, &i18n, &window);
        });
    }
#endif
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
