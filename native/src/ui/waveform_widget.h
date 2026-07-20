#pragma once

#include "render/waveform_slice.h"

#include <QWidget>

#include <optional>

namespace bf {

class WaveformWidget : public QWidget {
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setSlice(const WaveformSlice& slice);
    void setViewState(const WaveformViewState& state);
    const WaveformDisplayModel& cachedDisplayModel(int maxVisibleChannels = 32) const;
    int displayModelBuildCount() const { return displayModelBuildCount_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void invalidateDisplayModelCache();
    void ensureDisplayModel(int maxVisibleChannels) const;

    WaveformSlice slice_;
    WaveformViewState viewState_;
    mutable std::optional<WaveformDisplayModel> cachedModel_;
    mutable int cachedMaxVisibleChannels_ = -1;
    mutable int displayModelBuildCount_ = 0;
};

}  // namespace bf
