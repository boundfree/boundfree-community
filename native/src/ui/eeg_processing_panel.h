#pragma once

#include "controller/processing_view_state.h"
#include "i18n/i18n_service.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include "ui/control_bar.h"
#include <QObject>

namespace bf {

// EegProcessingPanel owns the reference / notch / bandpass filter controls that
// live in the signal bar and collapsible processing region. It is an internal UI-assembly unit extracted from
// MainWindow; MainWindow holds a pointer and forwards state to/from it. The
// panel does not own any DSP state itself -- it only mirrors ProcessingViewState
// into the widgets and reports user edits via changed().
class EegProcessingPanel : public QObject {
    Q_OBJECT
public:
    explicit EegProcessingPanel(QObject* parent = nullptr);

    // Append common signal controls to the top bar and detailed filters to the side panel,
    // creating the per-notch-band check/spin pairs from the initial state. The
    // band widget count is fixed at construction (matching the original behavior
    // where buildProcessingControls created them once from the default state).
    void buildInto(ControlBar* toolbar, ControlBar* filters, const ProcessingViewState& state);

    // Push state into the widgets (blocks emission of changed() while refreshing).
    void refreshFromState(const ProcessingViewState& state);

    // Read the current widget values back into state.
    void writeToState(ProcessingViewState& state) const;

    // Retranslate the panel labels/combo items.
    void retranslateUi(I18nService* i18n);

signals:
    // Emitted whenever the user edits any control (not during refresh).
    void changed();
    // User clicked "apply to selected": write the current bandpass low/high/enabled
    // as a per-channel override onto the currently visible channels. MainWindow
    // owns channel visibility and performs the actual ProcessingViewState write.
    void applyBandpassToSelected();
    // User clicked "clear per-channel": remove all per-channel bandpass overrides.
    void clearPerChannelBandpass();

private:
    void connectChanged();

    QLabel* referenceLabel_ = nullptr;
    QComboBox* referenceCombo_ = nullptr;
    QCheckBox* notchEnableCheck_ = nullptr;
    QVector<QCheckBox*> notchBandChecks_;
    QVector<QDoubleSpinBox*> notchBandSpins_;
    QCheckBox* notchHarmonicsCheck_ = nullptr;
    QSpinBox* notchHarmonicsSpin_ = nullptr;
    QCheckBox* bandpassEnableCheck_ = nullptr;
    QDoubleSpinBox* bandpassLowSpin_ = nullptr;
    QDoubleSpinBox* bandpassHighSpin_ = nullptr;
    QPushButton* applyToSelectedButton_ = nullptr;
    QPushButton* clearPerChannelButton_ = nullptr;
    bool refreshing_ = false;
};

}  // namespace bf
