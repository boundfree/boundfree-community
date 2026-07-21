#pragma once

#include <cstdint>

namespace bf {

struct VideoSyncCommand final {
    bool blank = false;
    bool pause = false;
    bool play = false;
    bool seek = false;
    std::int64_t seekPositionMs = 0;
};

class VideoSyncState final {
public:
    static constexpr std::int64_t kPlayingResyncThresholdMs = 400;
    static constexpr std::int64_t kPausedResyncThresholdMs = 50;

    void setDurationMs(std::int64_t durationMs);
    void setOffsetSeconds(double offsetSeconds);

    std::int64_t durationMs() const { return durationMs_; }
    double offsetSeconds() const { return offsetSeconds_; }
    bool isBlank() const { return blank_; }

    std::int64_t videoPositionMs(double eegTimeSeconds) const;
    VideoSyncCommand sync(
        double eegTimeSeconds,
        bool eegPlaying,
        std::int64_t playerPositionMs,
        bool playerPlaying);

private:
    std::int64_t durationMs_ = 0;
    double offsetSeconds_ = 0.0;
    bool blank_ = false;
};

}  // namespace bf
