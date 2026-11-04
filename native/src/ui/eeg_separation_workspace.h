#pragma once
#include "controller/eeg_separation_controller.h"
#include "i18n/i18n_service.h"
#include "render/live_ica_projection.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QWidget>
class QHideEvent;
class QTimer;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QComboBox;
class QTableWidget;
class QCheckBox;
class QSlider;
class QGridLayout;
class QResizeEvent;
class QDialog;
class QTabWidget;
class QRadioButton;
class QButtonGroup;
namespace bf {
class CollapsiblePanel;
class SeparationTraceView;
// Offline analysis owns its own input/settings and never changes the browser's filters or cursor.
class EegSeparationWorkspace final : public QWidget {
  public:
    EegSeparationWorkspace(EegSeparationController *controller, I18nService *i18n, QWidget *parent = nullptr);
    void setDarkTheme(bool dark);
    void retranslateUi();
    void pausePreview();
    void chooseSource();
    void openTeachingSource();
    bool canChangeInput() const;
    std::function<void()> inputActionsChanged;
    void setDeviceInput(const QString& name, const QJsonArray& streams, bool available);
    void setInputPending(bool pending);
    void showInputError(const AppError& error);
    void setInputProgress(double seconds);
    void setLiveAvailable(bool available);
    void setLiveActive(bool active);
    void setLiveFrame(std::shared_ptr<const LiveIcaFrame> frame);
    std::function<void()> liveToggleRequested;
    std::function<void()> inputCancellationRequested;
    std::function<void(const QString&)> deviceSnapshotRequested;
    bool viewControlsAvailable() const;
    bool viewPaused() const;
    QString viewPauseText() const;
    QString viewStatusText() const;
    QString viewHint() const;
    void toggleViewPause();
    void resetView() { resetGroupView(); }
    void showPlotHelp() { showHelp(2); }

  protected:
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QString text(const char *key, const QJsonObject &params = {}) const;
    void refresh();
    void refreshSource();
    void refreshComponents();
    void refreshPlots();
    void refreshBulkActions();
    void refreshPlaybackControls();
    void arrangePreviewControls();
    void togglePreview();
    void setPreviewPosition(double seconds);
    double previewDuration() const;
    void applySettings();
    void exportResults();
    void showError(const AppError &error);
    void showHelp(int tab);
    void refreshRoleHelp(QComboBox* combo);
    void refreshComponentSelection();
    void zoomGroupTime(double steps, double anchor);
    void zoomGroupAmplitude(double steps);
    void panGroupView(double seconds);
    void resetGroupView();
    void selectViewValue(QComboBox* combo, double value, bool time);
    EegSeparationController *controller_;
    I18nService *i18n_;
    bool refreshing_ = false;
    bool inputPending_ = false, deviceAvailable_ = false, inputError_ = false;
    bool liveAvailable_ = false, liveActive_ = false;
    double inputSeconds_ = -1;
    std::shared_ptr<const LiveIcaFrame> liveFrame_;
    std::shared_ptr<const LiveIcaFrame> latestLiveFrame_;
    bool liveViewFrozen_ = false;
    QString deviceName_;
    QTimer *previewTimer_;
    QElapsedTimer previewClock_;
    double previewPosition_ = 0.0, playbackAnchor_ = 0.0;
    double previewOffset_ = -1.0;
    bool draggingGroups_ = false;
    QPointF groupDragPosition_;
    std::shared_ptr<const SeparationOutput> spectrumOutput_;
    int spectrumComponent_ = -1;
    QString stage_;
    QString errorCode_;
    QJsonObject errorParams_;
    bool errorRetryable_ = false;
    QLabel *hardware_, *teachingHelp_, *title_, *subtitle_, *sourceTitle_, *source_, *hint_, *status_, *rangeLabel_,
        *rolesHelp_, *previewTitle_, *evidenceTitle_, *clues_, *sourceCaption_, *bulkSelection_,
        *amplitudeModeLabel_, *amplitudeZoomLabel_, *amplitudeHelp_, *previewChannelLabel_,
        *displayWindowLabel_, *playbackPosition_;
    QLabel *shortcutsHelp_, *sourceDetails_, *reviewHelp_, *reviewHint_, *reviewConfirmHint_;
    QPushButton *helpButton_, *metadataButton_, *helpClose_;
    QLabel* selectedComponent_;
    QPushButton *previousComponent_, *nextComponent_;
    std::array<QRadioButton*, 4> classificationButtons_;
    QButtonGroup* classificationGroup_;
    QDialog* helpDialog_;
    QTabWidget* helpTabs_;
    QWidget *bulkActions_;
    QPushButton *exampleLabels_, *bulk_, *undo_, *main_, *rerun_, *cancel_, *retry_,
        *play_, *restart_;
    QDoubleSpinBox *start_, *duration_;
    QPushButton *deviceInput_;
    QLabel *deviceInputStatus_;
    QPushButton *liveToggle_, *allObservations_;
    QLabel *liveStatus_;
    QWidget *liveControls_;
    std::array<QLabel*, 4> workflowSteps_;
    QComboBox *deviceStream_;
    QComboBox *bulkRole_, *rate_, *notch_, *channel_, *window_, *amplitudeMode_, *amplitudeZoom_;
    QTableWidget *channels_, *components_;
    QCheckBox *confirmed_;
    QSlider *scrub_;
    CollapsiblePanel *advanced_;
    QTabWidget* componentViews_;
    SeparationTraceView *groups_, *componentTrace_, *spectrum_;
    QGridLayout *previewControls_ = nullptr, *amplitudeControls_ = nullptr;
};
} // namespace bf
