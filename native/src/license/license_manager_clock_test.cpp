// LicenseManager clock-watermark end-to-end test (Linux/WSL2).
//
// Exercises the real integration between the monotonic UTC watermark
// (.last_run, clock_rollback.cpp) and the license.dat lifecycle through the
// public LicenseManager API, using a runtime-generated RSA-2048 key. The
// private key lives only in memory (OpenSSL EVP), never on disk; the public
// key is written to a QTemporaryDir resource root. The license data dir is
// redirected into the same QTemporaryDir via XDG_DATA_HOME, so no user
// APPDATA/XDG data, repository private keys, license-authority material, or
// production license files are ever read or touched.
//
// Scenario (shared context, one machine identity, one valid signed key):
//   (1) QTemporaryDir becomes both resource root and XDG_DATA_HOME; the
//       original XDG_DATA_HOME is saved and restored at the end.
//   (2) RSA-2048 key generated in memory; public key PEM exported to
//       <tmp>/license_public.pem (private key never serialized).
//   (3) machine_id = machineIdCanonical(readMachineGuid()) (via the
//       dev-only BF_MOCK_MACHINE_GUID source); payload signed with
//       RSA-PSS/MGF1-SHA256, MAX salt (server behavior), legacy envelope.
//   (4) saveLicense(validKey) -> Valid; license.dat AND .last_run exist.
//   (5) Delete .last_run: checkSavedLicense() -> ClockStateMissing;
//       saveLicense(validKey) -> ClockStateMissing; license.dat still
//       exists and is byte-identical; watermark is NOT auto-created.
//   (6) saveOnlineLicense(validKey) -> Valid; .last_run restored and
//       verifiable (raw check Ok, checkSavedLicense Valid).
//   (7) Corrupt .last_run: checkSavedLicense() -> ClockStateCorrupted;
//       saveOnlineLicense(validKey) -> Valid; watermark rewritten and
//       verifiable.
//   (8) XDG_DATA_HOME restored; prints "license-manager-clock-test: PASS".
//
// Style references license_smoke.cpp: custom main, check() counter, FAIL
// lines and a PASS/FAIL summary. No GUI, no network, no production data.

#include "license/license_key.h"
#include "license/license_manager.h"
#include "license/machine_id.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* label) {
    if (condition) {
        std::cout << "  ok   " << label << "\n";
    } else {
        std::cout << "  FAIL " << label << "\n";
        ++failures;
    }
}

// Generate an RSA-2048 key pair entirely in memory. On success *outKey owns
// the EVP_PKEY (caller must EVP_PKEY_free) and *outPem receives the
// SubjectPublicKeyInfo PEM — the only serialized part, and only the public
// half. The private key never leaves the EVP_PKEY object.
bool generateRsa2048InMemory(EVP_PKEY** outKey, QString* outPem) {
    EVP_PKEY_CTX* genCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY* pkey = nullptr;
    const bool generated = genCtx != nullptr
        && EVP_PKEY_keygen_init(genCtx) > 0
        && EVP_PKEY_CTX_set_rsa_keygen_bits(genCtx, 2048) > 0
        && EVP_PKEY_keygen(genCtx, &pkey) > 0;
    EVP_PKEY_CTX_free(genCtx);
    if (!generated) {
        return false;
    }

    BIO* pemBio = BIO_new(BIO_s_mem());
    bool pemOk = pemBio != nullptr && PEM_write_bio_PUBKEY(pemBio, pkey) == 1;
    if (pemOk) {
        char* data = nullptr;
        const long len = BIO_get_mem_data(pemBio, &data);
        *outPem = QString::fromLatin1(QByteArray(data, static_cast<int>(len)).trimmed());
    }
    BIO_free(pemBio);

    if (!pemOk) {
        EVP_PKEY_free(pkey);
        return false;
    }
    *outKey = pkey;
    return true;
}

