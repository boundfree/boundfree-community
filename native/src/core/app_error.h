#pragma once

#include <QJsonObject>
#include <QString>

namespace bf {

struct AppError {
    QString code;
    QJsonObject params;
    bool retryable = false;
    QString traceId;
};

AppError makeError(const QString& code, const QJsonObject& params = {}, bool retryable = false);
bool isRetryableError(const QString& code);

}  // namespace bf
