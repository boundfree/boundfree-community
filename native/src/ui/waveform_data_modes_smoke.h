#pragma once

#include "ui/main_window.h"
#include "ui/export_segment_dialog.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QPushButton>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <cmath>
#include <iostream>

inline int smokeWaveformDataModes(int argc, char** argv, const bf::VersionInfo& version, const QString& repoRoot) {
    QApplication app(argc, argv);
    bf::I18nService i18n(repoRoot);
    QString error;
    if (!i18n.loadLocale("zh_CN", &error)) return 1;
    bf::MainWindow window(version, &i18n, repoRoot);
    bf::FileOpenResult input;
    input.session.ok = true;
    input.session.path = "waveform-modes.csv";
    input.session.fs = 250;
    input.session.nChannels = 3;
    input.session.nSamples = 2000;
    input.session.duration = 8;
    input.session.chNames = {"A", "B", "C"};
    input.waveform.fs = 250;
    input.waveform.t1 = 8;
    input.waveform.channelNames = input.session.chNames;
    input.waveform.data.assign(3, std::vector<float>(2000));
    for (int ch = 0; ch < 3; ++ch)
        for (int s = 0; s < 2000; ++s)
            input.waveform.data[ch][s] = static_cast<float>(10 * ch + std::sin(s * (0.12 + ch * 0.04)) + 3 * std::cos(s * 0.7));
    window.showFileOpenResult(input);
    window.resize(1000, 700);
    window.show();
    const auto settle = []() { QEventLoop loop; QTimer::singleShot(30, &loop, &QEventLoop::quit); loop.exec(); };
    const auto fail = [](const char* message) { std::cerr << "waveform-data-modes-failed " << message << '\n'; return 1; };
    auto* mode = window.findChild<QComboBox*>("eegWaveformDataMode");
    auto* waveform = window.findChild<bf::WaveformWidget*>();
    if (!mode || !waveform || mode->currentIndex() != 1) return fail("initial_mode");
    bf::ProcessingViewState processing;
    processing.referenceMode = bf::dsp::ReferenceMode::Bipolar;
    processing.globalBandpassEnabled = true;
    processing.bandpassLow = 1;
    processing.bandpassHigh = 35;
    processing.setChannelBandpass(0, {true, 2, 25});
    window.setProcessingViewStateForBrowser(processing);
    const auto expected = bf::applyProcessingToSlice(input.waveform, processing);
    const auto matches = [&](const bf::WaveformSlice& slice) {
        return waveform->cachedDisplayModel().channelNames == slice.channelNames
            && waveform->cachedDisplayModel().data == slice.data;
    };
    if (!matches(expected)) return fail("processed_samples");
    for (const QString& locale : {QString("zh_CN"), QString("en_US"), QString("ja_JP"), QString("zh_TW"), QString("pseudo")}) {
        if (!i18n.loadLocale(locale, &error)) return fail("locale");
        for (int index : {0, 1}) {
            mode->setCurrentIndex(index);
            settle();
            if (!matches(index ? expected : input.waveform)) return fail("switch_samples_or_names");
            if (window.processingViewState().perChannelBandpass.size() != 1) return fail("lost_settings");
            if (mode->currentText() != i18n.text(index ? "app.signal.processed" : "app.signal.raw")) return fail("mode_label");
            QStyleOptionComboBox option;
            option.initFrom(mode); option.rect = mode->rect(); option.currentText = mode->currentText();
            const QRect field = mode->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, mode);
            if (mode->fontMetrics().horizontalAdvance(mode->currentText()) > field.width()) return fail("mode_clipped");
        }
    }
    if (!i18n.loadLocale("zh_CN", &error)) return fail("locale");
    // A new prefetch result must respect raw mode as well as a direct switch.
    mode->setCurrentIndex(0);
    window.showFileOpenResult(input);
    if (!matches(input.waveform)) return fail("raw_on_reload");
    bf::ExportSegmentDialog dialog(&i18n, 8, 1, 4, false, window.processingSummary(), &window);
    if (dialog.processed() || dialog.startTime() != 1 || dialog.endTime() != 4) return fail("export_defaults");
    auto* exportMode = dialog.findChild<QComboBox*>("eegExportDataMode");
    auto* end = dialog.findChild<QDoubleSpinBox*>("eegExportEnd");
    auto* save = dialog.findChild<QPushButton*>("eegExportContinue");
    exportMode->setCurrentIndex(1);
    if (!dialog.processed() || mode->currentIndex() != 0) return fail("independent_export_mode");
    end->setValue(0.5);
    if (save->isEnabled()) return fail("invalid_range");
    end->setValue(4);
    if (!save->isEnabled()) return fail("valid_range");
    const QString directory = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
    if (!directory.isEmpty()) {
        QDir().mkpath(directory);
        for (bool dark : {false, true}) {
            for (auto* action : window.findChildren<QAction*>())
                if (action->text() == i18n.text(dark ? "app.theme.dark" : "app.theme.light")) action->trigger();
            for (int index : {0, 1}) {
                mode->setCurrentIndex(index); settle();
                window.grab().save(QDir(directory).filePath(QString("waveform-%1-%2.png").arg(dark ? "dark" : "light").arg(index)));
            }
            dialog.show(); settle();
            dialog.grab().save(QDir(directory).filePath(QString("export-%1.png").arg(dark ? "dark" : "light")));
            dialog.hide();
        }
    }
    std::cout << "waveform-data-modes-ok raw=true processed=true channels=true settings=true dialog=true\n";
    return 0;
}
