#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QDate>
#include <QTimer>
#include <QJsonObject>

namespace bf {

class OnlineActivationClient : public QObject {
    Q_OBJECT
public:
    explicit OnlineActivationClient(QNetworkAccessManager* nam, const QUrl& serverUrl,
                                    QObject* parent = nullptr);

    void setServerUrl(const QUrl& url);

    // Mode A: submit registration request
    void registerDevice(const QString& machineId, const QString& clientVersion);

    // Mode B: activate with invite code
    void activateWithCode(const QString& machineId, const QString& inviteCode,
                          const QString& clientVersion);

    // Mode A post-approval: fetch license (no invite_code)
    void fetchLicense(const QString& machineId, const QString& clientVersion);

    // Query activation status
    void checkStatus(const QString& machineId);

signals:
    void registrationSubmitted(const QString& requestId);
    void activationSuccess(const QString& licenseKey, const QDate& expiresAt);
    void activationFailed(const QString& errorCode, const QJsonObject& params);
    void statusReceived(const QString& status, const QString& message, const QDate& expiresAt);
    void networkError(const QString& detail);
    void registrationAlreadyExists();

private:
    QNetworkReply* postJson(const QString& path, const QJsonObject& body,
                            bool useIpFallback = false);
    QNetworkReply* getStatus(const QString& machineId, bool useIpFallback = false);
    void handleRegisterResponse(QNetworkReply* reply, const QString& path,
                                const QJsonObject& body, bool usedIpFallback);
    void handleActivateResponse(QNetworkReply* reply, const QString& path,
                                const QJsonObject& body, bool usedIpFallback);
    void handleStatusResponse(QNetworkReply* reply, const QString& machineId,
                              bool usedIpFallback);
    static bool shouldRetryViaFallback(QNetworkReply* reply, int statusCode);
    static QString extractErrorCode(QNetworkReply* reply, const QByteArray& data);
    static QJsonObject extractErrorParams(const QByteArray& data);

    QNetworkAccessManager* nam_;
    QUrl serverUrl_;
    QString serverHostName_;
    static constexpr const char* kFallbackServerIp = "8.163.85.108";

    static constexpr int kTimeoutMs = 10000;
};

}  // namespace bf
