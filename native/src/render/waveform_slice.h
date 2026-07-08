#pragma once

#include "ipc/sidecar_client.h"

#include <QStringList>
#include <vector>

namespace bf {

struct WaveformSlice {
    double fs = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
    QString unit = "uV";
    QStringList channelNames;
    std::vector<std::vector<float>> data;
};

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice);

}  // namespace bf
