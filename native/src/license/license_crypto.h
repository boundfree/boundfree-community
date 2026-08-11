#pragma once

#include <QByteArray>
#include <QString>

namespace bf {
namespace license_crypto {

// Parsed RSA public key components from a DER SubjectPublicKeyInfo blob.
// Exposed for platform-independent golden-value tests of the SPKI parser.
struct RsaPublicKeyParts {
    QByteArray modulus;   // n, big-endian, no leading zero pad
    QByteArray exponent;  // e, big-endian
    bool valid = false;
};

// Parse a DER SubjectPublicKeyInfo blob and extract the RSA modulus + exponent.
// Platform-independent pure function. Returns .valid=false on any parse failure.
RsaPublicKeyParts parseRsaPublicKeyFromDerSpki(const QByteArray& derSpki);

// Build a BCRYPT_RSAKEY_BLOB from parsed modulus + exponent (platform-
// independent pure function; exposed for golden tests of the blob layout).
// Layout: header, then PublicExponent[cbPublicExp], then Modulus[cbModulus].
QByteArray buildCngRsaPublicBlob(const RsaPublicKeyParts& parts);

// RSA-2048 PSS SHA-256 signature verification backend.
//
// Contract:
//   publicKeyPem   : SubjectPublicKeyInfo PEM ("BEGIN PUBLIC KEY"), non-empty.
//   messageBytes   : the canonical bytes the signature was computed over.
//   signatureBytes : RSA-2048 PSS signature.
//
// Returns true if the signature is valid for the given key/message.
// Returns false on any verification failure or malformed input.
//
// Backend selection:
//   Windows: CNG (BCryptImportKeyPair + BCryptVerifySignature with
//            BCRYPT_PAD_PSS / MGF1-SHA256). Internally computes SHA-256 digest
//            of the message, parses the DER SPKI into modulus+exponent, builds
//            a BCRYPT_RSAPUBLIC_BLOB, and verifies with salt-length scan for
//            PSS.AUTO equivalence.
//   Non-Windows: OpenSSL EVP (RSA-PSS / MGF1-SHA256 with RSA_PSS_SALTLEN_AUTO,
//            i.e. the salt length is recovered from the signature, equivalent
//            to Python's PSS.AUTO). Requires libcrypto at build/run time;
//            Linux/WSL2 development builds exercise the same verification
//            path as the Windows release.
bool isBackendAvailable();
bool verifyRsaPssSha256(const QString& publicKeyPem, const QByteArray& messageBytes,
                        const QByteArray& signatureBytes);

}  // namespace license_crypto
}  // namespace bf
