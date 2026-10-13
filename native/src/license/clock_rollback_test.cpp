// Clock rollback watermark module self-test.
// Style references processing_view_state_test.cpp: custom main, counts
// failures, prints FAIL lines and a PASS/FAIL summary. No GUI, no network.
// Uses QTemporaryDir and injected timestamps for determinism.
//
// Coverage (contract matrix of bf::checkAndAdvanceClockWatermark /
// bf::initializeClockWatermark):
//   (1) check + missing file            -> MissingState, nothing created
//   (2) initialize + missing file       -> writes now, Ok
//   (3) valid state, now > lastSeen     -> atomic advance write, Ok
//   (4) 300s backward boundary          -> Ok, file unchanged
//   (5) 301s backward jump              -> RollbackDetected, file unchanged
//   (6) within-tolerance jump (120s)    -> Ok, watermark never lowered
//   (7) now == lastSeen                 -> Ok, still valid signed state
//   (8) initialize with existing state  -> same rules as check, never resets
//   (9) default (current-time) now      -> Ok on an old watermark
//   (10) corrupted states               -> CorruptedState, file preserved
//        (bad JSON, non-object, missing/empty fields, invalid/non-UTC
//         timestamps, signature/timestamp tampering, uppercase hex)
//   (11) unwritable path                -> WriteFailure
//   (12) reset (post-online-authorization): missing / corrupted / future
//        watermarks are all overwritten with a valid signed current-UTC
//        state; unwritable path -> WriteFailure; check/initialize semantics
//        are untouched afterwards

#include "license/clock_rollback.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageAuthenticationCode>
#include <QTemporaryDir>
#include <QTimeZone>

#include <iostream>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace {

int failures = 0;

void check(bool ok, const std::string& label) {
    if (!ok) {
        std::cerr << "FAIL: " << label << "\n";
        ++failures;
    }
}

QDateTime utc(int year, int month, int day, int hour, int minute, int second = 0) {
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second), QTimeZone::UTC);
}

QByteArray readFileBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

void writeFileBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        check(false, "harness: cannot open " + path.toStdString() + " for writing");
        return;
    }
    file.write(bytes);
}

// Independent re-derivation of the documented signature contract:
// key = SHA-256("BoundFree:last-run:" + machineId UTF-8), then
// HMAC-SHA256 over the timestamp UTF-8 bytes, encoded as lowercase hex.
QString expectedSignature(const QString& machineIdCanonical, const QString& timestampIso) {
    const QByteArray seed = QByteArrayLiteral("BoundFree:last-run:") + machineIdCanonical.toUtf8();
    const QByteArray key = QCryptographicHash::hash(seed, QCryptographicHash::Sha256);
    const QByteArray mac =
        QMessageAuthenticationCode::hash(timestampIso.toUtf8(), key, QCryptographicHash::Sha256);
    return QString::fromLatin1(mac.toHex());
}

bool parseStateFile(const QString& path, QString* timestampIso, QString* signature) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(readFileBytes(path), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonObject obj = doc.object();
    if (!obj.contains(QLatin1String("timestamp")) || !obj.contains(QLatin1String("signature"))) {
        return false;
    }
    *timestampIso = obj.value(QLatin1String("timestamp")).toString();
    *signature = obj.value(QLatin1String("signature")).toString();
    return true;
}

