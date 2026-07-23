// Minimal app_error stub for the standalone license smoke target.
// Avoids pulling in the full app_error.cpp (which loads error_codes.json from
// disk and is exercised by the main bf_app target). The smoke test only checks
// LicenseStatus enums, not AppError envelope contents.
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
    Q_UNUSED(code)
    return false;
}

AppError makeError(const QString& code, const QJsonObject& params, bool retryable) {
    AppError error;
    error.code = code;
    error.params = params;
    error.retryable = retryable;
    error.traceId = QString("%1-%2")
                        .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd"))
                        .arg(nextTraceId(), 6, 10, QLatin1Char('0'));
    return error;
}

}  // namespace bf
