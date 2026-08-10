#include "license/online_activation_client.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QUrlQuery>

namespace bf {
namespace {

QUrl apiUrl(const QUrl& serverUrl, const QString& path) {
    QUrl url = serverUrl;
    QString basePath = url.path();
    if (basePath.endsWith('/')) {
        basePath.chop(1);
    }
    url.setPath(basePath + path);
    url.setQuery(QString());
    return url;
}

QJsonObject parseJsonObject(const QByteArray& data) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    return document.object();
}

QDate parseExpiresAt(const QJsonObject& body) {
    return QDate::fromString(body.value("expires_at").toString(), Qt::ISODate);
}

}  // namespace

OnlineActivationClient::OnlineActivationClient(QNetworkAccessManager* nam, const QUrl& serverUrl,
                                               QObject* parent)
    : QObject(parent)
    , nam_(nam)
    , serverUrl_(serverUrl)
    , serverHostName_(serverUrl.host()) {}

void OnlineActivationClient::setServerUrl(const QUrl& url) {
    serverUrl_ = url;
    serverHostName_ = serverUrl_.host();
}

QNetworkReply* OnlineActivationClient::postJson(const QString& path, const QJsonObject& body,
                                                bool useIpFallback) {
    QUrl url = apiUrl(serverUrl_, path);
    if (useIpFallback) {
        url.setHost(QString::fromLatin1(kFallbackServerIp));
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(kTimeoutMs);
    if (useIpFallback) {
        request.setRawHeader("Host", serverHostName_.toUtf8());
        request.setPeerVerifyName(serverHostName_);
    }
    return nam_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QNetworkReply* OnlineActivationClient::getStatus(const QString& machineId, bool useIpFallback) {
    QUrl url = apiUrl(serverUrl_, QStringLiteral("/api/v1/status"));
    if (useIpFallback) {
        url.setHost(QString::fromLatin1(kFallbackServerIp));
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("machine_id"), machineId);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    if (useIpFallback) {
        request.setRawHeader("Host", serverHostName_.toUtf8());
        request.setPeerVerifyName(serverHostName_);
    }
    return nam_->get(request);
}

bool OnlineActivationClient::shouldRetryViaFallback(QNetworkReply* reply, int statusCode) {
    return (statusCode == 0 && reply->error() != QNetworkReply::NoError) ||
           (statusCode >= 500 && statusCode < 600);
}

void OnlineActivationClient::registerDevice(const QString& machineId, const QString& clientVersion) {
    const QString path = QStringLiteral("/api/v1/register");
    const QJsonObject body = {{"machine_id", machineId}, {"client_version", clientVersion}};
    QNetworkReply* reply = postJson(path, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, body] {
        handleRegisterResponse(reply, path, body, false);
    });
}

void OnlineActivationClient::activateWithCode(const QString& machineId, const QString& inviteCode,
                                              const QString& clientVersion) {
    const QString path = QStringLiteral("/api/v1/activate");
    const QJsonObject body = {{"machine_id", machineId}, {"invite_code", inviteCode},
                              {"client_version", clientVersion}};
    QNetworkReply* reply = postJson(path, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, body] {
        handleActivateResponse(reply, path, body, false);
    });
}

void OnlineActivationClient::fetchLicense(const QString& machineId, const QString& clientVersion) {
    const QString path = QStringLiteral("/api/v1/activate");
    const QJsonObject body = {{"machine_id", machineId}, {"client_version", clientVersion}};
    QNetworkReply* reply = postJson(path, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, body] {
        handleActivateResponse(reply, path, body, false);
    });
}

void OnlineActivationClient::checkStatus(const QString& machineId) {
    QNetworkReply* reply = getStatus(machineId);
    connect(reply, &QNetworkReply::finished, this, [this, reply, machineId] {
        handleStatusResponse(reply, machineId, false);
    });
}