bool isLowercaseHex(const QString& s) {
    for (const QChar c : s) {
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
              (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::cerr << "clock-rollback-test-failed cannot create QTemporaryDir\n";
        return 1;
    }
    const QString machineId = QStringLiteral("00112233445566778899AABBCCDDEEFF");
    const QString statePath = dir.filePath(QStringLiteral(".last_run"));
    const QDateTime t0 = utc(2026, 1, 1, 1, 0, 0);  // first watermark

    // ----- (1) check + missing file -> MissingState, nothing created ------
    {
        const bf::ClockRollbackResult r = bf::checkAndAdvanceClockWatermark(statePath, machineId, t0);
        check(r.status == bf::ClockRollbackStatus::MissingState, "check on missing state reports MissingState");
        check(!r.ok(), "check on missing state is not ok");
        check(!r.lastSeenUtc.isValid(), "check on missing state has no lastSeen");
        check(r.currentUtc == t0, "check reports injected now");
        check(!QFileInfo::exists(statePath), "check on missing state creates nothing");
    }

    // ----- (2) initialize + missing file -> writes now, Ok -----------------
    {
        const bf::ClockRollbackResult r = bf::initializeClockWatermark(statePath, machineId, t0);
        check(r.status == bf::ClockRollbackStatus::Ok && r.ok(), "initialize on missing state returns Ok");
        check(r.currentUtc == t0 && r.lastSeenUtc == t0, "initialize reports written watermark");

        QString ts, sig;
        check(parseStateFile(statePath, &ts, &sig), "initialize wrote parseable state JSON");
        check(ts == t0.toString(Qt::ISODateWithMs), "initialize wrote canonical UTC ISO-8601 timestamp");
        check(sig.size() == 64 && isLowercaseHex(sig), "initialize wrote 64-char lowercase-hex signature");
        check(sig == expectedSignature(machineId, ts), "initialize signed the timestamp with HMAC-SHA256");
    }

    // ----- (3) valid state, now > lastSeen -> atomic advance, Ok -----------
    {
        const QDateTime t1 = t0.addSecs(3600);
        const bf::ClockRollbackResult r = bf::checkAndAdvanceClockWatermark(statePath, machineId, t1);
        check(r.status == bf::ClockRollbackStatus::Ok && r.ok(), "advance reports Ok");
        check(r.lastSeenUtc == t0, "advance reports previous watermark as lastSeen");
        check(r.currentUtc == t1, "advance reports injected now");

        QString ts, sig;
        check(parseStateFile(statePath, &ts, &sig), "advance left parseable state");
        check(ts == t1.toString(Qt::ISODateWithMs), "advance wrote the new timestamp");
        check(sig == expectedSignature(machineId, ts), "advance re-signed the new timestamp");
    }

    // ----- (4)-(6) rollback tolerance on watermark t1 ----------------------
    {
        const QDateTime t1 = t0.addSecs(3600);  // current watermark
        const QByteArray before = readFileBytes(statePath);

        // exactly 300s back: tolerated
        {
            const bf::ClockRollbackResult r =
                bf::checkAndAdvanceClockWatermark(statePath, machineId, t1.addSecs(-300));
            check(r.status == bf::ClockRollbackStatus::Ok && r.ok(), "300s backward jump is tolerated");
            check(r.lastSeenUtc == t1, "tolerated rollback keeps lastSeen");
            check(readFileBytes(statePath) == before, "tolerated rollback leaves file byte-identical");
        }
        // 301s back: rollback detected
        {
            const bf::ClockRollbackResult r =
                bf::checkAndAdvanceClockWatermark(statePath, machineId, t1.addSecs(-301));
            check(r.status == bf::ClockRollbackStatus::RollbackDetected && !r.ok(),
                  "301s backward jump is detected");
            check(r.lastSeenUtc == t1, "detected rollback keeps lastSeen");
            check(readFileBytes(statePath) == before, "detected rollback leaves file byte-identical");
        }
        // 120s back (within tolerance): ok, but watermark is never lowered
        {
            const bf::ClockRollbackResult r =
                bf::checkAndAdvanceClockWatermark(statePath, machineId, t1.addSecs(-120));
            check(r.status == bf::ClockRollbackStatus::Ok && r.ok(), "120s backward jump is tolerated");
            check(readFileBytes(statePath) == before, "within-tolerance rollback never lowers watermark");
        }
    }

    // ----- (7) now == lastSeen -> Ok, state stays valid --------------------
    {
        const QDateTime t1 = t0.addSecs(3600);
        const bf::ClockRollbackResult r = bf::checkAndAdvanceClockWatermark(statePath, machineId, t1);
        check(r.status == bf::ClockRollbackStatus::Ok && r.ok(), "now == lastSeen returns Ok");

        QString ts, sig;
        check(parseStateFile(statePath, &ts, &sig) && ts == t1.toString(Qt::ISODateWithMs),
              "now == lastSeen keeps valid signed state");
        check(sig == expectedSignature(machineId, ts), "now == lastSeen keeps matching signature");
    }

    // ----- (8) initialize with existing state -> same as check, no reset ---
    {
        const QDateTime t1 = t0.addSecs(3600);  // current watermark
        const QByteArray before = readFileBytes(statePath);

        const bf::ClockRollbackResult rBack =
            bf::initializeClockWatermark(statePath, machineId, t1.addSecs(-400));
        check(rBack.status == bf::ClockRollbackStatus::RollbackDetected,
              "initialize with existing state never resets the watermark");
        check(readFileBytes(statePath) == before, "initialize rollback leaves file byte-identical");

        const QDateTime t2 = t1.addSecs(7200);
        const bf::ClockRollbackResult rAdv = bf::initializeClockWatermark(statePath, machineId, t2);
        check(rAdv.status == bf::ClockRollbackStatus::Ok && rAdv.ok(),
              "initialize with existing state advances like check");
        QString ts, sig;
        check(parseStateFile(statePath, &ts, &sig) && ts == t2.toString(Qt::ISODateWithMs),
              "initialize advance wrote the new timestamp");
    }

    // ----- (9) default nowUtc (current time) on an old watermark -----------
    {
        const QString path = dir.filePath(QStringLiteral("default_now.json"));
        const QDateTime old = utc(2020, 1, 1, 0, 0, 0);
        check(bf::initializeClockWatermark(path, machineId, old).ok(), "setup: initialize at 2020");
        const bf::ClockRollbackResult r = bf::checkAndAdvanceClockWatermark(path, machineId);
        check(r.status == bf::ClockRollbackStatus::Ok && r.ok(),
              "default nowUtc advances a 2020 watermark");
        check(r.currentUtc.isValid() && r.lastSeenUtc == old, "default nowUtc reports valid instants");
        QString ts, sig;
        check(parseStateFile(path, &ts, &sig) && ts != old.toString(Qt::ISODateWithMs),
              "default nowUtc wrote a new timestamp");
        check(sig == expectedSignature(machineId, ts), "default nowUtc re-signed the state");
    }

    // ----- (10) corrupted states -> CorruptedState, file preserved ---------
    {
        const QByteArray validSig =
            expectedSignature(machineId, QStringLiteral("2026-01-01T00:00:00.000Z")).toLatin1();
        struct CorruptCase {
            const char* name;
            QByteArray content;
        };
        const CorruptCase cases[] = {
            {"not json", QByteArray("this is not json")},
            {"json array", QByteArray("[1,2,3]")},
            {"json string", QByteArray("\"just a string\"")},
            {"json number", QByteArray("42")},
            {"missing signature",
             QByteArray("{\"timestamp\":\"2026-01-01T00:00:00.000Z\"}")},
            {"missing timestamp", QByteArray("{\"signature\":\"" + validSig + "\"}")},
            {"empty timestamp",
             QByteArray("{\"timestamp\":\"\",\"signature\":\"" + validSig + "\"}")},
            {"empty signature",
             QByteArray("{\"timestamp\":\"2026-01-01T00:00:00.000Z\",\"signature\":\"\"}")},
            {"numeric timestamp",
             QByteArray("{\"timestamp\":123,\"signature\":\"" + validSig + "\"}")},
            {"numeric signature",
             QByteArray("{\"timestamp\":\"2026-01-01T00:00:00.000Z\",\"signature\":42}")},
            {"garbage timestamp",
             QByteArray("{\"timestamp\":\"not-a-date\",\"signature\":\"" + validSig + "\"}")},
            {"date-only timestamp",
             QByteArray("{\"timestamp\":\"2026-01-01\",\"signature\":\"" + validSig + "\"}")},
            {"local timestamp",
             QByteArray("{\"timestamp\":\"2026-01-01T00:00:00.000\",\"signature\":\"" + validSig + "\"}")},
            {"offset timestamp",
             QByteArray("{\"timestamp\":\"2026-01-01T00:00:00.000+08:00\",\"signature\":\"" + validSig + "\"}")},
        };
        for (const CorruptCase& c : cases) {
            const QString path =
                dir.filePath(QStringLiteral("corrupt_") + QString::fromLatin1(c.name) + QStringLiteral(".json"));
            writeFileBytes(path, c.content);
            const bf::ClockRollbackResult r =
                bf::checkAndAdvanceClockWatermark(path, machineId, utc(2026, 1, 1, 12, 0, 0));
            check(r.status == bf::ClockRollbackStatus::CorruptedState,
                  std::string("corrupt case reports CorruptedState: ") + c.name);
            check(!r.ok(), std::string("corrupt case is not ok: ") + c.name);
            check(readFileBytes(path) == c.content,
                  std::string("corrupt case preserves the file: ") + c.name);
        }
    }

    // ----- (10b) signature tampering -> CorruptedState, file preserved -----
    {
        const QString path = dir.filePath(QStringLiteral("tampered_sig.json"));
        check(bf::initializeClockWatermark(path, machineId, utc(2026, 1, 1, 0, 0, 0)).ok(),
              "setup: initialize ok");
        QString ts, sig;
        check(parseStateFile(path, &ts, &sig), "setup: parse ok");
        QByteArray content = readFileBytes(path);
        const QByteArray hexSig = sig.toLatin1();
        const qsizetype idx = content.indexOf(hexSig);
        check(idx >= 0, "setup: signature found in file");
        content[idx] = (hexSig.at(0) == 'a') ? 'b' : 'a';
        writeFileBytes(path, content);

        const bf::ClockRollbackResult r =
            bf::checkAndAdvanceClockWatermark(path, machineId, utc(2026, 1, 1, 12, 0, 0));
        check(r.status == bf::ClockRollbackStatus::CorruptedState, "flipped signature hex digit is detected");
        check(readFileBytes(path) == content, "signature tamper preserves the tampered file");
    }
    {
        const QString path = dir.filePath(QStringLiteral("tampered_ts.json"));
        check(bf::initializeClockWatermark(path, machineId, utc(2026, 1, 1, 0, 0, 0)).ok(),
              "setup: initialize ok");
        QByteArray content = readFileBytes(path);
        content.replace("2026-01-01T00:00:00.000Z", "2026-06-01T00:00:00.000Z");
        writeFileBytes(path, content);

        const bf::ClockRollbackResult r =
            bf::checkAndAdvanceClockWatermark(path, machineId, utc(2026, 1, 1, 12, 0, 0));
        check(r.status == bf::ClockRollbackStatus::CorruptedState,
              "timestamp changed without re-signing is detected");
        check(readFileBytes(path) == content, "timestamp tamper preserves the tampered file");
    }
    {
        const QString path = dir.filePath(QStringLiteral("uppercase_sig.json"));
        check(bf::initializeClockWatermark(path, machineId, utc(2026, 1, 1, 0, 0, 0)).ok(),
              "setup: initialize ok");
        QByteArray content = readFileBytes(path);
        content = content.toUpper();  // signature must be lowercase hex per contract
        writeFileBytes(path, content);

        const bf::ClockRollbackResult r =
            bf::checkAndAdvanceClockWatermark(path, machineId, utc(2026, 1, 1, 12, 0, 0));
        check(r.status == bf::ClockRollbackStatus::CorruptedState, "uppercase signature hex is rejected");
        check(readFileBytes(path) == content, "uppercase tamper preserves the tampered file");
    }

    // ----- (11) unwritable path -> WriteFailure ----------------------------
    {
        // Parent is a regular file, so mkpath can never succeed.
        const QString blocker = dir.filePath(QStringLiteral("blocker"));
        writeFileBytes(blocker, QByteArray("x"));
        const QString badPath = blocker + QStringLiteral("/state.json");

        const bf::ClockRollbackResult rInit =
            bf::initializeClockWatermark(badPath, machineId, utc(2026, 1, 1, 0, 0, 0));
        check(rInit.status == bf::ClockRollbackStatus::WriteFailure,
              "uncreatable parent reports WriteFailure on initialize");
        check(!rInit.ok(), "write failure is not ok");
        check(!rInit.diagnostic.isEmpty(), "write failure carries a diagnostic");

        const bf::ClockRollbackResult rCheck =
            bf::checkAndAdvanceClockWatermark(badPath, machineId, utc(2026, 1, 1, 0, 0, 0));
        check(rCheck.status == bf::ClockRollbackStatus::MissingState,
              "check on missing (uncreatable) state still reports MissingState");
    }
#if defined(Q_OS_UNIX)
    {
        // Advance write into a read-only directory must fail. Skipped when
        // running as root, where permission bits are not enforced.
        if (::geteuid() != 0) {
            const QString sub = dir.filePath(QStringLiteral("readonly_dir"));
            check(QDir().mkpath(sub), "setup: mkpath readonly_dir");
            const QString path = sub + QStringLiteral("/state.json");
            check(bf::initializeClockWatermark(path, machineId, utc(2026, 1, 1, 0, 0, 0)).ok(),
                  "setup: initialize ok");
            check(QFile::setPermissions(sub, QFileDevice::ReadOwner | QFileDevice::ExeOwner),
                  "setup: make dir read-only");

            const bf::ClockRollbackResult r =
                bf::checkAndAdvanceClockWatermark(path, machineId, utc(2026, 1, 1, 1, 0, 0));
            check(r.status == bf::ClockRollbackStatus::WriteFailure,
                  "advance into read-only directory reports WriteFailure");
            // Restore permissions so QTemporaryDir can clean the tree up.
            QFile::setPermissions(sub, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner);
        }
    }
#endif

    // ----- (12) reset after online authorization overwrites any state ------
    {
        const QDateTime tNow = utc(2026, 2, 1, 6, 0, 0);

        // (12a) missing state -> created with a valid signed current-UTC state
        {
            const QString path = dir.filePath(QStringLiteral("reset_missing"));
            const bf::ClockRollbackResult r =
                bf::resetClockWatermarkAfterOnlineAuthorization(path, machineId, tNow);
            check(r.status == bf::ClockRollbackStatus::Ok && r.ok(),
                  "reset on missing state returns Ok");
            check(r.lastSeenUtc == tNow && r.currentUtc == tNow,
                  "reset on missing state reports the written watermark");
            QString ts, sig;
            check(parseStateFile(path, &ts, &sig), "reset on missing state wrote parseable JSON");
            check(ts == tNow.toString(Qt::ISODateWithMs),
                  "reset wrote canonical UTC ISO-8601 timestamp");
            check(sig.size() == 64 && isLowercaseHex(sig),
                  "reset wrote lowercase-hex signature");
            check(sig == expectedSignature(machineId, ts), "reset signed with HMAC-SHA256");
        }
        // (12b) corrupted state -> overwritten with a valid signed state
        {
            const QString path = dir.filePath(QStringLiteral("reset_corrupt"));
            writeFileBytes(path, QByteArray("garbage that is not json"));
            const bf::ClockRollbackResult r =
                bf::resetClockWatermarkAfterOnlineAuthorization(path, machineId, tNow);
            check(r.status == bf::ClockRollbackStatus::Ok && r.ok(),
                  "reset overwrites corrupted state with Ok");
            QString ts, sig;
            check(parseStateFile(path, &ts, &sig), "reset replaced corrupted state with valid JSON");
            check(ts == tNow.toString(Qt::ISODateWithMs), "reset timestamp is now");
            check(sig == expectedSignature(machineId, ts), "reset re-signed the new state");
        }
        // (12c) future watermark (lastSeen ahead of now) -> overwritten with now
        {
            const QString path = dir.filePath(QStringLiteral("reset_future"));
            const QDateTime future = tNow.addYears(5);
            check(bf::initializeClockWatermark(path, machineId, future).ok(),
                  "setup: initialize a future watermark");
            const bf::ClockRollbackResult r =
                bf::resetClockWatermarkAfterOnlineAuthorization(path, machineId, tNow);
            check(r.status == bf::ClockRollbackStatus::Ok && r.ok(),
                  "reset overwrites a future watermark with Ok");
            QString ts, sig;
            check(parseStateFile(path, &ts, &sig) && ts == tNow.toString(Qt::ISODateWithMs),
                  "reset lowered the future watermark to now");
            check(sig == expectedSignature(machineId, ts), "reset re-signed the lowered state");
        }
        // (12d) reset state is a normal watermark afterwards: check advances it
        {
            const QDateTime tLater = tNow.addSecs(3600);
            const bf::ClockRollbackResult r = bf::checkAndAdvanceClockWatermark(
                dir.filePath(QStringLiteral("reset_missing")), machineId, tLater);
            check(r.status == bf::ClockRollbackStatus::Ok && r.ok(),
                  "check advances a reset watermark normally");
        }
        // (12e) unwritable path -> WriteFailure (never Ok, never auto-clear)
        {
            const QString blocker = dir.filePath(QStringLiteral("reset_blocker"));
            writeFileBytes(blocker, QByteArray("x"));
            const QString badPath = blocker + QStringLiteral("/state");
            const bf::ClockRollbackResult r =
                bf::resetClockWatermarkAfterOnlineAuthorization(badPath, machineId, tNow);
            check(r.status == bf::ClockRollbackStatus::WriteFailure && !r.ok(),
                  "reset on unwritable path reports WriteFailure");
            check(!r.diagnostic.isEmpty(), "reset write failure carries a diagnostic");
        }
    }

    if (failures == 0) {
        std::cout << "clock-rollback-test: PASS\n";
        return 0;
    }
    std::cerr << "clock-rollback-test-failed failures=" << failures << "\n";
    return 1;
}
