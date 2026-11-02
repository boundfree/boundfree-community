#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include <QObject>
#include <QJsonObject>
#include <QStringList>
#include <QThread>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <functional>
#include <vector>

namespace bf {
using SeparationMatrix = std::vector<std::vector<double>>;
struct SeparationSource {
    QString path;
    double fs = 0.0;
    qint64 samples = 0;
    QStringList channels;
    QStringList channelIds;
    QJsonObject metadata;
    bool teaching = false;
    bool simulatorIca = false;
    double duration() const { return fs > 0.0 ? samples / fs : 0.0; }
};
struct SeparationSettings {
    // 0 unconfirmed, 1 observation, 2 eye reference, 3 muscle reference, 4 excluded.
    std::vector<int> roles;
    double start = 0.0;
    double duration = 60.0;
    int outputRate = 500;
    int notchHz = 0;
};
struct SeparationComponentScore {
    double eyeCorrelation = 0.0;
    double muscleCorrelation = 0.0;
    double highFrequencyRatio = 0.0;
};
struct SeparationOutput {
    double fs = 0.0;
    double start = 0.0;
    QStringList channels;
    SeparationMatrix original;
    SeparationMatrix analysis;
    SeparationMatrix sources;
    SeparationMatrix mixing;
    SeparationMatrix residual;
    std::vector<double> mean;
    std::vector<SeparationComponentScore> scores;
    int rank = 0;
    int iterations = 0;
    double convergence = 0.0;
    QJsonObject manifest;
};

class EegSeparationController final : public QObject {
    Q_OBJECT
public:
    enum class State { Empty, Inspecting, Ready, Running, Review, Exporting };
    EegSeparationController(QString repoRoot, VersionInfo version, QObject* parent = nullptr);
    ~EegSeparationController() override;
    State state() const { return state_; }
    bool isBusy() const;
    const SeparationSource& source() const { return source_; }
    const SeparationSettings& settings() const { return settings_; }
    std::shared_ptr<const SeparationOutput> output() const { return output_; }
    const std::vector<int>& labels() const { return labels_; }
    bool isConfirmed() const { return confirmed_; }
    QString exportedPath() const { return exportedPath_; }
    void openSource(const QString& path, const QString& csvUnit = {}, bool preserveOnFailure = false);
    void openTeachingSource();
    void applyTeachingLabels();
    bool canApplyTeachingLabels() const;
    void clearSource();
    void setSettings(const SeparationSettings& settings);
    void start();
    void cancel();
    // 0 EEG candidate, 1 eye estimate, 2 muscle contribution, 3 unresolved.
    void setLabel(int component, int group);
    void undoLabel();
    bool canUndoLabel() const { return !labelHistory_.empty(); }
    void setConfirmed(bool confirmed);
    const SeparationMatrix& projection(int group) const;
    void exportBundle(const QString& destination);
signals:
    void stateChanged();
    void sourceChanged();
    void resultChanged();
    void labelsChanged();
    void confirmationChanged();
    // Stable stage key, not a localized backend sentence.
    void progressChanged(const QString& stage);
    void failed(const bf::AppError& error);
    void sourceReplacementFailed(const bf::AppError& error);
    void exported(const QString& path);
    void unitRequired(const QString& path);
private:
    void setState(State state);
    void invalidate();
    void stopWorker();
    void launch(State state, std::function<void(quint64, std::shared_ptr<std::atomic_bool>)> task);
    QString repoRoot_;
    VersionInfo version_;
    State state_ = State::Empty;
    SeparationSource source_;
    SeparationSettings settings_;
    std::shared_ptr<SeparationOutput> output_;
    std::vector<int> labels_;
    std::vector<std::vector<int>> labelHistory_;
    std::array<SeparationMatrix, 4> projections_;
    bool confirmed_ = false;
    QString exportedPath_;
    struct ActiveWorker {
        QThread* thread = nullptr;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    std::vector<ActiveWorker> activeWorkers_;
    QThread* worker_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancellation_;
    quint64 generation_ = 0;
    std::shared_ptr<std::atomic_bool> publishing_;
    std::shared_ptr<std::mutex> publicationMutex_ = std::make_shared<std::mutex>();
};
}
