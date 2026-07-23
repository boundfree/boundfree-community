// Spike DSP deterministic golden baseline + structural correctness test.
//
// Two-layer golden, matching meta.json scheme:
//   1. Self-consistent regression (detect_timestamps.npy, cluster_sizes.npy,
//      sort_labels.npy, snippet_len): on first run, if these files are absent
//      the test writes them (freeze mode) and prints a notice; subsequent runs
//      compare the engine output against the frozen files byte-for-byte (same
//      values, since the engine is deterministic). sortSpikes uses a hand-written
//      Jacobi eigensolver + custom PRNG (seed=42), so it is NOT aligned with
//      sklearn/scipy; only self-consistency is frozen.
//   2. Structural correctness (always asserted): detect count matches the
//      ground-truth injected peak count with >=95% match rate within +/-2
//      samples; sort cluster purity >=0.8 against ground-truth template labels.
//
// Usage: spike_golden_test [tests/golden/spike]
// Prints spike-golden-test-ok (return 0) on success, spike-golden-test-failed
// (return 1) on any failure.
//
// All fs / window / threshold / sort parameters are read from meta.json; no
// hardcoding of 30000 Hz or fixed sample counts inside the test logic.

#include "dsp/npy_io.h"
#include "dsp/spike_detect.h"
#include "dsp/spike_sort.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---- Minimal meta.json scalar extractor (no JSON library dependency) -------
// Reads the whole meta.json text and pulls the first numeric value following a
// "key": token. Sufficient for the flat scalar fields this test needs; does not
// parse arrays or nested objects (ground-truth peaks are read from the injected
// list via a dedicated parser below).

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Extract the first number after `"key"` : ... in the text. Supports ints and
// doubles (including negatives and scientific notation). Returns 0.0 if absent.
double extractNumber(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t k = text.find(needle);
    if (k == std::string::npos) {
        return 0.0;
    }
    // scan forward past the colon to the first digit / minus / dot
    std::size_t i = k + needle.size();
    while (i < text.size() && text[i] != ':') {
        ++i;
    }
    if (i == std::string::npos) {
        return 0.0;
    }
    ++i;  // skip ':'
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
        ++i;
    }
    std::size_t start = i;
    while (i < text.size()) {
        char c = text[i];
        const bool isNumChar = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
        if (!isNumChar) {
            break;
        }
        ++i;
    }
    if (i == start) {
        return 0.0;
    }
    try {
        return std::stod(text.substr(start, i - start));
    } catch (...) {
        return 0.0;
    }
}

bool extractBool(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t k = text.find(needle);
    if (k == std::string::npos) {
        return false;
    }
    std::size_t i = k + needle.size();
    while (i < text.size() && text[i] != ':') {
        ++i;
    }
    if (i == text.size()) {
        return false;
    }
    ++i;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
        ++i;
    }
    if (i + 4 <= text.size() && text.compare(i, 4, "true") == 0) {
        return true;
    }
    return false;
}

// Parse the "peaks" array entries: each is {"peak_sample": <int>, "template_label": <int>}.
struct GroundTruthPeak {
    int peakSample = 0;
    int templateLabel = 0;
};

std::vector<GroundTruthPeak> parseGroundTruthPeaks(const std::string& text) {
    std::vector<GroundTruthPeak> peaks;
    const std::string key = "\"peaks\"";
    std::size_t k = text.find(key);
    if (k == std::string::npos) {
        return peaks;
    }
    // find the opening '[' for the peaks array
    std::size_t bracket = text.find('[', k);
    if (bracket == std::string::npos) {
        return peaks;
    }
    // walk object-by-object until the matching ']'
    std::size_t i = bracket + 1;
    while (i < text.size()) {
        char c = text[i];
        if (c == ']') {
            break;
        }
        if (c == '{') {
            // find matching close of this object
            std::size_t objEnd = text.find('}', i);
            if (objEnd == std::string::npos) {
                break;
            }
            std::string obj = text.substr(i, objEnd - i + 1);
            GroundTruthPeak p;
            p.peakSample = static_cast<int>(extractNumber(obj, "peak_sample"));
            p.templateLabel = static_cast<int>(extractNumber(obj, "template_label"));
            peaks.push_back(p);
            i = objEnd + 1;
        } else {
            ++i;
        }
    }
    return peaks;
}

// ---- .npy writer (little-endian float64, C-order) --------------------------
// Matches npy_io.h reader: writes a 1D vector as shape (N,) so loadNpyFloat64
// returns rows=N, cols=1.

