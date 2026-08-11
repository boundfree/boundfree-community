// Internal smoke test for the license module. On Linux/WSL2 this exercises the
// OpenSSL verification backend (round-trip sign/verify); on Windows it
// exercises the CNG backend when a public key is supplied.
// Not linked into bf_app; standalone executable bf_license_smoke.
#include "license/license_crypto.h"
#include "license/license_key.h"
#include "license/license_manager.h"
#include "license/machine_id.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#ifndef Q_OS_WIN
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#endif

#include <cstdio>
#include <cstdlib>
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

// Build a legacy-compatible key envelope (without a real signature) to test
// decoding. Returns the envelope string with '.' separators.
QString makeTestEnvelope(const QJsonObject& payload, const QByteArray& signature) {
    const QByteArray payloadBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray raw;
    const quint32 len = static_cast<quint32>(payloadBytes.size());
    for (int i = 3; i >= 0; --i) {
        raw.append(static_cast<char>((len >> (i * 8)) & 0xFF));
    }
    raw.append(payloadBytes);
    raw.append(signature);

    QByteArray b64 = raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    // Insert '.' every 8 chars (legacy grouping).
    QString grouped;
    for (int i = 0; i < b64.size(); i += 8) {
        if (i > 0) grouped += '.';
        grouped += QString::fromLatin1(b64.mid(i, 8));
    }
    return grouped;
}

void testMachineIdDeterminism() {
    std::cout << "[machine_id]\n";
    // Legacy algorithm: SHA-256(MachineGuid)[:32].upper() — NO MAC.
    // Verified against Python hashlib.sha256("GUID-001".encode()).hexdigest()[:32].upper()
    const QString id1 = bf::machineIdFromInputs("GUID-001", "AA:BB:CC:DD:EE:FF");
    const QString id2 = bf::machineIdFromGuid("GUID-001");
    check(id1 == id2, "machineIdFromInputs(guid, mac) == machineIdFromGuid(guid)");
    check(id1 == "0B9C9157-6A069C39-FA9C30B9-C0086DFA", "machineIdFromInputs fixed golden value");

    // MAC must NOT affect the result.
    const QString idNoMac = bf::machineIdFromInputs("GUID-001", "");
    check(idNoMac == id1, "machineIdFromInputs ignores MAC (legacy compat)");
    const QString idDifferentMac = bf::machineIdFromInputs("GUID-001", "11:22:33:44:55:66");
    check(idDifferentMac == id1, "different MAC produces same id");

    const QString canonical = bf::machineIdCanonical("GUID-001");
    check(canonical == "0B9C91576A069C39FA9C30B9C0086DFA", "machineIdCanonical no separators");

    const QString fromFmt = bf::machineIdCanonicalFromFormatted(id1);
    check(fromFmt == canonical, "machineIdCanonicalFromFormatted round-trip");
}

void testEnvelopeDecode() {
    std::cout << "[envelope]\n";
    QJsonObject payload;
    payload["machine_id"] = QString("0B9C91576A069C39FA9C30B9C0086DFA");
    payload["expiry"] = QString("2099-12-31");
    payload["version"] = QString("0.1.0-dev");

    const QByteArray dummySig(256, '\x01');
    const QString key = makeTestEnvelope(payload, dummySig);

    QByteArray outPayload;
    QByteArray outSig;
    bf::LicenseResult decoded = bf::decodeLicenseEnvelope(key, &outPayload, &outSig);
    check(decoded.status == bf::LicenseStatus::Valid, "decode envelope valid");
    check(outSig.size() == 256, "signature length 256");
    check(decoded.payload.value("version").toString() == "0.1.0-dev", "payload version parsed");

    // Malformed key.
    bf::LicenseResult bad = bf::decodeLicenseEnvelope("not-a-valid-key!!!");
    check(bad.status == bf::LicenseStatus::KeyFormatError, "malformed key -> KeyFormatError");

    // Empty key.
    bf::LicenseResult empty = bf::decodeLicenseEnvelope("");
    check(empty.status == bf::LicenseStatus::KeyFormatError, "empty key -> KeyFormatError");

    // Dots removable: strip dots and re-decode.
    QString noDots = key;
    noDots.remove('.');
    bf::LicenseResult noDotDecoded = bf::decodeLicenseEnvelope(noDots);
    check(noDotDecoded.status == bf::LicenseStatus::Valid, "envelope without dots decodes");
}

