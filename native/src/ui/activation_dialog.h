#pragma once

#include "license/license_key.h"

#include <QDate>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QJsonObject>

namespace bf {

class LicenseManager;
class OnlineActivationClient;
struct VersionInfo;
class I18nService;

class ActivationDialog : public QDialog {
    Q_OBJECT
public:
    // initialLicenseResult: the startup checkSavedLicense() outcome. It is
    // consumed immediately (status bar initialization) — the reference is
    // NOT stored beyond the constructor. Structured license failures
    // (license.clock_* / license.save_failed) are shown localized via their
    // error code + params; other results show no status bar (ok) or the
    // generic invalid-key text.
    ActivationDialog(LicenseManager* manager, const VersionInfo& version,
                     I18nService* i18n, const LicenseResult& initialLicenseResult,
                     QWidget* parent = nullptr);

    // Only signed server-managed licenses opt into the startup status check.
    // Legacy keys without an explicit mode are offline-compatible and never
    // depend on a server record.
    static bool requiresOnlineStatusCheck(const LicenseResult& savedLicense);

    // Startup online authorization check (fail-open) for signed server-managed
    // licenses. Returns false and removes the saved license only when the
    // server explicitly reports revoked, rejected, or not_found.
    static bool checkAndHandleRevocation(
        LicenseManager* manager,
        const VersionInfo& version,
        I18nService* i18n);

private slots:
    void onRequestActivation();
    void onInviteCode();
    void onOfflineActivate();
    void onCopyMachineId();
    void onRefreshStatus();

    void onRegistrationSubmitted(const QString& requestId);
    void onRegistrationAlreadyExists();
    void onActivationSuccess(const QString& licenseKey, const QDate& expiresAt);
    void onActivationFailed(const QString& errorCode, const QJsonObject& params);
    void onStatusReceived(const QString& status, const QDate& expiresAt);
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
    QLineEdit* inviteCodeEdit_;
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