void writeNpyFloat64(const std::string& path, const std::vector<double>& flat, int rows, int cols) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot write " + path);
    }
    // Build header dict. shape is (rows, cols) for 2D or (rows,) for 1D.
    std::string shapeStr;
    if (cols <= 1) {
        shapeStr = "(" + std::to_string(rows) + ",)";
    } else {
        shapeStr = "(" + std::to_string(rows) + ", " + std::to_string(cols) + ")";
    }
    std::string dict = "{'descr': '<f8', 'fortran_order': False, 'shape': " + shapeStr + ", }";
    // npy v1.0 header: magic + version(2) + header_len(2, little-endian) + header
    // header must be padded so total preamble is multiple of 64, ending with '\n'.
    std::size_t preamble = 6 + 2 + 2 + dict.size() + 1;  // +1 for trailing '\n'
    std::size_t pad = (64 - (preamble % 64)) % 64;
    dict.append(pad, ' ');
    dict.push_back('\n');
    const std::uint16_t headerLen = static_cast<std::uint16_t>(dict.size());
    char magic[6] = {'\x93', 'N', 'U', 'M', 'P', 'Y'};
    out.write(magic, 6);
    const char ver[2] = {1, 0};
    out.write(ver, 2);
    out.write(reinterpret_cast<const char*>(&headerLen), 2);
    out.write(dict.data(), static_cast<std::streamsize>(dict.size()));
    const std::size_t count = flat.size();
    out.write(reinterpret_cast<const char*>(flat.data()),
              static_cast<std::streamsize>(count * sizeof(double)));
}

// ---- Comparison helpers ----------------------------------------------------

bool loadGoldenVector(const std::string& path, std::vector<double>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;  // absent
    }
    in.close();
    bf::dsp::Matrix m = bf::dsp::loadNpyFloat64(path);
    out->clear();
    for (const auto& row : m) {
        out->insert(out->end(), row.begin(), row.end());
    }
    return true;
}

bool loadGoldenIntVector(const std::string& path, std::vector<int>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.close();
    bf::dsp::Matrix m = bf::dsp::loadNpyFloat64(path);
    out->clear();
    for (const auto& row : m) {
        for (double v : row) {
            out->push_back(static_cast<int>(std::llround(v)));
        }
    }
    return true;
}

void writeGoldenVector(const std::string& path, const std::vector<double>& v) {
    writeNpyFloat64(path, v, static_cast<int>(v.size()), 1);
}

