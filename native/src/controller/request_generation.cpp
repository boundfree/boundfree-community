#include "controller/request_generation.h"

#include <limits>

namespace bf {

int RequestGeneration::next() {
    return current_.fetch_add(1) + 1;
}

bool RequestGeneration::isCurrent(int generation) const {
    return generation == current_.load();
}

void RequestGeneration::invalidate() {
    current_.store(std::numeric_limits<int>::max());
}

int RequestGeneration::current() const {
    return current_.load();
}

}  // namespace bf
