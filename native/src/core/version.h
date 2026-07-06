#pragma once

#include <QString>

namespace bf {

struct VersionInfo {
    int schemaVersion = 0;
    QString productName;
    QString version;
    QString releaseChannel;
    int ipcProtocolVersion = 0;

    QString displayString() const;
};

class VersionService {
public:
    explicit VersionService(QString repoRoot);

    const QString& repoRoot() const;
    bool load(VersionInfo* out, QString* errorMessage = nullptr) const;

    static QString detectRepoRoot();

private:
    QString repoRoot_;
};

}  // namespace bf
