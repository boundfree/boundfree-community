#pragma once

#include "controller/export_controller.h"
#include <QCoreApplication>
#include <QDir>
#include <QTimer>
#include <iostream>
#include <functional>

inline int smokeExportDataModes(int argc, char** argv, const bf::VersionInfo& version,
                               const QString& repoRoot, const QString& source, const QString& directory) {
    QCoreApplication app(argc, argv);
    bf::SidecarClient metadata(repoRoot);
    const auto hello = metadata.startAndHello(version, 5000);
    const auto opened = hello.ok ? metadata.openFile(source, 30000) : bf::SidecarOpenResult{};
    metadata.stop();
    if (!opened.ok) return 1;
    bf::ExportController controller(repoRoot, version);
    const QDir output(directory);
    if (!QDir().mkpath(directory)) return 1;
    const QStringList modes{"raw", "filtered", "car", "bipolar"};
    const QStringList suffixes{"csv", "h5", "edf"};
    int current = 0, ticks = 0, exitCode = 1;
    QTimer activity;
    activity.setInterval(1);
    QObject::connect(&activity, &QTimer::timeout, [&]() { ++ticks; });
    std::function<void()> next;
    next = [&]() {
        if (current == modes.size() * suffixes.size()) {
            std::cout << "export-data-modes-ok outputs=" << current << " ticks=" << ticks << '\n';
            exitCode = ticks > 0 ? 0 : 1;
            app.quit();
            return;
        }
        const int mode = current / suffixes.size();
        std::optional<bf::ExportProcessingOptions> options;
        if (mode) {
            options = bf::ExportProcessingOptions{};
            options->contextStart = 0;
            options->contextEnd = 8;
            if (mode == 1) {
                options->state.notchEnabled = true;
                options->state.globalBandpassEnabled = true;
                options->state.bandpassLow = 1;
                options->state.bandpassHigh = 35;
                options->state.setChannelBandpass(1, {false, 1, 35});
            } else {
                options->state.referenceMode = mode == 2 ? bf::dsp::ReferenceMode::Car : bf::dsp::ReferenceMode::Bipolar;
            }
        }
        const QString path = output.filePath(modes[mode] + "." + suffixes[current % suffixes.size()]);
        if (!controller.exportAsync(source, 2, 6, 8, path, {{2.5, "stim"}, {1.0, "outside"}}, options)) {
            std::cerr << "export-data-modes-failed start_rejected\n";
            app.quit();
        }
        // Subsequent edits must not change the in-flight processing snapshot.
        if (options) options->state.referenceMode = bf::dsp::ReferenceMode::Raw;
    };
    QObject::connect(&controller, &bf::ExportController::completed, &app, [&](const bf::SidecarExportResult& result) {
        const int expectedChannels = current / suffixes.size() == 3 ? std::max(1, opened.nChannels - 1) : opened.nChannels;
        if (result.nChannels != expectedChannels || result.nSamples != static_cast<int>(4 * opened.fs) || result.markerCount != 1) {
            std::cerr << "export-data-modes-failed metadata\n";
            app.quit();
            return;
        }
        ++current;
        QTimer::singleShot(0, &app, next);
    });
    QObject::connect(&controller, &bf::ExportController::failed, &app, [&](const bf::AppError& error) {
        std::cerr << "export-data-modes-failed " << error.code.toStdString() << '\n';
        app.quit();
    });
    activity.start();
    QTimer::singleShot(0, &app, next);
    QTimer::singleShot(180000, &app, [&]() { std::cerr << "export-data-modes-failed timeout\n"; app.quit(); });
    app.exec();
    return exitCode;
}
