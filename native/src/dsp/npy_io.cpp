#include "dsp/npy_io.h"

#include <cstdint>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <vector>

namespace bf {
namespace dsp {
namespace {

struct NpyHeader {
    int rows = 0;
    int cols = 0;
    bool fortran = false;
    std::string descr;
};

NpyHeader parseHeader(const std::string& header) {
    NpyHeader out;
    if (header.find("'<f8'") == std::string::npos && header.find("|<f8|") == std::string::npos
        && header.find("descr': '<f8'") == std::string::npos) {
        // allow descr': '<f8'
        if (header.find("<f8") == std::string::npos) {
            throw std::runtime_error("npy: only little-endian float64 supported");
        }
    }
    out.fortran = header.find("fortran_order': True") != std::string::npos
        || header.find("fortran_order':True") != std::string::npos;

    std::smatch match;
    const std::regex shape2(R"(\((\d+)\s*,\s*(\d+)\s*\))");
    const std::regex shape1(R"(\((\d+)\s*,\s*\))");
    if (std::regex_search(header, match, shape2)) {
        out.rows = std::stoi(match[1]);
        out.cols = std::stoi(match[2]);
    } else if (std::regex_search(header, match, shape1)) {
        out.rows = std::stoi(match[1]);
        out.cols = 1;
    } else {
        throw std::runtime_error("npy: unsupported shape header: " + header);
    }
    return out;
}

}  // namespace

std::pair<int, int> npyShape(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("npy: cannot open " + path);
    }
    char magic[6];
    in.read(magic, 6);
    if (!in || magic[0] != '\x93' || std::string(magic + 1, magic + 6) != "NUMPY") {
        throw std::runtime_error("npy: bad magic");
    }
    unsigned char ver[2];
    in.read(reinterpret_cast<char*>(ver), 2);
    std::uint16_t headerLen = 0;
    if (ver[0] == 1) {
        in.read(reinterpret_cast<char*>(&headerLen), 2);
    } else {
        std::uint32_t len32 = 0;
        in.read(reinterpret_cast<char*>(&len32), 4);
        headerLen = static_cast<std::uint16_t>(len32);
    }
    std::string header(headerLen, '\0');
    in.read(header.data(), headerLen);
    const NpyHeader meta = parseHeader(header);
    return {meta.rows, meta.cols};
}

Matrix loadNpyFloat64(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("npy: cannot open " + path);
    }
    char magic[6];
    in.read(magic, 6);
    if (!in || magic[0] != '\x93' || std::string(magic + 1, magic + 6) != "NUMPY") {
        throw std::runtime_error("npy: bad magic");
    }
    unsigned char ver[2];
    in.read(reinterpret_cast<char*>(ver), 2);
    std::uint32_t headerLen = 0;
    if (ver[0] == 1) {
        std::uint16_t len16 = 0;
        in.read(reinterpret_cast<char*>(&len16), 2);
        headerLen = len16;
    } else {
        in.read(reinterpret_cast<char*>(&headerLen), 4);
    }
    std::string header(headerLen, '\0');
    in.read(header.data(), static_cast<std::streamsize>(headerLen));
    const NpyHeader meta = parseHeader(header);
    if (meta.fortran) {
        throw std::runtime_error("npy: fortran_order not supported");
    }

    const std::size_t count = static_cast<std::size_t>(meta.rows) * static_cast<std::size_t>(meta.cols);
    std::vector<double> flat(count);
    in.read(reinterpret_cast<char*>(flat.data()), static_cast<std::streamsize>(count * sizeof(double)));
    if (!in) {
        throw std::runtime_error("npy: truncated payload");
    }

    if (meta.cols == 1 && header.find(",)") != std::string::npos) {
        // 1D vector -> single row for generic handling? Callers for sos expect rows=sections.
        // Represent as rows x 1 only if shape (n,); but sos is (n,6). For 1D (n,) use rows=n cols=1.
        Matrix out = makeMatrix(static_cast<std::size_t>(meta.rows), 1);
        for (int i = 0; i < meta.rows; ++i) {
            out[static_cast<std::size_t>(i)][0] = flat[static_cast<std::size_t>(i)];
        }
        return out;
    }

    Matrix out = makeMatrix(static_cast<std::size_t>(meta.rows), static_cast<std::size_t>(meta.cols));
    for (int r = 0; r < meta.rows; ++r) {
        for (int c = 0; c < meta.cols; ++c) {
            out[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] =
                flat[static_cast<std::size_t>(r) * static_cast<std::size_t>(meta.cols) + static_cast<std::size_t>(c)];
        }
    }
    return out;
}

}  // namespace dsp
}  // namespace bf
