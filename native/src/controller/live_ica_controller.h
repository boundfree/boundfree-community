#pragma once
#include "controller/acquisition_controller.h"
#include "render/live_ica_projection.h"
#include <QTimer>
#include <QElapsedTimer>

namespace bf {
class LiveIcaController final : public QObject {
    Q_OBJECT
public:
    explicit LiveIcaController(AcquisitionController*, QObject* parent = nullptr);
    bool isActive() const { return active_; }
    void start(std::shared_ptr<const SeparationOutput>, const std::vector<int>& labels);
    void stop();
    void setLabels(const std::vector<int>&);
signals:
    void activeChanged(bool active);
    void frameReady(std::shared_ptr<const bf::LiveIcaFrame> frame);
    void failed(const bf::AppError& error);
private:
    void request();
    AcquisitionController* acquisition_;
    QTimer timer_;
    bool active_ = false, busy_ = false;
    quint64 revision_ = 0, requestId_ = (quint64(1) << 63);
    std::shared_ptr<const SeparationOutput> model_;
    std::vector<int> labels_;
    QElapsedTimer freshness_;
    double latestTime_ = -1;
    bool haveNewSamples_ = false;
};
}
