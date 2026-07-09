#pragma once

#include <atomic>

namespace bf {

class RequestGeneration {
public:
    int next();
    bool isCurrent(int generation) const;
    void invalidate();
    int current() const;

private:
    std::atomic<int> current_{0};
};

}  // namespace bf
