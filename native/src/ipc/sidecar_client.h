#pragma once

#include "core/app_error.h"
#include "core/version.h"

#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace bf {

struct SidecarHelloResult {
    bool ok = false;
    QString productVersion;
    int ipcProtocolVersion = 0;
    AppError error;
};

struct SidecarResponse {
    bool ok = false;
    QJsonObject result;
    AppError error;
};

struct SidecarOpenResult {
    bool ok = false;
    QString handle;
    double fs = 0.0;
    int nChannels = 0;
    int nSamples = 0;
    double duration = 0.0;
    QString format;
    QStringList chNames;
    AppError error;
};

class SidecarClient {
public:
    explicit SidecarClient(QString repoRoot);
    ~SidecarClient();

    SidecarHelloResult startAndHello(const VersionInfo& version, int timeoutMs = 3000);
    SidecarResponse request(const QString& method, const QJsonObject& params, int timeoutMs = 3000);
    SidecarOpenResult openFile(const QString& path, int timeoutMs = 3000);
    void stop();

private:
    bool writeJsonLine(const QJsonObject& object, int timeoutMs);
    QByteArray readJsonLine(int timeoutMs);

    QString repoRoot_;
    QProcess process_;
};

}  // namespace bf
