#include "ui/fluent_controls.h"
#include "ui/eeg_playback_panel.h"

#include "controller/playback_state.h"

namespace bf {

EegPlaybackPanel::EegPlaybackPanel(QObject* parent)
    : QObject(parent) {
    buildWidgets();
}

void EegPlaybackPanel::buildWidgets() {
    speedLabel_ = new QLabel;
    speedCombo_ = new ChoiceBox;
    for (double speed : PlaybackState::kSpeeds) {
        speedCombo_->addItem(QString::number(speed) + "x", speed);
    }
    speedCombo_->setCurrentIndex(2);
    speedCombo_->setMaximumWidth(80);

    playbackProgress_ = new ProgressSlider(Qt::Horizontal);
    playbackProgress_->setObjectName("eegPlaybackProgress");
    playbackProgress_->setRange(0, 10000);
    playbackProgress_->setMinimumWidth(160);
    connect(playbackProgress_, &QSlider::sliderMoved, this, [this](int position) {
        emit progressPreviewed(static_cast<double>(position) / playbackProgress_->maximum());
    });
    connect(playbackProgress_, &QSlider::valueChanged, this, [this](int value) {
        // Drag values only preview; keyboard and track steps commit directly.
        // Programmatic playback updates are blocked by setProgressFraction.
        if (!playbackProgress_->isSliderDown()) {
            emit progressMoved(static_cast<double>(value) / playbackProgress_->maximum());
        }
    });
}

void EegPlaybackPanel::buildInto(ControlBar* toolbar) {
    toolbar->addSeparator();
    toolbar->addPair(speedLabel_, speedCombo_);

    connect(speedCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (refreshing_ || index < 0) {
            return;
        }
        emit speedChanged(speedCombo_->itemData(index).toDouble());
    });

    connect(playbackProgress_, &QSlider::sliderReleased, this, [this]() {
        const double fraction = playbackProgress_->maximum() > 0
            ? static_cast<double>(playbackProgress_->sliderPosition()) / playbackProgress_->maximum()
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
