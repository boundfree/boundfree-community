#pragma once

#include "acquisition/control/acquisition_source.h"

namespace bf {

struct RtBciWifiOptions {
    QString host = QStringLiteral("192.168.4.1");
    quint16 httpPort = 80;
    QString localAddress; // Empty selects the route to host; never inferred from SSID.
    quint16 listenPort = 6677; // Zero is useful for isolated virtual-device tests.
    int channels = 0; // Auto; 8/16 are fallbacks if wire detection fails.
    int sampleRate = 250;
    int gain = 0; // Zero keeps raw count units; otherwise a user-declared PGA gain.
    double referenceVolts = 4.5; // Nominal/user-declared, never hardware-confirmed.
    QString expectedDeviceId;
};

struct RtBciProbeResult {
    bool compatible = false;
    int channels = 0;
    QString deviceId;
    QString model;
    QString localAddress;
    AppError error;
};

// Read-only, asynchronous probe. It never stops, configures or takes ownership.
class RtBciWifiProbe final : public QObject {
    Q_OBJECT
public:
    explicit RtBciWifiProbe(QObject* parent = nullptr);
    ~RtBciWifiProbe() override;
    quint64 probe(const QString& host, quint16 port = 80);
    void cancel();
signals:
    void finished(quint64 generation, const bf::RtBciProbeResult& result);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

class AcquisitionWifiSource final : public AcquisitionSource {
    Q_OBJECT
public:
    explicit AcquisitionWifiSource(QObject* parent = nullptr);
    ~AcquisitionWifiSource() override;
    void open(const RtBciWifiOptions& options);
    AcquisitionViewSnapshot snapshot() const override;
    std::optional<CaptureContext> captureContext() const override;
    void configure(int sampleRate) override;
    void startAcquisition() override;
    void stopAcquisition() override;
    void close() override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace bf
