#include "render/waveform_slice.h"

namespace bf {

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice) {
    WaveformSlice result;
    result.fs = slice.fs;
    result.t0 = slice.t0;
    result.t1 = slice.t1;
    result.unit = slice.unit;
    result.channelNames = session.chNames;
    result.data = slice.data;
    return result;
}

}  // namespace bf
