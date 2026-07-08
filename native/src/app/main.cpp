#include "controller/file_open_controller.h"
#include "controller/file_session.h"
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
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QTimer>

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
    parser.addOptions({versionOpt, smokeSidecarOpt, smokeSidecarOpenOpt, smokeOpt, languageOpt});
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
