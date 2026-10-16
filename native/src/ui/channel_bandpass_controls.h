#pragma once

#include "controller/processing_view_state.h"
#include "i18n/i18n_service.h"
#include "ui/control_bar.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <optional>

namespace bf {
// Keep DSP output indexing: bipolar row i (source i minus source i+1)
// continues to use override key i; the last source override stays dormant.
class ChannelBandpassControls final : public QWidget {
public:
    explicit ChannelBandpassControls(I18nService* i18n, QWidget* parent = nullptr)
        : QWidget(parent), i18n_(i18n), layout_(new QVBoxLayout(this)) {
        setObjectName("eegChannelBandpassRows");
        layout_->setContentsMargins(0, 0, 0, 0);
        layout_->setSpacing(4);
        title_ = new QLabel(this);
        layout_->addWidget(title_);
        selector_ = new QComboBox(this);
        selector_->setObjectName("eegBandpassChannel");
        selector_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        selector_->setMinimumContentsLength(12);
        layout_->addWidget(selector_);
        connect(selector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int channel) {
            for (int index = 0; index < rows_.size(); ++index)
                rows_[index].widget->setVisible(index == channel);
        });
        retranslate();
    }
    std::function<void(int, std::optional<ChannelBandpassUi>)> changed;

    void clearDrafts() {
        for (int channel = 0; channel < rows_.size(); ++channel) {
            rows_[channel].draft = false;
            rows_[channel].error->hide();
            refreshRow(channel, true);
        }
    }

