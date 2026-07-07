#pragma once

#include <QString>
#include <QStringList>

namespace bf {

struct FileSession {
    QString handle;
    QString format;
    double fs = 0.0;
    int nChannels = 0;
    int nSamples = 0;
    double duration = 0.0;
    QStringList channelNames;
};

QString formatChannelList(const QStringList& channels);

}  // namespace bf
