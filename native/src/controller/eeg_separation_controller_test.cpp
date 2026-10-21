// Integration regression: real stdio sidecar, public controller API, synthetic signals only.
#include "controller/eeg_separation_controller.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool valid, const QString& message) {
    ++checks;
    if (!valid) throw std::runtime_error(message.toStdString());
}
void waitUntil(const std::function<bool()>& condition, const QString& label, int timeout = 90000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    require(condition(), "Timed out: " + label);
}
void drain(int milliseconds) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(2);
    }
}
QByteArray sha(const QString& path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "Read source for hash");
    QCryptographicHash hash(QCryptographicHash::Sha256); require(hash.addData(&file), "Hash complete file"); return hash.result();
}
QJsonObject json(const QString& path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "Read JSON manifest");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Valid manifest JSON"); return document.object();
}
bf::SeparationMatrix csv(const QString& path, int channels, int samples, double start, double fs) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "Read output CSV");
    require(file.readLine().trimmed().split(',').size() == channels + 1, "CSV channel header");
    bf::SeparationMatrix result(channels, std::vector<double>(samples));
    int row = 0;
    while (!file.atEnd()) {
        const auto cells = file.readLine().trimmed().split(',');
        require(row < samples && cells.size() == channels + 1, "CSV shape");
        bool ok = false; const double time = cells[0].toDouble(&ok);
        require(ok && std::abs(time - (start + row / fs)) < 1e-12, "Source-file relative CSV time");
        for (int c = 0; c < channels; ++c) {
            const double value = cells[c + 1].toDouble(&ok);
            require(ok && std::isfinite(value), "CSV finite double"); result[c][row] = value;
        }
        ++row;
    }
    require(row == samples, "CSV exact sample count"); return result;
}
void verifyBundle(const QString& path, const bf::SeparationOutput& output, const std::vector<int>& labels) {
    const QStringList names{"original", "analysis", "eeg", "eog", "emg", "unresolved", "residual"};
    const auto files = QDir(path).entryList(QDir::Files | QDir::NoDotAndDotDot);
    require(files.size() == 8, "Bundle contains exactly seven CSV files and manifest");
    std::vector<bf::SeparationMatrix> values;
    for (const auto& name : names) values.push_back(csv(QDir(path).filePath(name + "_uV.csv"), output.channels.size(),
                                                     output.analysis.front().size(), output.start, output.fs));
    const auto meta = json(QDir(path).filePath("analysis.json"));
    require(meta.value("schema").toString() == "boundfree.community.offline-eeg.v1", "Community result schema");
    require(meta.value("manually_confirmed").toBool(), "Manual confirmation recorded");
    require(meta.value("dtype").toString() == "float64-le" && meta.value("unit").toString() == "uV", "Explicit float64 microvolt contract");
    const auto writtenLabels = meta.value("labels").toArray();
    require(writtenLabels.size() == int(labels.size()), "Every component label recorded");
    for (int k = 0; k < int(labels.size()); ++k) require(writtenLabels[k].toInt() == labels[k], "Component label roundtrip");
    double error = 0;
    for (std::size_t c = 0; c < output.analysis.size(); ++c) for (std::size_t t = 0; t < output.analysis[c].size(); ++t) {
        double sum = output.mean[c]; for (int group = 2; group < 7; ++group) sum += values[group][c][t];
        error = std::max(error, std::abs(sum - values[1][c][t]));
    }
    require(error <= 1e-6, "CSV readback five-group reconstruction with one mean");
    require(values[0] == output.original && values[1] == output.analysis, "CSV double precision original/analysis roundtrip");
    std::cout << "Bundle readback " << path.toStdString() << " max error " << error << " uV\n";
}
QString createHdf5(const QString& root) {
    const QString path = QDir(root).filePath("synthetic.h5");
    const QString python = qEnvironmentVariable("BF_PYTHON_EXECUTABLE", "python3");
    const QString script = QString::fromLatin1(R"PY(
import sys, h5py, numpy as np
fs=1000.; t=np.arange(12000)/fs
rng=np.random.default_rng(12138)
eye=18*np.sin(2*np.pi*.18*t)
for blink in (2.2,5.1,9.3,11.6): eye += 100*np.exp(-((t-blink)/.12)**2)
sources=np.array([12*np.sin(2*np.pi*10*t)+4*np.sin(2*np.pi*63*t), eye,
                 (rng.random(len(t))-.5)*(8+65*np.exp(-((t-7)/.5)**2))])
mix=np.array([[1,.5,.3],[.3,1.2,.1],[1,-.2,1.1]])
data=np.vstack([mix@sources+np.arange(13.,16.)[:,None],sources[1:],np.zeros((1,len(t)))])
with h5py.File(sys.argv[1],'w') as file:
    eeg=file.create_dataset('eeg',data=data,dtype='<f8')
    eeg.attrs['fs']=fs; eeg.attrs['unit']='uV'
    eeg.attrs['ch_names']=np.array(['obs0','obs1','obs2','eye-ref','muscle-ref','excluded'],dtype='S')
from pathlib import Path
import csv, pyedflib
root=Path(sys.argv[1]).parent
with (root/'synthetic.csv').open('w',newline='') as output:
    writer=csv.writer(output); writer.writerow(['time_s','obs0','obs1','obs2','eye-ref','muscle-ref','excluded'])
    writer.writerows([[format(t[i],'.17g'),*(format(x,'.17g') for x in data[:,i])] for i in range(len(t))])
headers=[{'label':name,'dimension':'uV','sample_frequency':fs,'physical_min':-1000.,'physical_max':1000.,
          'digital_min':-32768,'digital_max':32767,'transducer':'synthetic','prefilter':''}
         for name in ['obs0','obs1','obs2','eye-ref','muscle-ref','excluded']]
with pyedflib.EdfWriter(str(root/'synthetic.edf'),n_channels=6,file_type=pyedflib.FILETYPE_EDFPLUS) as output:
    output.setSignalHeaders(headers); output.writeSamples(list(data))
)PY");
    QProcess process; process.start(python, {"-c", script, path});
    require(process.waitForStarted(10000) && process.waitForFinished(30000) && process.exitCode() == 0,
            "Create HDF5 fixture: " + QString::fromUtf8(process.readAllStandardError()));
    return path;
}
void appendChange(const QString& path) {
    QFile file(path); require(file.open(QIODevice::Append), "Open disposable source for fingerprint change");
    require(file.write("fingerprint-change") == 18, "Modify disposable source bytes"); file.close();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        const QString repoRoot = argc > 1 ? QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath() : bf::VersionService::detectRepoRoot();
        const QString outputRoot = argc > 2 ? QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath() : QDir::tempPath();
        const QString sourceRoot = argc > 3 ? QFileInfo(QString::fromLocal8Bit(argv[3])).absoluteFilePath() :
            QDir(repoRoot).absoluteFilePath("tests/fixtures/generated/offline-eeg");
        require(QDir().mkpath(outputRoot), "Output parent available");
        QTemporaryDir artifacts(QDir(outputRoot).filePath("eeg-controller-XXXXXX"));
        require(artifacts.isValid(), "Unique integration artifact directory"); artifacts.setAutoRemove(false);
        const QString rhd = QDir(sourceRoot).filePath("synthetic.rhd"), rhs = QDir(sourceRoot).filePath("synthetic.rhs");
        require(QFileInfo::exists(rhd) && QFileInfo::exists(rhs), "Synthetic RHD/RHS fixtures exist");
        const QByteArray rhdHash = sha(rhd), rhsHash = sha(rhs);
        const QString hdf5 = createHdf5(artifacts.path());
        bf::VersionInfo version; QString versionError;
        require(bf::VersionService(repoRoot).load(&version, &versionError), "Load current protocol version: " + versionError);
        bf::EegSeparationController controller(repoRoot, version);
        QStringList failures; int exports = 0;
        QObject::connect(&controller, &bf::EegSeparationController::failed, [&](const bf::AppError& error) { failures.append(error.code); std::cerr << "Controller error " << error.code.toStdString() << " " << QJsonDocument(error.params).toJson(QJsonDocument::Compact).toStdString() << '\n'; });
        QObject::connect(&controller, &bf::EegSeparationController::exported, [&](const QString&) { ++exports; });
        const auto open = [&](const QString& path) {
            failures.clear(); controller.openSource(path);
            waitUntil([&] { return !controller.isBusy(); }, "Inspect " + path);
            require(failures.isEmpty(), "Source opened without errors: " + failures.join(','));
            require(controller.state() == bf::EegSeparationController::State::Ready && controller.source().path == QFileInfo(path).canonicalFilePath(), "Ready source path");
            require(!controller.output() && !controller.isConfirmed(), "New source has no stale result or confirmation");
        };
        const auto fit = [&] {
            auto settings = controller.settings(); settings.duration = 12.; settings.start = 0.;
            settings.outputRate = 500; settings.notchHz = 0; settings.roles = {1,1,1,2,3,4};
            controller.setSettings(settings); failures.clear(); controller.start();
            waitUntil([&] { return !controller.isBusy(); }, "Fit current source");
            require(failures.isEmpty(), "Fit without errors: " + failures.join(','));
            require(controller.state() == bf::EegSeparationController::State::Review && bool(controller.output()), "Fit publishes review result");
            require(controller.output()->rank == 3 && controller.output()->original.size() == 3 && controller.output()->original[0].size() == 6000, "Fit dimensions and effective rank");
            require(std::all_of(controller.labels().begin(), controller.labels().end(), [](int value) { return value == 3; }), "All components initially unresolved");
        };
        const auto exportTo = [&](const QString& destination) {
            failures.clear(); const int before = exports;
            controller.setConfirmed(true); controller.exportBundle(destination);
            waitUntil([&] { return !controller.isBusy(); }, "Export bundle");
            require(failures.isEmpty(), "Export without errors: " + failures.join(','));
            require(exports == before + 1 && controller.exportedPath() == destination, "Export success signal and path");
            verifyBundle(destination, *controller.output(), controller.labels());
        };

        open(rhd); fit(); const auto rhdOutput = controller.output();
        require(controller.projection(0)[0][0] == 0 && controller.projection(1)[0][0] == 0, "Unresolved state does not silently classify sources");
        exportTo(QDir(artifacts.path()).filePath("rhd-unknown"));
        controller.setLabel(0, 0); require(!controller.isConfirmed() && controller.canUndoLabel(), "Label change resets confirmation and supports undo");
        require(controller.output() == rhdOutput, "Manual labeling preserves the fitted result");
        controller.undoLabel(); require(controller.labels()[0] == 3 && !controller.isConfirmed(), "Undo restores unresolved label without confirmation");
        for (int k = 0; k < 3; ++k) controller.setLabel(k, k);
        const QString manual = QDir(artifacts.path()).filePath("rhd-manual"); exportTo(manual);
        const int previousExports = exports; failures.clear(); controller.exportBundle(manual);
        waitUntil([&] { return !controller.isBusy(); }, "Repeated export destination rejection");
        require(failures.contains("analysis.destination_exists") && exports == previousExports, "Existing directory never overwritten");
        require(controller.output() == rhdOutput && controller.state() == bf::EegSeparationController::State::Review, "Failed export preserves review result");
        auto settings = controller.settings(); settings.notchHz = 50; controller.setSettings(settings);
        require(!controller.output() && !controller.isConfirmed() && !controller.canUndoLabel(), "Input change invalidates fit, confirmation, undo");
        fit(); controller.start(); controller.cancel();
        require(!controller.output() && !controller.isBusy(), "Cancel fit removes partial result immediately");
        open(rhs); fit();
        require(controller.output()->original == rhdOutput->original, "Real sidecar RHD/RHS AC samples match exactly");
        exportTo(QDir(artifacts.path()).filePath("rhs-unknown"));

        open(QDir(artifacts.path()).filePath("synthetic.edf")); fit();
        require(controller.output()->manifest.value("source").toObject().value("format").toString().startsWith("EDF"), "EDF input provenance retained");
        exportTo(QDir(artifacts.path()).filePath("edf-unknown"));
        const QString csvSource = QDir(artifacts.path()).filePath("synthetic.csv");
        QString requiredUnitPath;
        QObject::connect(&controller, &bf::EegSeparationController::unitRequired, [&](const QString& path) { requiredUnitPath = path; });
        failures.clear(); controller.openSource(csvSource);
        waitUntil([&] { return !controller.isBusy(); }, "CSV requests unknown physical unit");
        require(failures.isEmpty() && requiredUnitPath == csvSource && controller.source().path.isEmpty() && !controller.output(), "Unknown CSV unit prompts confirmation without accepting source");
        controller.clearSource(); require(controller.state() == bf::EegSeparationController::State::Empty, "Cancelling CSV unit choice keeps source empty");
        controller.openSource(csvSource, "uV"); waitUntil([&] { return !controller.isBusy(); }, "Open CSV with explicit microvolt unit");
        require(controller.state() == bf::EegSeparationController::State::Ready && failures.isEmpty(), "Confirmed CSV unit opens regular source"); fit();
        require(controller.output()->manifest.value("source").toObject().value("input_unit_explicit_by_user").toBool(), "Manifest records explicit CSV unit decision");
        exportTo(QDir(artifacts.path()).filePath("csv-unknown"));

        // Replace a running generation through the public API; old completion must not replace HDF5.
        controller.start(); controller.openSource(hdf5);
        waitUntil([&] { return !controller.isBusy(); }, "Replace running analysis with HDF5 source");
        require(controller.source().path == QFileInfo(hdf5).canonicalFilePath() && !controller.output(), "New source wins over cancelled worker");
        fit(); const auto hdf5Output = controller.output(); drain(250);
        require(controller.output() == hdf5Output && controller.source().path == QFileInfo(hdf5).canonicalFilePath(), "Old callbacks cannot replace current result");
        exportTo(QDir(artifacts.path()).filePath("hdf5-unknown"));
        controller.clearSource(); require(controller.state() == bf::EegSeparationController::State::Empty && !controller.output(), "Clear source clears review state");
        controller.openSource(rhd); controller.openSource(hdf5);
        waitUntil([&] { return !controller.isBusy(); }, "Replace inspecting source");
        require(controller.source().path == QFileInfo(hdf5).canonicalFilePath(), "Latest source inspection wins");

        // Modify a disposable HDF5 only: the original RHD/RHS remain immutable.
        fit(); const auto changedSnapshot = controller.output(); controller.setConfirmed(true); appendChange(hdf5); failures.clear();
        const QString rejectedExport = QDir(artifacts.path()).filePath("changed-source-export");
        controller.exportBundle(rejectedExport);
        waitUntil([&] { return !controller.isBusy(); }, "Source fingerprint export rejection");
        require(failures.contains("analysis.source_changed") && !QFileInfo::exists(rejectedExport), "Changed source cannot be exported");
        require(controller.output() == changedSnapshot, "Source-change export failure retains result for inspection");
        failures.clear(); controller.start();
        waitUntil([&] { return !controller.isBusy(); }, "Source fingerprint analysis rejection");
        require(failures.contains("analysis.source_changed") && !controller.output(), "Changed source cannot be analyzed against stale snapshot");
        failures.clear(); controller.openTeachingSource();
        waitUntil([&] { return !controller.isBusy(); }, "Open bundled teaching record");
        require(failures.isEmpty() && controller.source().teaching, "Teaching fixture recognized by hash and explicit metadata");
        const auto teachingManifest = json(QDir(repoRoot).filePath("samples/offline-eeg/manifest.json"));
        require(controller.source().metadata.value("fingerprint").toObject().value("sha256") == teachingManifest.value("sha256"), "Teaching SHA-256 matches manifest");
        require(controller.settings().roles == std::vector<int>({1,1,1,2,3}) && controller.settings().duration == 16., "Verified teaching roles and full interval prefilled");
        require(!controller.canApplyTeachingLabels(), "Example labels unavailable before fitting");
        controller.start(); waitUntil([&] { return !controller.isBusy(); }, "Fit bundled teaching record");
        const auto teachingOutput = controller.output();
        require(teachingOutput && teachingOutput->rank == 3 && teachingOutput->analysis.front().size() == 8000 && controller.canApplyTeachingLabels(), "Teaching fit contains three sources at 500 Hz");
        controller.setConfirmed(true); controller.applyTeachingLabels();
        auto teachingLabels = controller.labels(); std::sort(teachingLabels.begin(), teachingLabels.end());
        require(teachingLabels == std::vector<int>({0,1,2}) && !controller.isConfirmed(), "Example groups cover brain/eye/muscle and reset confirmation");
        require(controller.output() == teachingOutput && controller.canUndoLabel(), "Example grouping only reconstructs existing fit");
        double teachingError = 0;
        for (std::size_t c = 0; c < teachingOutput->analysis.size(); ++c) for (std::size_t t = 0; t < teachingOutput->analysis[c].size(); ++t) {
            double sum = teachingOutput->mean[c] + teachingOutput->residual[c][t];
            for (int group = 0; group < 4; ++group) sum += controller.projection(group)[c][t];
            teachingError = std::max(teachingError, std::abs(sum - teachingOutput->analysis[c][t]));
        }
        require(teachingError < 1e-9, "Example projections reconstruct teaching observations");
        controller.undoLabel();
        require(controller.labels() == std::vector<int>({3,3,3}) && controller.output() == teachingOutput && !controller.canUndoLabel(), "One undo reverts entire example grouping without refit");
        controller.applyTeachingLabels(); exportTo(QDir(artifacts.path()).filePath("teaching-manual"));
        auto teachingSettings = controller.settings(); teachingSettings.duration = 10.; controller.setSettings(teachingSettings);
        controller.start(); waitUntil([&] { return !controller.isBusy(); }, "Fit altered teaching interval");
        require(controller.output() && !controller.canApplyTeachingLabels(), "Altered teaching interval cannot apply fixed example labels");
        controller.applyTeachingLabels(); require(controller.labels() == std::vector<int>({3,3,3}), "Unavailable example labeling is a no-op");
        const QString alteredTeaching = QDir(artifacts.path()).filePath("teaching.h5");
        require(QFile::copy(QDir(repoRoot).filePath("samples/offline-eeg/teaching.h5"), alteredTeaching), "Copy teaching fixture to disposable location");
        appendChange(alteredTeaching); open(alteredTeaching);
        require(!controller.source().teaching && controller.settings().roles == std::vector<int>(5,0), "Matching filename/channels cannot bypass teaching fingerprint");
        // The owner must outlive every worker delivery, including cancelled older generations.
        for (int iteration = 0; iteration < 4; ++iteration) {
            auto closing = std::make_unique<bf::EegSeparationController>(repoRoot, version);
            closing->openSource(rhd); closing.reset();
        }
        require(true, "Destroying inspecting controllers joins their cancelled workers");
        const auto makeTeaching = [&] {
            auto instance = std::make_unique<bf::EegSeparationController>(repoRoot, version);
            instance->openTeachingSource(); waitUntil([&] { return !instance->isBusy(); }, "Lifecycle teaching inspection");
            require(instance->source().teaching, "Lifecycle fixture is verified teaching source"); return instance;
        };
        {
            auto closing = makeTeaching(); bool fitting = false;
            QObject::connect(closing.get(), &bf::EegSeparationController::progressChanged,
                             [&](const QString& stage) { if (stage == "fitting") fitting = true; });
            closing->start(); waitUntil([&] { return fitting || !closing->isBusy(); }, "Lifecycle enters fitting");
            require(fitting, "Real sidecar read completed before fitting destruction"); closing.reset();
            require(true, "Destroying fitting controller joins its worker safely");
        }
        const QString cancelledRoot = QDir(artifacts.path()).filePath("cancelled-export");
        require(QDir().mkpath(cancelledRoot), "Cancelled export test directory");
        {
            auto closing = makeTeaching(); closing->start(); waitUntil([&] { return !closing->isBusy(); }, "Lifecycle export fit");
            require(bool(closing->output()), "Lifecycle export has fitted output");
            closing->setConfirmed(true);
            const QString cancelledDestination = QDir(cancelledRoot).filePath("must-not-publish");
            closing->exportBundle(cancelledDestination);
            waitUntil([&] { return !QDir(cancelledRoot).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty() || !closing->isBusy(); }, "Exporter begins staging real files");
            require(closing->isBusy() && !QFileInfo::exists(cancelledDestination), "Cancel while the real bundle is staged before publication");
            QElapsedTimer cancellationTime; cancellationTime.start(); closing->cancel();
            require(cancellationTime.elapsed() < 250, "UI cancellation does not join the exporter");
            closing->openTeachingSource(); waitUntil([&] { return !closing->isBusy(); }, "Cancel export then immediately inspect teaching source");
            closing->start(); waitUntil([&] { return !closing->isBusy(); }, "New fit while cancelled export retires");
            require(closing->output() && closing->output()->rank == 3, "Cancelled older export cannot replace new teaching result");
            closing.reset(); // Joins both current and older cancelled export generations.
            require(!QFileInfo::exists(cancelledDestination), "Cancelled export never publishes a final directory");
            require(QDir(cancelledRoot).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(), "Cancelled exporter removes its staged directory");
        }
        {
            auto closing = makeTeaching(); closing->start(); waitUntil([&] { return !closing->isBusy(); }, "Destroy export fit");
            closing->setConfirmed(true);
            const QString destination = QDir(cancelledRoot).filePath("destroyed-must-not-publish");
            closing->exportBundle(destination);
            waitUntil([&] { return !QDir(cancelledRoot).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty() || !closing->isBusy(); }, "Destructor test reaches real file staging");
            require(closing->isBusy() && !QFileInfo::exists(destination), "Destroy owner while exporter is writing staged files");
            closing.reset();
            require(!QFileInfo::exists(destination), "Destroying export controller cancels before publication");
            require(QDir(cancelledRoot).entryList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(), "Destroyed exporter removes staged artifacts");
        }
        {
            auto closing = makeTeaching(); closing->start(); waitUntil([&] { return !closing->isBusy(); }, "Publication guard fit");
            const auto commitOutput = closing->output(); const auto commitSource = closing->source().path;
            bool guarded = false;
            QObject::connect(closing.get(), &bf::EegSeparationController::progressChanged, [&](const QString& stage) {
                if (stage != "publishing") return;
                auto changed = closing->settings(); changed.notchHz = 50;
                closing->setSettings(changed); closing->openSource(rhs);
                guarded = closing->settings().notchHz == 0 && closing->source().path == commitSource &&
                          closing->state() == bf::EegSeparationController::State::Exporting;
            });
            closing->setConfirmed(true); const QString destination = QDir(artifacts.path()).filePath("publication-guard");
            closing->exportBundle(destination); waitUntil([&] { return !closing->isBusy(); }, "Publication guard final acknowledgement");
            require(guarded && closing->output() == commitOutput && QFileInfo::exists(destination), "Committed publication blocks input replacement until acknowledgement");
        }
        require(sha(rhd) == rhdHash && sha(rhs) == rhsHash, "Original RHD/RHS bytes unchanged");
        drain(250);
        std::cout << "PASS " << checks << " controller/stdio-sidecar checks (synthetic signals only)\nArtifacts: " << artifacts.path().toStdString() << '\n';
    } catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
