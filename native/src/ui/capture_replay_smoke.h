#pragma once
#include "ui/main_window.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextStream>
#include <iostream>
#include <cmath>

namespace bf {
inline int smokeCaptureReplay(int argc, char** argv, const VersionInfo& version, const QString& root, const QString& path) {
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QCoreApplication::setOrganizationName("BoundFreeCaptureReplaySmoke");
    QCoreApplication::setApplicationName("BoundFreeCaptureReplaySmoke");
    I18nService i18n(root); QString failure;
    if (!i18n.loadLocale("zh_CN", &failure)) return 1;
    MainWindow window(version, &i18n, root); window.resize(1280, 800); window.show();
    auto* automatic = window.findChild<QCheckBox*>("eegAutoAmplitude");
    if (!automatic) return 1;
    automatic->setChecked(false);
    FileOpenController controller(root, version);
    FileOpenResult latest; int loaded = 0;
    QObject::connect(&controller, &FileOpenController::loaded, &window, [&](const FileOpenResult& result) {
        latest = result; ++loaded; window.showFileOpenResult(result);
    });
    QObject::connect(&controller, &FileOpenController::failed, &window, [&](const AppError& error) { failure = error.code; });
    window.setOpenFileHandler([&](const QString& file) { controller.openInitialWindow(file); });
    window.setOpenCaptureHandler([&](const QString& file, const QString& stream) { controller.openInitialWindow(file, 10, stream); });
    window.setWindowRequestHandler([&](const QString& file, double start, double length) { controller.openWindow(file, start, length); });
    const auto wait = [](int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
    const auto until = [&](const auto& ready) {
        QElapsedTimer elapsed; elapsed.start();
        while (!ready() && failure.isEmpty() && elapsed.elapsed() < 45000) wait(10);
        return ready() && failure.isEmpty();
    };
    const auto fail = [&](const char* reason) { std::cerr << "capture-replay-failed " << reason << " code=" << failure.toStdString() << '\n'; return 1; };
    window.openPathForBrowser(path);
    if (!until([&] { return loaded > 0; })) return fail("open");
    auto* wave = window.findChild<WaveformWidget*>();
    auto* mode = window.findChild<QComboBox*>("eegWaveformDataMode");
    auto* streams = window.findChild<QComboBox*>("captureReplayStream");
    auto* summary = window.findChild<QLabel*>("captureReplaySummary");
    auto* bar = window.findChild<QWidget*>("captureReplayControls");
    auto* parameters = window.findChild<QTabWidget*>("eegParameterTabs");
    parameters->setCurrentIndex(0); wait(20);
    auto* position = window.findChild<QDoubleSpinBox*>("eegPlaybackPosition");
    auto* record = window.findChild<QAction*>("fileRecordAction");
    auto* exportAction = window.findChild<QAction*>("fileExportAction");
    auto* play = window.findChild<QAction*>("filePlaybackAction");
    if (!wave || !mode || !streams || !summary || !bar || !position || !record || !exportAction || !play ||
        !bar->isVisible() || !mode->isEnabled() || mode->currentIndex() != 1 || !record->isEnabled() || !exportAction->isEnabled() ||
        !automatic->isEnabled() || !window.findChild<QDoubleSpinBox*>("eegManualAmplitude")->isVisible()) return fail("shared_controls");
    const auto& display = wave->cachedDisplayModel();
    bool equal = display.data.size() == latest.initialSlice.data.size();
    for (std::size_t c = 0; equal && c < display.data.size(); ++c) {
        equal = display.data[c].size() == latest.initialSlice.data[c].size();
        for (std::size_t i = 0; equal && i < display.data[c].size(); ++i) {
            const auto a = display.data[c][i], b = latest.initialSlice.data[c][i];
            equal = a == b || (std::isnan(a) && std::isnan(b));
        }
    }
    if (latest.session.format != "BoundFree Capture" || latest.session.capture.isEmpty() ||
        wave->cachedDisplayModel().channelUnits != latest.initialSlice.channelUnits ||
        wave->cachedDisplayModel().sampleTimes.empty() || !equal) return fail("raw_units_and_coordinates");
    if (streams->count() > 1) {
        const auto expected = streams->itemData(1).toString(); const auto before = loaded;
        streams->setCurrentIndex(1);
        if (!until([&] { return loaded > before; }) || latest.session.capture.value("stream_key").toString() != expected)
            return fail("stream_selection");
    }
    const auto stream = latest.session.capture.value("stream_key").toString();
    if (latest.session.duration > 1) {
        const auto before = loaded;
        position->setValue(latest.session.duration * .6);
        QMetaObject::invokeMethod(position, "editingFinished", Qt::DirectConnection);
        if (!until([&] { return loaded > before; }) || latest.session.capture.value("stream_key").toString() != stream)
            return fail("seek_preserves_stream");
        const double start = position->value(); play->trigger(); wait(250); play->trigger();
        if (position->value() <= start) return fail("playback_advances");
    }
    parameters->setCurrentIndex(1);
    automatic->setChecked(true);
    const auto directory = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
    for (const auto& locale : {QString("zh_CN"), QString("en_US"), QString("ja_JP"), QString("zh_TW")}) {
        if (!i18n.loadLocale(locale, &failure)) return fail("locale");
        for (const auto size : {QSize(1280,800), QSize(760,560)}) {
            window.resize(size); wait(30);
            if (window.size() != size || bar->isHidden() || wave->height() < 180) {
                std::cerr<<"layout actual="<<window.width()<<"x"<<window.height()<<" expected="<<size.width()<<"x"<<size.height()<<" wave="<<wave->width()<<"x"<<wave->height()<<"\n";
                window.grab().save("/tmp/boundfree-unified-layout-failure.png");return fail("layout");
            }
            if (!directory.isEmpty()) {
                QDir().mkpath(directory);
                if (!window.grab().save(QDir(directory).filePath("capture-" + locale + "-" + QString::number(size.width()) + ".png"))) return fail("screenshot");
            }
        }
    }
    automatic->setChecked(false);
    const auto csv = QDir(settings.path()).filePath("ordinary.csv");
    { QFile file(csv); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return fail("fixture");
      QTextStream output(&file); output << "time_s,ch1\n";
      for (int i = 0; i < 500; ++i) output << i / 250.0 << ',' << std::sin(i * .1) << '\n'; }
    const auto before = loaded; window.openPathForBrowser(csv);
    if (!until([&] { return loaded > before; }) || !latest.session.capture.isEmpty() || bar->isVisible() ||
        !mode->isEnabled() || mode->currentIndex() != 1 || !record->isEnabled() || !exportAction->isEnabled() ||
        !automatic->isEnabled() || automatic->isChecked()) return fail("ordinary_file_restores_processing_and_scale");
    std::cout << "CAPTURE_REPLAY_SMOKE PASS raw=true units=true shared_tools=true seek=true playback=true stream_selection=true ordinary_file_restored=true locales=4 compact=true\n";
    return 0;
}
}
