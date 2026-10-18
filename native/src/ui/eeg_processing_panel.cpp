#include "ui/eeg_processing_panel.h"

#include "ui/control_bar.h"
#include <QAbstractItemView>
#include <QFormLayout>
#include <QFrame>
#include <QVBoxLayout>

namespace bf {

EegProcessingPanel::EegProcessingPanel(QObject* parent)
    : QObject(parent) {
    referenceLabel_ = new QLabel;
    referenceCombo_ = new QComboBox;
    referenceCombo_->setObjectName("eegReferenceMode");
    // Items receive their translated text after the toolbar is assembled.
    // Recompute on every item/locale change rather than caching the empty
    // first-show width, which can leave only an ellipsis in the signal bar.
    referenceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    referenceCombo_->view()->setTextElideMode(Qt::ElideNone);
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Raw));
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Car));
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Bipolar));

    notchEnableCheck_ = new QCheckBox;
    notchHarmonicsCheck_ = new QCheckBox;
    notchHarmonicsSpin_ = new QSpinBox;
    notchHarmonicsSpin_->setRange(2, 20);
    notchHarmonicsSpin_->setMinimumWidth(80);

    bandpassEnableCheck_ = new QCheckBox;
    bandpassLowSpin_ = new QDoubleSpinBox;
    bandpassLowSpin_->setRange(0.1, 400.0);
    bandpassLowSpin_->setDecimals(1);
    bandpassLowSpin_->setSuffix(" Hz");
    bandpassLowSpin_->setMinimumWidth(120);
    bandpassHighSpin_ = new QDoubleSpinBox;
    bandpassHighSpin_->setRange(0.5, 500.0);
    bandpassHighSpin_->setDecimals(1);
    bandpassHighSpin_->setSuffix(" Hz");
    bandpassHighSpin_->setMinimumWidth(120);

    applyToSelectedButton_ = new QPushButton;
    applyToSelectedButton_->setObjectName("eegApplyBandpassToSelected");
    clearPerChannelButton_ = new QPushButton;
    clearPerChannelButton_->setObjectName("eegClearChannelBandpass");
}

