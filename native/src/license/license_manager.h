#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include "license/clock_rollback.h"
#include "license/license_key.h"

#include <QString>

namespace bf {

// Native offline license manager. Owns the public-key source and the
// license.dat read/save/verify lifecycle. No user-visible localized text.
//
// Clock-rollback gate: a monotonic UTC watermark (.last_run next to
// license.dat, see clock_rollback.h) is checked and advanced on every
// successful verification and on every save. Missing, corrupted, rolled-back,
// or unwritable watermark state is a hard, structured failure — the state
// file is never auto-fixed, deleted, or overwritten with a lower value.
// The sole exception is resetClockWatermarkAfterOnlineAuthorization, which
// only runs right after the license server granted this machine a license.
//
// Public key source (priority order):
//   1. CMake-injected embed via BF_LICENSE_PUBLIC_KEY_EMBED (generated from
//      BF_LICENSE_PUBLIC_KEY_FILE when BF_REQUIRE_LICENSE=ON).
//   2. Runtime file license_public.pem in BF_RESOURCE_ROOT or application dir.
//   3. Empty -> NotConfigured (when BF_REQUIRE_LICENSE=OFF this is the dev default).
//
// license.dat path: %APPDATA%\BoundFreeEEG\license.dat on Windows;
//   $XDG_DATA_HOME/boundfree-eeg/license.dat or ~/.local/share/boundfree-eeg/
//   on non-Windows (development).
class LicenseManager {
public:
    LicenseManager(QString productVersion, QString resourceRoot);

    // The configured public key PEM. Empty when not configured.
    const QString& publicKeyPem() const;
    // True when a release public key is present (embed or runtime file).
    bool isConfigured() const;
    // True when the RSA crypto backend is available (Windows CNG, other
    // platforms OpenSSL).
    bool isBackendAvailable() const;

    // license.dat I/O. loadSavedLicense() returns the raw key string or
    // an empty string when the file is missing/unreadable.
    QString licenseFilePath() const;
    QString loadSavedLicense() const;

    // Save licenseKey as license.dat. The key is verified against the
    // configured public key and the local machine fingerprint first; an
    // invalid key never touches watermark state or license.dat. First
    // authorization (no license.dat yet) initializes the clock watermark
    // first and only writes the license when the watermark is established;
    // an existing license.dat requires a passing
    // checkAndAdvanceClockWatermark before it may be overwritten — the
    // watermark is never reset on this path. Returns Valid on success, or a
    // structured status/error (ClockRollback, ClockStateMissing,
    // ClockStateCorrupted, ClockStateWriteFailure, SaveFailed) on failure.
    LicenseResult saveLicense(const QString& licenseKey) const;

    // Path of the clock watermark state file (.last_run in the license
    // data directory).
    QString clockWatermarkFilePath() const;
    // Runs checkAndAdvanceClockWatermark on the watermark file and returns
    // the raw module outcome (no mapping, no license side effects).
    ClockRollbackResult checkClockRollback() const;

    // 只供 OnlineActivationClient 成功响应调用。内部必须先 verifyKey，
    // 然后 reset 水位，最后原子写 license.dat；任一步失败均不写 license.dat。
    LicenseResult saveOnlineLicense(const QString& licenseKey) const;

    // Verify a raw key string against the configured public key and the
    // local machine fingerprint.
    LicenseResult verifyKey(const QString& licenseKey) const;

    // Verify the saved license.dat. Returns Missing when the file is absent;
    // on a valid key the clock watermark is checked and its failure mapped to
    // a structured status (never Missing or KeyFormatError).
    LicenseResult checkSavedLicense() const;

    // Convenience for deterministic tests.
    LicenseResult verifyKey(const QString& licenseKey, const QString& localMachineIdCanonical,
                            const QString& todayIso) const;

private:
    QString resolvePublicKeyPem() const;
    QString resolveLocalMachineIdCanonical() const;

    QString productVersion_;
    QString resourceRoot_;
    QString publicKeyPem_;
};

}  // namespace bf
