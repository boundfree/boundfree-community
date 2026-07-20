#pragma once

#include "dsp/types.h"

#include <string>
#include <utility>
#include <vector>

namespace bf {
namespace dsp {

// Load little-endian float64 C-order .npy with shape (rows, cols) or (rows,).
// For 1D, returns a single-row matrix.
Matrix loadNpyFloat64(const std::string& path);

// Load shape metadata: returns {rows, cols}; cols=1 for 1D.
std::pair<int, int> npyShape(const std::string& path);

}  // namespace dsp
}  // namespace bf
