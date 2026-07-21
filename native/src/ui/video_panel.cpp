#include "ui/video_panel.h"

#include "i18n/i18n_service.h"

#include <QAudioOutput>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QResizeEvent>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace bf {

VideoPanel::VideoPanel(I18nService* i18n, QWidget* parent)
    : QWidget(parent), i18n_(i18n) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* header = new QHBoxLayout();
    titleLabel_ = new QLabel(this);
    openButton_ = new QPushButton(this);
    offsetLabel_ = new QLabel(this);
    offsetSpin_ = new QDoubleSpinBox(this);
    offsetSpin_->setRange(-3600.0, 3600.0);
    offsetSpin_->setDecimals(3);
    offsetSpin_->setSingleStep(0.1);
    offsetSpin_->setSuffix(" s");
    applyButton_ = new QPushButton(this);
    fileLabel_ = new QLabel(this);
    fileLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    header->addWidget(titleLabel_);
    header->addWidget(openButton_);
    header->addWidget(offsetLabel_);
    header->addWidget(offsetSpin_);
    header->addWidget(applyButton_);
    header->addStretch();
    header->addWidget(fileLabel_);
    layout->addLayout(header);

    videoLabel_ = new QLabel(this);
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setMinimumHeight(180);
    videoLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLabel_->setStyleSheet("background: #000000;");
    layout->addWidget(videoLabel_);

    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    audioOutput_->setVolume(0.8f);
    player_->setAudioOutput(audioOutput_);
    videoSink_ = new QVideoSink(this);
    player_->setVideoOutput(videoSink_);

    connect(openButton_, &QPushButton::clicked, this, &VideoPanel::chooseVideo);
    connect(applyButton_, &QPushButton::clicked, this, &VideoPanel::offsetApplyRequested);
    connect(videoSink_, &QVideoSink::videoFrameChanged, this, &VideoPanel::handleFrame);
    connect(player_, &QMediaPlayer::durationChanged, this, &VideoPanel::handleDurationChanged);
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& detail) {
        if (detail.isEmpty()) {
            return;
        }
        loaded_ = false;
        syncState_.setDurationMs(0);
        player_->stop();
        videoLabel_->clear();
        lastFrame_ = {};
        fileLabel_->setText(i18n_->text("app.video.not_loaded"));
        emit loadFailed(detail);
    });

    renderClock_.start();
    retranslateUi();
}

bool VideoPanel::load(const QString& path) {
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile()) {
        emit loadFailed(path);
        return false;
    }
    player_->stop();
    videoLabel_->clear();
    lastFrame_ = {};
    syncState_.setDurationMs(0);
    renderedFrameCount_ = 0;
    droppedFrameCount_ = 0;
    forceNextRender();
    player_->setSource(QUrl::fromLocalFile(file.absoluteFilePath()));
    loaded_ = true;
    fileLabel_->setText(file.fileName());
    syncTo(eegTimeSeconds_, eegPlaying_);
    return true;
}

void VideoPanel::syncTo(double eegTimeSeconds, bool eegPlaying) {
    if (!loaded_) {
        return;
    }
    eegTimeSeconds_ = std::isfinite(eegTimeSeconds) ? eegTimeSeconds : 0.0;
    eegPlaying_ = eegPlaying;
    const VideoSyncCommand command = syncState_.sync(
        eegTimeSeconds_,
        eegPlaying_,
        player_->position(),
        player_->playbackState() == QMediaPlayer::PlayingState);
    if (command.blank) {
        enterBlank();
    } else {
        exitBlank();
    }
    if (command.pause && player_->playbackState() == QMediaPlayer::PlayingState) {
        player_->pause();
    }
    if (command.seek) {
        forceNextRender();
        player_->setPosition(command.seekPositionMs);
    }
    if (command.play && player_->playbackState() != QMediaPlayer::PlayingState) {
        player_->play();
    }
}

void VideoPanel::setRate(double rate) {
    const double boundedRate = std::isfinite(rate) ? std::clamp(rate, 0.25, 4.0) : 1.0;
    player_->setPlaybackRate(boundedRate);
}

void VideoPanel::applyOffset(double eegTimeSeconds, bool eegPlaying) {
    syncState_.setOffsetSeconds(offsetSpin_->value());
    syncTo(eegTimeSeconds, eegPlaying);
}

double VideoPanel::videoDurationSeconds() const {
    return static_cast<double>(syncState_.durationMs()) / 1000.0;
}

double VideoPanel::playbackRate() const {
    return player_->playbackRate();
}

QString VideoPanel::statusSummary() const {
    return QString("loaded=%1 blank=%2 offset=%3 duration=%4 rate=%5 rendered=%6 dropped=%7")
        .arg(loaded_)
        .arg(syncState_.isBlank())
        .arg(syncState_.offsetSeconds(), 0, 'f', 3)
        .arg(videoDurationSeconds(), 0, 'f', 3)
        .arg(player_->playbackRate(), 0, 'f', 2)
        .arg(renderedFrameCount_)
        .arg(droppedFrameCount_);
}

void VideoPanel::retranslateUi() {
    titleLabel_->setText(i18n_->text("app.video.panel"));
    openButton_->setText(i18n_->text("app.video.open"));
    offsetLabel_->setText(i18n_->text("app.video.offset"));
    offsetSpin_->setToolTip(i18n_->text("app.video.offset_tooltip"));
    applyButton_->setText(i18n_->text("app.video.apply"));
    applyButton_->setToolTip(i18n_->text("app.video.apply_tooltip"));
    if (!loaded_) {
        fileLabel_->setText(i18n_->text("app.video.not_loaded"));
    }
}

void VideoPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    repaintFrame();
}

void VideoPanel::chooseVideo() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        i18n_->text("app.video.open"),
        {},
        i18n_->text("app.video.filter"));
    if (!path.isEmpty()) {
        load(path);
    }
}

void VideoPanel::handleFrame(const QVideoFrame& frame) {
    if (syncState_.isBlank()) {
        ++droppedFrameCount_;
        return;
    }
    const qint64 now = renderClock_.elapsed();
    if (now - lastRenderMs_ < kMinRenderIntervalMs) {
        ++droppedFrameCount_;
        return;
    }
    if (!frame.isValid()) {
        return;
    }
    const QImage image = frame.toImage();
    if (image.isNull()) {
        return;
    }
    lastRenderMs_ = now;
    lastFrame_ = QPixmap::fromImage(image);
    ++renderedFrameCount_;
    repaintFrame();
}

void VideoPanel::handleDurationChanged(qint64 durationMs) {
    syncState_.setDurationMs(durationMs);
    if (durationMs > 0) {
        emit durationKnown(static_cast<double>(durationMs) / 1000.0);
        syncTo(eegTimeSeconds_, eegPlaying_);
    }
}

void VideoPanel::enterBlank() {
    videoLabel_->clear();
    lastFrame_ = {};
}

void VideoPanel::exitBlank() {
    if (!lastFrame_.isNull()) {
        repaintFrame();
    }
}

void VideoPanel::forceNextRender() {
    lastRenderMs_ = -kMinRenderIntervalMs;
}

void VideoPanel::repaintFrame() {
    if (lastFrame_.isNull() || syncState_.isBlank()) {
        return;
    }
    videoLabel_->setPixmap(lastFrame_.scaled(
        videoLabel_->size(),
        Qt::KeepAspectRatio,
        Qt::FastTransformation));
}

}  // namespace bf
