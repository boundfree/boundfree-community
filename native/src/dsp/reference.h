#pragma once

#include "dsp/types.h"

#include <string>
#include <vector>

namespace bf {
namespace dsp {

Matrix applyReference(const Matrix& data, ReferenceMode mode);
std::vector<std::string> effectiveChannelNames(
    const std::vector<std::string>& channelNames,
    ReferenceMode mode,
    int nOut = -1);

}  // namespace dsp
}  // namespace bf
