#pragma once

#include "acquisition/buffer/signal_hub.h"
#include "acquisition/control/acquisition_source.h"
#include "core/version.h"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace bf {

// GUI-thread facade. TCP, decoding, history reduction and hub publication are
// serialized in a private event-loop thread; stateChanged carries no raw data.
class AcquisitionTcpSource final : public AcquisitionSource {
    Q_OBJECT
public:
    explicit AcquisitionTcpSource(VersionInfo version, QObject* parent = nullptr);
    ~AcquisitionTcpSource() override;

    void open(const QString& host, quint16 port, const QString& expectedDeviceId = {});
    void configure(int sampleRate) override;
    void startAcquisition() override;
    void stopAcquisition() override;
    void close() override;

    AcquisitionViewSnapshot snapshot() const override;
    std::optional<CaptureContext> captureContext() const override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace bf
