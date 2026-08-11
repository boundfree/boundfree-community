#pragma once

#include "core/app_error.h"

#include <QJsonObject>
#include <QString>

namespace bf {

// License verification status codes. Stable contract; consumed by the UI layer
// which maps these to i18n messages. Do NOT embed user-visible text here.
//
// Status            | Meaning
// -------------------+-----------------------------------------------------
// Valid             | Signature verified, machine matches, not expired.
// Missing           | No license.dat file present.
// InvalidSignature  | Envelope or RSA-2048 PSS signature rejected.
// WrongMachine      | payload.machine_id does not match local machine id.
// Expired           | payload.expiry is before today.
// KeyFormatError    | Envelope structure (base64url / length prefix / JSON) bad.
// NotConfigured     | No release public key configured (BF_REQUIRE_LICENSE=OFF).
// BackendNotAvailable| Crypto backend unavailable (Windows CNG / OpenSSL).
enum class LicenseStatus {
    Valid,
    Missing,
    InvalidSignature,
    WrongMachine,
    Expired,
    KeyFormatError,
    NotConfigured,
    BackendNotAvailable,
};

// Structured license outcome: status + payload (on success) + AppError envelope.
struct LicenseResult {
    LicenseStatus status = LicenseStatus::KeyFormatError;
    QJsonObject payload;
    AppError error;

    bool ok() const { return status == LicenseStatus::Valid; }
};

// Pure-function license key envelope decoder + verifier entry points.
// These are exposed for internal smoke tests and do not depend on the OS
// crypto backend for the *envelope* path (only signature verification does).

// Decode the legacy key envelope.
//
// Format: Base64url, '.' separators optional/ignorable, no '=' padding.
// Decoded bytes layout:
//   [4 bytes big-endian JSON payload length][JSON payload bytes][RSA-2048 PSS signature]
//
// On success fills outPayloadBytes (the canonical JSON bytes the signature was
// computed over), outPayload (parsed JSON object) and outSignature. Returns
// LicenseStatus::KeyFormatError on any structural failure. Pure function,
// no crypto backend required.
LicenseResult decodeLicenseEnvelope(const QString& licenseKey, QByteArray* outPayloadBytes = nullptr,
                                    QByteArray* outSignature = nullptr);

// Verify a decoded envelope against a public key PEM and the local machine id.
//
// publicKeyPem: SubjectPublicKeyInfo PEM ("BEGIN PUBLIC KEY"). Must be non-empty;
//   if empty the status is NotConfigured.
// signature / payloadBytes: the split produced by decodeLicenseEnvelope.
// localMachineIdCanonical: 32 uppercase hex, no separators (from machine_id.h).
// productVersion: current product version (version.json) for payload.version
//   sanity comparison (informational only; legacy does not enforce equality).
// todayIso: YYYY-MM-DD override for deterministic tests; empty means QDate::currentDate().
//
// On success status is Valid and payload is populated. Pure at the envelope
// level; signature verification delegates to the platform crypto backend
// (Windows CNG, other platforms OpenSSL). When the backend is unavailable the
// status is BackendNotAvailable.
LicenseResult verifyLicenseEnvelope(const QByteArray& payloadBytes, const QByteArray& signature,
                                    const QString& publicKeyPem, const QString& localMachineIdCanonical,
                                    const QString& productVersion, const QString& todayIso = QString());

// High-level convenience: decode + verify + machine/expiry checks.
LicenseResult verifyLicenseKey(const QString& licenseKey, const QString& publicKeyPem,
                               const QString& localMachineIdCanonical, const QString& productVersion,
                               const QString& todayIso = QString());

}  // namespace bf
