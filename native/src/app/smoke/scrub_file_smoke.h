#pragma once

#include "controller/file_open_controller.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QSlider>
#include <QTimer>

#include <cmath>
#include <iostream>

namespace bf {

// Exercises the actual CSV reader/IPC/controller path. Phase transitions depend
// on completed IO, not assumed machine speed or short sleeps.
inline int runScrubFileSmoke(const VersionInfo& version, const QString& repoRoot, const QString& path) {
    I18nService i18n(repoRoot);
    QString error;
    if (!i18n.loadLocale("en_US", &error)) return 1;
    MainWindow window(version, &i18n, repoRoot);
    FileOpenController controller(repoRoot, version);
    auto* slider = window.findChild<QSlider*>("eegPlaybackProgress");
    auto* position = window.findChild<QDoubleSpinBox*>("eegPlaybackPosition");
    auto* waveform = window.findChild<WaveformWidget*>();
    if (!slider || !position || !waveform) return 1;
    enum class Phase { Initial, FirstPreview, ObsoletePreview, LatestPreview, FinalCommit };
    Phase phase = Phase::Initial;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QString failure;
    bool completed = false;
    int requests = 0;
    const auto fail = [&](const QString& reason) {
        failure = reason;
        loop.quit();
    };
    const auto near = [](double actual, double expected) { return std::abs(actual - expected) < 1e-6; };
    const auto displayed = [&](double start) {
        const auto range = waveform->timeRange();
        return near(range.first, start) && near(range.second, start + 10.0);
    };
    window.setWindowRequestHandler([&](const QString& activePath, double start, double length) {
        ++requests;
        if (requests > 4) { fail("request_explosion"); return; }
        if (activePath != path || !near(length, 10.0)) { fail("request_contract"); return; }
        if (requests == 2) {
            if (!slider->isSliderDown() || !near(start, 70.0)) { fail("burst_dispatch"); return; }
            // Move again while this real request is in flight. Its completion
            // must not replace the last displayed 60s view with obsolete 70s.
            slider->setSliderPosition(8000);
            slider->setSliderPosition(8200);
            slider->setSliderPosition(7500);
        }
        controller.openWindow(activePath, start, length);
    });
    QObject::connect(&controller, &FileOpenController::loaded, &window, [&](const FileOpenResult& result) {
        if (result.waveform.data.size() != 2 || result.waveform.data.front().empty()
            || std::abs(result.waveform.data.front().front() - result.waveform.t0 * 10.0) > 1e-3) {
            fail("physical_slice_samples"); return;
        }
        window.showFileOpenResult(result);
        switch (phase) {
        case Phase::Initial:
            if (result.session.nChannels != 2 || !near(result.session.fs, 100.0)
                || !near(result.session.duration, 100.0)) { fail("fixture_metadata"); return; }
            phase = Phase::FirstPreview;
            slider->setSliderDown(true);
            slider->setSliderPosition(6000);
            break;
        case Phase::FirstPreview:
            if (!slider->isSliderDown() || !displayed(60.0) || !near(position->value(), 60.0)) {
                fail("remote_preview_not_visible_during_drag"); return;
            }
            phase = Phase::ObsoletePreview;
            for (int target : {2000, 3000, 4000, 5000, 7000}) slider->setSliderPosition(target);
            if (requests != 1) { fail("burst_not_coalesced"); return; }
            break;
        case Phase::ObsoletePreview:
            if (!near(result.waveform.t0, 70.0) || !slider->isSliderDown()
                || !displayed(60.0) || !near(position->value(), 75.0)) {
                fail("obsolete_result_overwrites_latest"); return;
            }
            phase = Phase::LatestPreview;
            break;
        case Phase::LatestPreview:
            if (!slider->isSliderDown() || !displayed(75.0) || !near(position->value(), 75.0)
                || requests != 3) { fail("latest_did_not_converge"); return; }
            phase = Phase::FinalCommit;
            slider->setSliderPosition(7625);
            slider->setSliderDown(false);
            if (requests != 4) { fail("release_dispatch_count"); return; }
            break;
        case Phase::FinalCommit:
            if (slider->isSliderDown() || slider->value() != 7625 || !near(position->value(), 76.25)
                || !near(result.waveform.t0, 76.25) || !displayed(76.25) || requests != 4) {
                fail("final_position_inexact"); return;
            }
            completed = true;
            loop.quit();
            break;
        }
    });
    QObject::connect(&controller, &FileOpenController::failed, &window, [&](const AppError& e) { fail(e.code); });
    QObject::connect(&deadline, &QTimer::timeout, &window, [&]() { fail("timeout"); });
    window.resize(1200, 800);
    window.show();
    deadline.start(15000);
    window.showFileOpenLoading(path);
    controller.openInitialWindow(path, 10.0);
    loop.exec();
    deadline.stop();
    if (!completed || !failure.isEmpty()) {
        std::cerr << "smoke-ui-scrub-file-failed " << failure.toStdString() << " requests=" << requests << '\n';
        return 1;
    }
    std::cout << "smoke-ui-scrub-file-ok remote=true coalesced=true stale=true final=true requests=" << requests << '\n';
    return 0;
}

}  // namespace bf
