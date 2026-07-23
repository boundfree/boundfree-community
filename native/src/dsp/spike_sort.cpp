#include "dsp/spike_sort.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace bf::dsp {
namespace {

// 确定性 PRNG（线性同余），便于跨平台复现
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t s) : state(s == 0 ? 0x9E3779B97F4A7C15ULL : s) {}
    uint64_t next() {
        // xorshift64*
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    double unit() {
        // [0, 1) 双精度
        return static_cast<double>(next() >> 11) / static_cast<double>(1ULL << 53);
    }
    int range(int n) {
        if (n <= 0) {
            return 0;
        }
        return static_cast<int>(next() % static_cast<uint64_t>(n));
    }
};

// 对称矩阵 Jacobi 特征值分解。A: n×n 对称矩阵（按行存）。
// 输出 eigenvalues（升序排序）与 eigenvectors（列向量为特征向量，对应升序排列）。
// maxSweeps 控制最大扫描轮数；converged 返回是否收敛。
void jacobiEigen(std::vector<double>& A, int n,
                 std::vector<double>& eval,
                 std::vector<double>& evec,
                 int maxSweeps = 100) {
    eval.resize(n, 0.0);
    evec.assign(n * n, 0.0);
    for (int i = 0; i < n; ++i) {
        evec[i * n + i] = 1.0;
    }
    if (n == 1) {
        eval[0] = A[0];
        return;
    }
    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        // 计算非对角范数
        double off = 0.0;
        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                off += A[p * n + q] * A[p * n + q];
            }
        }
        if (off < 1e-30) {
            break;
        }
        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                double apq = A[p * n + q];
                if (std::fabs(apq) < 1e-300) {
                    continue;
                }
                double app = A[p * n + p];
                double aqq = A[q * n + q];
                double phi = 0.5 * std::atan2(2.0 * apq, aqq - app);
                double c = std::cos(phi);
                double s = std::sin(phi);
                // 旋转 A
                for (int k = 0; k < n; ++k) {
                    double akp = A[k * n + p];
                    double akq = A[k * n + q];
                    A[k * n + p] = c * akp + s * akq;
                    A[k * n + q] = -s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    double apk = A[p * n + k];
                    double aqk = A[q * n + k];
                    A[p * n + k] = c * apk + s * aqk;
                    A[q * n + k] = -s * apk + c * aqk;
                }
                // 旋转 eigenvectors
                for (int k = 0; k < n; ++k) {
                    double vkp = evec[k * n + p];
                    double vkq = evec[k * n + q];
                    evec[k * n + p] = c * vkp + s * vkq;
                    evec[k * n + q] = -s * vkp + c * vkq;
                }
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        eval[i] = A[i * n + i];
    }
    // 按特征值降序排列（取前若干主成分）
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) {
        idx[i] = i;
    }
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return eval[a] > eval[b];
    });
    std::vector<double> evalSorted(n), evecSorted(n * n);
    for (int i = 0; i < n; ++i) {
        evalSorted[i] = eval[idx[i]];
        for (int k = 0; k < n; ++k) {
            evecSorted[k * n + i] = evec[k * n + idx[i]];
        }
    }
    eval = evalSorted;
    evec = evecSorted;
}

// 简单 K-Means，初始中心用固定 seed 随机选取现有点
struct KMeansResult {
    std::vector<int> labels;
    std::vector<std::vector<double>> centers;
};

