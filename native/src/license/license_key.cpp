#include "license/license_key.h"

#include "license/license_crypto.h"

#include <QByteArray>
#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

namespace bf {
namespace {

// Decode the legacy key envelope (pure, no crypto backend).
//
// Key format: Base64url, '.' separators optional/ignorable, no '=' padding.
// Decoded bytes layout:
//   [4 bytes big-endian JSON payload length][JSON payload bytes][RSA-2048 PSS signature]
LicenseResult decodeEnvelope(const QString& licenseKey, QByteArray* outPayloadBytes,
                             QByteArray* outSignature) {
    LicenseResult result;
    result.status = LicenseStatus::KeyFormatError;
    result.error = makeError("license.invalid_signature", {{"reason", "envelope"}});

    if (licenseKey.isEmpty()) {
        return result;
    }

    // Strip '.' separators and whitespace; Base64url characters only.
    QString compact = licenseKey;
    compact.remove('.').remove(' ').remove('\n').remove('\r').remove('\t');

    // Re-pad to a multiple of 4 with '='.
    const int pad = (4 - compact.length() % 4) % 4;
    compact.append(QString("=").repeated(pad));

    // QByteArray::fromBase64 with Base64UrlEncoding + padding.
    const QByteArray raw = QByteArray::fromBase64(compact.toLatin1(), QByteArray::Base64UrlEncoding);
    if (raw.size() < 5) {  // 4 length bytes + at least 1 payload byte
        return result;
    }

    const quint32 payloadLen = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(raw.constData()));
    if (payloadLen == 0 || static_cast<quint32>(raw.size()) < 4u + payloadLen + 1u) {
        return result;  // not enough room for payload + signature
    }

    const QByteArray payloadBytes = raw.mid(4, static_cast<int>(payloadLen));
    const QByteArray signature = raw.mid(4 + static_cast<int>(payloadLen));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payloadBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }

    if (outPayloadBytes) {
        *outPayloadBytes = payloadBytes;
    }
    if (outSignature) {
        *outSignature = signature;
    }

    result.payload = doc.object();
    result.status = LicenseStatus::Valid;
    result.error = {};
    return result;
}

// Parse an ISO date (YYYY-MM-DD) defensively.
bool parseIsoDate(const QString& s, QDate* out) {
    if (s.isEmpty()) {
        return false;
    }
    const QDate d = QDate::fromString(s, Qt::ISODate);
    if (!d.isValid()) {
        return false;
    }
    *out = d;
    return true;
}

}  // namespace

LicenseResult decodeLicenseEnvelope(const QString& licenseKey, QByteArray* outPayloadBytes,
                                    QByteArray* outSignature) {
    return decodeEnvelope(licenseKey, outPayloadBytes, outSignature);
}

LicenseResult verifyLicenseEnvelope(const QByteArray& payloadBytes, const QByteArray& signature,
                                    const QString& publicKeyPem, const QString& localMachineIdCanonical,
                                    const QString& productVersion, const QString& todayIso) {
    LicenseResult result;

    if (publicKeyPem.isEmpty()) {
        result.status = LicenseStatus::NotConfigured;
        result.error = makeError("license.invalid_signature", {{"reason", "not_configured"}});
        return result;
    }

    if (!license_crypto::isBackendAvailable()) {
        result.status = LicenseStatus::BackendNotAvailable;
        result.error = makeError("license.invalid_signature", {{"reason", "backend_unavailable"}});
        return result;
    }

    // Signature verification.
    if (!license_crypto::verifyRsaPssSha256(publicKeyPem, payloadBytes, signature)) {
        result.status = LicenseStatus::InvalidSignature;
        result.error = makeError("license.invalid_signature", {{"reason", "signature"}});
        return result;
    }

    // Parse the payload (already verified bytes).
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payloadBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.status = LicenseStatus::KeyFormatError;
        result.error = makeError("license.invalid_signature", {{"reason", "payload_json"}});
        return result;
    }
    const QJsonObject payload = doc.object();
    result.payload = payload;

    // Machine id check.
    const QString keyMachineId = payload.value("machine_id").toString().toUpper().remove('-');
    if (keyMachineId.isEmpty() || localMachineIdCanonical.isEmpty() ||
        keyMachineId != localMachineIdCanonical.toUpper()) {
        result.status = LicenseStatus::WrongMachine;
        result.error = makeError("license.invalid_signature", {{"reason", "wrong_machine"}});
        return result;
    }

    // Expiry check (YYYY-MM-DD). Legacy default '2099-12-31' when absent.
    const QString expiryStr = payload.value("expiry").toString("2099-12-31");
    QDate expiry;
    if (!parseIsoDate(expiryStr, &expiry)) {
        result.status = LicenseStatus::KeyFormatError;
        result.error = makeError("license.invalid_signature", {{"reason", "expiry_format"}});
        return result;
    }
    const QDate today = todayIso.isEmpty() ? QDate::currentDate() : QDate::fromString(todayIso, Qt::ISODate);
    if (today > expiry) {
        result.status = LicenseStatus::Expired;
        result.error = makeError("license.expired", {{"expires_at", expiryStr}});
        return result;
    }

    // Version field: informational only (legacy does not enforce equality).
    // Ensure the field is present so the payload shape is well-formed.
    const QString version = payload.value("version").toString();
    if (version.isEmpty()) {
        result.status = LicenseStatus::KeyFormatError;
        result.error = makeError("license.invalid_signature", {{"reason", "version_missing"}});
        return result;
    }
    Q_UNUSED(productVersion)

    // Expose the effective legacy expiry even when the signed payload omitted it.
    result.payload.insert("expiry", expiryStr);
    result.status = LicenseStatus::Valid;
    result.error = {};
    return result;
}

LicenseResult verifyLicenseKey(const QString& licenseKey, const QString& publicKeyPem,
                               const QString& localMachineIdCanonical, const QString& productVersion,
                               const QString& todayIso) {
    QByteArray payloadBytes;
    QByteArray signature;
    LicenseResult decoded = decodeLicenseEnvelope(licenseKey, &payloadBytes, &signature);
    if (decoded.status != LicenseStatus::Valid) {
        // decodeEnvelope sets KeyFormatError on failure; surface as-is.
        return decoded;
    }
    return verifyLicenseEnvelope(payloadBytes, signature, publicKeyPem,
                                 localMachineIdCanonical, productVersion, todayIso);
}

}  // namespace bf
