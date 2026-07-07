#pragma once

#include <QString>
#include <vector>

namespace bf {

struct MinMaxBucket {
    int bucket = 0;
    float min = 0.0f;
    float max = 0.0f;
};

std::vector<MinMaxBucket> minMaxDecimate(const std::vector<float>& samples, int bucketSize);
QString formatMinMaxBuckets(const std::vector<MinMaxBucket>& buckets);

}  // namespace bf
