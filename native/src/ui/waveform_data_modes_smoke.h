#pragma once

#include "ui/main_window.h"
#include "ui/export_segment_dialog.h"
#include "ui/plot_style.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QPushButton>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QToolButton>
#include <QToolTip>
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
        window.findChild<QTabWidget*>("eegParameterTabs")->setCurrentIndex(1);
        window.findChild<bf::CollapsiblePanel*>("signalAdvancedProcessing")->setExpanded(true);
        settle();
        window.findChild<QScrollArea*>("eegProcessingScroll")->ensureWidgetVisible(
            window.findChild<QLabel*>("signalFilteringHint"));
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
    // Drive the actual shared browser, not just the renderer's signal output.
    auto* seconds = window.findChild<QDoubleSpinBox*>("eegWindowSeconds");
    auto* automatic = window.findChild<QCheckBox*>("eegAutoAmplitude");
    bf::DeviceWorkspace* device = nullptr;
    for (auto* child : window.findChildren<QWidget*>())
        if (auto* panel = dynamic_cast<bf::DeviceWorkspace*>(child)) { device = panel; break; }
    auto* play = window.findChild<QAction*>("filePlaybackAction");
    if (!seconds || !automatic || !device || !play) return fail("browser_controls");
    const auto wheel = [&](Qt::KeyboardModifiers modifiers, int delta = 120, bool pixels = false) {
        auto area = bf::plot::plotArea(*waveform, bf::plot::kWaveformMargins);
        auto* scrollbar = waveform->findChild<QScrollBar*>("eegWaveformChannelScroll");
        if (scrollbar && scrollbar->maximum() > 0) area.setRight(area.right() - scrollbar->sizeHint().width());
        const QPointF point(area.left() + area.width() * .4, area.center().y());
        QWheelEvent event(point, waveform->mapToGlobal(point.toPoint()), pixels ? QPoint(0, delta) : QPoint(),
            pixels ? QPoint() : QPoint(0, delta), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(waveform, &event);
    };
    const auto key = [&](int code) {
        QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
        QApplication::sendEvent(waveform, &event);
    };
    mode->setCurrentIndex(0);
    window.setBrowserWindowSecondsForBrowser(8);
    const auto scientificData = waveform->cachedDisplayModel().data;
    auto range = waveform->timeRange();
    wheel(Qt::ControlModifier);
    auto zoomed = waveform->timeRange();
    if (std::abs(seconds->value() - 6.4) > 1e-9 ||
        std::abs(zoomed.first - (range.first + .4 * (8 - 6.4))) > .02 ||
        std::abs(zoomed.second - zoomed.first - 6.4) > 1e-9) {
        std::cerr << "file zoom before=" << range.first << ',' << range.second << " after=" << zoomed.first << ',' << zoomed.second
            << " seconds=" << seconds->value() << '\n';
        return fail("file_pointer_time_zoom");
    }
    wheel(Qt::ShiftModifier);
    if (waveform->amplitudeZoom() != 1.25 || waveform->cachedDisplayModel().data != scientificData)
        return fail("file_display_zoom_changes_samples");
    auto* scroll = waveform->findChild<QScrollBar*>("eegWaveformChannelScroll");
    wheel(Qt::NoModifier, -120);
    if (!scroll || (scroll->maximum() > 0 && scroll->value() == 0) || waveform->timeRange() != zoomed ||
        waveform->amplitudeZoom() != 1.25) return fail("file_channel_scroll");
    automatic->setChecked(false);
    device->setLiveViewActive(false); // An idle device panel cannot disable file gestures or reset display preferences.
    window.retranslateUi();
    wheel(Qt::ShiftModifier, 1, true);
    if (waveform->amplitudeZoom() != 1.5625 || waveform->autoScaleEnabled() || automatic->isChecked() ||
        waveform->toolTip() != i18n.text("app.browser.view_hint")) return fail("shared_preferences_ownership");
    key(Qt::Key_Right);
    if (waveform->timeRange().first <= zoomed.first) return fail("file_keyboard_pan");
    auto* marking = window.findChild<QAction*>("eegMarkerMode");
    if (!marking || waveform->cursor().shape() != Qt::OpenHandCursor) return fail("file_pan_cursor");
    marking->setChecked(true);
    const auto markerRange = waveform->timeRange();
    key(Qt::Key_Right);
    if (waveform->timeRange() != markerRange || waveform->cursor().shape() != Qt::CrossCursor)
        return fail("file_marker_mode_disables_pan");
    marking->setChecked(false);
    key(Qt::Key_0);
    if (seconds->value() != 10 || waveform->amplitudeZoom() != 1 || waveform->autoScaleEnabled())
        return fail("file_reset_preserves_scale_mode");
    key(Qt::Key_Space);
    if (play->text() != i18n.text("app.playback.pause")) return fail("file_space_starts_playback");
    key(Qt::Key_Space);
    if (play->text() != i18n.text("app.playback.play")) return fail("file_space_pauses_playback");
    auto* footerPause = window.findChild<QPushButton*>("deviceFreeze");
    auto* footerReset = window.findChild<QPushButton*>("deviceResetView");
    auto* footerHelp = window.findChild<QToolButton*>("deviceViewHelp");
    if (!footerPause || !footerReset || !footerHelp) return fail("shared_footer_controls");
    footerPause->click();
    if (play->text() != i18n.text("app.playback.pause")) return fail("file_footer_playback");
    footerPause->click();
    if (play->text() != i18n.text("app.playback.play")) return fail("file_footer_pause");
    wheel(Qt::ShiftModifier);
    footerReset->click();
    if (waveform->amplitudeZoom() != 1 || seconds->value() != 10) return fail("file_footer_reset");
    footerHelp->click();
    if (QToolTip::text() != i18n.text("app.browser.view_hint")) return fail("file_footer_help");
    QToolTip::hideText();
    // Zooming outside the loaded cache requests full samples and keeps the
    // latest anchored viewport when the asynchronous reader completes.
    auto longer = input;
    longer.session.path = "zoom-window.csv";
    longer.session.duration = 100;
    longer.session.nSamples = 25000;
    window.setChannelVisibleForBrowser(1, false);
    window.setFirstVisibleChannelForBrowser(2);
    window.showFileOpenResult(input);
    if (waveform->cachedDisplayModel().channelNames != QStringList{"C"}) return fail("same_source_keeps_channels");
    window.showFileOpenResult(longer);
    if (waveform->cachedDisplayModel().channelNames != input.session.chNames) return fail("new_source_resets_channels");
    window.setBrowserWindowSecondsForBrowser(8);
    int reads = 0;
    double requestedStart = -1, requestedSeconds = -1;
    window.setWindowRequestHandler([&](const QString&, double start, double length) {
        ++reads; requestedStart = start; requestedSeconds = length;
    });
    wheel(Qt::ControlModifier, -120);
    wheel(Qt::ControlModifier, -120);
    if (reads != 2 || requestedSeconds != 12.5 || requestedStart != 0) return fail("file_zoom_reads_latest_window");
    longer.waveform.t0 = requestedStart;
    longer.waveform.t1 = requestedStart + requestedSeconds;
    longer.waveform.data.assign(3, std::vector<float>(3125, 2));
    window.showFileOpenResult(longer);
    if (waveform->timeRange() != std::make_pair(0., 12.5) || seconds->value() != 12.5 ||
        waveform->cachedDisplayModel().data[0].size() != 3125) return fail("file_zoom_async_result");
    window.setBrowserWindowSecondsForBrowser(.25);
    const auto minimumRange = waveform->timeRange();
    wheel(Qt::ControlModifier);
    if (waveform->timeRange() != minimumRange || seconds->value() != .25) return fail("file_zoom_minimum");
    window.setWindowRequestHandler({});
    std::cout << "waveform-data-modes-ok raw=true processed=true channels=true settings=true dialog=true file_zoom=true shared_preferences=true async_zoom=true\n";
    return 0;
}
