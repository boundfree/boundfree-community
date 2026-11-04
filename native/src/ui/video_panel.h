#pragma once

#include "controller/video_sync_state.h"

#include <QElapsedTimer>
#include <QPixmap>
#include <QWidget>

class QAudioOutput;
class QDoubleSpinBox;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QResizeEvent;
class QVideoFrame;
class QVideoSink;

namespace bf {

class I18nService;

class VideoPanel final : public QWidget {
    Q_OBJECT

public:
    explicit VideoPanel(I18nService* i18n, QWidget* parent = nullptr);

    bool load(const QString& path);
    void clear();
    bool isLoaded() const { return loaded_; }
    void syncTo(double eegTimeSeconds, bool eegPlaying);
    void setRate(double rate);
    void applyOffset(double eegTimeSeconds, bool eegPlaying);
    void retranslateUi();

    double offsetSeconds() const { return syncState_.offsetSeconds(); }
    double videoDurationSeconds() const;
    bool isBlank() const { return syncState_.isBlank(); }
    int renderedFrameCount() const { return renderedFrameCount_; }
    int droppedFrameCount() const { return droppedFrameCount_; }
    double playbackRate() const;
    QString statusSummary() const;

signals:
    void offsetApplyRequested();
    void durationKnown(double seconds);
    void loadFailed(const QString& detail);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    static constexpr qint64 kMinRenderIntervalMs = 50;

    void chooseVideo();
    void handleFrame(const QVideoFrame& frame);
    void handleDurationChanged(qint64 durationMs);
    void enterBlank();
    void exitBlank();
    void forceNextRender();
    void repaintFrame();

    I18nService* i18n_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QLabel* offsetLabel_ = nullptr;
    QDoubleSpinBox* offsetSpin_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QLabel* fileLabel_ = nullptr;
    QLabel* videoLabel_ = nullptr;
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOutput_ = nullptr;
    QVideoSink* videoSink_ = nullptr;
    VideoSyncState syncState_;
    QElapsedTimer renderClock_;
    qint64 lastRenderMs_ = 0;
    double eegTimeSeconds_ = 0.0;
    bool eegPlaying_ = false;
    bool loaded_ = false;
    int renderedFrameCount_ = 0;
    int droppedFrameCount_ = 0;
    QPixmap lastFrame_;
};

}  // namespace bf
