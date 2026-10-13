#include "license/clock_rollback.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageAuthenticationCode>
#include <QSaveFile>

namespace bf {
namespace {

// Monotonic watermark tolerance: a backward clock jump of up to 300 seconds
// is tolerated (call succeeds, no write); anything larger is a detected
// rollback. This value is part of the security contract and must not change.
constexpr qint64 kMaxRollbackToleranceMs = 300LL * 1000;

const char kTimestampKey[] = "timestamp";
const char kSignatureKey[] = "signature";
const char kKeySeedPrefix[] = "BoundFree:last-run:";

QByteArray hmacKeyFor(const QString& machineIdCanonical) {
    // HMAC key = SHA-256("BoundFree:last-run:" + machineIdCanonical UTF-8),
    // used as the raw 32-byte digest.
    const QByteArray seed = QByteArray(kKeySeedPrefix) + machineIdCanonical.toUtf8();
    return QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
}

QString signTimestamp(const QString& timestampIso, const QByteArray& hmacKey) {
    const QByteArray mac =
        QMessageAuthenticationCode::hash(timestampIso.toUtf8(), hmacKey, QCryptographicHash::Sha256);
    return QString::fromLatin1(mac.toHex());  // lowercase hex per the state contract
}

// Reads and validates the state file. Returns true and sets *missing when the
// file does not exist; returns true and sets *lastSeenUtc for a trusted
// state; returns false (with diagnostic) for any unreadable, invalid, or
// mismatched state. Never modifies the file.
bool readValidState(const QString& stateFilePath, const QString& machineIdCanonical,
                    bool* missing, QDateTime* lastSeenUtc, QString* diagnostic) {
    *missing = false;
    const QFileInfo info(stateFilePath);
    if (!info.exists()) {
        *missing = true;
        return true;
    }
    QFile file(stateFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        *diagnostic = QStringLiteral("cannot open clock watermark state file for reading");
        return false;
    }
    const QByteArray bytes = file.readAll();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *diagnostic = QStringLiteral("clock watermark state file is not a JSON object");
        return false;
    }
    const QJsonObject obj = doc.object();
    if (!obj.contains(QLatin1String(kTimestampKey)) || !obj.contains(QLatin1String(kSignatureKey))) {
        *diagnostic = QStringLiteral("clock watermark state file is missing required fields");
        return false;
    }
    const QString timestampIso = obj.value(QLatin1String(kTimestampKey)).toString();
    const QString signature = obj.value(QLatin1String(kSignatureKey)).toString();
    if (timestampIso.isEmpty() || signature.isEmpty()) {
        *diagnostic = QStringLiteral("clock watermark state file has empty required fields");
        return false;
    }
    // The state records UTC instants only. Anything that does not parse as a
    // UTC ISO-8601 instant (date-only, local time, explicit offset, garbage)
    // is corrupt state, never silently interpreted.
    const QDateTime timestamp = QDateTime::fromString(timestampIso, Qt::ISODateWithMs);
    if (!timestamp.isValid() || timestamp.timeSpec() != Qt::UTC) {
        *diagnostic = QStringLiteral("clock watermark state file has an invalid or non-UTC timestamp");
        return false;
    }
    if (signature != signTimestamp(timestampIso, hmacKeyFor(machineIdCanonical))) {
        *diagnostic = QStringLiteral("clock watermark state file signature mismatch");
        return false;
    }
    *lastSeenUtc = timestamp;
    return true;
}

// Atomically writes a fresh state for nowUtc. Returns false (with diagnostic)
// on any failure to create the parent directory, open, write, or commit.
bool writeValidState(const QString& stateFilePath, const QString& machineIdCanonical,
                     const QDateTime& nowUtc, QString* diagnostic) {
    const QString timestampIso = nowUtc.toString(Qt::ISODateWithMs);
    QJsonObject obj;
    obj.insert(QLatin1String(kTimestampKey), timestampIso);
    obj.insert(QLatin1String(kSignatureKey), signTimestamp(timestampIso, hmacKeyFor(machineIdCanonical)));
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    const QDir parentDir = QFileInfo(stateFilePath).dir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        *diagnostic = QStringLiteral("cannot create parent directory for clock watermark state file");
        return false;
    }
    QSaveFile file(stateFilePath);
    file.setDirectWriteFallback(false);  // atomic rename only; never fall back to in-place writes
    if (!file.open(QIODevice::WriteOnly)) {
        *diagnostic = QStringLiteral("cannot open clock watermark state file for writing");
        return false;
    }
    if (file.write(payload) != payload.size()) {
        *diagnostic = QStringLiteral("failed to write clock watermark state file");
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *diagnostic = QStringLiteral("failed to commit clock watermark state file");
        return false;
    }
    return true;
}

