#include "ui/eeg_processing_panel.h"

#include <QToolBar>

namespace bf {

EegProcessingPanel::EegProcessingPanel(QObject* parent)
    : QObject(parent) {
    referenceLabel_ = new QLabel;
    referenceCombo_ = new QComboBox;
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Raw));
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Car));
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Bipolar));

    notchEnableCheck_ = new QCheckBox;
    notchHarmonicsCheck_ = new QCheckBox;
    notchHarmonicsSpin_ = new QSpinBox;
    notchHarmonicsSpin_->setRange(2, 20);
    notchHarmonicsSpin_->setMaximumWidth(70);

    bandpassEnableCheck_ = new QCheckBox;
    bandpassLowSpin_ = new QDoubleSpinBox;
    bandpassLowSpin_->setRange(0.1, 400.0);
    bandpassLowSpin_->setDecimals(1);
    bandpassLowSpin_->setSuffix(" Hz");
    bandpassLowSpin_->setMaximumWidth(90);
    bandpassHighSpin_ = new QDoubleSpinBox;
    bandpassHighSpin_->setRange(0.5, 500.0);
    bandpassHighSpin_->setDecimals(1);
    bandpassHighSpin_->setSuffix(" Hz");
    bandpassHighSpin_->setMaximumWidth(90);

    applyToSelectedButton_ = new QPushButton;
    clearPerChannelButton_ = new QPushButton;
    applyToSelectedButton_->setMaximumWidth(180);
    clearPerChannelButton_->setMaximumWidth(180);
}

void EegProcessingPanel::buildInto(QToolBar* toolbar, const ProcessingViewState& state) {
    toolbar->addSeparator();
    toolbar->addWidget(referenceLabel_);
    toolbar->addWidget(referenceCombo_);

    toolbar->addSeparator();
    toolbar->addWidget(notchEnableCheck_);
    notchBandChecks_.clear();
    notchBandSpins_.clear();
    for (const NotchBandUi& band : state.notchBands) {
        auto* chk = new QCheckBox;
        chk->setChecked(band.enabled);
        auto* spin = new QDoubleSpinBox;
        spin->setRange(1.0, 500.0);
        spin->setDecimals(1);
        spin->setValue(band.frequency);
        spin->setSuffix(" Hz");
        spin->setMaximumWidth(90);
        notchBandChecks_.push_back(chk);
        notchBandSpins_.push_back(spin);
        toolbar->addWidget(chk);
        toolbar->addWidget(spin);
    }
    toolbar->addWidget(notchHarmonicsCheck_);
    toolbar->addWidget(notchHarmonicsSpin_);

    toolbar->addSeparator();
    toolbar->addWidget(bandpassEnableCheck_);
    toolbar->addWidget(bandpassLowSpin_);
    toolbar->addWidget(bandpassHighSpin_);
    toolbar->addWidget(applyToSelectedButton_);
    toolbar->addWidget(clearPerChannelButton_);
    connectChanged();
    refreshFromState(state);
}

void EegProcessingPanel::connectChanged() {
    connect(referenceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!refreshing_) emit changed();
    });
    connect(notchEnableCheck_, &QCheckBox::toggled, this, [this](bool) {
        if (!refreshing_) emit changed();
    });
    for (QCheckBox* chk : notchBandChecks_) {
        connect(chk, &QCheckBox::toggled, this, [this](bool) { if (!refreshing_) emit changed(); });
    }
    for (QDoubleSpinBox* spin : notchBandSpins_) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { if (!refreshing_) emit changed(); });
    }
    connect(notchHarmonicsCheck_, &QCheckBox::toggled, this, [this](bool) {
        if (!refreshing_) emit changed();
    });
    connect(notchHarmonicsSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        if (!refreshing_) emit changed();
    });
    connect(bandpassEnableCheck_, &QCheckBox::toggled, this, [this](bool) {
        if (!refreshing_) emit changed();
    });
    connect(bandpassLowSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (!refreshing_) emit changed();
    });
    connect(bandpassHighSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (!refreshing_) emit changed();
    });
    connect(applyToSelectedButton_, &QPushButton::clicked, this, [this]() { emit applyBandpassToSelected(); });
    connect(clearPerChannelButton_, &QPushButton::clicked, this, [this]() { emit clearPerChannelBandpass(); });
}

