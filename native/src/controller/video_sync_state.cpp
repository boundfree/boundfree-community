#include "controller/video_sync_state.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace bf {

void VideoSyncState::setDurationMs(std::int64_t durationMs) {
    durationMs_ = std::max<std::int64_t>(0, durationMs);
}

void VideoSyncState::setOffsetSeconds(double offsetSeconds) {
    offsetSeconds_ = std::isfinite(offsetSeconds) ? offsetSeconds : 0.0;
}

std::int64_t VideoSyncState::videoPositionMs(double eegTimeSeconds) const {
    if (!std::isfinite(eegTimeSeconds)) {
        eegTimeSeconds = 0.0;
    }
    const double milliseconds = (eegTimeSeconds - offsetSeconds_) * 1000.0;
    if (milliseconds >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (milliseconds <= static_cast<double>(std::numeric_limits<std::int64_t>::min())) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return static_cast<std::int64_t>(milliseconds);
}

VideoSyncCommand VideoSyncState::sync(
    double eegTimeSeconds,
    bool eegPlaying,
    std::int64_t playerPositionMs,
    bool playerPlaying) {
    const std::int64_t targetMs = videoPositionMs(eegTimeSeconds);
    const bool outsideVideo = targetMs < 0 || (durationMs_ > 0 && targetMs >= durationMs_);
    if (outsideVideo) {
        const bool enteringBlank = !blank_;
        blank_ = true;
        return {
            true,
            playerPlaying || enteringBlank,
            false,
            enteringBlank,
            0,
        };
    }

    const bool leavingBlank = blank_;
    blank_ = false;
    const std::int64_t driftMs = playerPositionMs - targetMs;
    const std::int64_t absoluteDriftMs = driftMs == std::numeric_limits<std::int64_t>::min()
        ? std::numeric_limits<std::int64_t>::max()
        : std::abs(driftMs);
    if (eegPlaying) {
        return {
            false,
            false,
            !playerPlaying,
            leavingBlank || absoluteDriftMs > kPlayingResyncThresholdMs,
            targetMs,
        };
    }
    return {
        false,
        playerPlaying,
        false,
        leavingBlank || absoluteDriftMs > kPausedResyncThresholdMs,
        targetMs,
    };
}

}  // namespace bf
