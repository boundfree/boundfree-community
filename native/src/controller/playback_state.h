#pragma once

#include <array>

namespace bf {

class PlaybackState final {
public:
    static constexpr std::array<double, 5> kSpeeds{0.25, 0.5, 1.0, 2.0, 4.0};

    void setDuration(double seconds);
    void seek(double seconds);
    void setSpeed(double speed);
    void play();
    void pause();
    void advance(double elapsedSeconds);

    bool isPlaying() const { return playing_; }
    double position() const { return position_; }
    double duration() const { return duration_; }
    double speed() const { return speed_; }

private:
    double duration_ = 0.0;
    double position_ = 0.0;
    double speed_ = 1.0;
    bool playing_ = false;
};

}  // namespace bf