void EegProcessingPanel::refreshFromState(const ProcessingViewState& state) {
    if (referenceCombo_ == nullptr) {
        return;
    }
    refreshing_ = true;
    const int refIndex = referenceCombo_->findData(static_cast<int>(state.referenceMode));
    referenceCombo_->setCurrentIndex(refIndex >= 0 ? refIndex : 0);
    notchEnableCheck_->setChecked(state.notchEnabled);
    for (int i = 0; i < notchBandChecks_.size() && i < static_cast<int>(state.notchBands.size()); ++i) {
        notchBandChecks_[i]->setChecked(state.notchBands[static_cast<std::size_t>(i)].enabled);
        notchBandSpins_[i]->setValue(state.notchBands[static_cast<std::size_t>(i)].frequency);
        const bool enableBand = state.notchEnabled;
        notchBandChecks_[i]->setEnabled(enableBand);
        notchBandSpins_[i]->setEnabled(enableBand);
    }
    notchHarmonicsCheck_->setChecked(state.notchHarmonics);
    notchHarmonicsSpin_->setValue(state.notchHarmonicMultiple);
    notchHarmonicsCheck_->setEnabled(state.notchEnabled);
    notchHarmonicsSpin_->setEnabled(state.notchEnabled && state.notchHarmonics);
    bandpassEnableCheck_->setChecked(state.globalBandpassEnabled);
    bandpassLowSpin_->setValue(state.bandpassLow);
    bandpassHighSpin_->setValue(state.bandpassHigh);
    bandpassLowSpin_->setEnabled(state.globalBandpassEnabled);
    bandpassHighSpin_->setEnabled(state.globalBandpassEnabled);
    refreshing_ = false;
}

void EegProcessingPanel::writeToState(ProcessingViewState& state) const {
    state.referenceMode = static_cast<dsp::ReferenceMode>(referenceCombo_->currentData().toInt());
    state.notchEnabled = notchEnableCheck_->isChecked();
    state.notchBands.resize(static_cast<std::size_t>(notchBandChecks_.size()));
    for (int i = 0; i < notchBandChecks_.size(); ++i) {
        state.notchBands[static_cast<std::size_t>(i)].enabled = notchBandChecks_[i]->isChecked();
        state.notchBands[static_cast<std::size_t>(i)].frequency = notchBandSpins_[i]->value();
    }
    state.notchHarmonics = notchHarmonicsCheck_->isChecked();
    state.notchHarmonicMultiple = notchHarmonicsSpin_->value();
    state.globalBandpassEnabled = bandpassEnableCheck_->isChecked();
    state.bandpassLow = bandpassLowSpin_->value();
    state.bandpassHigh = bandpassHighSpin_->value();
}

void EegProcessingPanel::retranslateUi(I18nService* i18n) {
    if (referenceLabel_ == nullptr || i18n == nullptr) {
        return;
    }
    referenceLabel_->setText(i18n->text("app.processing.reference"));
    referenceCombo_->setItemText(0, i18n->text("app.processing.ref.raw"));
    referenceCombo_->setItemText(1, i18n->text("app.processing.ref.car"));
    referenceCombo_->setItemText(2, i18n->text("app.processing.ref.bipolar"));
    notchEnableCheck_->setText(i18n->text("app.processing.notch"));
    notchHarmonicsCheck_->setText(i18n->text("app.processing.harmonics"));
    bandpassEnableCheck_->setText(i18n->text("app.processing.bandpass"));
    if (applyToSelectedButton_ != nullptr) {
        applyToSelectedButton_->setText(i18n->text("app.processing.apply_to_selected"));
    }
    if (clearPerChannelButton_ != nullptr) {
        clearPerChannelButton_->setText(i18n->text("app.processing.clear_per_channel"));
    }
}

}  // namespace bf
