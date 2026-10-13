#pragma once

#include <QDateTime>
#include <QString>

namespace bf {

// Outcome of a clock watermark check/initialize call.
enum class ClockRollbackStatus {
    Ok,
    MissingState,
    CorruptedState,
    RollbackDetected,
    WriteFailure,
};

struct ClockRollbackResult {
    ClockRollbackStatus status = ClockRollbackStatus::Ok;
    // English diagnostic for logs/dev tests only — never user-facing copy.
    QString diagnostic;
    // Watermark read from the existing state file (invalid when none exists).
    QDateTime lastSeenUtc;
    // The effective "now" the call ran with, normalized to UTC.
    QDateTime currentUtc;
    bool ok() const { return status == ClockRollbackStatus::Ok; }
};

// Clock rollback watermark for the license gate.
//
// State file (JSON):
//   {"timestamp":"<UTC ISO-8601>","signature":"<lowercase hex>"}
//   timestamp: a UTC instant in canonical form yyyy-MM-ddTHH:mm:ss.zzzZ
//              (QDateTime::toString(Qt::ISODateWithMs) on a UTC QDateTime).
//   signature: HMAC-SHA256 over the timestamp UTF-8 bytes, keyed by the raw
//              SHA-256 digest of "BoundFree:last-run:" + machineIdCanonical
//              (UTF-8), encoded as lowercase hex.
//
// The state is a monotonic time watermark: backward jumps of up to 300
// seconds are tolerated (Ok, file untouched), larger backward jumps report
// RollbackDetected (file untouched), and the watermark is never lowered,
// repaired, or reset. Any missing, unreadable, invalid, or mismatched state
// is an explicit failure (MissingState/CorruptedState) — the file is never
// auto-fixed, overwritten, or deleted. Writes are atomic (QSaveFile with the
// direct-write fallback disabled), and a failed parent-dir creation, open,
// write, or commit reports WriteFailure.
//
// nowUtc: injected UTC instant for deterministic tests; a default-constructed
// (invalid) QDateTime selects QDateTime::currentDateTimeUtc().
ClockRollbackResult checkAndAdvanceClockWatermark(
    const QString& stateFilePath,
    const QString& machineIdCanonical,
    const QDateTime& nowUtc = QDateTime());

// Creates the watermark, intended for the later "first successful
// authorization" path. If a state file already exists it is handled exactly
// like checkAndAdvanceClockWatermark: checked and advanced, never reset,
// overwritten, or repaired.
ClockRollbackResult initializeClockWatermark(
    const QString& stateFilePath,
    const QString& machineIdCanonical,
    const QDateTime& nowUtc = QDateTime());

// 仅在授权服务器刚刚成功签发此机许可证后调用。该函数有意覆写既有
// 水位，以恢复已升级但缺少/损坏本地状态的合法在线授权；它不是普通
// 启动或离线授权流程的一部分。失败只返回 WriteFailure。
ClockRollbackResult resetClockWatermarkAfterOnlineAuthorization(
    const QString& stateFilePath,
    const QString& machineIdCanonical,
    const QDateTime& nowUtc = QDateTime());

}  // namespace bf
