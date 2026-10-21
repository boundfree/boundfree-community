#include "controller/eeg_separation_controller.h"
#include "dsp/eeg_separation.h"
#include "ipc/sidecar_client.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QMetaObject>
#include <QPointer>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace bf {
namespace {
struct TaskFailure { AppError error; };
void require(bool valid, const QString& code = "analysis.invalid_request", const QJsonObject& params = {}) {
    if (!valid) {
        QJsonObject details = params;
        if (code == "analysis.invalid_request") {
            if (!details.contains("field")) details.insert("field", "settings");
            if (!details.contains("reason")) details.insert("reason", "invalid_configuration");
        } else if (code == "analysis.resource_limit" && !details.contains("limit_bytes")) {
            details.insert("limit_bytes", 256 * 1024 * 1024);
        }
        throw TaskFailure{makeError(code, details)};
    }
}
QJsonObject checked(const SidecarResponse& response) {
    if (!response.ok) throw TaskFailure{response.error};
    return response.result;
}
void connectAnalysis(SidecarClient& client, const VersionInfo& version) {
    const auto hello = client.startAndHello(version, 30000);
    if (!hello.ok) throw TaskFailure{hello.error};
    require(hello.analysisCapabilities.contains("source-f64-v1") &&
            hello.analysisCapabilities.contains("bundle-f64-v1"), "analysis.unsupported_source",
            {{"reason", "sidecar_analysis_capability_missing"}});
}
SeparationSource parseSource(const QJsonObject& meta) {
    SeparationSource source;
    source.path = QDir::fromNativeSeparators(meta.value("path").toString());
    source.fs = meta.value("fs").toDouble();
    source.samples = meta.value("n_samples").toInteger();
    for (const auto& name : meta.value("ch_names").toArray()) source.channels.append(name.toString());
    for (const auto& id : meta.value("channel_ids").toArray()) source.channelIds.append(id.toString());
    source.metadata = meta;
    require(std::isfinite(source.fs) && source.fs > 0 && source.samples > 0 &&
            source.channels.size() == source.channelIds.size() && !source.channels.empty() &&
            meta.value("unit").toString() == "uV", "analysis.source_invalid",
            {{"reason", "invalid_metadata"}});
    return source;
}
QJsonArray numbers(const std::vector<double>& values) {
    QJsonArray array;
    for (double value : values) array.append(value);
    return array;
}
QJsonArray matrixJson(const SeparationMatrix& values) {
    QJsonArray array;
    for (const auto& row : values) array.append(numbers(row));
    return array;
}
QJsonArray labelJson(const std::vector<int>& values) {
    QJsonArray array;
    for (int value : values) array.append(value);
    return array;
}
SeparationMatrix readDoublePayload(const QJsonObject& meta, const QByteArray& payload, int channels, qint64 samples) {
    const auto shape = meta.value("shape").toArray();
    const qint64 expected = static_cast<qint64>(channels) * samples * 8;
    require(channels > 0 && samples > 0 && expected <= 256LL * 1024 * 1024 &&
            shape.size() == 2 && shape[0].toInteger() == channels && shape[1].toInteger() == samples &&
            meta.value("dtype").toString() == "float64-le" && meta.value("unit").toString() == "uV" &&
            meta.value("payload_bytes").toInteger() == expected && payload.size() == expected,
            "ipc.invalid_frame", {{"reason", "analysis_payload_mismatch"}});
    SeparationMatrix result(channels, std::vector<double>(samples));
    auto bytes = reinterpret_cast<const uchar*>(payload.constData());
    for (auto& row : result) for (double& sample : row) {
        const quint64 bits = qFromLittleEndian<quint64>(bytes);
        std::memcpy(&sample, &bits, 8);
        bytes += 8;
        require(std::isfinite(sample), "analysis.source_invalid", {{"reason", "non_finite_samples"}});
    }
    return result;
}
QByteArray bundlePayload(const SeparationOutput& output, const std::vector<int>& labels) {
    const auto n = output.analysis.front().size();
    const auto c = output.analysis.size();
    const quint64 size = 7ULL * c * n * 8;
    require(size <= 256ULL * 1024 * 1024, "analysis.resource_limit", {{"bytes", static_cast<qint64>(size)}});
    QByteArray payload(static_cast<qsizetype>(size), Qt::Uninitialized);
    char* destination = payload.data();
    const auto append = [&](const SeparationMatrix& matrix) {
        require(matrix.size() == c, "analysis.invalid_input");
        for (const auto& row : matrix) {
            require(row.size() == n, "analysis.invalid_input");
            for (double value : row) {
                require(std::isfinite(value), "analysis.non_finite");
                quint64 bits;
                std::memcpy(&bits, &value, 8);
                qToLittleEndian(bits, reinterpret_cast<uchar*>(destination));
                destination += 8;
            }
        }
    };
    append(output.original);
    append(output.analysis);
    separation::ica::Result core;
    core.analysis = output.analysis;
    core.sources = output.sources;
    core.mixing = output.mixing;
    core.residual = output.residual;
    core.mean = output.mean;
    core.rank = output.rank;
    for (int group = 0; group < 4; ++group) append(separation::ica::project(core, labels, group));
    append(output.residual);
    return payload;
}
}