void testVerifyWithoutKey() {
    std::cout << "[verify]\n";
    // NotConfigured when no public key.
    QJsonObject payload;
    payload["machine_id"] = QString("0B9C91576A069C39FA9C30B9C0086DFA");
    payload["expiry"] = QString("2099-12-31");
    payload["version"] = QString("0.1.0-dev");
    const QByteArray dummySig(256, '\x01');
    const QString key = makeTestEnvelope(payload, dummySig);

    bf::LicenseResult r = bf::verifyLicenseKey(key, QString(), "0B9C91576A069C39FA9C30B9C0086DFA", "0.1.0-dev");
    check(r.status == bf::LicenseStatus::NotConfigured, "empty public key -> NotConfigured");

    // A malformed public key (or a dummy signature) must fail verification on
    // every backend: CNG on Windows, OpenSSL elsewhere.
    bf::LicenseResult r2 = bf::verifyLicenseKey(key, "-----BEGIN PUBLIC KEY-----\nMIIBIjAN\n-----END PUBLIC KEY-----\n",
                                                "0B9C91576A069C39FA9C30B9C0086DFA", "0.1.0-dev");
    check(r2.status == bf::LicenseStatus::InvalidSignature,
          "dummy key rejected by backend");
}

void testManagerLifecycle() {
    std::cout << "[manager]\n";
    bf::LicenseManager mgr("0.1.0-dev", QDir::currentPath());
    // No public key configured by default in dev (BF_REQUIRE_LICENSE=OFF).
    check(!mgr.isConfigured() || mgr.isConfigured(), "manager constructs");
    // checkSavedLicense on missing file.
    bf::LicenseResult saved = mgr.checkSavedLicense();
    check(saved.status == bf::LicenseStatus::Missing || saved.status == bf::LicenseStatus::Valid,
          "checkSavedLicense missing or valid");

    // Backend availability: CNG on Windows, OpenSSL elsewhere.
    check(mgr.isBackendAvailable(), "verification backend available");
}

#ifndef Q_OS_WIN
void testOpenSslVerifyRoundTrip() {
    std::cout << "[openssl_verify]\n";
    // Generate a throwaway RSA-2048 key, PSS-SHA256 sign a fixed message
    // (MAX salt length, mirroring the server's issue_license_key), then
    // verify with our OpenSSL backend. Also checks tamper/wrong-key rejection.
    const QByteArray message =
        QByteArrayLiteral("BoundFree-EEG round-trip verification message");

    EVP_PKEY_CTX* genCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY* pkey = nullptr;
    const bool generated = genCtx != nullptr
        && EVP_PKEY_keygen_init(genCtx) > 0
        && EVP_PKEY_CTX_set_rsa_keygen_bits(genCtx, 2048) > 0
        && EVP_PKEY_keygen(genCtx, &pkey) > 0;
    check(generated, "RSA-2048 key generated");
    EVP_PKEY_CTX_free(genCtx);
    if (!generated) {
        return;
    }

    // Serialize the public key to SubjectPublicKeyInfo PEM, the same shape as
    // license_public.pem consumed by LicenseManager.
    BIO* pemBio = BIO_new(BIO_s_mem());
    const bool pemOk = pemBio != nullptr && PEM_write_bio_PUBKEY(pemBio, pkey) == 1;
    check(pemOk, "public key PEM serialized");
    QByteArray pem;
    if (pemOk) {
        char* data = nullptr;
        const long len = BIO_get_mem_data(pemBio, &data);
        pem = QByteArray(data, static_cast<int>(len));
    }
    BIO_free(pemBio);

    // Sign with PSS/MGF1-SHA256 and MAX salt length (server behavior).
    // EVP_DigestSign digests the message internally, matching Python's
    // private_key.sign(payload, PSS(MGF1(SHA256), MAX_LENGTH), SHA256()).
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    EVP_PKEY_CTX* pctx = nullptr;
    bool signedOk = mdCtx != nullptr
        && EVP_DigestSignInit(mdCtx, &pctx, EVP_sha256(), nullptr, pkey) > 0
        && EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) > 0
        && EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, RSA_PSS_SALTLEN_MAX) > 0
        && EVP_PKEY_CTX_set_signature_md(pctx, EVP_sha256()) > 0
        && EVP_DigestSignUpdate(mdCtx,
                                reinterpret_cast<const unsigned char*>(message.constData()),
                                static_cast<size_t>(message.size())) > 0;
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
    EVP_PKEY_free(pkey);
    check(signedOk && signature.size() == 256, "PSS signature generated (256 bytes)");
    if (!signedOk || signature.size() != 256) {
        return;
    }

    check(bf::license_crypto::verifyRsaPssSha256(pem, message, signature),
          "valid signature accepted");
    check(!bf::license_crypto::verifyRsaPssSha256(pem, message + "tampered", signature),
          "tampered message rejected");
    check(!bf::license_crypto::verifyRsaPssSha256(pem, message, QByteArray(256, '\x00')),
          "garbage signature rejected");
    check(!bf::license_crypto::verifyRsaPssSha256(QString(), message, signature),
          "empty public key rejected");
}
#endif

}  // namespace

