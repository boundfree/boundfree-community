#pragma once
#include "ui/main_window.h"
#include "dsp/ica_core.h"
#include "app/smoke/live_ica_wifi_smoke.h"
#include <QApplication>
#include <QWheelEvent>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDialog>
#include <QTabWidget>
#include <QToolButton>
#include <QStatusBar>
#include <QEventLoop>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QThreadPool>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace bf {
inline int smokeLiveIca(int argc, char** argv, const VersionInfo& version, const QString& root) {
    QApplication app(argc, argv); QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QCoreApplication::setOrganizationName("BoundFreeLiveIcaTest"); QCoreApplication::setApplicationName("workbench");
    qputenv("BF_ACQUISITION_LOG_DIRECTORY", temp.path().toUtf8());
    I18nService i18n(root); QString localeError;
    if (!i18n.loadLocale("zh_CN", &localeError)) return 1;
    MainWindow window(version, &i18n, root); window.resize(1280, 900); window.show();
    const auto wait = [](int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
    const auto until = [&](const auto& ready, int timeout = 30000) {
        QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < timeout) wait(10); return ready();
    };
    const auto fail = [&](const char* reason) {
        const auto view = window.findChild<AcquisitionController*>()->snapshot();
        std::cerr << "LIVE_ICA_UI FAIL " << reason << " samples=" << view.samples
                  << " window=" << window.width() << 'x' << window.height()
                  << " minimum=" << window.minimumSizeHint().width() << 'x' << window.minimumSizeHint().height()
                  << " device_error=" << view.error.code.toStdString()
                  << " separation_state=" << int(window.findChild<EegSeparationController*>()->state())
                  << " status=" << window.findChild<QLabel*>("separationStatus")->text().toStdString() << '\n';
        return 1;
    };
    auto* acquisition = window.findChild<AcquisitionController*>();
    auto* separation = window.findChild<EegSeparationController*>();
    auto* live = window.findChild<LiveIcaController*>();
    auto* navigation = window.findChild<QTabBar*>("eegWorkspaceNavigation");
    auto* snapshot = window.findChild<QPushButton*>("separationDeviceSnapshot");
    auto* toggle = window.findChild<QPushButton*>("separationLiveToggle");
    const auto screenshots = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
    const auto captureStep = [&](const char* name) {
        if (screenshots.isEmpty()) return true;
        QDir().mkpath(screenshots); wait(40);
        return window.grab().save(QDir(screenshots).filePath(QString::fromLatin1(name) + ".png"));
    };
    QString error; QObject::connect(separation, &EegSeparationController::failed, &window, [&](const AppError& e) { error = e.code; });
    QObject::connect(live, &LiveIcaController::failed, &window, [&](const AppError& e) { error = e.code; });
    if (!toggle || toggle->isEnabled()) return fail("empty_live_enabled");
    // Connections belong exclusively to the Device menu. The separation page
    // consumes the resulting signal and guides calibration, review and display.
    navigation->setCurrentIndex(1);
    if (window.findChild<QPushButton*>("separationConnectDevice")) return fail("duplicate_connection_entry");
    if (window.findChild<QWidget*>("separationAlternativeInput") || window.findChild<QPushButton*>("separationOpen") ||
        window.findChild<QPushButton*>("separationOpenTeaching")) return fail("duplicate_file_entry");
    if (window.findChild<QWidget*>("eegSeparationWorkspace")->property("workflowStep").toInt() != 1) return fail("empty_workflow_step");
    if (!captureStep("workflow-empty-zh_CN")) return fail("empty_capture");
    window.findChild<QAction*>("deviceManageConnections")->trigger();
    if (navigation->count() != 2 || !window.findChild<QDialog*>("deviceConnectionWindow")->isVisible()) return fail("device_menu_window");
    if (window.findChild<QWidget*>("deviceWorkspace")->property("appVersion").toString() != version.version)
        return fail("device_tab_diagnostic_version_missing");
    if (!captureStep("workflow-device-zh_CN")) return fail("device_capture");
    window.findChild<QComboBox*>("deviceSimulatorProfile")->setCurrentIndex(1);
    window.findChild<QPushButton*>("deviceSelectSimulator")->click();
    if (!until([&] { return acquisition->snapshot().samples > 0; })) return fail("connect_from_ica");
    if (navigation->currentIndex() != 0 || !window.property("deviceWaveformActive").toBool()) return fail("connection_did_not_show_waveform");
    navigation->setCurrentIndex(1);
    auto* cancel = window.findChild<QPushButton*>("separationCancel");
    if (!until([&] { return snapshot->isEnabled() || !cancel->isHidden(); })) return fail("initial_capture_state");
    if (snapshot->isEnabled()) snapshot->click();
    if (!until([&] { return !cancel->isHidden(); })) return fail("capture_cancel_hidden");
    cancel->click();
    if (!until([&] { return snapshot->isEnabled(); }) || acquisition->snapshot().acquisition != DeviceAcquisitionState::Streaming)
        return fail("cancel_stops_acquisition");
    snapshot->click();
    if (!until([&] { return separation->state() == EegSeparationController::State::Ready; })) return fail("waiting_capture_failed");
    if (separation->source().duration() < 5 || separation->source().duration() > 10.01) return fail("capture_duration");
    auto* workspace = window.findChild<QWidget*>("eegSeparationWorkspace");
    if (workspace->property("workflowStep").toInt() != 2) return fail("channel_workflow_step");
    if (!captureStep("workflow-channels-zh_CN")) return fail("channel_capture");
    std::cout << "LIVE_ICA_STAGE capture_ready\n" << std::flush;
    window.findChild<QPushButton*>("separationAllObservations")->click();
    if (separation->settings().roles != std::vector<int>(4, 1)) return fail("observation_shortcut");
    window.findChild<QPushButton*>("separationMainAction")->click();
    if (!until([&] { return !separation->isBusy(); }, 60000) || !separation->output()) {
        std::cerr << error.toStdString() << '\n'; return fail("calibration_fit");
    }
    const auto model = separation->output(); const auto calibration = model->analysis;
    if (workspace->property("workflowStep").toInt() != 3) return fail("review_workflow_step");
    if (!captureStep("workflow-review-zh_CN")) return fail("review_capture");
    std::cout << "LIVE_ICA_STAGE model_fitted\n" << std::flush;
    if (toggle->isEnabled()) return fail("live_without_review");
    // Set a label through the actual editor and confirm before applying it.
    window.findChild<QRadioButton*>("separationClassify0")->click();
    window.findChild<QCheckBox*>("separationConfirmed")->setChecked(true);
    if (!until([&] { return toggle->isEnabled(); })) return fail("reviewed_model_unavailable");
    if (workspace->property("workflowStep").toInt() != 4) return fail("result_workflow_step");
    auto* rolesTable = window.findChild<QTableWidget*>("separationChannels");
    rolesTable->setRangeSelected(QTableWidgetSelectionRange(0, 0, 1, 1), true);
    auto* bulkRole = window.findChild<QComboBox*>("separationBulkRole");
    auto* applyRole = window.findChild<QPushButton*>("separationApplyRole");
    if (!bulkRole->isEnabled() || !applyRole->isEnabled()) return fail("bulk_role_setup");
    acquisition->startRecording(temp.path());
    if (!until([&] { return acquisition->snapshot().recording.state == CaptureRecordState::Recording; })) return fail("recording_start");
    int frames = 0; std::shared_ptr<const LiveIcaFrame> latest;
    QObject::connect(live, &LiveIcaController::frameReady, &window, [&](auto frame) { ++frames; latest = frame; });
    const auto sampleCount = acquisition->snapshot().samples;
    toggle->click();
    if (!until([&] { return frames >= 3; })) { std::cerr << error.toStdString() << '\n'; return fail("live_frames"); }
    const auto lockedRoles = separation->settings().roles;
    applyRole->click();
    if (bulkRole->isEnabled() || applyRole->isEnabled() || separation->settings().roles != lockedRoles || !live->isActive())
        return fail("live_bulk_roles_can_change_model_input");
    std::cout << "LIVE_ICA_STAGE live_frames\n" << std::flush;
    auto* grouped = window.findChild<QWidget*>("separationGroupTraces");
    const int beforeView = frames;
    const double beforeViewEnd = latest->end;
    const QPointF focus(grouped->width()*.5, grouped->height()*.5);
    for (auto* target : {grouped, window.findChild<QWidget*>("separationComponentTrace")})
    for (const auto modifiers : {Qt::ControlModifier, Qt::ShiftModifier}) {
        QWheelEvent zoom(focus, target->mapToGlobal(focus.toPoint()), QPoint(), QPoint(0,120),
                         Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
        QApplication::sendEvent(target,&zoom);
    }
    if (!until([&] { return frames > beforeView + 1 && latest->end > beforeViewEnd; }) ||
        !live->isActive() || separation->output()!=model || model->analysis!=calibration ||
        acquisition->snapshot().recording.state!=CaptureRecordState::Recording)
        return fail("live_plot_zoom_changes_model_or_stops_recording");

    auto* sharedBar = window.findChild<QWidget*>("deviceLivePage");
    auto* pauseView = window.findChild<QPushButton*>("deviceFreeze");
    if (!sharedBar || !sharedBar->isVisible() || sharedBar->parentWidget()!=window.centralWidget() ||
        window.findChildren<QPushButton*>("deviceAcquire").size()!=1 || !pauseView)
        return fail("device_actions_not_shared_between_workspaces");
    auto* recordNotice = window.findChild<QLabel*>("deviceRecordingStatusBar");
    if (!recordNotice || !recordNotice->isVisible() || !window.statusBar()->isAncestorOf(recordNotice) ||
        recordNotice->toolTip()!=recordNotice->text() || window.findChild<QLabel*>("deviceRecordStatus")->isVisible())
        return fail("recording_status_not_in_the_existing_status_bar");
    const int statusHeight = window.statusBar()->height();
    window.statusBar()->showMessage("transient diagnostic",100);
    if (recordNotice->isVisible()) return fail("recording_notice_covers_diagnostics");
    if (!until([&] {return recordNotice->isVisible();},2000) || window.statusBar()->height()!=statusHeight)
        return fail("recording_notice_not_restored_or_status_bar_grew");
    pauseView->click();
    if (!until([&] { return workspace->property("liveViewPaused").toBool(); })) return fail("ica_view_pause");
    const double frozenEnd = workspace->property("liveEndSeconds").toDouble();
    const double frozenLatest = latest->end;
    const auto frozenFrame = latest;
    if (!until([&] { return latest->end > frozenLatest + .5; }) ||
        workspace->property("liveEndSeconds").toDouble()!=frozenEnd || !live->isActive() ||
        acquisition->snapshot().recording.state!=CaptureRecordState::Recording || separation->output()!=model)
        return fail("view_pause_stops_processing_or_recording");
    navigation->setCurrentIndex(0); wait(100);
    if (!sharedBar->isVisible() || pauseView->isChecked() ||
        !window.findChild<QToolButton*>("deviceViewHelp")->isVisible()) return fail("browser_shared_bar_context");
    navigation->setCurrentIndex(1); wait(100);
    if (!sharedBar->isVisible() || !pauseView->isChecked() ||
        workspace->property("liveEndSeconds").toDouble()!=frozenEnd) return fail("ica_pause_not_retained_on_switch");
    window.findChild<QRadioButton*>("separationClassify1")->click();
    const auto peaks = grouped->property("visiblePeakToPeak").toList();
    if (peaks.size()!=5 || peaks[2].toDouble()<=0 || workspace->property("liveEndSeconds").toDouble()!=frozenEnd)
        return fail("frozen_view_does_not_regroup_with_labels");
    ::bf::separation::ica::Result reference;
    reference.analysis=frozenFrame->signal.analysis; reference.sources=frozenFrame->signal.sources;
    reference.mixing=model->mixing; reference.rank=model->rank;
    const auto expectedEye = ::bf::separation::ica::project(reference, separation->labels(), 1);
    const size_t first = size_t(grouped->property("previewOffsetSeconds").toDouble()*model->fs);
    const size_t last = std::min(expectedEye[0].size(),first+size_t(window.findChild<QComboBox*>("separationDisplayWindow")->currentData().toDouble()*model->fs));
    const auto bounds = std::minmax_element(expectedEye[0].begin()+first,expectedEye[0].begin()+last);
    if (last<=first || std::abs(peaks[2].toDouble()-(*bounds.second-*bounds.first))>1e-7)
        return fail("frozen_group_differs_from_core_projection");
    if (!captureStep("ica-view-paused-shared-controls")) return fail("paused_capture");
    pauseView->click();
    if (!until([&] { return !workspace->property("liveViewPaused").toBool() &&
        workspace->property("liveEndSeconds").toDouble()>frozenEnd; })) return fail("ica_resume_not_latest");
    window.findChild<QPushButton*>("deviceResetView")->click();
    if (window.findChild<QComboBox*>("separationDisplayWindow")->currentData().toDouble()!=10 ||
        window.findChild<QComboBox*>("separationAmplitudeZoom")->currentData().toDouble()!=1)
        return fail("shared_reset_does_not_target_ica");
    if (window.findChild<QToolButton*>("deviceViewHelp")->isVisible() ||
        window.findChild<QPushButton*>("separationRoleHelpButton") ||
        window.findChild<QPushButton*>("separationPlotHelpButton"))
        return fail("duplicate_ica_help_entries");
    window.findChild<QPushButton*>("separationHelpButton")->click();
    auto* guide = window.findChild<QDialog*>("separationHelpDialog");
    const int beforeHelp = frames;
    if (!guide || !guide->isVisible() || guide->isModal() ||
        window.findChild<QTabWidget*>("separationHelpTabs")->currentIndex()!=4 ||
        !until([&] { return frames>beforeHelp+1; }) || !live->isActive())
        return fail("shared_help_blocks_live_processing");
    window.findChild<QPushButton*>("separationHelpClose")->click();
    const double firstEnd = latest->end;
    if (!until([&] { return latest->end > firstEnd + .5; })) return fail("not_advancing");
    if (latest->signal.channels.size() != 4 || acquisition->snapshot().samples <= sampleCount ||
        acquisition->snapshot().recording.state != CaptureRecordState::Recording ||
        separation->output() != model || model->analysis != calibration) return fail("live_mutates_calibration_or_recording");
    if (snapshot->isEnabled() || window.findChild<QPushButton*>("separationPlay")->isEnabled() ||
        window.findChild<QPushButton*>("separationMainAction")->isEnabled()) return fail("live_offline_actions_enabled");
    if (window.findChild<QAction*>("openCurrentWorkspaceFile")->isEnabled() ||
        window.findChild<QAction*>("openTeachingRecording")->isEnabled()) return fail("live_file_actions_enabled");
    window.findChild<QRadioButton*>("separationClassify1")->click();
    const int beforeLabel = frames;
    if (!until([&] { return frames > beforeLabel + 1; }) || !live->isActive() || separation->output() != model) return fail("label_causes_refit");
    bool projectedEye = false;
    for (double value : latest->groups[1][0]) projectedEye = projectedEye || (std::isfinite(value) && std::abs(value) > 1e-8);
    if (!projectedEye) return fail("live_label_not_applied");
    window.findChild<QAction*>("deviceManageConnections")->trigger(); const double managerEnd = latest->end;
    if (!until([&] { return latest->end > managerEnd + .3; })) return fail("device_manager_stops_live");
    window.findChild<QDialog*>("deviceConnectionWindow")->hide();
    navigation->setCurrentIndex(0); const double hiddenEnd = latest->end;
    if (!until([&] { return latest->end > hiddenEnd + .3; })) return fail("navigation_stops_live");
    navigation->setCurrentIndex(1);
    const auto screens = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
    if (!screens.isEmpty()) {
        QDir().mkpath(screens);
        for (const auto& locale : {"zh_CN", "en_US", "ja_JP", "zh_TW"}) {
            if (!i18n.loadLocale(locale, &localeError)) return fail("locale");
            for (int width : {1280, 920}) {
                window.resize(width, 900); wait(100);
                if (window.size() != QSize(width, 900)) {
                    window.grab().save(QDir(screens).filePath("live-ica-layout-failure.png"));
                    return fail("ica_window_size_clamped");
                }
                if (!window.grab().save(QDir(screens).filePath(QString("live-ica-%1-%2.png").arg(locale).arg(width)))) return fail("screenshot");
            }
        }
        i18n.loadLocale("zh_CN", &localeError); window.resize(1280, 900);
    }
    toggle->click(); const int stopped = frames; wait(400);
    if (live->isActive() || frames != stopped || separation->output() != model || !snapshot->isEnabled()) return fail("stop_loses_calibration_or_late_frame");
    window.findChild<QCheckBox*>("separationConfirmed")->setChecked(true);
    toggle->click();
    if (!until([&] { return frames > stopped; })) return fail("restart_live");
    window.findChild<QPushButton*>("deviceRecord")->click();
    if (!until([&] { auto s = acquisition->snapshot().recording.state; return s == CaptureRecordState::Saved || s == CaptureRecordState::Partial; })) return fail("recording_finalize");
    const auto recording = acquisition->snapshot().recording;
    if (!recording.writtenSamples) return fail("recording_empty");
    window.findChild<QPushButton*>("deviceAcquire")->click();
    if (!until([&] { return !acquisition->hasActiveTasks() && !live->isActive(); })) return fail("disconnect_live_active");
    const int disconnected = frames; wait(400);
    if (frames != disconnected) return fail("late_frame_after_disconnect");
    acquisition->startAcquisition();
    if (!until([&] { return acquisition->snapshot().samples > 100; })) return fail("reconnect");
    if (toggle->isEnabled()) return fail("old_model_reused_after_reconnect");
    acquisition->stopAcquisition();
    if (!until([&] { return !acquisition->hasActiveTasks(); })) return fail("shutdown");
    if (!smokeWifiLiveIca(window, until, wait)) return fail("wifi_count_8_16_integration");
    QThreadPool::globalInstance()->waitForDone(30000);
    std::cout << "LIVE_ICA_UI PASS waiting_cancel=true fit=true live_updates=true labels=true immutable_calibration=true recording=true reconnect_rejected=true frames=" << frames << '\n';
    return 0;
}
}
