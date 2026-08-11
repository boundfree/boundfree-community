#include "license/license_crypto.h"

#include <QByteArray>
#include <QString>
#include <QCryptographicHash>
#include <QtEndian>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")
#endif

namespace bf {
namespace license_crypto {

// ─────────────────────────────────────────────────────────────────────────
// DER SubjectPublicKeyInfo parser (platform-independent).
//
// RSA public key in SubjectPublicKeyInfo (RFC 5280):
//   SEQUENCE {
//     SEQUENCE { OID rsaEncryption (1.2.840.113549.1.1.1), NULL }
//     BIT STRING containing DER RSAPublicKey {
//       SEQUENCE { INTEGER modulus, INTEGER publicExponent }
//     }
//   }
//
// Minimal hand-rolled DER walker sufficient for RSA-2048 SPKI. Exposes
// parseRsaPublicKeyFromDerSpki() for golden tests and buildCngRsaPublicBlob()
// for Windows CNG import. No private-key material anywhere.
// ─────────────────────────────────────────────────────────────────────────

// DER tag constants.
namespace {
constexpr quint8 kDerSequence = 0x30;
constexpr quint8 kDerBitString = 0x03;
constexpr quint8 kDerInteger = 0x02;
constexpr quint8 kDerNull = 0x05;
constexpr quint8 kDerOid = 0x06;

// Read a DER length field starting at *pos within [start, end). Returns true
// and advances *pos past the length octets. *outLen receives the length.
bool readDerLength(const QByteArray& data, int pos, int end, int* nextPos, int* outLen) {
    if (pos >= end) return false;
    quint8 b = static_cast<quint8>(data.at(pos++));
    if ((b & 0x80) == 0) {
        *outLen = b;
        *nextPos = pos;
        return true;
    }
    const int numBytes = b & 0x7F;
    if (numBytes == 0 || numBytes > 4 || pos + numBytes > end) return false;
    int len = 0;
    for (int i = 0; i < numBytes; ++i) {
        len = (len << 8) | static_cast<quint8>(data.at(pos++));
    }
    *outLen = len;
    *nextPos = pos;
    return true;
}

// Expect a specific tag at *pos within [start, end). On success sets
// *contentStart / *contentEnd to the value bytes and advances *pos to
// contentEnd (past the value). Returns false on mismatch or overflow.
//
// To traverse children inside a SEQUENCE, the caller should use contentStart as
// the starting position and contentEnd as the boundary, NOT *pos.
bool expectTag(const QByteArray& data, int* pos, int end, quint8 tag,
               int* contentStart, int* contentEnd) {
    if (*pos >= end || static_cast<quint8>(data.at(*pos)) != tag) return false;
    (*pos)++;
    int len = 0;
    if (!readDerLength(data, *pos, end, pos, &len)) return false;
    *contentStart = *pos;
    *contentEnd = *pos + len;
    if (*contentEnd > end) return false;
    *pos = *contentEnd;
    return true;
}

// Read a DER INTEGER within [start, end) and return its value bytes, stripping
// the leading 0x00 sign pad if present. Advances *pos past the INTEGER.
bool readDerInteger(const QByteArray& data, int* pos, int end, QByteArray* out) {
    int start, valueEnd;
    if (!expectTag(data, pos, end, kDerInteger, &start, &valueEnd)) return false;
    int i = start;
    if (valueEnd - i > 1 && data.at(i) == 0) i++;
    out->resize(0);
    for (; i < valueEnd; ++i) out->append(data.at(i));
    return true;
}

// Walk the outer SubjectPublicKeyInfo and return the BIT STRING content
// (the DER RSAPublicKey). Uses bounded traversal so sibling nodes are read
// in order within the outer SEQUENCE content.
QByteArray extractRsaPublicKeyBitString(const QByteArray& der) {
    int pos = 0;
    int outerStart, outerEnd;
    // Outer SEQUENCE
    if (!expectTag(der, &pos, der.size(), kDerSequence, &outerStart, &outerEnd)) return {};
    // Enter outer content: traverse AlgorithmIdentifier + BIT STRING in order.
    pos = outerStart;
    int algStart, algEnd;
    // AlgorithmIdentifier SEQUENCE
    if (!expectTag(der, &pos, outerEnd, kDerSequence, &algStart, &algEnd)) return {};
    // Enter the AlgorithmIdentifier content to read OID + NULL within [algStart, algEnd].
    const int outerPosAfterAlg = pos;  // resume here for the BIT STRING
    pos = algStart;
    // OID (rsaEncryption 1.2.840.113549.1.1.1)
    int oidStart, oidEnd;
    if (!expectTag(der, &pos, algEnd, kDerOid, &oidStart, &oidEnd)) return {};
    static const unsigned char kRsaOid[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    if (oidEnd - oidStart != 9) return {};
    for (int i = 0; i < 9; ++i) {
        if (static_cast<quint8>(der.at(oidStart + i)) != kRsaOid[i]) return {};
    }
    // NULL (algorithm parameters)
    int nullStart, nullEnd;
    if (!expectTag(der, &pos, algEnd, kDerNull, &nullStart, &nullEnd)) return {};
    // Resume at the outer level for the BIT STRING.
    pos = outerPosAfterAlg;
    int bsStart, bsEnd;
    if (!expectTag(der, &pos, outerEnd, kDerBitString, &bsStart, &bsEnd)) return {};
    // BIT STRING has a leading "unused bits" byte (must be 0).
    if (bsEnd - bsStart < 1 || der.at(bsStart) != 0) return {};
    return der.mid(bsStart + 1, bsEnd - bsStart - 1);
}

}  // namespace

RsaPublicKeyParts parseRsaPublicKeyFromDerSpki(const QByteArray& derSpki) {
    RsaPublicKeyParts result;
    if (derSpki.isEmpty()) return result;

    const QByteArray rsaKeyDer = extractRsaPublicKeyBitString(derSpki);
    if (rsaKeyDer.isEmpty()) return result;

    // RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }
    int pos = 0;
    int seqStart, seqEnd;
    if (!expectTag(rsaKeyDer, &pos, rsaKeyDer.size(), kDerSequence, &seqStart, &seqEnd)) return result;
    pos = seqStart;

    QByteArray modulus, exponent;
    if (!readDerInteger(rsaKeyDer, &pos, seqEnd, &modulus)) return result;
    if (!readDerInteger(rsaKeyDer, &pos, seqEnd, &exponent)) return result;

    result.modulus = modulus;
    result.exponent = exponent;
    result.valid = !modulus.isEmpty() && !exponent.isEmpty();
    return result;
}

// Build a BCRYPT_RSAKEY_BLOB from parsed modulus + exponent (platform-
// independent pure function; exposed for golden tests).
// Layout per BCRYPT_RSAKEY_BLOB: header, then PublicExponent[cbPublicExp],
// then Modulus[cbModulus] (then primes for private keys, omitted here).
QByteArray buildCngRsaPublicBlob(const RsaPublicKeyParts& parts) {
    const int cbModulus = parts.modulus.size();
    const int cbExp = parts.exponent.size();

    // BCRYPT_RSAKEY_BLOB header (see bcrypt.h).
    struct {
        quint32 Magic;       // BCRYPT_RSAPUBLIC_MAGIC = 0x31415352
        quint32 BitLength;
        quint32 cbPublicExp;
        quint32 cbModulus;
        quint32 cbPrime1;
        quint32 cbPrime2;
    } header;
    memset(&header, 0, sizeof(header));
    header.Magic = 0x31415352;   // BCRYPT_RSAPUBLIC_MAGIC 'RSA1'
    header.BitLength = static_cast<quint32>(cbModulus * 8);
    header.cbPublicExp = static_cast<quint32>(cbExp);
    header.cbModulus = static_cast<quint32>(cbModulus);
    header.cbPrime1 = 0;
    header.cbPrime2 = 0;

    QByteArray blob;
    blob.append(reinterpret_cast<const char*>(&header), sizeof(header));
    blob.append(parts.exponent);   // exponent first
    blob.append(parts.modulus);    // then modulus
    return blob;
}

#ifdef Q_OS_WIN

namespace {

struct KeyHandle {
    BCRYPT_KEY_HANDLE handle = nullptr;
    ~KeyHandle() { if (handle) BCryptDestroyKey(handle); }
};

struct AlgorithmHandle {
    BCRYPT_ALG_HANDLE handle = nullptr;
    ~AlgorithmHandle() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};

bool importPublicKey(const QString& publicKeyPem, KeyHandle* outKey) {
    if (publicKeyPem.isEmpty()) return false;

    // Extract base64 body between PEM markers.
    const QString pem = publicKeyPem.trimmed();
    int begin = pem.indexOf("-----BEGIN PUBLIC KEY-----");
    int end = pem.indexOf("-----END PUBLIC KEY-----");
    if (begin < 0 || end <= begin) return false;
    begin += QString("-----BEGIN PUBLIC KEY-----").length();
    QString b64Body = pem.mid(begin, end - begin);
    b64Body.remove('\n').remove('\r').remove(' ').remove('\t');
    const int pad = (4 - b64Body.length() % 4) % 4;
    b64Body.append(QString("=").repeated(pad));

    const QByteArray der = QByteArray::fromBase64(b64Body.toLatin1());
    if (der.isEmpty()) return false;

    const RsaPublicKeyParts parts = parseRsaPublicKeyFromDerSpki(der);
    if (!parts.valid) return false;

    const QByteArray blob = buildCngRsaPublicBlob(parts);

    AlgorithmHandle rsaAlg;
    if (BCryptOpenAlgorithmProvider(&rsaAlg.handle, BCRYPT_RSA_ALGORITHM, nullptr, 0) != 0)
        return false;

    // BCryptImportKeyPair signature (7 args):
    //   hAlgorithm, hImportKey, pszBlobType, phKey, pbInput, cbInput, dwFlags
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    NTSTATUS status = BCryptImportKeyPair(
        rsaAlg.handle,
        nullptr,
        BCRYPT_RSAPUBLIC_BLOB,
        &keyHandle,
        reinterpret_cast<PUCHAR>(const_cast<char*>(blob.constData())),
        static_cast<ULONG>(blob.size()),
        0);
    if (status != 0) return false;

    outKey->handle = keyHandle;
    return true;
}

bool verifyWithSalt(const KeyHandle& key, const QByteArray& digest,
                   const QByteArray& signature, ULONG saltLength) {
    // BCRYPT_PSS_PADDING_INFO only has pszAlgId + cbSalt (no pbSalt field).
    BCRYPT_PSS_PADDING_INFO pssInfo;
    pssInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;
    pssInfo.cbSalt = saltLength;

    // CNG BCryptVerifySignature: pbHash (digest), cbHash, pbSignature, cbSignature.
    // dwFlags must be BCRYPT_PAD_PSS for PSS padding verification.
    const NTSTATUS status = BCryptVerifySignature(
        key.handle, &pssInfo,
        reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
        static_cast<ULONG>(digest.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(signature.constData())),
        static_cast<ULONG>(signature.size()), BCRYPT_PAD_PSS);
    return status == 0;
}

}  // namespace

bool isBackendAvailable() { return true; }

bool verifyRsaPssSha256(const QString& publicKeyPem, const QByteArray& messageBytes,
                        const QByteArray& signatureBytes) {
    KeyHandle key;
    if (!importPublicKey(publicKeyPem, &key)) return false;

    // RSA-2048 PSS signature must be exactly 256 bytes (modulus size).
    if (signatureBytes.size() != 256) return false;

    // CNG PSS verification takes the message DIGEST (SHA-256), not the message.
    const QByteArray digest = QCryptographicHash::hash(messageBytes, QCryptographicHash::Sha256);
    if (digest.size() != 32) return false;

    // Legacy keygen signs with PSS.MAX_LENGTH = modulusBytes - hashBytes - 2 = 222.
    // Python's PSS.AUTO recovers salt from the signature; CNG does not, so we
    // try the legacy MAX_LENGTH first, then fall back to a bounded scan (AUTO equiv).
    ULONG keyBits = 0;
    ULONG copied = 0;
    BCryptGetProperty(key.handle, BCRYPT_KEY_LENGTH,
                      reinterpret_cast<PUCHAR>(&keyBits), sizeof(keyBits), &copied, 0);
    const ULONG modulusBytes = keyBits > 0 ? keyBits / 8 : 256;
    const ULONG maxSalt = (modulusBytes >= 34) ? (modulusBytes - 34) : 0;  // -32 - 2
    if (maxSalt == 0) return false;

    if (verifyWithSalt(key, digest, signatureBytes, maxSalt)) return true;
    for (ULONG salt = 0; salt < maxSalt; ++salt) {
        if (verifyWithSalt(key, digest, signatureBytes, salt)) return true;
    }
    return false;
}

#else  // Non-Windows (OpenSSL backend)

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

bool isBackendAvailable() { return true; }

bool verifyRsaPssSha256(const QString& publicKeyPem, const QByteArray& messageBytes,
                        const QByteArray& signatureBytes) {
    // RSA-2048 PSS signature must be exactly the modulus size (256 bytes).
    if (signatureBytes.size() != 256) {
        return false;
    }

    // Parse the SubjectPublicKeyInfo PEM into an EVP_PKEY.
    const QByteArray pemUtf8 = publicKeyPem.toUtf8();
    BIO* bio = BIO_new_mem_buf(pemUtf8.constData(), -1);
    if (bio == nullptr) {
        return false;
    }
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (pkey == nullptr) {
        return false;
    }

    bool ok = false;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (ctx != nullptr) {
        // PSS padding / MGF1-SHA256 with AUTO salt length: the salt is
        // recovered from the signature, equivalent to Python's PSS.AUTO and
        // the CNG salt-length scan above.
        if (EVP_PKEY_verify_init(ctx) > 0
            && EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PSS_PADDING) > 0
            && EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx, RSA_PSS_SALTLEN_AUTO) > 0
            && EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha256()) > 0) {
            // EVP_PKEY_verify treats its input as the message digest (no
            // internal hashing), so hash the message first — the same
            // contract as the CNG backend (BCryptVerifySignature).
            const QByteArray digest =
                QCryptographicHash::hash(messageBytes, QCryptographicHash::Sha256);
            ok = EVP_PKEY_verify(
                     ctx,
                     reinterpret_cast<const unsigned char*>(signatureBytes.constData()),
                     static_cast<size_t>(signatureBytes.size()),
                     reinterpret_cast<const unsigned char*>(digest.constData()),
                     static_cast<size_t>(digest.size()))
                 == 1;
        }
        EVP_PKEY_CTX_free(ctx);
    }
    EVP_PKEY_free(pkey);
    return ok;
}

#endif

}  // namespace license_crypto
}  // namespace bf