void testSpkiParser() {
    std::cout << "[spki_parser]\n";
    // Known RSA-2048 SubjectPublicKeyInfo DER (golden).
    // Public exponent = 65537 (0x010001), modulus = 256 bytes.
    const QByteArray der = QByteArray::fromHex(
        "30820122300d06092a864886f70d01010105000382010f003082010a0282010100"
        "ab058cee890d1f450e201ead95c5a2763fa17e8612485ff0bcf171819e6222"
        "f7ebba227eaa63a7a83b977264b9d3c4197f8f7dd9eec6496e82a9038808b629"
        "5cee96ff2f810392f4f93609619785dfd08f2e899dbd847ff86df3630b9b3933"
        "97d452db7155662926cef3db7ba2749e40dd09c18c4f56c3abca8d7478e7ce87"
        "66e359d4bc273fb6dbf41f0219429c03d4e0a927f7afa16a91259778caf510ef"
        "d4ccae4459ec3ec3b7afae5f8b71b02fb42e4675d144b6a54fbc292f5b3146c4"
        "3abbc22ed2fecfd697a70323b0855db7eb9a5edbaaf1436cee10f764707bc197"
        "8ff90e2d9d898b6ca5bc05f53a32fe10bf9d8784a388cd5dbab4106103bf634a"
        "df0203010001");
    const bf::license_crypto::RsaPublicKeyParts parts =
        bf::license_crypto::parseRsaPublicKeyFromDerSpki(der);
    check(parts.valid, "SPKI parse valid");
    check(parts.modulus.size() == 256, "modulus is 256 bytes");
    check(parts.exponent.size() == 3, "exponent is 3 bytes");
    // Exponent should be 0x01, 0x00, 0x01 = 65537.
    check(static_cast<unsigned char>(parts.exponent[0]) == 0x01 &&
          static_cast<unsigned char>(parts.exponent[1]) == 0x00 &&
          static_cast<unsigned char>(parts.exponent[2]) == 0x01,
          "exponent value is 65537");
    // CNG blob layout: BCRYPT_RSAKEY_BLOB header (24 bytes) then exponent (3 bytes).
    // First three bytes after the 24-byte header must be 0x01, 0x00, 0x01.
    const QByteArray blob = bf::license_crypto::buildCngRsaPublicBlob(parts);
    check(blob.size() == 24 + 3 + 256, "CNG blob size = header + exp + modulus");
    check(static_cast<unsigned char>(blob[24]) == 0x01 &&
          static_cast<unsigned char>(blob[25]) == 0x00 &&
          static_cast<unsigned char>(blob[26]) == 0x01,
          "CNG blob header+3 bytes are 01 00 01 (exponent first)");
    // Malformed DER.
    const bf::license_crypto::RsaPublicKeyParts bad =
        bf::license_crypto::parseRsaPublicKeyFromDerSpki(QByteArray("not valid der"));
    check(!bad.valid, "malformed DER -> invalid");
    // Empty.
    const bf::license_crypto::RsaPublicKeyParts empty =
        bf::license_crypto::parseRsaPublicKeyFromDerSpki(QByteArray());
    check(!empty.valid, "empty DER -> invalid");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    testMachineIdDeterminism();
    testEnvelopeDecode();
    testVerifyWithoutKey();
    testManagerLifecycle();
#ifndef Q_OS_WIN
    testOpenSslVerifyRoundTrip();
#endif
    testSpkiParser();

    std::cout << "\n";
    if (failures == 0) {
        std::cout << "license-smoke: ALL PASSED\n";
        return 0;
    }
    std::cout << "license-smoke: " << failures << " FAILURE(S)\n";
    return 1;
}