EegSeparationController::EegSeparationController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent), repoRoot_(std::move(repoRoot)), version_(std::move(version)) {}
EegSeparationController::~EegSeparationController() {
    // QObject and its QPointer guards must outlive every task that can enqueue a delivery.
    // Normal UI cancellation never joins; only destruction waits for cooperative shutdown.
    stopWorker();
    for (const auto& active : activeWorkers_) {
        active.thread->disconnect(this);
        active.thread->wait();
        delete active.thread;
    }
    activeWorkers_.clear();
}
bool EegSeparationController::isBusy() const {
    return state_ == State::Inspecting || state_ == State::Running || state_ == State::Exporting;
}
void EegSeparationController::setState(State state) { state_ = state; emit stateChanged(); }
void EegSeparationController::launch(State state, std::function<void(quint64, std::shared_ptr<std::atomic_bool>)> task) {
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++generation_;
    auto cancelled = cancellation_;
    auto worker = QThread::create([task = std::move(task), generation, cancelled]() { task(generation, cancelled); });
    worker_ = worker;
    activeWorkers_.push_back({worker, cancelled});
    connect(worker, &QThread::finished, this, [this, worker]() {
        if (worker_ == worker) worker_ = nullptr;
        const auto active = std::find_if(activeWorkers_.begin(), activeWorkers_.end(),
                                        [worker](const ActiveWorker& item) { return item.thread == worker; });
        if (active != activeWorkers_.end()) activeWorkers_.erase(active);
        worker->deleteLater();
    });
    if (state != State::Exporting) publishing_.reset();
    QPointer<EegSeparationController> guard(this);
    worker->start();
    setState(state);
    if (guard) emit progressChanged(state == State::Inspecting ? "inspecting" : state == State::Running ? "reading" : "writing");
}
void EegSeparationController::stopWorker() {
    // Track cancelled generations until they finish: exports may still need to discard a stage.
    for (const auto& active : activeWorkers_) active.cancelled->store(true);
    if (cancellation_) cancellation_->store(true);
    ++generation_;
    worker_ = nullptr;
}
void EegSeparationController::invalidate() {
    output_.reset(); projections_ = {}; labels_.clear(); labelHistory_.clear(); confirmed_ = false; exportedPath_.clear();
    emit resultChanged(); emit confirmationChanged();
}
void EegSeparationController::clearSource() {
    // Publication has a single commit point. Let its acknowledgement finish before replacing input.
    std::lock_guard<std::mutex> lock(*publicationMutex_);
    if (state_ == State::Exporting && publishing_ && publishing_->load()) return;
    stopWorker(); source_ = {}; settings_ = {}; invalidate(); setState(State::Empty); emit sourceChanged();
}
void EegSeparationController::openSource(const QString& path, const QString& csvUnit) {
    if (state_ == State::Exporting && publishing_ && publishing_->load()) return;
    clearSource();
    if (state_ != State::Empty || path.isEmpty()) return;
    const auto root = repoRoot_; const auto version = version_; QPointer<EegSeparationController> owner(this);
    launch(State::Inspecting, [owner, root, version, path, csvUnit](quint64 generation, auto cancelled) {
        const auto deliver = [owner, generation, cancelled](std::function<void(EegSeparationController*)> action) {
            if (!owner || cancelled->load()) return;
            QMetaObject::invokeMethod(owner, [owner, generation, cancelled, action]() {
                if (owner && !cancelled->load() && owner->generation_ == generation) action(owner);
            }, Qt::QueuedConnection);
        };
        try {
            SidecarClient client(root); client.setCancellation([cancelled] { return cancelled->load(); });
            connectAnalysis(client, version);
            QJsonObject openParams{{"path", path}};
            if (!csvUnit.isEmpty()) openParams.insert("csv_unit", csvUnit);
            auto meta = checked(client.request("analysis_source_open", openParams, 60000));
            if (!csvUnit.isEmpty()) {
                meta.insert("csv_unit", csvUnit);
                meta.insert("input_unit_explicit_by_user", true);
            }
            auto source = parseSource(meta);
            QFile teachingFile(QDir(root).filePath("samples/offline-eeg/manifest.json"));
            QJsonObject teaching;
            if (teachingFile.open(QIODevice::ReadOnly)) teaching = QJsonDocument::fromJson(teachingFile.readAll()).object();
            source.teaching = teaching.value("schema").toString() == "boundfree.separation-teaching.v1" &&
                teaching.value("synthetic").toBool() &&
                teaching.value("sha256").toString() == meta.value("fingerprint").toObject().value("sha256").toString() &&
                teaching.value("fs").toDouble() == source.fs && teaching.value("n_samples").toInteger() == source.samples &&
                teaching.value("ch_names").toArray() == meta.value("ch_names").toArray() &&
                teaching.value("channel_ids").toArray() == meta.value("channel_ids").toArray() &&
                teaching.value("roles").toArray() == QJsonArray{1,1,1,2,3};
            deliver([source](auto self) {
                self->source_ = source;
                self->settings_.roles.assign(source.channels.size(), 0);
                if (source.teaching) self->settings_.roles = {1,1,1,2,3};
                self->settings_.duration = std::min(120.0, source.duration());
                self->settings_.outputRate = source.fs >= 500 ? 500 : 250;
                self->setState(State::Ready); emit self->sourceChanged();
            });
        } catch (const TaskFailure& failure) { deliver([failure, path, csvUnit](auto self) {
            self->setState(State::Empty);
            if (csvUnit.isEmpty() && path.endsWith(".csv", Qt::CaseInsensitive) &&
                failure.error.code == "analysis.unknown_unit" && failure.error.params.value("unit").toString() == "unknown")
                emit self->unitRequired(path);
            else emit self->failed(failure.error);
        }); }
          catch (...) { deliver([](auto self) { self->setState(State::Empty); emit self->failed(makeError("analysis.failed")); }); }
    });
}
void EegSeparationController::openTeachingSource() {
    openSource(QDir(repoRoot_).filePath("samples/offline-eeg/teaching.h5"));
}
bool EegSeparationController::canApplyTeachingLabels() const {
    return source_.teaching && output_ && !isBusy() && settings_.roles == std::vector<int>({1,1,1,2,3}) &&
        settings_.start == 0.0 && settings_.duration == 16.0 && settings_.outputRate == 500 &&
        settings_.notchHz == 0 && output_->rank == 3 && output_->scores.size() == 3;
}
void EegSeparationController::applyTeachingLabels() {
    if (!canApplyTeachingLabels()) return;
    int eye = -1, muscle = -1;
    for (int i = 0; i < 3; ++i) {
        if (eye < 0 || output_->scores[i].eyeCorrelation > output_->scores[eye].eyeCorrelation) eye = i;
        if (muscle < 0 || output_->scores[i].muscleCorrelation > output_->scores[muscle].muscleCorrelation) muscle = i;
    }
    // This mapping is exclusively a demonstration on the hash-verified mathematical fixture.
    if (eye == muscle || output_->scores[eye].eyeCorrelation < .9 || output_->scores[muscle].muscleCorrelation < .8) return;
    const auto previous = labels_;
    const auto historySize = labelHistory_.size();
    for (int i = 0; i < 3; ++i) setLabel(i, i == eye ? 1 : i == muscle ? 2 : 0);
    labelHistory_.resize(historySize);
    if (labels_ != previous) labelHistory_.push_back(previous);
    emit labelsChanged();
}
void EegSeparationController::setSettings(const SeparationSettings& settings) {
    if (settings.roles == settings_.roles && settings.start == settings_.start && settings.duration == settings_.duration &&
        settings.outputRate == settings_.outputRate && settings.notchHz == settings_.notchHz) return;
    if (isBusy()) {
        cancel();
        if (isBusy()) return; // The atomic publication commit already started.
    }
    settings_ = settings; invalidate(); setState(source_.path.isEmpty() ? State::Empty : State::Ready);
}
void EegSeparationController::start() {
    if (isBusy() || source_.path.isEmpty()) return;
    const auto settings = settings_; const auto source = source_; const auto root = repoRoot_; const auto version = version_;
    try {
        const auto observationCount = std::count(settings.roles.begin(), settings.roles.end(), 1);
        const auto selected = std::count_if(settings.roles.begin(), settings.roles.end(), [](int role) { return role >= 1 && role <= 3; });
        require(settings.roles.size() == static_cast<size_t>(source.channels.size()) && observationCount >= 2 && observationCount <= 32 &&
                selected <= 40 && std::none_of(settings.roles.begin(), settings.roles.end(), [](int role) { return role < 1 || role > 4; }) &&
                std::isfinite(settings.start) && std::isfinite(settings.duration) && settings.start >= 0 && settings.duration >= 5 &&
                settings.duration <= 120 && settings.start + settings.duration <= source.duration() + 0.5 / source.fs &&
                (settings.outputRate == 250 || settings.outputRate == 500) && settings.outputRate <= source.fs &&
                (settings.notchHz == 0 || settings.notchHz == 50 || settings.notchHz == 60));
    } catch (const TaskFailure& failure) { emit failed(failure.error); return; }
    invalidate(); QPointer<EegSeparationController> owner(this);
    launch(State::Running, [owner, settings, source, root, version](quint64 generation, auto cancelled) {
        const auto deliver = [owner, generation, cancelled](std::function<void(EegSeparationController*)> action) {
            if (!owner || cancelled->load()) return;
            QMetaObject::invokeMethod(owner, [owner, generation, cancelled, action]() {
                if (owner && !cancelled->load() && owner->generation_ == generation) action(owner);
            }, Qt::QueuedConnection);
        };
        try {
            deliver([](auto self) { emit self->progressChanged("reading"); });
            SidecarClient client(root); client.setCancellation([cancelled] { return cancelled->load(); });
            connectAnalysis(client, version);
            QJsonObject openParams{{"path", source.path}};
            const auto csvUnit = source.metadata.value("csv_unit").toString();
            if (!csvUnit.isEmpty()) openParams.insert("csv_unit", csvUnit);
            auto meta = checked(client.request("analysis_source_open", openParams, 60000));
            require(meta.value("fingerprint").toObject() == source.metadata.value("fingerprint").toObject(),
                    "analysis.source_changed", {{"path", source.path}});
            separation::Request request;
            request.sampleRate = source.fs; request.outputRate = settings.outputRate; request.notchHz = settings.notchHz;
            request.totalSamples = source.samples;
            request.firstSample = static_cast<quint64>(std::llround(settings.start * source.fs));
            const auto end = std::min<quint64>(source.samples, std::llround((settings.start + settings.duration) * source.fs));
            request.sampleCount = end - request.firstSample;
            const int half = separation::resamplingHalfWidth(source.fs, settings.outputRate);
            request.readFirstSample = request.firstSample > static_cast<quint64>(half) ? request.firstSample - half : 0;
            const auto readEnd = std::min<quint64>(source.samples, end + half);
            const auto readCount = readEnd - request.readFirstSample;
            QJsonArray ids;
            QStringList observations;
            for (int i = 0; i < source.channels.size(); ++i) {
                if (settings.roles[i] == 4) continue;
                ids.append(source.channelIds[i]); request.channelIds.push_back(source.channelIds[i].toStdString());
                request.roles.push_back(settings.roles[i] == 1 ? separation::Role::Observation :
                                        settings.roles[i] == 2 ? separation::Role::EogReference : separation::Role::EmgReference);
                if (settings.roles[i] == 1) observations.append(source.channels[i]);
            }
            require(readCount * ids.size() * 8ULL <= 256ULL * 1024 * 1024, "analysis.resource_limit",
                    {{"bytes", static_cast<qint64>(readCount * ids.size() * 8ULL)}});
            QByteArray bytes;
            const auto slice = checked(client.requestBinary("analysis_source_read",
                {{"handle", meta.value("handle")}, {"first_sample", static_cast<qint64>(request.readFirstSample)},
                 {"n_samples", static_cast<qint64>(readCount)}, {"channel_ids", ids}}, {}, &bytes, 60000));
            require(slice.value("fs").toDouble() == source.fs &&
                    slice.value("first_sample").toInteger(-1) == static_cast<qint64>(request.readFirstSample),
                    "ipc.invalid_frame", {{"reason", "analysis_sample_coordinates_mismatch"}});
            request.samples = readDoublePayload(slice, bytes, ids.size(), readCount);
            bytes.clear();
            deliver([](auto self) { emit self->progressChanged("fitting"); });
            const auto result = separation::analyze(request, [cancelled] { return cancelled->load(); });
            auto output = std::make_shared<SeparationOutput>();
            output->fs = result.sampleRate; output->start = result.actualStart; output->channels = observations;
            output->original = result.original; output->analysis = result.ica.analysis; output->sources = result.ica.sources;
            output->mixing = result.ica.mixing; output->residual = result.ica.residual; output->mean = result.ica.mean;
            output->rank = result.ica.rank; output->iterations = result.ica.iterations; output->convergence = result.ica.convergence;
            QJsonArray scores;
            for (const auto& score : result.scores) {
                output->scores.push_back({score.eogCorrelation, score.emgCorrelation, score.highFrequencyRatio});
                scores.append(QJsonObject{{"eog_correlation", score.eogCorrelation}, {"emg_correlation", score.emgCorrelation},
                                          {"high_frequency_ratio", score.highFrequencyRatio}});
            }
            output->manifest = QJsonObject{{"schema", "boundfree.community.offline-eeg.v1"}, {"product_version", version.version},
                {"source", source.metadata}, {"source_path", source.path}, {"source_fingerprint", source.metadata.value("fingerprint")},
                {"first_sample", static_cast<qint64>(request.firstSample)}, {"sample_count", static_cast<qint64>(request.sampleCount)},
                {"read_first_sample", static_cast<qint64>(request.readFirstSample)}, {"channel_ids", ids},
                {"channel_roles", labelJson(settings.roles)}, {"source_rate", source.fs}, {"output_rate", settings.outputRate},
                {"observation_channel_ids", [&result] { QJsonArray ids; for (const auto& id : result.channelIds) ids.append(QString::fromStdString(id)); return ids; }()},
                {"read_diagnostics", slice.value("saturation")}, {"resample_half_width", result.resampleHalfWidth}, {"notch_hz", settings.notchHz}, {"fit_highpass_hz", 1.0},
                {"seed", 42}, {"max_iterations", 600}, {"tolerance", 1e-6}, {"rank", result.ica.rank},
                {"iterations", result.ica.iterations}, {"convergence", result.ica.convergence}, {"mean", numbers(output->mean)},
                {"mixing", matrixJson(result.ica.mixing)}, {"unmixing", matrixJson(result.ica.unmixing)}, {"scores", scores},
                {"reference", "recorded"}, {"browser_processing_applied", false}};
            deliver([output](auto self) {
                self->output_ = output; self->labels_.assign(output->rank, 3); self->confirmed_ = false;
                for (auto& group : self->projections_) group.assign(output->analysis.size(), std::vector<double>(output->analysis.front().size()));
                for (size_t c = 0; c < output->analysis.size(); ++c)
                    for (size_t t = 0; t < output->analysis[c].size(); ++t)
                        self->projections_[3][c][t] = output->analysis[c][t] - output->mean[c] - output->residual[c][t];
                self->setState(State::Review); emit self->resultChanged(); emit self->labelsChanged();
            });
        } catch (const TaskFailure& failure) { deliver([failure](auto self) { self->setState(State::Ready); emit self->failed(failure.error); }); }
          catch (const separation::Error& error) {
            const auto code = QString::fromLatin1(error.what());
            QJsonObject params;
            if (error.reason == separation::Failure::Reconstruction && std::isfinite(error.diagnosticErrorUv))
                params.insert("error_uV", error.diagnosticErrorUv);
            deliver([code, params](auto self) { self->setState(State::Ready); emit self->failed(makeError(code, params)); }); }
          catch (...) { deliver([](auto self) { self->setState(State::Ready); emit self->failed(makeError("analysis.failed")); }); }
    });
}
void EegSeparationController::cancel() {
    std::lock_guard<std::mutex> lock(*publicationMutex_);
    if (state_ == State::Exporting && publishing_ && publishing_->load()) return;
    stopWorker(); setState(output_ ? State::Review : source_.path.isEmpty() ? State::Empty : State::Ready);
}
void EegSeparationController::setLabel(int component, int group) {
    if (!output_ || isBusy() || component < 0 || component >= static_cast<int>(labels_.size()) || group < 0 || group > 3 || labels_[component] == group) return;
    labelHistory_.push_back(labels_);
    const int previous = labels_[component];
    for (size_t c = 0; c < output_->mixing.size(); ++c)
        for (size_t t = 0; t < output_->sources[component].size(); ++t) {
            const double contribution = output_->mixing[c][component] * output_->sources[component][t];
            projections_[previous][c][t] -= contribution;
            projections_[group][c][t] += contribution;
        }
    labels_[component] = group; confirmed_ = false;
    emit labelsChanged(); emit confirmationChanged();
}
void EegSeparationController::undoLabel() {
    if (isBusy() || labelHistory_.empty()) return;
    const auto previous = labelHistory_.back(); labelHistory_.pop_back();
    for (size_t component = 0; component < labels_.size(); ++component) {
        if (previous[component] == labels_[component]) continue;
        for (size_t c = 0; c < output_->mixing.size(); ++c)
            for (size_t t = 0; t < output_->sources[component].size(); ++t) {
                const double contribution = output_->mixing[c][component] * output_->sources[component][t];
                projections_[labels_[component]][c][t] -= contribution;
                projections_[previous[component]][c][t] += contribution;
            }
    }
    labels_ = previous; confirmed_ = false;
    emit labelsChanged(); emit confirmationChanged();
}
void EegSeparationController::setConfirmed(bool confirmed) {
    confirmed_ = output_ && !isBusy() && confirmed; emit confirmationChanged();
}
const SeparationMatrix& EegSeparationController::projection(int group) const {
    static const SeparationMatrix empty;
    return group >= 0 && group < 4 && output_ ? projections_[group] : empty;
}
void EegSeparationController::exportBundle(const QString& destination) {
    if (isBusy() || !confirmed_ || !output_ || destination.isEmpty()) return;
    const auto output = output_; const auto labels = labels_; const auto root = repoRoot_; const auto version = version_;
    QPointer<EegSeparationController> owner(this);
    publishing_ = std::make_shared<std::atomic_bool>(false);
    const auto publishing = publishing_; const auto mutex = publicationMutex_;
    launch(State::Exporting, [owner, output, labels, root, version, destination, publishing, mutex](quint64 generation, auto cancelled) {
        const auto deliver = [owner, generation](std::function<void(EegSeparationController*)> action) {
            if (!owner) return;
            QMetaObject::invokeMethod(owner, [owner, generation, action]() {
                if (owner && owner->generation_ == generation) action(owner);
            }, Qt::QueuedConnection);
        };
        try {
            deliver([](auto self) { emit self->progressChanged("writing"); });
            auto bytes = bundlePayload(*output, labels);
            if (cancelled->load()) return;
            QTemporaryDir controls;
            require(controls.isValid(), "analysis.export_failed", {{"path", destination}, {"reason", "control_directory_unavailable"}});
            const QString cancelPath = QDir(controls.path()).filePath("cancel");
            SidecarClient client(root);
            // Let the sidecar unwind staging and return its error instead of killing it mid-write.
            client.setCancellation([cancelled, cancelPath] {
                if (cancelled->load()) { QFile marker(cancelPath); if (marker.open(QIODevice::WriteOnly)) marker.close(); }
                return false;
            });
            connectAnalysis(client, version);
            QJsonObject manifest = output->manifest;
            manifest.insert("labels", labelJson(labels)); manifest.insert("manually_confirmed", true);
            const auto staged = checked(client.requestBinary("analysis_bundle_write",
                {{"path", destination}, {"fs", output->fs}, {"t0", output->start},
                 {"ch_names", QJsonArray::fromStringList(output->channels)},
                 {"shape", QJsonArray{7, static_cast<qint64>(output->analysis.size()), static_cast<qint64>(output->analysis.front().size())}},
                 {"dtype", "float64-le"}, {"unit", "uV"}, {"manifest", manifest}, {"stage_only", true},
                 {"cancel_path", cancelPath}}, bytes, nullptr, 120000));
            const auto token = staged.value("token");
            {
                std::lock_guard<std::mutex> lock(*mutex);
                if (!cancelled->load()) publishing->store(true);
            }
            if (cancelled->load()) {
                client.request("analysis_bundle_discard", {{"token", token}}, 10000); return;
            }
            deliver([](auto self) { emit self->progressChanged("publishing"); });
            const auto result = checked(client.request("analysis_bundle_publish", {{"token", token}}, 30000));
            const auto path = QDir::fromNativeSeparators(result.value("path").toString(destination));
            deliver([path](auto self) { self->exportedPath_ = path; self->setState(State::Review); emit self->exported(path); });
        } catch (const TaskFailure& failure) {
            if (!cancelled->load()) deliver([failure](auto self) { self->setState(State::Review); emit self->failed(failure.error); });
        } catch (...) {
            if (!cancelled->load()) deliver([](auto self) { self->setState(State::Review); emit self->failed(makeError("analysis.export_failed", {{"path", QString()}, {"reason", "native_export_failure"}})); });
        }
    });
}
} // namespace bf