// Sign the payload and wrap it in the legacy key envelope:
//   Base64url(no padding, '.' every 8 chars) of
//   [4 bytes big-endian JSON length][compact JSON][RSA-2048 PSS-SHA256 signature]
// The signature covers exactly the compact JSON bytes, mirroring the
// server's issue_license_key (PSS/MGF1-SHA256, MAX salt length).
QString buildSignedLicenseKey(EVP_PKEY* pkey, const QString& machineId,
                              const QString& productVersion, bool includeExpiry = true) {
    QJsonObject payload;
    payload["machine_id"] = machineId;
    if (includeExpiry) payload["expiry"] = QStringLiteral("2099-12-31");
    payload["version"] = productVersion;
    const QByteArray payloadBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    EVP_PKEY_CTX* pctx = nullptr;
    bool signedOk = mdCtx != nullptr
        && EVP_DigestSignInit(mdCtx, &pctx, EVP_sha256(), nullptr, pkey) > 0
        && EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) > 0
        && EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_MAX) > 0
        && EVP_PKEY_CTX_set_signature_md(pctx, EVP_sha256()) > 0
        && EVP_DigestSignUpdate(mdCtx,
                                reinterpret_cast<const unsigned char*>(payloadBytes.constData()),
                                static_cast<size_t>(payloadBytes.size())) > 0;
    QByteArray signature;
    if (signedOk) {
        size_t sigLen = 0;
        signedOk = EVP_DigestSignFinal(mdCtx, nullptr, &sigLen) > 0;
        signature.resize(static_cast<int>(sigLen));
        signedOk = signedOk
            && EVP_DigestSignFinal(mdCtx,
                                   reinterpret_cast<unsigned char*>(signature.data()),
                                   &sigLen) > 0;
        signature.resize(static_cast<int>(sigLen));
    }
    EVP_MD_CTX_free(mdCtx);
    if (!signedOk || signature.size() != 256) {
        return QString();
    }

    QByteArray raw;
    const quint32 len = static_cast<quint32>(payloadBytes.size());
    for (int i = 3; i >= 0; --i) {
        raw.append(static_cast<char>((len >> (i * 8)) & 0xFF));
    }
    raw.append(payloadBytes);
    raw.append(signature);

    QByteArray b64 = raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QString grouped;
    for (int i = 0; i < b64.size(); i += 8) {
        if (i > 0) {
            grouped += '.';
        }
        grouped += QString::fromLatin1(b64.mid(i, 8));
    }
    return grouped;
}