KMeansResult kMeans(const std::vector<std::vector<double>>& data,
                    int k, int maxIters, unsigned seed) {
    KMeansResult r;
    const int n = static_cast<int>(data.size());
    r.labels.assign(n, 0);
    if (n == 0 || k <= 0) {
        return r;
    }
    const int dim = static_cast<int>(data[0].size());
    if (k > n) {
        k = n;
    }
    r.centers.assign(k, std::vector<double>(dim, 0.0));
    Rng rng(seed);
    // k-means++ 初始化（确定性）
    int first = rng.range(n);
    for (int d = 0; d < dim; ++d) {
        r.centers[0][d] = data[first][d];
    }
    std::vector<double> minDist(n, std::numeric_limits<double>::infinity());
    for (int c = 1; c < k; ++c) {
        for (int i = 0; i < n; ++i) {
            double dd = 0.0;
            for (int d = 0; d < dim; ++d) {
                double t = data[i][d] - r.centers[c - 1][d];
                dd += t * t;
            }
            if (dd < minDist[i]) {
                minDist[i] = dd;
            }
        }
        double total = 0.0;
        for (int i = 0; i < n; ++i) {
            total += minDist[i];
        }
        int pick = 0;
        if (total <= 0.0) {
            pick = rng.range(n);
        } else {
            double target = rng.unit() * total;
            double acc = 0.0;
            pick = n - 1;
            for (int i = 0; i < n; ++i) {
                acc += minDist[i];
                if (acc >= target) {
                    pick = i;
                    break;
                }
            }
        }
        for (int d = 0; d < dim; ++d) {
            r.centers[c][d] = data[pick][d];
        }
    }

    std::vector<int> prevLabels(n, -1);
    for (int iter = 0; iter < maxIters; ++iter) {
        // 分配
        bool changed = false;
        for (int i = 0; i < n; ++i) {
            int best = 0;
            double bestDist = std::numeric_limits<double>::infinity();
            for (int c = 0; c < k; ++c) {
                double dd = 0.0;
                for (int d = 0; d < dim; ++d) {
                    double t = data[i][d] - r.centers[c][d];
                    dd += t * t;
                }
                if (dd < bestDist) {
                    bestDist = dd;
                    best = c;
                }
            }
            if (r.labels[i] != best) {
                changed = true;
                r.labels[i] = best;
            }
        }
        if (!changed && iter > 0) {
            break;
        }
        // 更新中心
        std::vector<std::vector<double>> newCenters(
            k, std::vector<double>(dim, 0.0));
        std::vector<int> counts(k, 0);
        for (int i = 0; i < n; ++i) {
            int c = r.labels[i];
            counts[c]++;
            for (int d = 0; d < dim; ++d) {
                newCenters[c][d] += data[i][d];
            }
        }
        for (int c = 0; c < k; ++c) {
            if (counts[c] > 0) {
                for (int d = 0; d < dim; ++d) {
                    newCenters[c][d] /= counts[c];
                }
                r.centers[c] = newCenters[c];
            } else {
                // 空簇：保留原中心
            }
        }
        prevLabels = r.labels;
    }
    return r;
}

}  // namespace

SpikeSortResult sortSpikes(const std::vector<std::vector<double>>& waveforms,
                           const SpikeSortConfig& cfg) {
    SpikeSortResult r;
    const int n = static_cast<int>(waveforms.size());
    if (n == 0) {
        return r;
    }
    const int snippetLen = static_cast<int>(waveforms[0].size());
    if (snippetLen == 0) {
        return r;
    }

    // n < nClusters：每个 spike 单独成簇
    if (n < cfg.nClusters) {
        r.labels.resize(n);
        for (int i = 0; i < n; ++i) {
            r.labels[i] = i;
        }
        r.pcaCoords.assign(n, std::vector<double>(
                                  static_cast<std::size_t>(cfg.nComponents), 0.0));
        return r;
    }

    // 1) 中心化：每列（样本点位置）减去该列均值
    std::vector<double> colMean(snippetLen, 0.0);
    for (const auto& wf : waveforms) {
        for (int j = 0; j < snippetLen; ++j) {
            colMean[j] += wf[static_cast<std::size_t>(j)];
        }
    }
    for (int j = 0; j < snippetLen; ++j) {
        colMean[static_cast<std::size_t>(j)] /= n;
    }
    std::vector<std::vector<double>> centered(n,
                                              std::vector<double>(snippetLen));
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < snippetLen; ++j) {
            centered[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                waveforms[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] -
                colMean[static_cast<std::size_t>(j)];
        }
    }

    // 2) 协方差矩阵（snippetLen × snippetLen，对称），1/(n-1) 归一
    const int dim = snippetLen;
    std::vector<double> cov(static_cast<std::size_t>(dim * dim), 0.0);
    for (int i = 0; i < dim; ++i) {
        for (int j = i; j < dim; ++j) {
            double s = 0.0;
            for (int k = 0; k < n; ++k) {
                s += centered[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)] *
                     centered[static_cast<std::size_t>(k)][static_cast<std::size_t>(j)];
            }
            s /= (n - 1 > 0) ? (n - 1) : 1;
            cov[static_cast<std::size_t>(i * dim + j)] = s;
            cov[static_cast<std::size_t>(j * dim + i)] = s;
        }
    }

    // 3) Jacobi 特征分解
    std::vector<double> eval;
    std::vector<double> evec;
    jacobiEigen(cov, dim, eval, evec);

    // 取前 nComponents（特征值已降序）
    int nComp = std::min(cfg.nComponents, dim);
    // 4) 投影得到 pcaCoords
    r.pcaCoords.assign(n, std::vector<double>(nComp, 0.0));
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < nComp; ++c) {
            double v = 0.0;
            for (int d = 0; d < dim; ++d) {
                v += centered[static_cast<std::size_t>(i)][static_cast<std::size_t>(d)] *
                     evec[static_cast<std::size_t>(d * dim + c)];
            }
            r.pcaCoords[static_cast<std::size_t>(i)][static_cast<std::size_t>(c)] = v;
        }
    }

    // 5) K-Means 聚类
    KMeansResult km = kMeans(r.pcaCoords, cfg.nClusters, cfg.maxIters, cfg.seed);
    r.labels = std::move(km.labels);

    return r;
}

}  // namespace bf::dsp