    void refresh(const QStringList& names, double fs, const ProcessingViewState& state) {
        const ProcessingViewState previous = state_;
        state_ = state;
        fs_ = fs;
        if (names != names_) {
            for (const Row& row : rows_) {
                row.mode->blockSignals(true);
                row.low->blockSignals(true);
                row.high->blockSignals(true);
                layout_->removeWidget(row.widget);
                row.widget->hide();
                // Remove obsolete object names immediately; deleteLater alone
                // leaves findChild and accessibility traversals seeing both rows.
                row.widget->setParent(nullptr);
                row.widget->deleteLater();
            }
            rows_.clear();
            const QString selectedName = names_.value(selector_->currentIndex());
            const int selectedIndex = selector_->currentIndex();
            names_ = names;
            const QSignalBlocker blocker(selector_);
            selector_->clear();
            selector_->addItems(names);
            int restored = names.indexOf(selectedName);
            if (restored < 0) restored = names.isEmpty() ? -1 : std::clamp(selectedIndex, 0, int(names.size()) - 1);
            selector_->setCurrentIndex(restored);
            for (int channel = 0; channel < names.size(); ++channel) {
                addRow(channel, names[channel]);
                rows_[channel].widget->setVisible(channel == restored);
            }
            selector_->setEnabled(!names.isEmpty());
        }
        for (int channel = 0; channel < rows_.size(); ++channel) {
            const auto before = effectiveSetting(previous, channel);
            const auto after = effectiveSetting(state_, channel);
            const bool changedSetting = before.enabled != after.enabled || before.low != after.low || before.high != after.high
                || previous.perChannelBandpass.count(channel) != state_.perChannelBandpass.count(channel);
            if (changedSetting) {
                rows_[channel].draft = false;
                rows_[channel].error->hide();
            }
            refreshRow(channel, changedSetting);
        }
        retranslate();
    }
    void retranslate() {
        title_->setText(i18n_->text("app.processing.per_channel"));
        selector_->setAccessibleName(i18n_->text("app.browser.channels"));
        const QSignalBlocker selectorBlocker(selector_);
        for (int channel = 0; channel < names_.size(); ++channel) {
            const auto setting = state_.perChannelBandpass.find(channel);
            const QString mode = setting == state_.perChannelBandpass.end()
                ? i18n_->text("app.processing.inherit")
                : setting->second.enabled ? i18n_->text("app.processing.enabled")
                    : i18n_->text("app.processing.disabled");
            selector_->setItemText(channel, names_[channel] + " — " + mode);
        }
        for (Row& row : rows_) {
            const QSignalBlocker blocker(row.mode);
            row.mode->setItemText(0, i18n_->text("app.processing.inherit"));
            row.mode->setItemText(1, i18n_->text("app.processing.disabled"));
            row.mode->setItemText(2, i18n_->text("app.processing.enabled"));
            row.lowLabel->setText(i18n_->text("app.processing.low"));
            row.highLabel->setText(i18n_->text("app.processing.high"));
            row.low->setAccessibleName(i18n_->text("app.processing.low"));
            row.high->setAccessibleName(i18n_->text("app.processing.high"));
            row.error->setText(i18n_->text("app.processing.invalid_range", {{"nyquist", fs_ / 2.0}}));
        }
    }
private:
    struct Row {
        QWidget* widget;
        QComboBox* mode;
        QDoubleSpinBox* low;
        QDoubleSpinBox* high;
        QLabel* lowLabel;
        QLabel* highLabel;
        QLabel* error;
        bool draft = false;
    };
    static ChannelBandpassUi effectiveSetting(const ProcessingViewState& state, int channel) {
        const auto it = state.perChannelBandpass.find(channel);
        return it == state.perChannelBandpass.end()
            ? ChannelBandpassUi{state.globalBandpassEnabled, state.bandpassLow, state.bandpassHigh} : it->second;
    }
    ChannelBandpassUi effectiveSetting(int channel) const { return effectiveSetting(state_, channel); }
    void addRow(int channel, const QString&) {
        auto* widget = new QWidget(this);
        widget->setObjectName(QString("eegBandpassRow_%1").arg(channel));
        auto* box = new QVBoxLayout(widget);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(0);
        auto* header = new ControlBar(widget);
        auto* mode = new QComboBox(header);
        mode->setObjectName(QString("eegBandpassMode_%1").arg(channel));
        mode->addItems({QString(), QString(), QString()});
        header->addWidget(mode);
        box->addWidget(header);
        auto* values = new ControlBar(widget);
        auto* lowLabel = new QLabel(values);
        auto* highLabel = new QLabel(values);
        auto* low = new QDoubleSpinBox(values);
        auto* high = new QDoubleSpinBox(values);
        low->setObjectName(QString("eegBandpassLow_%1").arg(channel));
        high->setObjectName(QString("eegBandpassHigh_%1").arg(channel));
        for (auto* spin : {low, high}) {
            spin->setRange(0.0, 1000000.0);
            spin->setDecimals(2);
            spin->setSuffix(" Hz");
            spin->setMinimumWidth(120);
            spin->setKeyboardTracking(false);
        }
        values->addPair(lowLabel, low);
        values->addPair(highLabel, high);
        box->addWidget(values);
        auto* error = new QLabel(widget);
        error->setObjectName(QString("eegBandpassError_%1").arg(channel));
        error->setWordWrap(true);
        error->hide();
        box->addWidget(error);
        layout_->addWidget(widget);
        rows_.push_back({widget, mode, low, high, lowLabel, highLabel, error});
        connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, channel](int) { commit(channel, true); });
        connect(low, &QDoubleSpinBox::editingFinished, this, [this, channel]() { commit(channel); });
        connect(high, &QDoubleSpinBox::editingFinished, this, [this, channel]() { commit(channel); });
    }
    void refreshRow(int channel, bool force) {
        Row& row = rows_[channel];
        // Keep a rejected edit available for correction even if playback
        // refreshes the file window. It has not entered ProcessingViewState.
        if (row.draft && !force) return;
        const auto it = state_.perChannelBandpass.find(channel);
        const ChannelBandpassUi setting = effectiveSetting(channel);
        const QSignalBlocker modeBlock(row.mode), lowBlock(row.low), highBlock(row.high);
        row.mode->setCurrentIndex(it == state_.perChannelBandpass.end() ? 0 : setting.enabled ? 2 : 1);
        if (force || !row.low->hasFocus()) row.low->setValue(setting.low);
        if (force || !row.high->hasFocus()) row.high->setValue(setting.high);
        row.low->setEnabled(it != state_.perChannelBandpass.end());
        row.high->setEnabled(it != state_.perChannelBandpass.end());
    }
    void commit(int channel, bool modeChanged = false) {
        Row& row = rows_[channel];
        const int mode = row.mode->currentIndex();
        ChannelBandpassUi setting{mode == 2, row.low->value(), row.high->value()};
        const bool disabling = modeChanged && mode == 1;
        if (disabling) {
            // Turning filtering off must work even when inherited cutoffs are
            // outside this file's Nyquist range; discard any invalid draft.
            setting = effectiveSetting(channel);
            setting.enabled = false;
        }
        if (mode != 0 && !disabling
            && (!(setting.low > 0.0) || !(setting.low < setting.high) || !(setting.high < fs_ / 2.0))) {
            row.draft = true;
            row.error->show();
            row.low->setEnabled(true);
            row.high->setEnabled(true);
            return;
        }
        row.draft = false;
        row.error->hide();
        const auto it = state_.perChannelBandpass.find(channel);
        if (mode == 0) {
            if (it == state_.perChannelBandpass.end()) { refreshRow(channel, true); return; }
            state_.clearChannelBandpass(channel);
        } else {
            if (it != state_.perChannelBandpass.end() && it->second.enabled == setting.enabled
                && it->second.low == setting.low && it->second.high == setting.high) { refreshRow(channel, true); return; }
            state_.setChannelBandpass(channel, setting);
        }
        refreshRow(channel, true);
        retranslate();
        if (changed) changed(channel, mode == 0 ? std::nullopt : std::optional<ChannelBandpassUi>(setting));
    }
    I18nService* i18n_;
    QVBoxLayout* layout_;
    QLabel* title_;
    QComboBox* selector_;
    QStringList names_;
    QVector<Row> rows_;
    ProcessingViewState state_;
    double fs_ = 0.0;
};
}  // namespace bf