bool writeFile(const QString& path, const QByteArray& content) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(content) == content.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    (void)app;

    // ---- (1) save the original environment, prepare the temp root ---------
    const QString savedXdgDataHome = qEnvironmentVariable("XDG_DATA_HOME");
    const QString savedMockGuid = qEnvironmentVariable("BF_MOCK_MACHINE_GUID");

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::cerr << "license-manager-clock-test: cannot create QTemporaryDir\n";
        return 1;
    }
    const QString tempRoot = QDir(tempDir.path()).absolutePath();

    // ---- (2) in-memory RSA-2048 keygen; public key PEM to resource root ----
    EVP_PKEY* pkey = nullptr;
    QString publicKeyPem;
    check(generateRsa2048InMemory(&pkey, &publicKeyPem) && publicKeyPem.contains("BEGIN PUBLIC KEY"),
          "RSA-2048 key generated in memory, public PEM serialized");
    if (pkey == nullptr) {
        std::cerr << "license-manager-clock-test: key generation failed\n";
        return 1;
    }
    check(writeFile(QDir(tempRoot).filePath(QStringLiteral("license_public.pem")),
                    publicKeyPem.toUtf8()),
          "public key PEM written to temp resource root (private key never on disk)");

    // ---- (3) machine identity + signed legacy envelope ----------------------
    // BF_MOCK_MACHINE_GUID is the documented non-Windows dev source for
    // readMachineGuid(); the canonical fingerprint is computed exactly like
    // production: machineIdCanonical(readMachineGuid()).
    qputenv("BF_MOCK_MACHINE_GUID", "GUID-001");
    const QString machineId = bf::machineIdCanonical(bf::readMachineGuid());
    check(machineId == "0B9C91576A069C39FA9C30B9C0086DFA", "canonical machine id (legacy golden)");

    const QString productVersion = QStringLiteral("0.1.0-dev");
    const QString validKey = buildSignedLicenseKey(pkey, machineId, productVersion);
    const QString legacyKey = buildSignedLicenseKey(pkey, machineId, productVersion, false);
    EVP_PKEY_free(pkey);
    pkey = nullptr;
    check(!validKey.isEmpty(), "legacy envelope signed with RSA-PSS-SHA256");
    check(!legacyKey.isEmpty(), "legacy license without expiry signed");

    // Redirect the license data dir into the temp root BEFORE any
    // LicenseManager use. licenseDataDir() consults XDG_DATA_HOME on every
    // call; the pre-flight check below guards against a stale environment
    // cache so no I/O can ever escape the temp root.
    qputenv("XDG_DATA_HOME", tempRoot.toUtf8());

    bf::LicenseManager mgr(productVersion, tempRoot);
    const QString licensePath = mgr.licenseFilePath();
    const QString watermarkPath = mgr.clockWatermarkFilePath();
    const QString expectedDataDir = QDir(tempRoot).filePath(QStringLiteral("boundfree-eeg"));
    check(licensePath.startsWith(expectedDataDir) && watermarkPath.startsWith(expectedDataDir),
          "license data dir redirected into temp XDG_DATA_HOME");
    if (!licensePath.startsWith(expectedDataDir)) {
        // Never touch user data: abort before any license I/O.
        if (savedXdgDataHome.isEmpty()) {
            qunsetenv("XDG_DATA_HOME");
        } else {
            qputenv("XDG_DATA_HOME", savedXdgDataHome.toUtf8());
        }
        if (savedMockGuid.isEmpty()) {
            qunsetenv("BF_MOCK_MACHINE_GUID");
        } else {
            qputenv("BF_MOCK_MACHINE_GUID", savedMockGuid.toUtf8());
        }
        return 1;
    }
    check(mgr.isConfigured(), "manager loaded runtime license_public.pem");
    check(mgr.isBackendAvailable(), "OpenSSL verification backend available");
    qputenv("BF_MOCK_MACHINE_GUID", "");
    check(mgr.verifyKey(validKey).status == bf::LicenseStatus::WrongMachine,
          "valid signature with empty machine GUID is rejected");
    qputenv("BF_MOCK_MACHINE_GUID", "GUID-001");

    // ---- (4) first offline authorization: license + watermark both appear --
    {
        const bf::LicenseResult r = mgr.saveLicense(validKey);
        check(r.status == bf::LicenseStatus::Valid, "saveLicense(valid) -> Valid");
        check(r.payload.value("machine_id").toString() == machineId &&
              r.payload.value("expiry").toString() == "2099-12-31", "saveLicense returns verified payload");
        check(QFileInfo::exists(licensePath), "license.dat written");
        check(QFileInfo::exists(watermarkPath), ".last_run written alongside license.dat");
        const bf::LicenseResult legacy = mgr.saveLicense(legacyKey);
        check(legacy.ok() && legacy.payload.value("expiry").toString() == "2099-12-31",
              "legacy license without expiry exposes the effective expiry");
        check(mgr.saveLicense(validKey).ok(), "restore license for watermark checks");
    }

    // ---- (5) watermark removed: hard ClockStateMissing, license.dat intact -
    {
        const QByteArray licenseBefore = readFile(licensePath);
        check(!licenseBefore.isEmpty() && QFile::remove(watermarkPath), "removed .last_run");

        const bf::LicenseResult rCheck = mgr.checkSavedLicense();
        check(rCheck.status == bf::LicenseStatus::ClockStateMissing,
              "checkSavedLicense -> ClockStateMissing when .last_run absent");

        const bf::LicenseResult rSave = mgr.saveLicense(validKey);
        check(rSave.status == bf::LicenseStatus::ClockStateMissing,
              "saveLicense -> ClockStateMissing when .last_run absent");

        check(QFileInfo::exists(licensePath), "license.dat still present after failed save");
        check(readFile(licensePath) == licenseBefore, "license.dat content unchanged");
        check(!QFileInfo::exists(watermarkPath),
              "watermark not auto-created by offline save (state never auto-fixed)");
    }

    // ---- (6) online authorization restores a verifiable watermark ----------
    {
        const bf::LicenseResult r = mgr.saveOnlineLicense(validKey);
        check(r.status == bf::LicenseStatus::Valid, "saveOnlineLicense(valid) -> Valid");
        check(QFileInfo::exists(watermarkPath), ".last_run restored");
        check(mgr.checkClockRollback().ok(), "restored watermark verifiable (raw check)");
        check(mgr.checkSavedLicense().status == bf::LicenseStatus::Valid,
              "checkSavedLicense -> Valid after online restore");
    }

    // ---- (7) corrupted watermark: ClockStateCorrupted, online path recovers
    {
        check(writeFile(watermarkPath, QByteArrayLiteral("tampered-not-json")),
              "corrupted .last_run with garbage content");

        const bf::LicenseResult rCheck = mgr.checkSavedLicense();
        check(rCheck.status == bf::LicenseStatus::ClockStateCorrupted,
              "checkSavedLicense -> ClockStateCorrupted on tampered .last_run");

        const bf::LicenseResult rSave = mgr.saveOnlineLicense(validKey);
        check(rSave.status == bf::LicenseStatus::Valid, "saveOnlineLicense(valid) -> Valid");
        check(QFileInfo::exists(watermarkPath), ".last_run rewritten by online path");
        check(mgr.checkClockRollback().ok(), "recovered watermark verifiable (raw check)");
        check(mgr.checkSavedLicense().status == bf::LicenseStatus::Valid,
              "checkSavedLicense -> Valid after corrupted-state recovery");
    }

    // ---- (8) restore the original environment ------------------------------
    if (savedXdgDataHome.isEmpty()) {
        qunsetenv("XDG_DATA_HOME");
    } else {
        qputenv("XDG_DATA_HOME", savedXdgDataHome.toUtf8());
    }
    if (savedMockGuid.isEmpty()) {
        qunsetenv("BF_MOCK_MACHINE_GUID");
    } else {
        qputenv("BF_MOCK_MACHINE_GUID", savedMockGuid.toUtf8());
    }
    check(qEnvironmentVariable("XDG_DATA_HOME") == savedXdgDataHome,
          "XDG_DATA_HOME restored to original value");

    std::cout << "\n";
    if (failures == 0) {
        std::cout << "license-manager-clock-test: PASS\n";
        return 0;
    }
    std::cout << "license-manager-clock-test: FAIL (" << failures << " failure(s))\n";
    return 1;
}
