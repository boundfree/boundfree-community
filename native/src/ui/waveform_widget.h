#pragma once

#include "render/waveform_slice.h"

#include <QWidget>

namespace bf {

class WaveformWidget : public QWidget {
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setSlice(const WaveformSlice& slice);
    void setViewState(const WaveformViewState& state);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    WaveformSlice slice_;
    WaveformViewState viewState_;
};

}  // namespace bf
