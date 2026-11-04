#pragma once

#include "license/license_key.h"
#include <FluentQt/FluentQt.h>
#include <QDate>
#include <QDialog>

class QTimer;

namespace bf {

class ActionButton;
class I18nService;
class LicenseManager;
class OnlineActivationClient;
class TextInput;
struct VersionInfo;

class ActivationDialog : public QDialog {
    Q_OBJECT
public:
    ActivationDialog(LicenseManager* manager, const VersionInfo& version,
                     I18nService* i18n, const LicenseResult& initialLicenseResult,
                     QWidget* parent = nullptr);
    static bool requiresOnlineStatusCheck(const LicenseResult& savedLicense);
    static void checkAndHandleRevocation(LicenseManager* manager, const VersionInfo& version,
                                         I18nService* i18n, QWidget* parent);

private slots:
    void onRequestActivation();
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
    void setStatus(const QString& text, fluent::status_info::InfoBar::InfoBarSeverity severity,
                   const QString& action = {});
    void setBusy(bool busy);
    void finishActivation(const QDate& expiresAt);

    LicenseManager* manager_;
    I18nService* i18n_;
    QString productVersion_;
    QString canonicalMachineId_;
    QString displayMachineId_;
    QWidget* requestPanel_ = nullptr;
    TextInput* machineIdEdit_ = nullptr;
    TextInput* orgNameEdit_ = nullptr;
    ActionButton* copyBtn_ = nullptr;
    ActionButton* requestBtn_ = nullptr;
    fluent::textfields::TextEdit* offlineKeyEdit_ = nullptr;
    ActionButton* offlineActivateBtn_ = nullptr;
    fluent::status_info::InfoBar* statusBar_ = nullptr;
    ActionButton* refreshBtn_ = nullptr;
    OnlineActivationClient* client_;
    QTimer* pollTimer_;
    bool busy_ = false;
};

}  // namespace bf
