#include "core/app_error.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace bf {
namespace {

int nextTraceId() {
    static int counter = 0;
    return ++counter;
}

QJsonArray loadErrorCodes() {
    QFile file(QDir(QStringLiteral(BF_REPO_ROOT)).filePath("contracts/error_codes.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object().value("error_codes").toArray();
}

}  // namespace

bool isRetryableError(const QString& code) {
    const QJsonArray codes = loadErrorCodes();
    for (const QJsonValue& value : codes) {
        const QJsonObject entry = value.toObject();
        if (entry.value("code").toString() == code) {
            return entry.value("retryable").toBool(false);
        }
    }
    return false;
}

AppError makeError(const QString& code, const QJsonObject& params, bool retryable) {
    AppError error;
    error.code = code;
    error.params = params;
    error.retryable = retryable || isRetryableError(code);
    error.traceId = QString("%1-%2")
                        .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd"))
                        .arg(nextTraceId(), 6, 10, QLatin1Char('0'));
    return error;
}

}  // namespace bf
