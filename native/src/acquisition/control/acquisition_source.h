#pragma once

#include "acquisition/control/acquisition_state.h"
#include <optional>

namespace bf {

// GUI-thread command facade shared by hardware sources. Raw data stays on workers.
class AcquisitionSource : public QObject {
    Q_OBJECT
public:
    struct CaptureContext {
        acquisition::DeviceDescriptor device;
        acquisition::CaptureDescriptor capture;
        std::shared_ptr<acquisition::SignalHub> hub;
        std::shared_ptr<acquisition::SignalConsumer> history;
    };
    explicit AcquisitionSource(QObject* parent = nullptr) : QObject(parent) {}
    virtual AcquisitionViewSnapshot snapshot() const = 0;
    virtual std::optional<CaptureContext> captureContext() const = 0;
    virtual void configure(int sampleRate) = 0;
    virtual void startAcquisition() = 0;
    virtual void stopAcquisition() = 0;
    virtual void close() = 0;
    quint64 lastCloseRequest() const { return closeRequest_; }

signals:
    void stateChanged();

protected:
    quint64 beginClose() { return ++closeRequest_; } // GUI-thread request identity.
private:
    quint64 closeRequest_ = 0;
};

} // namespace bf
