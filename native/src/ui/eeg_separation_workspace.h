#pragma once
#include "controller/eeg_separation_controller.h"
#include "i18n/i18n_service.h"
#include <QElapsedTimer>
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

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    QString text(const char *key, const QJsonObject &params = {}) const;
    void refresh();
    void refreshSource();
    void refreshComponents();
    void refreshPlots();
    void refreshBulkActions();
    void refreshPlaybackControls();
    void togglePreview();
    void setPreviewPosition(double seconds);
    double previewDuration() const;
    void applySettings();
    void chooseSource();
    void exportResults();
    void showError(const AppError &error);
    EegSeparationController *controller_;
    I18nService *i18n_;
    bool refreshing_ = false;
    QTimer *previewTimer_;
    QElapsedTimer previewClock_;
    double previewPosition_ = 0.0, playbackAnchor_ = 0.0;
    std::shared_ptr<const SeparationOutput> spectrumOutput_;
    int spectrumComponent_ = -1;
    QString stage_;
    QString errorCode_;
    QJsonObject errorParams_;
    bool errorRetryable_ = false;
    QLabel *hardware_, *teachingHelp_, *title_, *subtitle_, *source_, *hint_, *status_, *rangeLabel_,
        *rolesHelp_, *previewTitle_, *evidenceTitle_, *clues_, *sourceCaption_, *bulkSelection_,
        *amplitudeModeLabel_, *amplitudeZoomLabel_, *amplitudeHelp_, *previewChannelLabel_,
        *displayWindowLabel_, *playbackPosition_;
    QWidget *bulkActions_;
    QPushButton *teaching_, *exampleLabels_, *open_, *bulk_, *undo_, *main_, *cancel_, *retry_,
        *play_, *restart_;
    QDoubleSpinBox *start_, *duration_;
    QComboBox *bulkRole_, *rate_, *notch_, *channel_, *window_, *amplitudeMode_, *amplitudeZoom_;
    QTableWidget *channels_, *components_;
    QCheckBox *confirmed_;
    QSlider *scrub_;
    CollapsiblePanel *advanced_;
    CollapsiblePanel *evidenceDetails_;
    SeparationTraceView *groups_, *componentTrace_, *spectrum_;
};
} // namespace bf