void writeGoldenIntVector(const std::string& path, const std::vector<int>& v) {
    std::vector<double> dv;
    dv.reserve(v.size());
    for (int x : v) {
        dv.push_back(static_cast<double>(x));
    }
    writeNpyFloat64(path, dv, static_cast<int>(dv.size()), 1);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string goldenDir = (argc > 1) ? argv[1] : std::string("tests/golden/spike");
    int failures = 0;

    try {
        const std::string metaText = readFile(goldenDir + "/meta.json");

        // ---- Build configs from meta.json (no hardcoding of 30000) ----
        bf::dsp::SpikeDetectConfig dcfg;
        dcfg.fs = extractNumber(metaText, "fs");
        dcfg.thresholdMad = extractNumber(metaText, "threshold_mad");
        dcfg.useAbsoluteUv = extractBool(metaText, "use_absolute_uv");
        dcfg.absoluteThresholdUv = extractNumber(metaText, "absolute_threshold_uv");
        dcfg.detectNegative = extractBool(metaText, "detect_negative");
        dcfg.refractoryMs = extractNumber(metaText, "refractory_ms");
        dcfg.preMs = extractNumber(metaText, "pre_ms");
        dcfg.postMs = extractNumber(metaText, "post_ms");

        bf::dsp::SpikeSortConfig scfg;
        scfg.nComponents = static_cast<int>(extractNumber(metaText, "n_components"));
        scfg.nClusters = static_cast<int>(extractNumber(metaText, "n_clusters"));
        scfg.maxIters = static_cast<int>(extractNumber(metaText, "max_iters"));
        scfg.seed = static_cast<unsigned>(std::llround(extractNumber(metaText, "seed")));

        const double tolSamples = extractNumber(metaText, "detect_match_samples");
        const double minMatchRate = extractNumber(metaText, "detect_match_rate_min");
        const double minPurity = extractNumber(metaText, "sort_purity_min");
        const int expectedSnippetLen = static_cast<int>(extractNumber(metaText, "snippet_len"));
        const int nInjected = static_cast<int>(extractNumber(metaText, "n_injected"));

        if (dcfg.fs <= 0.0) {
            std::cerr << "FAIL: meta.json fs invalid\n";
            std::cout << "spike-golden-test-failed\n";
            return 1;
        }

        // ---- Load input signal ([1, N]) ----
        const bf::dsp::Matrix inputMat = bf::dsp::loadNpyFloat64(goldenDir + "/input_signal.npy");
        std::vector<double> signal;
        for (const auto& row : inputMat) {
            signal.insert(signal.end(), row.begin(), row.end());
        }
        const double fs = dcfg.fs;

        // ---- Run detectSpikes ----
        bf::dsp::DetectedSpikes det = bf::dsp::detectSpikes(signal, dcfg);

        // ---- Ground truth (structural correctness) ----
        std::vector<GroundTruthPeak> gtPeaks = parseGroundTruthPeaks(metaText);

        // ===== Layer 2: structural correctness =====
        // Detect count vs ground truth
        int matched = 0;
        const double tolSec = tolSamples / fs;
        for (const auto& gt : gtPeaks) {
            double expected = static_cast<double>(gt.peakSample) / fs;
            for (double got : det.timestamps) {
                if (std::fabs(got - expected) <= tolSec) {
                    ++matched;
                    break;
                }
            }
        }
        const double matchRate = gtPeaks.empty() ? 1.0
            : static_cast<double>(matched) / static_cast<double>(gtPeaks.size());
        std::cout << "structural.detect: matched=" << matched << "/" << gtPeaks.size()
                  << " matchRate=" << matchRate << " (min " << minMatchRate << ")\n";
        if (static_cast<int>(det.timestamps.size()) != nInjected) {
            std::cerr << "FAIL detect: count " << det.timestamps.size()
                      << " != ground-truth " << nInjected << "\n";
            ++failures;
        } else if (matchRate < minMatchRate) {
            std::cerr << "FAIL detect: matchRate " << matchRate << " < " << minMatchRate << "\n";
            ++failures;
        }

        // snippetLen
        std::cout << "structural.snippet_len: got=" << det.snippetLen
                  << " expected=" << expectedSnippetLen << "\n";
        if (det.snippetLen != expectedSnippetLen) {
            std::cerr << "FAIL snippetLen: " << det.snippetLen << " != " << expectedSnippetLen << "\n";
            ++failures;
        }

        // ---- Run sortSpikes on detected waveforms ----
        bf::dsp::SpikeSortResult srt = bf::dsp::sortSpikes(det.waveforms, scfg);

        // Map each detected timestamp to a ground-truth template label by nearest
        // ground-truth peak (within tolerance). Spikes with no match are excluded
        // from purity (they shouldn't exist if detect is correct).
        std::vector<int> trueLabels;
        trueLabels.reserve(det.timestamps.size());
        for (double got : det.timestamps) {
            int bestLabel = -1;
            double bestDist = std::numeric_limits<double>::infinity();
            for (const auto& gt : gtPeaks) {
                double expected = static_cast<double>(gt.peakSample) / fs;
                double d = std::fabs(got - expected);
                if (d <= tolSec && d < bestDist) {
                    bestDist = d;
                    bestLabel = gt.templateLabel;
                }
            }
            trueLabels.push_back(bestLabel);
        }

        // Purity: for each cluster, majority true-label; overall accuracy.
        // Only consider spikes with a valid true-label (>=0).
        int nValid = 0;
        int nCorrect = 0;
        for (std::size_t i = 0; i < srt.labels.size(); ++i) {
            if (i >= trueLabels.size() || trueLabels[i] < 0) {
                continue;
            }
            ++nValid;
        }
        if (nValid > 0) {
            // Determine cluster -> majority class mapping.
            const int nClasses = 2;  // ground-truth template classes A/B
            std::vector<std::vector<int>> counts(scfg.nClusters, std::vector<int>(nClasses, 0));
            for (std::size_t i = 0; i < srt.labels.size(); ++i) {
                if (trueLabels[i] < 0 || trueLabels[i] >= nClasses) {
                    continue;
                }
                if (srt.labels[i] >= 0 && srt.labels[i] < scfg.nClusters) {
                    counts[srt.labels[i]][trueLabels[i]]++;
                }
            }
            std::vector<int> clusterToClass(scfg.nClusters, 0);
            for (int c = 0; c < scfg.nClusters; ++c) {
                clusterToClass[c] = (counts[c][1] > counts[c][0]) ? 1 : 0;
            }
            for (std::size_t i = 0; i < srt.labels.size(); ++i) {
                if (trueLabels[i] < 0 || trueLabels[i] >= nClasses) {
                    continue;
                }
                if (srt.labels[i] >= 0 && srt.labels[i] < scfg.nClusters
                    && clusterToClass[srt.labels[i]] == trueLabels[i]) {
                    ++nCorrect;
                }
            }
        }
        const double purity = nValid > 0 ? static_cast<double>(nCorrect) / static_cast<double>(nValid) : 0.0;
        std::cout << "structural.sort: purity=" << purity << " (min " << minPurity << ")"
                  << " nValid=" << nValid << " nClusters=" << scfg.nClusters << "\n";
        if (purity < minPurity) {
            std::cerr << "FAIL sort purity: " << purity << " < " << minPurity << "\n";
            ++failures;
        }

        // ===== Layer 1: self-consistent regression golden =====
        // detect_timestamps.npy
        const std::string tsPath = goldenDir + "/detect_timestamps.npy";
        std::vector<double> goldenTs;
        if (loadGoldenVector(tsPath, &goldenTs)) {
            bool tsOk = (goldenTs.size() == det.timestamps.size());
            double maxErr = 0.0;
            if (tsOk) {
                for (std::size_t i = 0; i < goldenTs.size(); ++i) {
                    double e = std::fabs(goldenTs[i] - det.timestamps[i]);
                    if (e > maxErr) {
                        maxErr = e;
                    }
                }
                tsOk = (maxErr == 0.0);  // exact: deterministic engine
            }
            std::cout << "regression.detect_timestamps: size=" << det.timestamps.size()
                      << " maxAbsErr=" << maxErr << (tsOk ? " OK" : " MISMATCH") << "\n";
            if (!tsOk) {
                std::cerr << "FAIL regression detect_timestamps mismatch (maxErr=" << maxErr << ")\n";
                ++failures;
            }
        } else {
            std::cout << "regression.detect_timestamps: ABSENT, freezing now\n";
            writeGoldenVector(tsPath, det.timestamps);
        }

        // cluster_sizes.npy (size of each cluster)
        std::vector<int> clusterSizes(scfg.nClusters, 0);
        for (int lbl : srt.labels) {
            if (lbl >= 0 && lbl < scfg.nClusters) {
                ++clusterSizes[lbl];
            }
        }
        const std::string csPath = goldenDir + "/cluster_sizes.npy";
        std::vector<int> goldenCs;
        if (loadGoldenIntVector(csPath, &goldenCs)) {
            bool csOk = (goldenCs.size() == clusterSizes.size());
            int diff = 0;
            if (csOk) {
                for (std::size_t i = 0; i < goldenCs.size(); ++i) {
                    if (goldenCs[i] != clusterSizes[i]) {
                        ++diff;
                    }
                }
                csOk = (diff == 0);
            }
            std::cout << "regression.cluster_sizes:";
            for (int s : clusterSizes) {
                std::cout << " " << s;
            }
            std::cout << (csOk ? " OK" : " MISMATCH") << "\n";
            if (!csOk) {
                std::cerr << "FAIL regression cluster_sizes mismatch\n";
                ++failures;
            }
        } else {
            std::cout << "regression.cluster_sizes: ABSENT, freezing now\n";
            writeGoldenIntVector(csPath, clusterSizes);
        }

        // sort_labels.npy
        const std::string lbPath = goldenDir + "/sort_labels.npy";
        std::vector<int> goldenLb;
        if (loadGoldenIntVector(lbPath, &goldenLb)) {
            bool lbOk = (goldenLb.size() == srt.labels.size());
            int diff = 0;
            if (lbOk) {
                for (std::size_t i = 0; i < goldenLb.size(); ++i) {
                    if (goldenLb[i] != srt.labels[i]) {
                        ++diff;
                    }
                }
                lbOk = (diff == 0);
            }
            std::cout << "regression.sort_labels: size=" << srt.labels.size()
                      << " diffs=" << diff << (lbOk ? " OK" : " MISMATCH") << "\n";
            if (!lbOk) {
                std::cerr << "FAIL regression sort_labels mismatch (diffs=" << diff << ")\n";
                ++failures;
            }
        } else {
            std::cout << "regression.sort_labels: ABSENT, freezing now\n";
            writeGoldenIntVector(lbPath, srt.labels);
        }
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION " << ex.what() << "\n";
        std::cout << "spike-golden-test-failed\n";
        return 2;
    }

    if (failures == 0) {
        std::cout << "spike-golden-test-ok failures=0\n";
        return 0;
    }
    std::cout << "spike-golden-test-failed failures=" << failures << "\n";
    return 1;
}