ClockRollbackResult runClockWatermark(const QString& stateFilePath,
                                      const QString& machineIdCanonical,
                                      const QDateTime& nowUtc, bool createIfMissing) {
    ClockRollbackResult result;
    result.currentUtc = nowUtc.isValid() ? nowUtc.toUTC() : QDateTime::currentDateTimeUtc();

    bool missing = false;
    QDateTime lastSeenUtc;
    if (!readValidState(stateFilePath, machineIdCanonical, &missing, &lastSeenUtc, &result.diagnostic)) {
        result.status = ClockRollbackStatus::CorruptedState;
        return result;
    }
    if (missing) {
        if (!createIfMissing) {
            result.status = ClockRollbackStatus::MissingState;
            return result;
        }
        if (!writeValidState(stateFilePath, machineIdCanonical, result.currentUtc, &result.diagnostic)) {
            result.status = ClockRollbackStatus::WriteFailure;
            return result;
        }
        result.lastSeenUtc = result.currentUtc;
        result.status = ClockRollbackStatus::Ok;
        return result;
    }

    result.lastSeenUtc = lastSeenUtc;
    // Positive when lastSeen is ahead of now, i.e. the clock went backwards.
    const qint64 rollbackMs = result.currentUtc.msecsTo(lastSeenUtc);
    if (rollbackMs <= 0) {
        // now >= lastSeen: advance the watermark atomically.
        if (!writeValidState(stateFilePath, machineIdCanonical, result.currentUtc, &result.diagnostic)) {
            result.status = ClockRollbackStatus::WriteFailure;
            return result;
        }
        result.status = ClockRollbackStatus::Ok;
        return result;
    }
    if (rollbackMs <= kMaxRollbackToleranceMs) {
        // Backward jump within the 300s tolerance: keep going, but never
        // lower the watermark — the file stays untouched.
        result.diagnostic = QStringLiteral("clock rolled back within 300s tolerance; watermark kept");
        result.status = ClockRollbackStatus::Ok;
        return result;
    }
    result.diagnostic = QStringLiteral("clock rollback beyond 300s tolerance detected");
    result.status = ClockRollbackStatus::RollbackDetected;
    return result;
}

}  // namespace

ClockRollbackResult checkAndAdvanceClockWatermark(const QString& stateFilePath,
                                                  const QString& machineIdCanonical,
                                                  const QDateTime& nowUtc) {
    return runClockWatermark(stateFilePath, machineIdCanonical, nowUtc, /*createIfMissing=*/false);
}

ClockRollbackResult initializeClockWatermark(const QString& stateFilePath,
                                             const QString& machineIdCanonical,
                                             const QDateTime& nowUtc) {
    return runClockWatermark(stateFilePath, machineIdCanonical, nowUtc, /*createIfMissing=*/true);
}

ClockRollbackResult resetClockWatermarkAfterOnlineAuthorization(
    const QString& stateFilePath, const QString& machineIdCanonical, const QDateTime& nowUtc) {
    // Deliberately ignores any existing state: the server just granted this
    // machine a license, so the local watermark is reset to now even when it
    // is missing, corrupted, or ahead of the current time. This is the only
    // path allowed to overwrite watermark state; it never reads or validates
    // the old file first, so the only possible failure is the write itself.
    ClockRollbackResult result;
    result.currentUtc = nowUtc.isValid() ? nowUtc.toUTC() : QDateTime::currentDateTimeUtc();
    if (!writeValidState(stateFilePath, machineIdCanonical, result.currentUtc, &result.diagnostic)) {
        result.status = ClockRollbackStatus::WriteFailure;
        return result;
    }
    result.lastSeenUtc = result.currentUtc;
    result.status = ClockRollbackStatus::Ok;
    return result;
}

}  // namespace bf
