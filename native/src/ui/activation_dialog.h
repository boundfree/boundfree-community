#pragma once

#include <QDate>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QNetworkAccessManager>

namespace bf {

class LicenseManager;
class OnlineActivationClient;
struct VersionInfo;
class I18nService;

class ActivationDialog : public QDialog {
    Q_OBJECT
public:
    ActivationDialog(LicenseManager* manager, const VersionInfo& version,
                     I18nService* i18n, QWidget* parent = nullptr);

private slots:
    void onRequestActivation();
    void onInviteCode();
    void onOfflineActivate();
    void onCopyMachineId();
    void onRefreshStatus();

    void onRegistrationSubmitted(const QString& requestId);
    void onRegistrationAlreadyExists();
    void onActivationSuccess(const QString& licenseKey, const QDate& expiresAt);
    void onActivationFailed(const QString& errorCode, const QString& message);
    void onStatusReceived(const QString& status, const QString& message, const QDate& expiresAt);
    void onNetworkError(const QString& detail);

private:
    void buildUi();
    void setState(const QString& stateLabel, bool showStatusBar);
    void startPolling();
    void stopPolling();

    LicenseManager* manager_;
    I18nService* i18n_;
    QString productVersion_;
    QString canonicalMachineId_;
    QString displayMachineId_;

    // UI elements
    QLineEdit* machineIdEdit_;
    QPushButton* copyBtn_;
    QPushButton* requestBtn_;
    QPushButton* inviteBtn_;
    QPlainTextEdit* offlineKeyEdit_;
    QPushButton* offlineActivateBtn_;
    QLabel* statusLabel_;
    QPushButton* refreshBtn_;

    // Network
    QNetworkAccessManager* nam_;
    OnlineActivationClient* client_;
    QTimer* pollTimer_;

    enum State { IDLE, REGISTERING, WAITING_APPROVAL, ACTIVATING, VERIFYING };
    State state_ = IDLE;
};

}  // namespace bf
