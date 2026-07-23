#pragma once

#include "dsp/spike_types.h"

#include <QWidget>

namespace bf {

// 4 个纯 QPainter 自绘的 Spike 可视化控件，消费 bf::dsp::SpikeAnalysis。
// 设计与 ui/analysis_widgets.{h,cpp} 保持一致：final QWidget 子类、无 Q_OBJECT、
// setDarkTheme + paintEvent、emptyLabel 空状态、kLeft/kTop/... 边距与 plotRect 映射。
// 所有时间/幅值映射均从 SpikeAnalysis::fs/duration/snippetLen/preMs/postMs 推导，
// 禁止硬编码采样率或时长。

class SpikeClusterWidget final : public QWidget {
public:
    explicit SpikeClusterWidget(QWidget* parent = nullptr);
    void setAnalysis(const dsp::SpikeAnalysis& analysis);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::SpikeAnalysis analysis_;
    QString emptyLabel_ = "No spike data";
    bool darkTheme_ = true;
};

class SpikeRasterWidget final : public QWidget {
public:
    explicit SpikeRasterWidget(QWidget* parent = nullptr);
    void setAnalysis(const dsp::SpikeAnalysis& analysis);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::SpikeAnalysis analysis_;
    QString emptyLabel_ = "No spike data";
    bool darkTheme_ = true;
};

class SpikePsthWidget final : public QWidget {
public:
    explicit SpikePsthWidget(QWidget* parent = nullptr);
    void setAnalysis(const dsp::SpikeAnalysis& analysis);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::SpikeAnalysis analysis_;
    QString emptyLabel_ = "No spike data";
    bool darkTheme_ = true;
};

class SpikeIsiWidget final : public QWidget {
public:
    explicit SpikeIsiWidget(QWidget* parent = nullptr);
    void setAnalysis(const dsp::SpikeAnalysis& analysis);
    void setDarkTheme(bool dark);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    dsp::SpikeAnalysis analysis_;
    QString emptyLabel_ = "No spike data";
    bool darkTheme_ = true;
};

}  // namespace bf
