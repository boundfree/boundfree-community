#pragma once

#include "i18n/i18n_service.h"

#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include "ui/control_bar.h"
#include <QObject>

namespace bf {

// EegPlaybackPanel owns the playback speed selector (label + combo) and the
// progress slider that live on the browser toolbar. It is an internal
// UI-assembly unit extracted from MainWindow; MainWindow holds a pointer and
// forwards state to/from it. The panel does not own any playback model state
// (PlaybackState, timers, prefetch flags stay in MainWindow) -- it only mirrors
// position/speed into the widgets and reports user edits via signals.
//
// Boundary chosen (what moved vs. stayed):
//   MOVED into this panel: speedLabel_, speedCombo_, playbackProgress_.
//   STAYED in MainWindow:  playAction_ (a QAction added directly to the
//     toolbar via addAction(icon) -- toolbar owns it and MainWindow swaps its
//     play/pause icon/text), markerModeAction_ (unrelated to playback), and
//     all model/timer state (PlaybackState, playbackTimer_, playbackElapsed_,
//     signalDurationSeconds_, playbackPrefetchPending_, recordSourceStart_).
//
// The progress slider is added to the toolbar by MainWindow at a specific
// position (between the position spin and the window controls) via
// progressSlider(); the speed label/combo are appended by buildInto() in the
// playback-control region. This preserves the exact original toolbar layout.
class EegPlaybackPanel : public QObject {
    Q_OBJECT
public:
    explicit EegPlaybackPanel(QObject* parent = nullptr);

    // Append a separator + speed label + speed combo to the host toolbar.
    void buildInto(ControlBar* toolbar);

    // The progress slider widget. MainWindow adds it to the toolbar at the
    // position it belongs (between position spin and window controls).
    QSlider* progressSlider() const { return playbackProgress_; }

    // Speed accessors. setSpeed selects the matching combo item (blocked emit).
    void setSpeed(double speed);
    double speed() const;

    // Push the current playback position [0..1] into the slider (blocked emit
    // and skipped while the user is dragging the slider).
    void setProgressFraction(double fraction);

    // Whether the user is currently dragging the progress slider.
    bool isProgressSliderDown() const;

    // Retranslate the speed label.
    void retranslateUi(I18nService* i18n);

signals:
    // Lightweight drag feedback; does not request data or change playback.
    void progressPreviewed(double fraction);

    // Emitted when the user changes the speed combo (not during setSpeed).
    void speedChanged(double speed);

    // Emitted on release or a keyboard/track step. fraction is the
    // sought position in [0..1] of the playback duration.
    void progressMoved(double fraction);

private:
    void buildWidgets();

    QLabel* speedLabel_ = nullptr;
    QComboBox* speedCombo_ = nullptr;
    QSlider* playbackProgress_ = nullptr;
    bool refreshing_ = false;
};

}  // namespace bf