QWidget* EegProcessingPanel::buildWidget(const ProcessingViewState& state, QWidget* parent) {
    auto* page = new QWidget(parent);
    page->setObjectName("eegFilterControls");
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(8, 8, 8, 8);
    box->setSpacing(8);
    const auto label = [page](bool bold = false) {
        auto* result = new QLabel(page);
        result->setWordWrap(true);
        result->setTextFormat(Qt::PlainText);
        auto font = result->font(); font.setBold(bold); result->setFont(font);
        return result;
    };
    const auto separator = [&]() {
        auto* line = new QFrame(page);
        line->setFrameShape(QFrame::HLine);
        line->setProperty("processingDivider", true);
        box->addSpacing(4); box->addWidget(line); box->addSpacing(4);
    };
    const auto form = [](QWidget* section) {
        auto* layout = new QFormLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
        return layout;
    };
    title_ = label(true); scope_ = label();
    scope_->setObjectName("eegProcessingScope");
    box->addWidget(title_); box->addWidget(scope_);
    separator();
    auto* reference = new QWidget(page);
    reference->setObjectName("eegReferenceSection");
    referenceTitle_ = label(true); box->addWidget(referenceTitle_);
    form(reference)->addRow(referenceLabel_, referenceCombo_);
    box->addWidget(reference);
    separator();
    auto* notch = new QWidget(page);
    notch->setObjectName("eegNotchSection");
    auto* notchLayout = new QVBoxLayout(notch);
    notchLayout->setContentsMargins(0, 0, 0, 0); notchLayout->setSpacing(8);
    notchEnableCheck_->setObjectName("eegNotchEnabled");
    notchLayout->addWidget(notchEnableCheck_);
    for (int index = 0; index < static_cast<int>(state.notchBands.size()); ++index) {
        const auto& band = state.notchBands[index];
        auto* row = new QWidget(notch);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(12, 0, 0, 0); layout->setSpacing(6);
        auto* chk = new QCheckBox(row);
        auto* spin = new QDoubleSpinBox(row);
        chk->setObjectName(QString("eegNotchBandEnabled_%1").arg(index));
        spin->setObjectName(QString("eegNotchFrequency_%1").arg(index));
        chk->setChecked(band.enabled);
        spin->setRange(1.0, 500.0); spin->setDecimals(1);
        spin->setValue(band.frequency); spin->setSuffix(" Hz");
        spin->setMinimumWidth(120); spin->setKeyboardTracking(false);
        notchBandChecks_.push_back(chk); notchBandSpins_.push_back(spin);
        layout->addWidget(chk); layout->addWidget(spin, 1);
        notchLayout->addWidget(row);
    }
    auto* harmonics = new QWidget(notch);
    auto* harmonicsLayout = form(harmonics);
    harmonicsLayout->addRow(notchHarmonicsCheck_, notchHarmonicsSpin_);
    notchLayout->addWidget(harmonics);
    box->addWidget(notch);
    separator();
    auto* bandpass = new QWidget(page);
    bandpass->setObjectName("eegGlobalBandpassSection");
    auto* bandpassLayout = form(bandpass);
    bandpassEnableCheck_->setObjectName("eegGlobalBandpassEnabled");
    bandpassLayout->addRow(bandpassEnableCheck_);
    lowLabel_ = label(); highLabel_ = label(); bandpassScope_ = label();
    bandpassLowSpin_->setObjectName("eegGlobalBandpassLow");
    bandpassHighSpin_->setObjectName("eegGlobalBandpassHigh");
    bandpassLowSpin_->setKeyboardTracking(false); bandpassHighSpin_->setKeyboardTracking(false);
    bandpassLayout->addRow(lowLabel_, bandpassLowSpin_);
    bandpassLayout->addRow(highLabel_, bandpassHighSpin_);
    bandpassLayout->addRow(bandpassScope_);
    box->addWidget(bandpass);
    separator();
    // Keep existing bulk override actions available in the same signal scope.
    box->addWidget(applyToSelectedButton_);
    box->addWidget(clearPerChannelButton_);
    feedback_ = label();
    feedback_->setObjectName("eegProcessingFeedback");
    box->addWidget(feedback_);
    connectChanged();
    refreshFromState(state);
    return page;
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
    referenceCombo_->setMinimumWidth(referenceCombo_->sizeHint().width());
    int longestItem = 0;
    for (int item = 0; item < referenceCombo_->count(); ++item)
        longestItem = std::max(longestItem, referenceCombo_->view()->fontMetrics().horizontalAdvance(referenceCombo_->itemText(item)));
    // Native popup delegates also reserve check/icon gutters. The row's full
    // width is not its usable text width (notably with Fusion's menu popup).
    referenceCombo_->view()->setMinimumWidth(std::max(referenceCombo_->minimumWidth(), longestItem + 64));
    referenceCombo_->updateGeometry();
    title_->setText(i18n->text("app.processing.panel"));
    scope_->setText(i18n->text("app.processing.scope"));
    referenceTitle_->setText(i18n->text("app.processing.reference_section"));
    lowLabel_->setText(i18n->text("app.processing.low"));
    highLabel_->setText(i18n->text("app.processing.high"));
    bandpassScope_->setText(i18n->text("app.processing.default_scope"));
    feedback_->setText(i18n->text("app.processing.automatic"));
    referenceLabel_->setBuddy(referenceCombo_);
    referenceCombo_->setAccessibleName(referenceLabel_->text());
    bandpassLowSpin_->setAccessibleName(lowLabel_->text());
    bandpassHighSpin_->setAccessibleName(highLabel_->text());
    for (int index = 0; index < notchBandChecks_.size(); ++index) {
        const QString name = i18n->text("app.processing.notch_band", {{"index", index + 1}});
        notchBandChecks_[index]->setAccessibleName(name);
        notchBandSpins_[index]->setAccessibleName(name);
    }
    notchEnableCheck_->setText(i18n->text("app.processing.notch"));
    notchHarmonicsCheck_->setText(i18n->text("app.processing.harmonics"));
    bandpassEnableCheck_->setText(i18n->text("app.processing.bandpass"));
    if (applyToSelectedButton_ != nullptr) {
        applyToSelectedButton_->setText(i18n->text("app.processing.apply_to_selected"));
        applyToSelectedButton_->setToolTip(applyToSelectedButton_->text());
    }
    if (clearPerChannelButton_ != nullptr) {
        clearPerChannelButton_->setText(i18n->text("app.processing.clear_per_channel"));
        clearPerChannelButton_->setToolTip(clearPerChannelButton_->text());
    }
}

}  // namespace bf
