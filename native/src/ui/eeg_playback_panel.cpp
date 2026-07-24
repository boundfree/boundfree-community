#include "ui/eeg_playback_panel.h"

#include "controller/playback_state.h"

namespace bf {

EegPlaybackPanel::EegPlaybackPanel(QWidget* parent)
    : QWidget(parent) {
    buildWidgets();
}

void EegPlaybackPanel::buildWidgets() {
    speedLabel_ = new QLabel(this);
    speedCombo_ = new QComboBox(this);
    for (double speed : PlaybackState::kSpeeds) {
        speedCombo_->addItem(QString::number(speed) + "x", speed);
    }
    speedCombo_->setCurrentIndex(2);
    speedCombo_->setMaximumWidth(80);

    playbackProgress_ = new QSlider(Qt::Horizontal, this);
    playbackProgress_->setRange(0, 10000);
    playbackProgress_->setMinimumWidth(160);
}

void EegPlaybackPanel::buildInto(QToolBar* toolbar) {
    toolbar->addSeparator();
    toolbar->addWidget(speedLabel_);
    toolbar->addWidget(speedCombo_);

    connect(speedCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (refreshing_ || index < 0) {
            return;
        }
        emit speedChanged(speedCombo_->itemData(index).toDouble());
    });

    connect(playbackProgress_, &QSlider::sliderReleased, this, [this]() {
        const double fraction = playbackProgress_->maximum() > 0
            ? static_cast<double>(playbackProgress_->value()) / playbackProgress_->maximum()
            : 0.0;
        emit progressMoved(fraction);
    });
}

void EegPlaybackPanel::setSpeed(double speed) {
    if (speedCombo_ == nullptr) {
        return;
    }
    refreshing_ = true;
    for (int i = 0; i < speedCombo_->count(); ++i) {
        if (qAbs(speedCombo_->itemData(i).toDouble() - speed) < 1e-9) {
            speedCombo_->setCurrentIndex(i);
            break;
        }
    }
    refreshing_ = false;
}

double EegPlaybackPanel::speed() const {
    if (speedCombo_ == nullptr) {
        return 1.0;
    }
    return speedCombo_->currentData().toDouble();
}

void EegPlaybackPanel::setProgressFraction(double fraction) {
    if (playbackProgress_ == nullptr || playbackProgress_->isSliderDown()) {
        return;
    }
    const QSignalBlocker blocker(playbackProgress_);
    const int value = fraction > 0.0
        ? static_cast<int>(playbackProgress_->maximum() * fraction)
        : 0;
    playbackProgress_->setValue(value);
}

bool EegPlaybackPanel::isProgressSliderDown() const {
    return playbackProgress_ != nullptr && playbackProgress_->isSliderDown();
}

void EegPlaybackPanel::retranslateUi(I18nService* i18n) {
    if (speedLabel_ == nullptr || i18n == nullptr) {
        return;
    }
    speedLabel_->setText(i18n->text("app.playback.speed"));
}

}  // namespace bf
