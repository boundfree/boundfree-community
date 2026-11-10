#pragma once
#include "ui/main_window.h"
#include "controller/sidecar_slice.h"
#include "dsp/spectral.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
#include <cmath>
namespace bf {
inline int smokeUnifiedSignalTools(int argc,char** argv,const VersionInfo& version,const QString& root){
    QApplication app(argc,argv);QTemporaryDir temp;
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    QCoreApplication::setOrganizationName("BoundFreeUnifiedSignalTest");QCoreApplication::setApplicationName("workbench");
    qputenv("BF_ACQUISITION_LOG_DIRECTORY",temp.path().toUtf8());
    I18nService i18n(root);QString error;if(!i18n.loadLocale("zh_CN",&error))return 1;
    MainWindow window(version,&i18n,root);window.resize(1280,820);window.show();
    auto* controller=window.findChild<AcquisitionController*>();
    const auto wait=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
    const auto until=[&](const auto& ready,int timeout=30000){QElapsedTimer timer;timer.start();while(!ready() && timer.elapsed()<timeout)wait(10);return ready();};
    const auto fail=[&](const char* why){std::cerr<<"UNIFIED_SIGNAL_TOOLS FAIL "<<why<<'\n';return 1;};
    auto* deviceInput=window.findChild<QPushButton*>("separationDeviceSnapshot");
    auto* connectDevice=window.findChild<QPushButton*>("separationConnectDevice");
    if(!deviceInput || deviceInput->isEnabled() || connectDevice)return fail("ica_device_empty_state");
    window.findChild<QAction*>("deviceManageConnections")->trigger();
    window.findChild<QComboBox*>("deviceSimulatorProfile")->setCurrentIndex(1);
    window.findChild<QPushButton*>("deviceSelectSimulator")->click();
    auto* status=window.findChild<QLabel*>("eegAnalysisStatus_0");
    if(!until([&]{return controller->snapshot().samples>=7000 && status->property("analysisState")=="ready";}))return fail("live_full_samples_and_analysis");
    const auto& full=window.signalWindow(true);
    if(full.displayOnly || full.fs!=250 || full.data.empty() || full.data.front().size()<=1600)return fail("envelope_used_for_analysis");
    for(std::size_t i=1;i<full.sampleTimes.front().size();++i)
        if(std::abs(full.sampleTimes[0][i]-full.sampleTimes[0][i-1]-.004)>1e-7)return fail("raw_sampling_grid");
    auto* mode=window.findChild<QComboBox*>("eegWaveformDataMode");
    if(!mode->isEnabled() || !window.findChild<QWidget*>("eegFilterPanel")->isEnabled() || !window.findChild<QWidget*>("eegAnalysisPanel")->isVisible())return fail("shared_tools_unavailable");
    controller->startRecording(temp.path());
    if(!until([&]{return controller->snapshot().recording.state==CaptureRecordState::Recording;}))return fail("raw_record_start");
    wait(1200);window.findChild<QPushButton*>("deviceFreeze")->click();wait(50);
    const auto frozen=window.signalWindow(true);const auto before=controller->snapshot().samples;
    auto processing=window.processingViewState();processing.notchEnabled=true;processing.globalBandpassEnabled=true;
    window.setProcessingViewStateForBrowser(processing);wait(200);
    if(window.signalWindow(true).data!=frozen.data || controller->snapshot().samples<=before || controller->snapshot().recording.state!=CaptureRecordState::Recording)return fail("processing_mutates_raw_or_stops_acquisition");
    if(window.signalWindow().data==frozen.data)return fail("processing_not_applied");
    auto* tabs=window.findChild<QTabWidget*>("eegAnalysisTabs");
    for(int i=0;i<4;++i){tabs->setCurrentIndex(i);auto* state=window.findChild<QLabel*>("eegAnalysisStatus_"+QString::number(i));if(!until([&]{return state->property("analysisState")=="ready";}))return fail("analysis_tool_not_ready");}
    controller->stopRecording();
    if(!until([&]{const auto s=controller->snapshot().recording.state;return s==CaptureRecordState::Saved || s==CaptureRecordState::Partial;}))return fail("raw_record_finalize");
    const auto recorded=controller->snapshot().recording;
    if(!recorded.writtenSamples)return fail("raw_record_empty");
    window.findChild<QPushButton*>("deviceFreeze")->click();wait(600);
    SidecarClient sidecar(root);if(!sidecar.startAndHello(version,30000).ok)return fail("sidecar");
    const auto opened=sidecar.openFile(recorded.path,30000);if(!opened.ok)return fail("recorded_source_open");
    const auto slice=sidecar.getSlice(opened.handle,0,opened.duration,30000);if(!slice.ok)return fail("recorded_source_read");
    const auto replay=normalizedSignalWindow(waveformSliceFromSidecar(opened,slice));
    const auto raw=window.signalWindow(true);const auto origin=opened.capture.value("record_start_sample_index").toString().toULongLong();
    const double first=double(origin)/opened.fs;
    const auto comparable=signalWindowRange(raw,first,first+opened.duration);
    if(comparable.data!=replay.data)return fail("recorded_raw_samples_differ_from_adapter_input");
    for(bool causal:{false,true}){
        SignalProcessor one,two;const auto a=one.process(comparable,processing,causal),b=two.process(replay,processing,causal);
        if(a.data.size()!=b.data.size())return fail("cross_source_processing_shape");
        for(std::size_t c=0;c<a.data.size();++c)for(std::size_t i=0;i<a.data[c].size();++i)
            if(std::abs(a.data[c][i]-b.data[c][i])>1e-4)return fail("cross_source_processing_values");
        const auto pa=dsp::computeWelchPsd(std::vector<double>(a.data[0].begin(),a.data[0].end()),a.fs);
        const auto pb=dsp::computeWelchPsd(std::vector<double>(b.data[0].begin(),b.data[0].end()),b.fs);
        if(pa.values!=pb.values)return fail("cross_source_spectrum");
    }
    FileOpenController files(root,version);
    window.setCloseFileHandler([&]{ files.closeSession(); });
    QObject::connect(&files,&FileOpenController::loaded,&window,[&](const auto& value){window.showFileOpenResult(value);});
    QObject::connect(&files,&FileOpenController::failed,&window,[&](const auto& e){error=e.code;});
    window.setOpenFileHandler([&](const QString& p){files.openInitialWindow(p);});
    window.setOpenCaptureHandler([&](const QString& p,const QString& s){files.openInitialWindow(p,10,s);});
    tabs->setCurrentIndex(0);
    window.openPathForBrowser(recorded.path);
    if(!until([&]{return !window.property("deviceWaveformActive").toBool() && window.signalWindow().fs==250 && status->property("analysisState")=="ready";}) || !error.isEmpty())return fail("replay_shared_tools");
    if(!mode->isEnabled() || !window.findChild<QAction*>("fileExportAction")->isEnabled())return fail("replay_tool_policy");
    if(controller->snapshot().acquisition!=DeviceAcquisitionState::Streaming)return fail("file_view_stops_device");
    // Switching to device discards the reader; reopening the same path must
    // establish a fresh file view while acquisition continues in the background.
    window.findChild<QPushButton*>("deviceGlobalStatus")->click();
    if(!until([&]{return window.property("deviceWaveformActive").toBool() && !window.signalWindow(true).sampleTimes.empty();}))return fail("file_to_device_discard");
    window.openPathForBrowser(recorded.path);
    if(!until([&]{return !window.property("deviceWaveformActive").toBool() && window.signalWindow().fs==250 && status->property("analysisState")=="ready";}) || !error.isEmpty())return fail("file_reader_reopen_after_device");
    auto* navigation=window.findChild<QTabBar*>("eegWorkspaceNavigation");navigation->setCurrentIndex(1);
    auto* separation=window.findChild<EegSeparationController*>();
    // A connected device is the default input when separation has no source,
    // including while the browser displays a short file. It is captured once
    // without another connection or source-selection step.
    if(!until([&]{return separation->state()==EegSeparationController::State::Ready;}) ||
       separation->source().metadata.value("signal_provenance").toObject().value("source_id")!=controller->signalSourceId() ||
       separation->source().duration()<5)return fail("connected_device_not_inherited_automatically");
    const auto browserBefore=window.signalWindow(true);
    controller->startRecording(temp.path());
    if(!until([&]{return controller->snapshot().recording.state==CaptureRecordState::Recording;}))return fail("ica_raw_record_start");
    const auto initialSnapshot = separation->source().path;
    deviceInput->click();
    // An existing calibration remains Ready while replacement is captured.
    // Wait for its new identity, as the disabled UI action already requires.
    if(!until([&]{return separation->state()==EegSeparationController::State::Ready &&
                        separation->source().path!=initialSnapshot;}))return fail("shared_ica_snapshot");
    const auto source=separation->source();
    const auto provenance=source.metadata.value("signal_provenance").toObject();
    if(source.metadata.value("unit")!="uV" || source.duration()<5 || source.duration()>10.01 ||
       source.channels.size()!=4 || provenance.value("source_id")!=controller->signalSourceId() ||
       !provenance.value("simulated").toBool() || provenance.value("channel_ids").toArray().size()!=4 ||
       provenance.value("input_precision")!="float32_projection")return fail("snapshot_identity_units_and_duration");
    if(window.signalWindow(true).data!=browserBefore.data || window.property("deviceWaveformActive").toBool())return fail("ica_changes_browser_source");
    for(int channel=0;channel<4;++channel)
        window.findChild<QComboBox*>(QString("separationRole%1").arg(channel))->setCurrentIndex(1);
    window.findChild<QDoubleSpinBox*>("separationDuration")->setValue(5);
    const auto cfg=separation->settings();
    QString icaError;
    QObject::connect(separation,&EegSeparationController::failed,&window,[&](const auto& e){icaError=e.code;});
    const auto samplesBefore=controller->snapshot().samples;
    separation->start();
    if(deviceInput->isEnabled())return fail("ica_input_enabled_during_fit");
    if(!until([&]{return !separation->isBusy();},60000) || !separation->output()){
        std::cerr<<"ICA_ERROR "<<icaError.toStdString()<<'\n';return fail("live_ica_fit");
    }
    const auto output=separation->output();
    wait(300);
    if(controller->snapshot().samples<=samplesBefore || separation->output()!=output ||
       controller->snapshot().recording.state!=CaptureRecordState::Recording) {
        const auto view = controller->snapshot();
        std::cerr << "ICA_RECORD_DIAGNOSTIC samples_before=" << samplesBefore << " samples_after=" << view.samples
                  << " same_model=" << (separation->output()==output)
                  << " recording_state=" << int(view.recording.state)
                  << " error=" << view.recording.error.code.toStdString() << '\n';
        return fail("ica_stops_recording_or_refits_each_frame");
    }
    const auto screenshots=qEnvironmentVariable("BF_LAYOUT_SCREENSHOT_DIR");
    if(!screenshots.isEmpty()){
        QDir().mkpath(screenshots);
        for(const auto& locale:{"zh_CN","en_US","ja_JP","zh_TW","pseudo"}){
            if(!i18n.loadLocale(locale,&error)) {
                // The pseudo locale is a development fixture and is not shipped.
                if(QString::fromLatin1(locale)=="pseudo" && error.startsWith("missing .qm")) {
                    std::cout<<"ICA_DEVICE_PSEUDO_LOCALE_SKIPPED_NOT_PACKAGED\n";
                    continue;
                }
                std::cerr<<"ICA_DEVICE_LOCALE "<<locale<<" "<<error.toStdString()<<'\n';
                return fail("ica_device_locale");
            }
            for(int width:{1280,920}){
                window.resize(width,900);wait(40);
                if(!window.grab().save(QDir(screenshots).filePath(QString("ica-device-%1-%2.png").arg(locale).arg(width))))return fail("ica_device_screenshot");
            }
        }
        error.clear();
        if(!i18n.loadLocale("zh_CN",&error))return fail("ica_device_locale_reset");
        error.clear();window.resize(1280,900);
    }
    separation->setLabel(0,0);separation->setConfirmed(true);
    const auto bundle=QDir(temp.path()).filePath("device-analysis");separation->exportBundle(bundle);
    if(!until([&]{return !separation->isBusy();},60000) || separation->exportedPath()!=bundle)return fail("device_ica_bundle_export");
    QFile manifest(QDir(bundle).filePath("analysis.json"));
    if(!manifest.open(QIODevice::ReadOnly) || QJsonDocument::fromJson(manifest.readAll()).object()
        .value("source").toObject().value("signal_provenance").toObject()!=provenance)return fail("device_export_loses_provenance");
    separation->openSource(QDir(temp.path()).filePath("missing-snapshot.h5"),{},true);
    if(!until([&]{return !separation->isBusy();}) || separation->output()!=output || separation->source().path!=source.path ||
       !separation->isConfirmed())return fail("snapshot_inspection_failure_loses_results");
    auto* workspace=static_cast<EegSeparationWorkspace*>(window.findChild<QStackedWidget*>("eegWorkspaceStack")->widget(1));
    workspace->deviceSnapshotRequested("missing-stream");
    if(!until([&]{return deviceInput->isEnabled();}) || separation->output()!=output || !separation->isConfirmed() ||
       window.findChild<QLabel*>("separationStatus")->text().isEmpty())return fail("failed_capture_replaces_results");
    deviceInput->click();
    if(!until([&]{return separation->state()==EegSeparationController::State::Ready && separation->source().path!=source.path;}))return fail("ica_repeated_capture");
    if(separation->output() || separation->isConfirmed() || !separation->labels().empty() || separation->settings().roles!=cfg.roles ||
       separation->source().metadata.value("signal_provenance").toObject().value("source_end_seconds").toDouble()<=provenance.value("source_end_seconds").toDouble())return fail("ica_snapshot_replacement_state");
    controller->stopRecording();
    if(!until([&]{auto state=controller->snapshot().recording.state;return state==CaptureRecordState::Saved || state==CaptureRecordState::Partial;}))return fail("ica_raw_record_finalize");
    window.findChild<QAction*>("openTeachingRecording")->trigger();
    if(!until([&]{return !separation->isBusy();}) || !separation->source().teaching)return fail("ica_file_after_device");
    deviceInput->click();
    if(!until([&]{return separation->state()==EegSeparationController::State::Ready && !separation->source().teaching;}))return fail("ica_device_after_file");
    if(separation->settings().roles!=std::vector<int>(4,0))return fail("ica_roles_leak_between_sources");
    deviceInput->click();
    window.openPathForBrowser(QDir(root).filePath("samples/offline-eeg/teaching.h5"));
    if(!until([&]{return window.signalWindow(true).fs==500;}) || !error.isEmpty())return fail("browser_source_change_during_snapshot");
    wait(300);
    if(!separation->source().path.isEmpty() || separation->isBusy())return fail("late_device_snapshot_overwrites_new_source");
    navigation->setCurrentIndex(0);controller->stopAcquisition();
    if(!until([&]{return !controller->hasActiveTasks();}))return fail("shutdown");
    std::cout<<"UNIFIED_SIGNAL_TOOLS PASS full_samples=true all_analysis_tools=true processing=true raw_record_independent=true cross_source_values=true cross_source_spectra=true ica_snapshot=true device_ica_fit=true device_ica_export=true repeated_capture=true\n";
    return 0;
}
}
