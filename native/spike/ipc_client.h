#pragma once

#include <QProcess>
#include <QString>

#include <vector>

struct SliceResult {
    int nCh = 0;
    int nSamp = 0;
    std::vector<float> uv;
};

class SidecarClient {
public:
    bool start(const QString& python, const QString& script);
    bool open(const QString& path, QString* metaJsonOut);
    bool getSlice(double t0, double t1, SliceResult* out);
    void stop();

private:
    QProcess proc_;

    bool writeJson(const QByteArray& json);
    QByteArray readFrame(int timeoutMs = 30000);
};
