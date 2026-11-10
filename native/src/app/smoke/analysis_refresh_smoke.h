#pragma once

#include "ui/main_window.h"

#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QEventLoop>
#include <QImage>
#include <QTimer>

#include <array>
#include <cmath>
#include <functional>
#include <iostream>

// A synchronous observer cancels the DSP generation before a queued worker
// completion can run. The regression never depends on how fast FFT finishes.
class AnalysisGenerationObserver final : public QObject {
public:
    std::function<void(int)> changed;
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::DynamicPropertyChange
            && static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == "analysisGeneration"
            && changed) changed(object->property("analysisGeneration").toInt());
        return QObject::eventFilter(object, event);
    }
};

inline int smokeAnalysisRefresh(int argc, char** argv, const bf::VersionInfo& version, const QString& repoRoot) {
    QApplication app(argc, argv);
    bf::I18nService i18n(repoRoot);
    QString error;
    if (!i18n.loadLocale("en_US", &error)) return 1;
    bf::MainWindow window(version, &i18n, repoRoot);
    window.resize(1280, 800);
    auto* service = window.findChild<bf::AnalysisService*>();
    auto* status = window.findChild<QLabel*>("eegAnalysisStatus_0");
    auto* summary = window.findChild<QLabel*>("eegAnalysisSummary_0");
    auto* plot = window.findChild<QWidget*>("eegAnalysisPlot_0");
    auto* waveform = window.findChild<bf::WaveformWidget*>();
    auto* position = window.findChild<QDoubleSpinBox*>("eegPlaybackPosition");
    QAction* play = nullptr;
    for (auto* action : window.findChildren<QAction*>())
        if (action->text() == i18n.text("app.playback.play")) play = action;
    const auto fail = [](const char* reason) {
        std::cerr << "analysis-refresh-failed " << reason << '\n';
        return 1;
    };
    if (!service || !status || !summary || !plot || !waveform || !position || !play) return fail("controls");
    const auto pumpUntil = [](const std::function<bool()>& predicate, int timeout) {
        QEventLoop loop;
        QTimer poll, deadline;
        poll.setInterval(5); deadline.setSingleShot(true);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (predicate()) loop.quit(); });
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        if (predicate()) return true;
        poll.start(); deadline.start(timeout); loop.exec();
        return predicate();
    };
    const auto wait = [](int milliseconds) {
        QEventLoop loop; QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit); loop.exec();
    };
    const auto state = [&]() { return status->property("analysisState").toString(); };
    const auto generation = [&]() { return status->property("analysisGeneration").toInt(); };
    bf::FileOpenResult input;
    input.session.ok = true; input.session.path = "analysis-refresh.csv"; input.session.format = "CSV";
    input.session.fs = 200; input.session.duration = 30; input.session.nSamples = 6000; input.session.nChannels = 2;
    input.session.chNames = {"alpha", "beta"};
    input.waveform.fs = 200; input.waveform.t0 = 0; input.waveform.t1 = 30;
    input.waveform.channelNames = input.session.chNames; input.waveform.data.resize(2);
    for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < 6000; ++n)
        input.waveform.data[ch].push_back(30 * std::sin(2 * 3.141592653589793 * (8 + ch * 3) * n / 200.));
    // A bounded fixture cache spans the whole short recording, so playback IO
    // cannot freeze the clock before the periodic analysis refresh is tested.
    int reads = 0;
    window.setWindowRequestHandler([&](const QString&, double, double) { ++reads; window.showFileOpenResult(input); });
    window.showFileOpenResult(input); window.show();
    if (!pumpUntil([&]() { return state() == "ready"; }, 4000)) return fail("real_worker_not_ready");
    wait(60); // Allow the ready footer's height-for-width layout to settle.
    const int initialGeneration = generation();
    const QString initialStatus = status->text(), initialSummary = summary->text();
    const QImage initialImage = plot->grab().toImage();
    const QRect initialGeometry = plot->geometry();
    if (initialSummary.isEmpty()) return fail("initial_summary_empty");
    int captured = 0, heldGeneration = 0;
    double requestStart = 0, requestEnd = 0;
    bool armed = true, stableAtDispatch = false;
    AnalysisGenerationObserver observer;
    observer.changed = [&](int current) {
        if (!armed || current <= 0 || current == initialGeneration) return;
        service->cancel();
        ++captured;
        if (heldGeneration != 0) return;
        heldGeneration = current;
        requestStart = std::ceil(waveform->timeRange().first * 200 - 1e-6) / 200;
        requestEnd = requestStart + 10;
        stableAtDispatch = state() == "ready" && status->text() == initialStatus
            && summary->text() == initialSummary && plot->geometry() == initialGeometry
            && plot->grab().toImage() == initialImage;
    };
    status->installEventFilter(&observer);
    play->trigger();
    if (!pumpUntil([&]() { return heldGeneration != 0; }, 2500)) return fail("periodic_refresh_not_dispatched");
    if (!stableAtDispatch || !(requestStart > 0.1)) return fail("background_cleared_ready_at_dispatch");
    const double beforeHold = position->value();
    wait(750); // Exceeds the 500ms refresh period while the generation is held.
    if (captured != 1 || generation() != heldGeneration) return fail("background_restarts_held_request");
    if (state() != "ready" || status->text() != initialStatus || summary->text() != initialSummary
        || plot->geometry() != initialGeometry || plot->grab().toImage() != initialImage)
        return fail("ready_surface_changed_while_waiting");
    if (!(position->value() > beforeHold + 0.2) || reads != 0) return fail("playback_frozen_or_cache_waiting");
    // Retranslation must use the context of the displayed completed result,
    // even though a newer time window is already being calculated.
    if (!i18n.loadLocale("zh_CN", &error)) return fail("hold_locale_load");
    const QString translatedInitialContext = i18n.text("app.analysis.context", {
        {"channel", "alpha"}, {"start", i18n.formatNumber(0, 2)}, {"end", i18n.formatNumber(10, 2)},
    });
    if (state() != "ready" || generation() != heldGeneration || captured != 1
        || status->text() != i18n.text("app.analysis.ready") + "\n" + translatedInitialContext
        || summary->text().isEmpty()) return fail("hold_locale_mixes_pending_context");
    if (!i18n.loadLocale("en_US", &error)) return fail("hold_locale_restore");
    if (state() != "ready" || generation() != heldGeneration || captured != 1
        || status->text() != initialStatus || summary->text() != initialSummary)
        return fail("hold_locale_recomputes_or_changes_result");
    bf::AnalysisResult replacement;
    replacement.kind = bf::AnalysisKind::Psd; replacement.channel = 0;
    for (int frequency = 0; frequency <= 100; ++frequency) {
        replacement.spectrum.frequencies.push_back(frequency);
        replacement.spectrum.values.push_back(1234.0 + frequency);
    }
    // 频段功率随结果一起到达。这里故意给出与 spectrum 自行积分不同的数值，
    // 界面若重新计算就会显示另一组数字。
    const std::array<double, 5> replacementPowers{4321.0, 4322.0, 4323.0, 4324.0, 4325.0};
    replacement.bandPowers.assign(replacementPowers.begin(), replacementPowers.end());
    service->completed(heldGeneration, replacement);
    const QString expectedContext = i18n.text("app.analysis.context", {
        {"channel", "alpha"}, {"start", i18n.formatNumber(requestStart, 2)}, {"end", i18n.formatNumber(requestEnd, 2)},
    });
    const QString expectedPower = i18n.formatNumber(replacement.bandPowers.front(), 2);
    if (state() != "ready" || summary->text() == initialSummary || !summary->text().contains(expectedPower)
        || status->text() != i18n.text("app.analysis.ready") + "\n" + expectedContext)
        return fail("replacement_summary_context_not_paired");
    const QString freshStatus = status->text(), freshSummary = summary->text();
    const QImage freshImage = plot->grab().toImage();
    if (freshImage == initialImage) return fail("replacement_plot_not_updated");
    replacement.spectrum.values.assign(replacement.spectrum.values.size(), 99999.0);
    service->completed(initialGeneration, replacement);
    if (status->text() != freshStatus || summary->text() != freshSummary || plot->grab().toImage() != freshImage)
        return fail("old_generation_overwrites_ready");
    QComboBox* channel = nullptr;
    for (auto* combo : plot->parentWidget()->findChildren<QComboBox*>())
        if (combo->count() == 2 && combo->itemText(0) == "alpha" && combo->itemText(1) == "beta") channel = combo;
    auto* maximumFrequency = plot->parentWidget()->findChild<QSpinBox*>();
    if (!channel || !maximumFrequency) return fail("explicit_controls");
    channel->setCurrentIndex(1); // The observer also holds explicit work deterministically.
    const int channelGeneration = generation();
    if (channelGeneration == heldGeneration || state() != "pending" || !summary->text().isEmpty()
        || plot->grab().toImage() == freshImage) return fail("channel_change_keeps_wrong_ready_result");
    maximumFrequency->setValue(maximumFrequency->value() - 1);
    if (generation() == channelGeneration || state() != "pending" || !summary->text().isEmpty())
        return fail("explicit_parameter_did_not_replace");
    const QString pendingStatus = status->text();
    service->completed(heldGeneration, replacement);
    if (state() != "pending" || status->text() != pendingStatus || !summary->text().isEmpty())
        return fail("old_generation_overwrites_explicit_pending");
    service->failed(heldGeneration, "analysis.failed", "obsolete-background-failure");
    if (state() != "pending" || status->text() != pendingStatus || !summary->text().isEmpty())
        return fail("old_failure_overwrites_explicit_pending");
    armed = false;
    status->removeEventFilter(&observer);
    std::cout << "analysis-refresh-ok background=true preserved=true atomic=true stale=true explicit=true timer=true\n";
    return 0;
}
