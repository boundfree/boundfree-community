#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <memory>

namespace bf::transport {

// Move this object to a dedicated event-loop thread before calling open(). All
// methods must execute in its owning thread; queued signals/slots carry only Qt
// built-in types. The constructor does not create a socket or start a timer.
class BfspTcpClient final : public QObject {
    Q_OBJECT
public:
    enum class State {
        Disconnected, Connecting, Handshaking, Ready, Prepared, Starting,
        Streaming, Stopping, OutcomeUnknown, Faulted
    };
    Q_ENUM(State)

    explicit BfspTcpClient(QObject* parent = nullptr);
    ~BfspTcpClient() override;

public slots:
    // Set before opening a connection. The caller supplies its actual stamped
    // build version; the default version is "unknown".
    void setClientIdentity(const QString& clientName, const QString& clientVersion);
    // generation is chosen by the owner and must increase on each open/close.
    // expectedDeviceId is optional; a mismatch fails the handshake.
    void open(quint64 generation, const QString& host, quint16 port,
              const QString& expectedDeviceId = {});
#ifdef BFSP_ENABLE_QT_SERIALPORT
    // Explicit serial settings; no probing bytes or implicit flow control.
    void openSerial(quint64 generation, const QString& portName, qint32 baudRate,
                    int dataBits, int parity, int stopBits, int flowControl,
                    const QString& expectedDeviceId = {});
#endif
    void close(quint64 generation);
    // requestId must be a fresh UUID for this session. No timed-out mutation is
    // automatically replayed; get_status is polled for at most 10 seconds to
    // reconcile its outcome. Late responses are checked against that status
    // before the restored transport state is reported to consumers.
    // First DATA allows at least 5 seconds or two maximum descriptor block
    // periods, whichever is longer; unrepresentable periods fail explicitly.
    void command(quint64 generation, const QString& requestId,
                 const QString& method, const QJsonObject& params);
    // Call after the consumer has taken ownership of / processed the DATA block.
    // Missing acknowledgements exhaust the bounded delivery budget and fault
    // the transport rather than silently accumulating queued Qt events.
    void acknowledgeData(quint64 generation, const QString& captureId,
                         quint32 streamId, quint64 firstSampleIndex);

signals:
    void stateChanged(quint64 generation, bf::transport::BfspTcpClient::State state);
    void connected(quint64 generation, const QJsonObject& helloAck);
    void responseReceived(quint64 generation, const QJsonObject& response);
    void descriptorReceived(quint64 generation, quint32 streamId, const QJsonObject& descriptor);
    // payload is the complete validated DATA payload. Metadata mirrors its
    // prefix; uint64 values are decimal strings, as in the BFSP JSON contract.
    void dataReceived(quint64 generation, quint32 streamId,
                      const QJsonObject& metadata, const QByteArray& payload);
    void eventReceived(quint64 generation, quint32 streamId, const QJsonObject& event);
    void statusReceived(quint64 generation, const QJsonObject& status);
    void streamEnded(quint64 generation, quint32 streamId, const QJsonObject& end);
    void errorOccurred(quint64 generation, const QString& code, const QJsonObject& params);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace bf::transport

Q_DECLARE_METATYPE(bf::transport::BfspTcpClient::State)