void OnlineActivationClient::handleRegisterResponse(QNetworkReply* reply, const QString& path,
                                                    const QJsonObject& body, bool usedIpFallback) {
    const QByteArray data = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!usedIpFallback && shouldRetryViaFallback(reply, statusCode)) {
        QNetworkReply* fallbackReply = postJson(path, body, true);
        connect(fallbackReply, &QNetworkReply::finished, this, [this, fallbackReply, path, body] {
            handleRegisterResponse(fallbackReply, path, body, true);
        });
        reply->deleteLater();
        return;
    }
    if (statusCode > 0) {
        if (statusCode == 202) {
            emit registrationSubmitted(parseJsonObject(data).value("request_id").toString());
        } else if (extractErrorCode(reply, data) == QStringLiteral("activation.already_exists")) {
            emit registrationAlreadyExists();
        } else {
            const QString errorCode = extractErrorCode(reply, data);
            emit activationFailed(errorCode, extractErrorParams(data));
        }
    } else {
        emit networkError(reply->errorString());
    }
    reply->deleteLater();
}

void OnlineActivationClient::handleActivateResponse(QNetworkReply* reply, const QString& path,
                                                    const QJsonObject& body, bool usedIpFallback) {
    const QByteArray data = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!usedIpFallback && shouldRetryViaFallback(reply, statusCode)) {
        QNetworkReply* fallbackReply = postJson(path, body, true);
        connect(fallbackReply, &QNetworkReply::finished, this, [this, fallbackReply, path, body] {
            handleActivateResponse(fallbackReply, path, body, true);
        });
        reply->deleteLater();
        return;
    }
    if (statusCode == 200) {
        const QJsonObject response = parseJsonObject(data);
        const QString key = response.value("license_key").toString();
        const QDate expiresAt = parseExpiresAt(response);
        if (key.isEmpty() || !expiresAt.isValid()) {
            emit activationFailed(QStringLiteral("activation.invalid_response"), {});
        } else {
            emit activationSuccess(key, expiresAt);
        }
    } else if (statusCode > 0 &&
               extractErrorCode(reply, data) == QStringLiteral("activation.pending_approval")) {
        emit statusReceived(QStringLiteral("pending_approval"), QString(), QDate());
    } else if (statusCode > 0) {
        emit activationFailed(extractErrorCode(reply, data), extractErrorParams(data));
    } else {
        emit networkError(reply->errorString());
    }
    reply->deleteLater();
}

void OnlineActivationClient::handleStatusResponse(QNetworkReply* reply, const QString& machineId,
                                                  bool usedIpFallback) {
    const QByteArray data = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (!usedIpFallback && shouldRetryViaFallback(reply, statusCode)) {
        QNetworkReply* fallbackReply = getStatus(machineId, true);
        connect(fallbackReply, &QNetworkReply::finished, this, [this, fallbackReply, machineId] {
            handleStatusResponse(fallbackReply, machineId, true);
        });
        reply->deleteLater();
        return;
    }
    if (statusCode == 200) {
        const QJsonObject response = parseJsonObject(data);
        emit statusReceived(response.value("status").toString(), response.value("reason").toString(),
                            parseExpiresAt(response));
    } else if (statusCode > 0) {
        emit activationFailed(extractErrorCode(reply, data), extractErrorParams(data));
    } else {
        emit networkError(reply->errorString());
    }
    reply->deleteLater();
}

QString OnlineActivationClient::extractErrorCode(QNetworkReply* reply, const QByteArray& data) {
    const QJsonObject response = parseJsonObject(data);
    const QString code = response.value("error").toObject().value("code").toString();
    if (!code.isEmpty()) {
        return code;
    }
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return statusCode > 0 ? QStringLiteral("activation.request_failed")
                          : QStringLiteral("activation.network_error");
}

QJsonObject OnlineActivationClient::extractErrorParams(const QByteArray& data) {
    return parseJsonObject(data).value("error").toObject().value("params").toObject();
}

}  // namespace bf
