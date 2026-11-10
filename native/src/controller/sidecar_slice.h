#pragma once

#include "ipc/sidecar_client.h"
#include "render/waveform_slice.h"

namespace bf {

// 边车读取结果到公共信号窗口的转换。放在 controller 层，render 只保留数值窗口操作，
// 不为了一个转换函数去引用边车客户端。
WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice);

}  // namespace bf
