#pragma once
#include "ui/main_window.h"
#include "controller/eeg_separation_controller.h"
#include "controller/live_ica_controller.h"
#include "controller/file_open_controller.h"
#include "acquisition/adapters/simulator.h"
#include "acquisition/session/acquisition_session.h"
#include "dsp/ica_core.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace bf {
inline int smokeSimulatorIca(int argc, char** argv, const VersionInfo& version, const QString& root) {
    QApplication app(argc, argv); QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QCoreApplication::setOrganizationName("BoundFreeSimulatorIcaTest");
    QCoreApplication::setApplicationName("workbench");
    qputenv("BF_ACQUISITION_LOG_DIRECTORY", temp.path().toUtf8());
    I18nService i18n(root); QString localeError;
    if (!i18n.loadLocale("zh_CN", &localeError)) return 1;
    MainWindow window(version, &i18n, root); window.resize(1280, 900); window.show();
    auto* acquisition = window.findChild<AcquisitionController*>();
    auto* separation = window.findChild<EegSeparationController*>();
    auto* live = window.findChild<LiveIcaController*>();
    auto* navigation = window.findChild<QTabBar*>("eegWorkspaceNavigation");
    const auto wait = [](int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
    const auto until = [&](const auto& ready, int timeout = 30000) {
        QElapsedTimer timer; timer.start(); while (!ready() && timer.elapsed() < timeout) wait(10); return ready();
    };
    QString error;
    QObject::connect(separation, &EegSeparationController::failed, &window, [&](const AppError& e) { error = e.code; });
    QObject::connect(live, &LiveIcaController::failed, &window, [&](const AppError& e) { error = e.code; });
    const auto fail = [&](const char* reason) {
        std::cerr << "SIMULATOR_ICA FAIL " << reason << " error=" << error.toStdString()
                  << " channels=" << acquisition->snapshot().totalChannels
                  << " status=" << window.findChild<QLabel*>("separationStatus")->text().toStdString() << '\n';
        return 1;
    };
    const auto screenshot = [&](const QString& name, QWidget* widget = nullptr) {
        const auto directory = qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
        if (directory.isEmpty()) return true;
        QDir().mkpath(directory); wait(30);
        return (widget ? widget : &window)->grab().save(QDir(directory).filePath(name + ".png"));
    };
    window.findChild<QAction*>("deviceManageConnections")->trigger();
    auto* profile = window.findChild<QComboBox*>("deviceSimulatorProfile");
    auto* dialog = window.findChild<QDialog*>("deviceConnectionWindow");
    window.findChild<QToolButton*>("deviceConnectionAdvanced")->setChecked(true);
    if (!profile || !profile->currentData().toBool() || !profile->isVisible()) return fail("default_profile");
    for (const auto& locale : {"zh_CN", "en_US", "zh_TW", "ja_JP"}) {
        if (!i18n.loadLocale(locale, &localeError) || profile->itemText(0).startsWith("app.")) return fail("profile_locale");
        if (!screenshot("simulator-profile-" + QString(locale), dialog)) return fail("profile_screenshot");
    }
    i18n.loadLocale("zh_CN", &localeError);
    window.findChild<QPushButton*>("deviceSelectSimulator")->click();
    if (!until([&] { return acquisition->snapshot().samples >= 20; }) ||
        acquisition->snapshot().totalChannels != 5 || !acquisition->snapshot().simulated || profile->isEnabled())
        return fail("connect_profile");
    acquisition->startRecording(temp.path());
    if (!until([&] { return acquisition->snapshot().recording.state == CaptureRecordState::Recording; })) return fail("record_start");
    navigation->setCurrentIndex(1);
    if (!until([&] { return separation->source().simulatorIca && !separation->isBusy(); }) ||
        separation->settings().roles != std::vector<int>({1,1,1,2,3})) return fail("calibration_roles");
    window.findChild<QPushButton*>("separationMainAction")->click();
    if (!until([&] { return bool(separation->output()) && !separation->isBusy(); }) ||
        !error.isEmpty() || separation->output()->rank != 3 || !separation->canApplyTeachingLabels()) return fail("fit");
    const auto model = separation->output();
    const double calibrationEnd = separation->source().metadata.value("signal_provenance").toObject().value("source_end_seconds").toDouble();
    window.findChild<QPushButton*>("separationTeachingLabels")->click();
    auto sorted = separation->labels(); std::sort(sorted.begin(), sorted.end());
    if (sorted != std::vector<int>({0,1,2}) || separation->output() != model) {
        for (const auto& score : model->scores) std::cerr << "eye=" << score.eyeCorrelation << " muscle=" << score.muscleCorrelation << '\n';
        return fail("example_classification");
    }
    if (!screenshot("simulator-ica-classified")) return fail("classification_screenshot");
    window.findChild<QCheckBox*>("separationConfirmed")->setChecked(true);
    std::shared_ptr<const LiveIcaFrame> latest;
    QObject::connect(live, &LiveIcaController::frameReady, &window, [&](const auto& frame) { latest = frame; });
    window.findChild<QPushButton*>("separationLiveToggle")->click();
    if (!until([&] { return latest && latest->end > calibrationEnd + 4.2; }) || !live->isActive() ||
        separation->output() != model || acquisition->snapshot().recording.state != CaptureRecordState::Recording)
        return fail("continuous_projection");
    auto* viewPause = window.findChild<QPushButton*>("deviceFreeze");
    if (!until([&] { return viewPause->isEnabled(); })) return fail("shared_view_controls_stale");
    viewPause->click();
    if (!until([&] { return window.findChild<QWidget*>("eegSeparationWorkspace")->property("liveViewPaused").toBool(); }))
        return fail("shared_view_pause");
    viewPause->click();

    // Generate the known sources independently of ICA, including held-out samples.
    const auto expectedSamples = [&](int count) {
        ::bf::acquisition::MemoryBudget budget(4 * 1024 * 1024);
        auto config = ::bf::acquisition::icaSimulatorStream(250);
        ::bf::acquisition::InProcessSimulator simulator({config}, budget);
        ::bf::acquisition::AcquisitionSession session(simulator.deviceDescriptor(), ::bf::acquisition::SessionId{"reference"}, 1);
        auto prepared = session.prepare(simulator.descriptors());
        if (!prepared || simulator.prepare(*prepared.value) || simulator.start(prepared.value->capture_id))
            throw std::runtime_error("reference simulator");
        std::vector<std::vector<double>> values(5);
        while (int(values[0].size()) < count) {
            const auto step = simulator.step();
            if (step.error || step.blocks.size() != 1) throw std::runtime_error("reference samples");
            const auto& block = step.blocks[0]; const auto& data = *block.storage->data<float>();
            for (int c = 0; c < 5; ++c)
                values[c].insert(values[c].end(), data.begin() + c * block.sample_count, data.begin() + (c + 1) * block.sample_count);
        }
        return values;
    };
    const auto frame = latest;
    const int origin = int(std::llround(frame->signal.start * 250));
    const int count = int(frame->signal.analysis[0].size());
    const auto expected = expectedSamples(origin + count);
    const int first = std::max(0, int(std::ceil((calibrationEnd + .2 - frame->signal.start) * 250)));
    for (int group = 0; group < 3; ++group) {
        std::vector<double> truth, actual;
        for (int t = first; t < count; ++t) {
            const int at = origin + t;
            truth.push_back(group == 0 ? expected[0][at] - .5 * expected[3][at] - .3 * expected[4][at] : expected[group + 2][at]);
            actual.push_back(frame->groups[group][0][t]);
        }
        const double correlation = std::abs(::bf::separation::ica::correlation(truth, actual));
        std::cout << "SIMULATOR_ICA_HELDOUT group=" << group << " correlation=" << correlation << '\n';
        if (!std::isfinite(correlation) || correlation < .9) return fail("heldout_source_recovery");
    }
    if (!screenshot("simulator-ica-continuous")) return fail("continuous_screenshot");
    acquisition->stopRecording();
    if (!until([&] { return acquisition->snapshot().recording.state == CaptureRecordState::Saved; })) return fail("record_save");
    const auto recording = acquisition->snapshot().recording;
    acquisition->stopAcquisition();
    if (!until([&] { return !acquisition->hasActiveTasks() && !live->isActive(); })) return fail("stop");
    QFile file(QDir(recording.path).filePath("manifest.json"));
    if (!file.open(QIODevice::ReadOnly)) return fail("record_manifest");
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    if (manifest.value("acquisition_metadata").toObject().value("simulator_signal_profile") != "ica_mix_v1")
        return fail("record_profile_metadata");
    const auto descriptor = manifest.value("streams").toArray().first().toObject().value("descriptor").toObject();
    const int recordOrigin = descriptor.value("record_start_sample_index").toString().toInt();
    if (descriptor.value("dtype") != "float32") return fail("record_typed_samples");

    FileOpenController files(root, version); int loaded = 0;
    QObject::connect(&files, &FileOpenController::loaded, &window, [&](const auto& result) { ++loaded; window.showFileOpenResult(result); });
    QObject::connect(&files, &FileOpenController::failed, &window, [&](const auto& e) { error = e.code; });
    window.setOpenCaptureHandler([&](const QString& path, const QString& stream) { files.openInitialWindow(path, 10, stream); });
    window.setOpenFileHandler([&](const QString& path) { files.openInitialWindow(path); });
    window.openPathForBrowser(recording.path);
    if (!until([&] { return loaded > 0 && window.signalWindow(true).data.size() == 5; }) || !error.isEmpty()) return fail("replay");
    const auto replay = window.signalWindow(true);
    if (!replay.simulated || replay.unit != "uV" || replay.data[0].size() < 1250) return fail("replay_signal_contract");
    const auto recordedExpected = expectedSamples(recordOrigin + int(replay.data[0].size()));
    for (int c = 0; c < 5; ++c)
        for (std::size_t t = 0; t < replay.data[c].size(); ++t)
            if (std::abs(replay.data[c][t] - recordedExpected[c][recordOrigin + int(t)]) > 1e-5)
                return fail("raw_record_replay_values");
    navigation->setCurrentIndex(1);
    if (!until([&] { return !separation->isBusy() && separation->source().simulatorIca && !separation->output(); }) ||
        separation->settings().roles != std::vector<int>({1,1,1,2,3})) return fail("replay_ica_roles");
    window.findChild<QPushButton*>("separationMainAction")->click();
    if (!until([&] { return bool(separation->output()) && !separation->isBusy(); }) || !error.isEmpty()) return fail("replay_ica_fit");
    window.findChild<QPushButton*>("separationTeachingLabels")->click();
    sorted = separation->labels(); std::sort(sorted.begin(), sorted.end());
    if (sorted != std::vector<int>({0,1,2})) return fail("replay_classification");
    if (!screenshot("simulator-recording-ica")) return fail("replay_screenshot");
    acquisition->configureSimulator(500);
    if (!until([&] { return acquisition->snapshot().connection == DeviceConnectionState::Connected &&
        acquisition->snapshot().sampleRate == 500; })) return fail("configure_500");
    acquisition->startAcquisition();
    if (!until([&] { return acquisition->snapshot().samples >= 40; }) || acquisition->snapshot().totalChannels != 5 ||
        !acquisition->snapshot().deviceName.contains("ICA mix")) return fail("profile_retained_at_500");
    acquisition->stopAcquisition();
    if (!until([&] { return !acquisition->hasActiveTasks(); })) return fail("stop_500");
    QThreadPool::globalInstance()->waitForDone(30000);
    std::cout << "SIMULATOR_ICA PASS default_profile=true roles=true classified=true heldout=true continuous=true raw_record_replay=true replay_ica=true\n";
    return 0;
}
}
