#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include "license/license_key.h"

#include <QString>

namespace bf {

// Native offline license manager. Owns the public-key source and the
// license.dat read/save/verify lifecycle. No user-visible localized text.
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
    // True when the RSA crypto backend is available (Windows CNG).
    bool isBackendAvailable() const;

    // license.dat I/O. loadSavedLicense() returns the raw key string or
    // an empty string when the file is missing/unreadable.
    QString licenseFilePath() const;
    QString loadSavedLicense() const;
    bool saveLicense(const QString& licenseKey) const;

    // Verify a raw key string against the configured public key and the
    // local machine fingerprint.
    LicenseResult verifyKey(const QString& licenseKey) const;

    // Verify the saved license.dat. Returns Missing when the file is absent.
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
