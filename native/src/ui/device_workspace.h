#pragma once

#include "controller/acquisition_controller.h"
#include "controller/acquisition_wifi_source.h"
#include "i18n/i18n_service.h"

#include <QElapsedTimer>
#include <QWidget>
#include <vector>
#include <functional>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace bf {
class WaveformWidget;

// Connection editor and live controls. The main window supplies its existing
// waveform renderer; freezing only the display leaves acquisition/recording running.
class DeviceWorkspace final : public QWidget {
public:
    DeviceWorkspace(AcquisitionController* controller, I18nService* i18n, QWidget* parent = nullptr,
                    WaveformWidget* sharedWaveform = nullptr);
    void retranslateUi();
    void setDarkTheme(bool dark);
    QWidget* takeLiveControls();
    void setLiveViewActive(bool active);
    void openConnections();
    std::function<void()> liveRequested;
    std::function<void(double,double,bool,int,double)> signalViewRequested;
    std::function<QStringList()> signalChannelNames;
    void setViewSeconds(double);
    void zoomViewTime(int steps, double anchor);
    void panView(double seconds);
    void toggleViewPause();
    void resetView();
    std::function<void()> connectionsRequested;
    std::function<void(const QString&)> captureOpenRequested;
    struct ViewControls {
        bool paused = false, available = false;
        QString pauseText, status, hint;
        bool helpVisible = true;
    };
    std::function<std::optional<ViewControls>()> viewControlsOverride;
    std::function<bool()> pauseViewOverride, resetViewOverride, viewHelpOverride;
    std::function<void(const QString&, bool)> recordingStatusChanged;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Profile {
        QString id, name, deviceId, model, lastVerified;
        RtBciWifiOptions options;
    };
    enum class Adapter { None, Simulator, Tcp, Wifi };
    void showDeviceList();
    void showTcp();
    void cancelWifiConnection();
    void updateManagerNavigation();
    void showWifi(const Profile* profile = nullptr);
    void showLive();
    void startProbeRound();
    void probeOnce();
    void cancelProbe();
    void refreshConnectionPage();
    void rebuildRecent();
    void loadProfiles();
    void saveProfile();
    bool persistProfiles();
    void openWifiSettings();
    void beginWifiConnection();
    void handlePendingConnection();
    void completeConnection();
    void updateWaveform();
    void setViewTabOrder();
    bool switchBlocked() const;
    QString errorText(const AppError& error) const;
    void refresh(bool forceStatus = false);
    void chooseRecordingDirectory();
    QString text(const char* key, const QJsonObject& params = {}) const;

    AcquisitionController* controller_;
    I18nService* i18n_;
    QTimer* timer_;
    WaveformWidget* traces_;
    bool sharedWaveform_ = false, liveViewActive_ = false, autoStartPending_ = false;
    quint64 autoStartGeneration_ = 0;
    QToolButton *liveAdvancedToggle_, *connectionAdvancedToggle_, *viewHelp_;
    QWidget *liveAdvanced_, *connectionAdvanced_;
    QLabel *title_, *deviceHeading_, *device_, *settingsHeading_, *rateLabel_, *status_, *quality_,
           *recordStatus_, *hint_, *unsupported_, *viewStatus_, *scaleNotice_, *channelLimit_,
           *sourceLabel_, *hostLabel_, *portLabel_, *identityLabel_, *tcpNotice_;
    QPushButton *connect_, *apply_, *acquire_, *record_, *freeze_, *openLogs_, *resetView_, *openRecording_;
    QWidget* recordingNotice_ = nullptr;
    QComboBox *rate_, *source_, *viewWindow_, *viewChannel_;
    QComboBox* simulatorProfile_;
    QLabel* simulatorProfileLabel_;
    bool pendingSimulatorIca_ = true;
    QLabel *viewWindowLabel_, *viewChannelLabel_;
    QStringList viewChannelNames_;
    int appliedViewChannel_ = -2;
    double viewEndOffset_ = 0;
    QLineEdit *host_, *expectedIdentity_;
    QSpinBox* port_;
    QStackedWidget* pages_;
    QWidget *managerHeader_, *tcpPage_;
    QLabel *managerTitle_, *managerIntro_, *managerState_, *managerSummary_, *tcpTitle_, *tcpIntro_, *tcpError_,
           *wifiNetworkHeading_, *wifiParametersHeading_, *wifiNext_;
    QPushButton *managerDisconnect_, *tcpConnect_;
    QWidget *listPage_, *wifiPage_, *livePage_, *wifiAdvanced_;
    QVBoxLayout* recentLayout_;
    QLabel *listTitle_, *listIntro_, *addHeading_, *rtTitle_, *rtDescription_, *recentHeading_, *listNotice_,
           *profileNotice_, *wifiTitle_, *wifiStatus_, *wifiDetail_, *wifiGainNotice_, *wifiError_,
           *wifiNameLabel_, *wifiChannelsLabel_, *wifiRateLabel_, *wifiHostLabel_, *wifiHttpLabel_,
           *wifiListenLabel_, *wifiLocalLabel_, *wifiGainLabel_, *wifiReferenceLabel_, *wifiIdLabel_;
    QPushButton *listWifi_, *listSimulator_, *listTcp_, *listCurrent_, *liveList_, *wifiBack_,
                *wifiSettings_, *wifiRetry_, *wifiConnect_, *wifiCopyDiagnostics_;
    QToolButton* wifiAdvancedToggle_;
    QLineEdit *wifiName_, *wifiHost_, *wifiLocal_, *wifiExpectedId_;
    QSpinBox *wifiHttpPort_, *wifiListenPort_;
    QDoubleSpinBox* wifiReference_;
    QComboBox *wifiChannels_, *wifiRate_, *wifiGain_;
    RtBciWifiProbe* probe_;
    QTimer *probeTimer_, *networkTimer_;
    quint64 probeGeneration_ = 0;
    int probesRemaining_ = 0;
    bool probeInFlight_ = false, probeCompatible_ = false, wifiConnecting_ = false;
    bool wifiOpenStarted_ = false, waitingForDisconnect_ = false, networkSettingsError_ = false, reprobeWhenIdle_ = false;
    bool profilesWritable_ = true, profilesWarning_ = false;
    Adapter adapter_ = Adapter::None;
    Adapter pendingAdapter_ = Adapter::None;
    QString networkFingerprint_, selectedProfileId_, detectedDeviceId_, detectedModel_, detectedLocalAddress_;
    AppError probeError_, wifiConnectionError_;
    RtBciWifiOptions pendingWifiOptions_;
    std::vector<Profile> profiles_;
    bool frozen_ = false;
    int lastAppliedRate_ = 250;
    QElapsedTimer statusRefresh_;
    std::vector<AcquisitionTrace> frozenTraces_;
};
} // namespace bf
