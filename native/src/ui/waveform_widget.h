#pragma once

#include "render/waveform_slice.h"

#include <QWidget>

namespace bf {

class WaveformWidget : public QWidget {
public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setSlice(const WaveformSlice& slice);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    WaveformSlice slice_;
};

}  // namespace bf
