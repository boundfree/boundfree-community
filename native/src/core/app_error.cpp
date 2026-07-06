#include "core/app_error.h"

#include <QDateTime>

namespace bf {
namespace {

int nextTraceId() {
    static int counter = 0;
    return ++counter;
}

}  // namespace

bool isRetryableError(const QString& code) {
    return code == "ipc.sidecar_start_failed" || code == "ipc.sidecar_timeout" ||
           code == "ipc.sidecar_exited" || code == "sidecar.initialization_failed";
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
