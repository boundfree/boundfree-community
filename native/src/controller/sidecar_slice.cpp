#include "controller/sidecar_slice.h"

#include <QJsonArray>

namespace bf {

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice) {
    WaveformSlice result;
    result.fs = slice.fs;
    result.t0 = slice.t0;
    result.t1 = slice.t1;
    result.unit = slice.unit;
    result.channelNames = session.chNames;
    result.data = slice.data;
    result.channelUnits = slice.channelUnits;
    for(const auto& id:session.capture.value("channel_ids").toArray())result.channelIds<<id.toString();
    if(result.channelIds.isEmpty())for(int c=0;c<session.chNames.size();++c)result.channelIds<<session.path+"/"+QString::number(c);
    result.sourceId=session.path;result.streamId=session.capture.value("stream_key").toString();
    result.metadata=session.signalMetadata;
    for(auto i=session.capture.begin();i!=session.capture.end();++i)result.metadata.insert(i.key(),i.value());
    result.simulated=result.metadata.value("simulated").toBool();result.metadata.insert("format",session.format);
    result.metadata.insert("source_unit",session.unit);result.metadata.insert("projection_dtype","float32");
    if (!slice.sampleTimes.empty()) result.sampleTimes.assign(result.data.size(), slice.sampleTimes);
    return result;
}

}  // namespace bf
