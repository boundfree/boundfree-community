#include "ipc_client.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QtEndian>

#include <cstring>

namespace {

QByteArray u32be(quint32 n) {
    QByteArray bytes(4, 0);
    qToBigEndian(n, reinterpret_cast<uchar*>(bytes.data()));
    return bytes;
}

QByteArray makeRequest(const QJsonObject& obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

}  // namespace

bool SidecarClient::start(const QString& python, const QString& script) {
    proc_.setProgram(python);
    proc_.setArguments({script});
    proc_.setProcessChannelMode(QProcess::SeparateChannels);
    proc_.start();
    return proc_.waitForStarted(3000);
}

bool SidecarClient::writeJson(const QByteArray& json) {
    proc_.write(u32be(static_cast<quint32>(json.size())));
    proc_.write(json);
    return proc_.waitForBytesWritten(2000);
}

QByteArray SidecarClient::readFrame(int timeoutMs) {
    QByteArray header;
    while (header.size() < 4) {
        if (proc_.bytesAvailable() == 0 && !proc_.waitForReadyRead(timeoutMs)) {
            qWarning() << "readFrame timeout before header"
                       << "state" << proc_.state()
                       << "error" << proc_.error()
                       << "stderr" << proc_.readAllStandardError();
            return {};
        }
        header += proc_.read(4 - header.size());
    }

    const auto nBytes = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(header.constData()));
    QByteArray payload;
    payload.reserve(static_cast<int>(nBytes));
    while (static_cast<quint32>(payload.size()) < nBytes) {
        if (proc_.bytesAvailable() == 0 && !proc_.waitForReadyRead(timeoutMs)) {
            qWarning() << "readFrame timeout during payload"
                       << "expected" << nBytes
                       << "got" << payload.size()
                       << "state" << proc_.state()
                       << "error" << proc_.error()
                       << "stderr" << proc_.readAllStandardError();
            return {};
        }
        payload += proc_.read(static_cast<qint64>(nBytes - static_cast<quint32>(payload.size())));
    }
    return payload;
}

bool SidecarClient::open(const QString& path, QString* metaJsonOut) {
    const QByteArray req = makeRequest({{"method", "open"}, {"path", path}});
    if (!writeJson(req)) {
        return false;
    }
    const QByteArray frame = readFrame();
    if (frame.isEmpty()) {
        return false;
    }
    if (metaJsonOut != nullptr) {
        *metaJsonOut = QString::fromUtf8(frame);
    }
    return true;
}

bool SidecarClient::getSlice(double t0, double t1, SliceResult* out) {
    const QByteArray req = makeRequest({{"method", "get_slice"}, {"t0", t0}, {"t1", t1}});
    if (!writeJson(req)) {
        return false;
    }

    const QByteArray frame = readFrame();
    if (frame.size() < 8 || out == nullptr) {
        return false;
    }

    out->nCh = qFromBigEndian<qint32>(reinterpret_cast<const uchar*>(frame.constData()));
    out->nSamp = qFromBigEndian<qint32>(reinterpret_cast<const uchar*>(frame.constData() + 4));
    const std::size_t count = static_cast<std::size_t>(out->nCh) * static_cast<std::size_t>(out->nSamp);
    const std::size_t expectedBytes = 8 + count * sizeof(float);
    if (out->nCh <= 0 || out->nSamp < 0 || static_cast<std::size_t>(frame.size()) != expectedBytes) {
        return false;
    }

    out->uv.resize(count);
    std::memcpy(out->uv.data(), frame.constData() + 8, count * sizeof(float));
    return true;
}

void SidecarClient::stop() {
    proc_.closeWriteChannel();
    proc_.waitForFinished(2000);
    if (proc_.state() != QProcess::NotRunning) {
        proc_.kill();
        proc_.waitForFinished(1000);
    }
}
