#pragma once

#include "core/app_error.h"
#include "core/version.h"

#include <QProcess>
#include <QString>

namespace bf {

struct SidecarHelloResult {
    bool ok = false;
    QString productVersion;
    int ipcProtocolVersion = 0;
    AppError error;
};

class SidecarClient {
public:
    explicit SidecarClient(QString repoRoot);
    ~SidecarClient();

    SidecarHelloResult startAndHello(const VersionInfo& version, int timeoutMs = 3000);
    void stop();

private:
    bool writeJsonLine(const QJsonObject& object, int timeoutMs);
    QByteArray readJsonLine(int timeoutMs);

    QString repoRoot_;
    QProcess process_;
};

}  // namespace bf
