#include "render/minmax_decimator.h"

#include <QStringList>
#include <algorithm>

namespace bf {

std::vector<MinMaxBucket> minMaxDecimate(const std::vector<float>& samples, int bucketSize) {
    std::vector<MinMaxBucket> buckets;
    if (bucketSize <= 0) {
        return buckets;
    }
    for (int start = 0, bucket = 0; start < static_cast<int>(samples.size()); start += bucketSize, ++bucket) {
        const int stop = std::min(start + bucketSize, static_cast<int>(samples.size()));
        auto minmax = std::minmax_element(samples.begin() + start, samples.begin() + stop);
        buckets.push_back(MinMaxBucket{bucket, *minmax.first, *minmax.second});
    }
    return buckets;
}

QString formatMinMaxBuckets(const std::vector<MinMaxBucket>& buckets) {
    QStringList parts;
    for (const MinMaxBucket& bucket : buckets) {
        parts.push_back(QString("%1:%2,%3").arg(bucket.bucket).arg(bucket.min).arg(bucket.max));
    }
    return parts.join(" ");
}

}  // namespace bf
