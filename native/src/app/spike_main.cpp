#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ipc/sidecar_client.h"
#include "ui/spike_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <iostream>

int main(int argc, char** argv) {
    const QString repoRoot = bf::VersionService::detectRepoRoot(QString::fromLocal8Bit(argv[0]));
    bf::VersionService versionService(repoRoot);
    bf::VersionInfo version;
    QString versionError;
    if (!versionService.load(&version, &versionError)) {
        std::cerr << "failed to load version.json: " << versionError.toStdString() << "\n";
        return 1;
    }

    // 解析标志位参数。
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);

        if (arg == "--version") {
            QCoreApplication app(argc, argv);
            std::cout << version.displayString().toStdString() << "\n";
            return 0;
        }

        // 端到端冒烟：offscreen 下构造 SpikeWindow、打开文件、等待分析完成。
        // 证明 open→slice→detect→sort→视图 管线连通。
        if (arg == "--smoke-spike-open" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            i18n.loadLocale(QStringLiteral("en_US"), &i18nError);

            bf::SpikeWindow window(version, &i18n, repoRoot);
            const QString path = QString::fromLocal8Bit(argv[i + 1]);

            if (!window.openPath(path)) {
                std::cout << "smoke-spike-open-failed openPath returned false\n";
                return 1;
            }

            // 等待分析完成信号，最多若干秒。
            QEventLoop loop;
            QObject::connect(&window, &bf::SpikeWindow::analysisSettled,
                             &loop, &QEventLoop::quit);
            QTimer::singleShot(30000, &loop, [&]() { loop.quit(); });
            loop.exec();

            if (!window.analysisCompleted()) {
                std::cout << "smoke-spike-open-failed analysis did not settle\n";
                return 1;
            }
            if (!window.analysisSucceeded()) {
                std::cout << "smoke-spike-open-failed analysis reported failure\n";
                return 1;
            }

            std::cout << "smoke-spike-open-ok spikes=" << window.lastTotalSpikes()
                      << " clusters=" << window.lastClusterCount() << "\n";
            return 0;
        }
    }

    // 默认：正常启动 GUI。
    QApplication app(argc, argv);
    bf::I18nService i18n(repoRoot);
    QString i18nError;
    i18n.loadLocale(QStringLiteral("en_US"), &i18nError);

    bf::SpikeWindow window(version, &i18n, repoRoot);
    window.show();
    return app.exec();
}
