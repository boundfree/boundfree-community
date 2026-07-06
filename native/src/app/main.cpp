#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ipc/sidecar_client.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

#include <iostream>

int main(int argc, char** argv) {
    const QString repoRoot = bf::VersionService::detectRepoRoot();
    bf::VersionService versionService(repoRoot);
    bf::VersionInfo version;
    QString versionError;
    if (!versionService.load(&version, &versionError)) {
        std::cerr << "failed to load version.json: " << versionError.toStdString() << "\n";
        return 1;
    }

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
    }

    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("BoundFree EEG Native");
    parser.addHelpOption();
    const QCommandLineOption versionOpt("version", "Print product version.");
    const QCommandLineOption smokeSidecarOpt("smoke-sidecar", "Start sidecar, run hello, and exit.");
    const QCommandLineOption smokeOpt("smoke-ui", "Start UI and exit automatically.");
    const QCommandLineOption languageOpt("language", "Set startup locale.", "locale", "zh_CN");
    parser.addOptions({versionOpt, smokeSidecarOpt, smokeOpt, languageOpt});
    parser.process(app);

    bf::I18nService i18n(repoRoot);
    QString i18nError;
    if (!i18n.loadLocale(parser.value(languageOpt), &i18nError)) {
        std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
        return 1;
    }

    bf::MainWindow window(version, &i18n);
    bf::SidecarClient sidecar(repoRoot);
    window.show();
    window.setSidecarConnecting();

    QTimer::singleShot(0, &window, [&]() {
        const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
        if (hello.ok) {
            window.setSidecarConnected();
        } else {
            window.setSidecarFailed(hello.error);
        }
    });

    if (parser.isSet(smokeOpt)) {
        QTimer::singleShot(500, &app, [&]() {
            sidecar.stop();
            std::cout << "smoke-ui-ok\n";
            app.quit();
        });
    }

    const int rc = app.exec();
    sidecar.stop();
    return rc;
}
