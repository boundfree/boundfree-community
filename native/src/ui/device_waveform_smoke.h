#pragma once

#include "ui/main_window.h"
#include "ui/collapsible_panel.h"
#include <QScrollArea>
#include <QFontMetrics>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QComboBox>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <iostream>

namespace bf {
// Exercise the actual connection window and the one existing browser renderer.
inline int smokeDeviceWaveform(int argc, char** argv, const VersionInfo& version, const QString& root) {
    QApplication app(argc, argv);
    QTemporaryDir preferences;
    if (!preferences.isValid()) return 1;
    qputenv("BF_ACQUISITION_LOG_DIRECTORY", preferences.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    QCoreApplication::setOrganizationName("BoundFreeDeviceWaveformSmoke");
    QCoreApplication::setApplicationName("BoundFreeDeviceWaveformSmoke");
    I18nService i18n(root); QString error;
    if (!i18n.loadLocale("zh_CN", &error)) return 1;
    MainWindow window(version, &i18n, root); window.resize(1280, 800); window.show();
    const auto wait = [](int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
    const auto until = [&](const std::function<bool()>& ready, int timeout = 15000) {
        QElapsedTimer timer; timer.start();
        while (!ready() && timer.elapsed() < timeout) wait(10);
        return ready();
    };
    auto* controller = window.findChild<AcquisitionController*>();
    auto* waveform = window.findChild<WaveformWidget*>();
    const auto fail = [&](const char* reason) {
        std::cerr << "smoke-ui-device-waveform-failed " << reason << '\n';
        if (controller) { controller->stopAcquisition(); until([&] { return !controller->hasActiveTasks(); }); }
        return 1;
    };
    QString revealedLog;
    window.setLogLocationHandler([&](const QString& path) { revealedLog = path; return QFileInfo::exists(path); });
    if (!controller || !waveform || window.findChildren<WaveformWidget*>().size() != 1) return fail("shared_renderer");
    FileOpenResult file;
    file.session.ok = true; file.session.path = "device-smoke-file.csv"; file.session.format = "CSV";
    file.session.fs = 100; file.session.duration = 10; file.session.nChannels = 2; file.session.nSamples = 1000;
    file.session.chNames = {"file-A", "file-B"}; file.waveform.channelNames = file.session.chNames;
    file.waveform.fs = 100; file.waveform.t0 = 0; file.waveform.t1 = 10;
    file.waveform.data.assign(2, std::vector<float>(1000, 3.0f));
    auto* manage = window.findChild<QAction*>("deviceManageConnections");
    auto* openLogs = window.findChild<QAction*>("deviceOpenLogsAction");
    auto* deviceMenu = window.findChild<QMenu*>("deviceMenu");
    auto* dialog = window.findChild<QDialog*>("deviceConnectionWindow");
    auto* details = window.findChild<QWidget*>("deviceLiveDetails");
    auto* connections = window.findChild<QToolButton*>("deviceConnectionAdvanced");
    if (!manage || !deviceMenu || !dialog || !details || !connections ||
        !window.menuBar()->actions().contains(deviceMenu->menuAction()) ||
        !deviceMenu->actions().contains(manage) || window.findChild<QPushButton*>("deviceManageConnections")) return fail("device_menu_controls");
    if (!openLogs || !deviceMenu->actions().contains(openLogs)) return fail("open_logs_menu");
    openLogs->trigger(); if (!QFileInfo(revealedLog).isDir()) return fail("open_logs_before_connection");
    const QString screenshots = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
    const auto screenshot = [&](const QString& name, QWidget* widget = nullptr) {
        if (screenshots.isEmpty()) return true;
        wait(20); return QDir().mkpath(screenshots) && (widget ? widget : &window)->grab().save(QDir(screenshots).filePath(name));
    };
    if (!screenshot("device-menu-main-zh_CN.png")) return fail("menu_main_screenshot");
    deviceMenu->popup(window.menuBar()->mapToGlobal(window.menuBar()->actionGeometry(deviceMenu->menuAction()).bottomLeft()));
    if (!screenshot("device-menu-zh_CN.png", deviceMenu)) return fail("menu_screenshot");
    deviceMenu->hide();
    int fileCloses = 0, fileOpens = 0;
    window.setCloseFileHandler([&] { ++fileCloses; });
    window.setOpenFileHandler([&](const QString&) { ++fileOpens; window.showFileOpenResult(file); });
    window.showFileOpenResult(file);
    const auto fileData = waveform->cachedDisplayModel().data;
    manage->trigger(); wait(20);
    if (!dialog->isVisible() || !window.findChild<QWidget*>("deviceConnectionAlternatives")->isHidden()) return fail("advanced_connections_default");
    if (!screenshot("connections-zh_CN.png", dialog)) return fail("connection_screenshot");
    connections->setChecked(true);
    window.findChild<QComboBox*>("deviceSimulatorProfile")->setCurrentIndex(1);
    window.findChild<QPushButton*>("deviceSelectSimulator")->click();
    if (!until([&] { return controller->snapshot().acquisition == DeviceAcquisitionState::Streaming &&
        controller->snapshot().samples >= 1500 && !waveform->cachedDisplayModel().sampleTimes.empty(); })) return fail("connect_directly_to_waveform");
    if (dialog->isVisible() || !waveform->isVisible() || !details->isHidden() ||
        window.findChild<WaveformWidget*>() != waveform || !window.property("deviceWaveformActive").toBool()) return fail("unified_view");
    const auto liveModel = waveform->cachedDisplayModel();
    const QString sessionLog = controller->diagnosticLogPath();
    auto* logButton = window.findChild<QPushButton*>("deviceOpenLogs");
    if (!logButton || !logButton->isVisible() || !QFileInfo(sessionLog).isFile()) return fail("simulator_log_created");
    logButton->click(); if (revealedLog != sessionLog || !controller->hasActiveTasks()) return fail("open_live_log");
    if (liveModel.data.empty() || liveModel.channelUnits.isEmpty() || liveModel.sampleTimes[0].empty()) return fail("live_signal_model");
    auto* viewWindow = window.findChild<QComboBox*>("deviceViewWindow");
    auto* viewChannel = window.findChild<QComboBox*>("deviceViewChannel");
    auto* freeze = window.findChild<QPushButton*>("deviceFreeze");
    auto* reset = window.findChild<QPushButton*>("deviceResetView");
    auto* viewHelp = window.findChild<QToolButton*>("deviceViewHelp");
    if (!viewWindow || !viewChannel || !freeze || !reset || !freeze->isVisible() || !reset->isVisible() ||
        viewWindow->currentData().toDouble() != 2 || viewChannel->currentData().toInt() != -1 ||
        std::abs(waveform->timeRange().second - waveform->timeRange().first - 2) > 1e-6) return fail("friendly_default_view");
    if (!viewHelp || !viewHelp->isVisible() || viewHelp->text() != i18n.text("app.device.view_help")) return fail("view_help_discoverable");
    const std::vector<QWidget*> tabOrder{window.findChild<QPushButton*>("deviceAcquire"), window.findChild<QPushButton*>("deviceRecord"),
        logButton, window.findChild<QToolButton*>("deviceLiveAdvanced"), freeze, reset, viewHelp, waveform};
    for (std::size_t i = 1; i < tabOrder.size(); ++i) {
        auto* next = tabOrder[i-1];
        for (int visited = 0; visited < 100; ++visited) {
            next = next->nextInFocusChain();
            if (next->isVisible() && next->isEnabled() && (next->focusPolicy() & Qt::TabFocus)) break;
        }
        if (next != tabOrder[i]) return fail("view_keyboard_tab_order");
    }
    viewHelp->click(); wait(20);
    if (QToolTip::text() != i18n.text("app.device.view_only_hint")) return fail("view_help_content");
    QToolTip::hideText();
    const QPointF point(waveform->width() * .6, waveform->height() * .4);
    const auto wheel = [&](Qt::KeyboardModifiers modifiers) {
        QWheelEvent event(point, waveform->mapToGlobal(point.toPoint()), QPoint(), QPoint(0,120), Qt::NoButton,
                          modifiers, Qt::NoScrollPhase, false); QApplication::sendEvent(waveform, &event);
    };
    const auto key = [&](int code) { QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier); QApplication::sendEvent(waveform, &event); };
    wheel(Qt::ControlModifier);
    if (viewWindow->currentData().toDouble() != 1) return fail("ctrl_wheel_time_zoom");
    key(Qt::Key_Space);
    if (!freeze->isChecked() || freeze->text() != i18n.text("app.device.return_live")) return fail("space_pauses_view");
    const auto pausedRange = waveform->timeRange();
    const auto pausedSamples = controller->snapshot().samples;
    const auto pausedData = waveform->cachedDisplayModel().data;
    wait(150);
    if (waveform->timeRange() != pausedRange || controller->snapshot().samples <= pausedSamples ||
        controller->snapshot().acquisition != DeviceAcquisitionState::Streaming) return fail("paused_view_keeps_acquiring");
    wheel(Qt::ShiftModifier);
    if (std::abs(waveform->amplitudeZoom() - 1.25) > 1e-9 || waveform->cachedDisplayModel().data != pausedData)
        return fail("shift_wheel_display_gain_preserves_data");
    auto* sharedSeconds = window.findChild<QDoubleSpinBox*>("eegWindowSeconds");
    sharedSeconds->setValue(.75);
    if (viewWindow->currentData().toDouble() != .75 ||
        std::abs(waveform->timeRange().second - waveform->timeRange().first - .75) > 1e-6)
        return fail("shared_custom_window_matches_live_range");
    wheel(Qt::ControlModifier);
    if (viewWindow->currentData().toDouble() != .5) return fail("paused_time_zoom");
    const auto beforePan = waveform->timeRange();
    QMouseEvent press(QEvent::MouseButtonPress, point, waveform->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(waveform, &press);
    const QPointF moved = point + QPointF(100,0);
    QMouseEvent move(QEvent::MouseMove, moved, waveform->mapToGlobal(moved.toPoint()), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(waveform, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, moved, waveform->mapToGlobal(moved.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(waveform, &release);
    const auto pannedRange = waveform->timeRange();
    if (pannedRange.second >= beforePan.second || pannedRange.first < pausedRange.first - 10) return fail("paused_drag_pan");
    key(Qt::Key_Right);
    if (waveform->timeRange().second <= pannedRange.second) return fail("paused_keyboard_pan");
    viewChannel->setCurrentIndex(2);
    if (waveform->cachedDisplayModel().data.size() != 1 || waveform->cachedDisplayModel().channelNames[0] != liveModel.channelNames[1])
        return fail("single_channel_detail");
    if (!screenshot("live-paused-detail-zh_CN.png")) return fail("detail_screenshot");
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, point, waveform->mapToGlobal(point.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(waveform, &doubleClick);
    if (viewWindow->currentData().toDouble() != 2 || viewChannel->currentData().toInt() != -1 ||
        waveform->amplitudeZoom() != 1 || !freeze->isChecked()) return fail("double_click_resets_without_resuming");
    wheel(Qt::ShiftModifier); key(Qt::Key_0);
    if (waveform->amplitudeZoom() != 1 || !freeze->isChecked()) return fail("keyboard_reset_view");
    key(Qt::Key_Space);
    if (!until([&] { return !freeze->isChecked() && waveform->timeRange().second > pausedRange.second; })) return fail("space_returns_to_latest");
    if (!screenshot("live-zh_CN.png")) return fail("live_screenshot");
    window.resize(1774,1000); window.findChild<QTabWidget*>("eegParameterTabs")->setCurrentIndex(1); wait(50);
    const auto headingY=[&](const char* name){auto* heading=window.findChild<QLabel*>(name);return heading->mapTo(&window,QPoint()).y();};
    if(std::abs(headingY("eegWaveformHeading")-headingY("eegAnalysisHeading"))>8)
        return fail("legacy_column_headers_alignment");
    if(window.findChild<QPushButton*>("deviceAcquire")->mapTo(&window,QPoint()).y()<waveform->mapTo(&window,QPoint(0,waveform->height())).y())
        return fail("transport_controls_above_workbench");
    if(viewWindow->isVisible() || viewChannel->isVisible())return fail("duplicate_live_view_controls");
    auto* columns=window.findChild<QSplitter*>("eegWorkspaceSplitter");
    const auto originalSizes=columns->sizes();
    for(int index:{1,2}) {
        auto* handle=columns->handle(index);
        const auto before=columns->sizes();
        const QPointF start=handle->rect().center();
        const QPointF global=handle->mapToGlobal(start.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress,start,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(handle,&press);
        QMouseEvent move(QEvent::MouseMove,start+QPointF(35,0),global+QPointF(35,0),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(handle,&move);
        QMouseEvent release(QEvent::MouseButtonRelease,start,global+QPointF(35,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(handle,&release);wait(20);
        if(before==columns->sizes())return fail("splitter_drag");
        handle->setFocus();
        const auto beforeKey = columns->sizes();
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(handle, &right);wait(20);
        if (columns->sizes() == beforeKey || handle->toolTip().isEmpty()
            || handle->accessibleName().isEmpty()) return fail("splitter_keyboard_accessibility");
        columns->setSizes(originalSizes);wait(20);
    }
    if(!screenshot("live-1774-zh_CN.png"))return fail("wide_layout_screenshot");
    auto* parameters = window.findChild<QTabWidget*>("eegParameterTabs");
    parameters->setCurrentIndex(0);
    for (const QSize desktop : {QSize(1920, 1080), QSize(2560, 1440)}) {
        window.resize(desktop); wait(50);
        auto* panel = window.findChild<CollapsiblePanel*>();
        auto* channelTab = parameters->widget(0);
        auto* scroll = channelTab->findChild<QScrollArea*>();
        if (!panel || !scroll || scroll->height() < channelTab->height() / 2 ||
            scroll->mapTo(channelTab, QPoint()).y() > 100)
            return fail("desktop_channel_scroll_geometry");
        auto* navigation = window.findChild<QTabBar*>("eegWorkspaceNavigation");
        if (!navigation || navigation->height() < 34 || QFontMetrics(navigation->font()).height() < 14)
            return fail("desktop_navigation_readability");
        if (!screenshot(QString("live-desktop-%1-zh_CN.png").arg(desktop.width())))
            return fail("desktop_screenshot");
    }
    window.resize(1774, 1000); parameters->setCurrentIndex(1); wait(30);
    auto* grip=columns->handle(2);
    QEnterEvent enter(grip->rect().center(),grip->rect().center(),grip->mapToGlobal(grip->rect().center()));
    QApplication::sendEvent(grip,&enter);wait(20);
    if(!screenshot("splitter-hover-zh_CN.png"))return fail("hover_screenshot");
    QEvent leave(QEvent::Leave);QApplication::sendEvent(grip,&leave);

    for (const QString& locale : {QString("zh_CN"), QString("en_US"), QString("ja_JP"), QString("zh_TW")}) {
        if (!i18n.loadLocale(locale, &error)) return fail("locale");
        if (deviceMenu->title() != i18n.text("app.menu.device") || manage->text() != i18n.text("app.device.connections") ||
            openLogs->text() != i18n.text("app.device.open_logs") || logButton->text() != i18n.text("app.device.open_logs")) return fail("device_menu_translation");
        if (viewWindow->accessibleName() != i18n.text("app.device.view_window") ||
            viewChannel->itemText(0) != i18n.text("app.device.view_all_channels") || reset->text() != i18n.text("app.device.view_reset")) return fail("view_controls_translation");
        for (const QSize size : {QSize(1280,800), QSize(900,620), QSize(760,560)}) {
            window.resize(size); wait(20);
            if (window.size() != size || !waveform->isVisible() || waveform->height() < 180 || !details->isHidden()) {
                std::cerr << "COMPACT_WAVEFORM locale=" << locale.toStdString()
                          << " requested=" << size.width() << 'x' << size.height()
                          << " actual=" << window.width() << 'x' << window.height()
                          << " waveform_visible=" << waveform->isVisible() << " waveform_height=" << waveform->height()
                          << " details_hidden=" << details->isHidden() << '\n';
                return fail("compact_waveform");
            }
            if (size.width() == 760 && !screenshot("live-" + locale + "-760.png")) return fail("compact_view_screenshot");
        }
    }
    i18n.loadLocale("zh_CN", &error); window.resize(1280,800);
    auto delayed = file; delayed.waveform.data.assign(2, std::vector<float>(1000, 9999.0f));
    window.showFileOpenResult(delayed);
    if (waveform->cachedDisplayModel().sampleTimes.empty()) return fail("late_file_result_overwrites_live");
    delayed.session.path = "late-new-file.csv";
    window.showFileOpenResult(delayed);
    window.showFileOpenError(makeError("ipc.sidecar_timeout"));
    if (!window.property("deviceWaveformActive").toBool() || !waveform->isVisible() ||
        waveform->cachedDisplayModel().sampleTimes.empty()) return fail("late_new_file_or_error_overwrites_live");
    const auto beforeManager = controller->snapshot().samples;
    manage->trigger(); wait(100);
    if (!until([&] { return controller->snapshot().samples > beforeManager + 30; }) ||
        controller->snapshot().acquisition != DeviceAcquisitionState::Streaming) return fail("manager_interrupts_acquisition");
    dialog->hide();
    if (window.findChild<QPushButton*>("deviceShowFileWaveform") || fileCloses != 1)
        return fail("file_session_not_discarded");
    wheel(Qt::ShiftModifier);
    window.openPathForBrowser(file.session.path);
    if (fileOpens != 1 || window.property("deviceWaveformActive").toBool() ||
        waveform->cachedDisplayModel().data != fileData || waveform->amplitudeZoom() != 1)
        return fail("explicit_file_reopen");
    const auto beforeFile = controller->snapshot().samples;
    wait(100);
    if (waveform->cachedDisplayModel().data != fileData || controller->snapshot().samples <= beforeFile) return fail("live_updates_overwrite_file");
    window.findChild<QPushButton*>("deviceGlobalStatus")->click();
    if (!until([&] { return !waveform->cachedDisplayModel().sampleTimes.empty(); })) return fail("return_to_live");
    if (fileCloses != 2) return fail("reopened_file_session_not_discarded");
    reset->click();
    auto* advanced = window.findChild<QToolButton*>("deviceLiveAdvanced"); advanced->setChecked(true);
    if (details->isHidden()) return fail("advanced_access"); advanced->setChecked(false);
    if (!screenshot("live-returned-zh_CN.png")) return fail("return_screenshot");
    auto* manager = window.findChild<QWidget*>("deviceWorkspace");
    if (!manager || !manager->styleSheet().isEmpty()) return fail("independent_device_theme");
    for (auto* action : window.findChildren<QAction*>())
        if (action->text() == i18n.text("app.theme.dark")) { action->trigger(); break; }
    manage->trigger(); wait(20);
    if (dialog->palette().color(QPalette::Window) != window.palette().color(QPalette::Window) ||
        dialog->palette().color(QPalette::Highlight) != window.palette().color(QPalette::Highlight)) return fail("shared_dark_palette");
    if (!screenshot("connections-dark-zh_CN.png", dialog)) return fail("dark_connection_screenshot");
    dialog->hide();
    if (!screenshot("live-dark-zh_CN.png")) return fail("dark_live_screenshot");
    controller->stopAcquisition();
    if (!until([&] { return !controller->hasActiveTasks(); })) return fail("stop");
    openLogs->trigger(); if (revealedLog != sessionLog) return fail("open_log_after_stop");
    QFile saved(sessionLog); if (!saved.open(QIODevice::ReadOnly)) return fail("read_simulator_log");
    const auto savedBytes = saved.readAll();
    if (!savedBytes.contains("stream_stopped") || !savedBytes.contains("signal_stats") ||
        !savedBytes.contains("simulator.in_process") || !savedBytes.contains("\"simulated\":true")) return fail("simulator_log_lifecycle_and_stats");
    controller->disconnectDevice();
    if (!until([&] { return controller->snapshot().connection == DeviceConnectionState::Disconnected; })) return fail("disconnect");
    logButton->click(); if (revealedLog != sessionLog) return fail("open_log_after_disconnect");
    std::cout << "smoke-ui-device-waveform-ok device_menu=true shared_instance=true auto_start=true advanced_hidden=true file_isolation=true manager_background=true locales=4 compact=true\n";
    return 0;
}
} // namespace bf
