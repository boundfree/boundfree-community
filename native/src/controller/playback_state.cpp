#include "controller/playback_state.h"

#include <algorithm>
#include <cmath>

namespace bf {
namespace {

double clampPosition(double position, double duration) {
    return std::clamp(std::isfinite(position) ? position : 0.0, 0.0, duration);
}

bool isSupportedSpeed(double speed) {
    return std::find(PlaybackState::kSpeeds.begin(), PlaybackState::kSpeeds.end(), speed) != PlaybackState::kSpeeds.end();
}

}  // namespace

void PlaybackState::setDuration(double seconds) {
    duration_ = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
    position_ = clampPosition(position_, duration_);
    if (duration_ <= 0.0) {
        playing_ = false;
    }
}

void PlaybackState::seek(double seconds) {
    position_ = clampPosition(seconds, duration_);
}

void PlaybackState::setSpeed(double speed) {
    if (isSupportedSpeed(speed)) {
        speed_ = speed;
    }
}

void PlaybackState::play() {
    if (duration_ > 0.0) {
        playing_ = true;
    }
}

void PlaybackState::pause() {
    playing_ = false;
}

void PlaybackState::advance(double elapsedSeconds) {
    if (!playing_) {
        return;
    }
    position_ = clampPosition(position_ + std::max(0.0, elapsedSeconds) * speed_, duration_);
    if (position_ >= duration_) {
        position_ = 0.0;
        playing_ = false;
    }
}

}  // namespace bf
