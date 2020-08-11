
#ifndef SUPERGENIUS_SRC_CRYPTO_SECP256K1_TYPES_HPP
#define SUPERGENIUS_SRC_CRYPTO_SECP256K1_TYPES_HPP

#include "base/blob.hpp"

namespace sgns::crypto::secp256k1 {
  namespace constants {
    static constexpr size_t kUncompressedPublicKeySize = 65u;
    static constexpr size_t kCompressedPublicKeySize = 33u;
    static constexpr size_t kCompactSignatureSize = 65u;
    static constexpr size_t kGeneralPublicKeySize = 64u;
  }  // namespace constants

  using EcdsaVerifyError = uint8_t;
  namespace ecdsa_verify_error {
    static constexpr EcdsaVerifyError kInvalidRS = 0;
    static constexpr EcdsaVerifyError kInvalidV = 1;
    static constexpr EcdsaVerifyError kInvalidSignature = 2;
  }  // namespace ecdsa_verify_error

  /**
   * compressed form of public key
   */
  using CompressedPublicKey = base::Blob<constants::kCompressedPublicKeySize>;

  /**
   * uncompressed form of public key
   */
  using UncompressedPublicKey =
      base::Blob<constants::kUncompressedPublicKeySize>;

  /**
   * truncated form of uncompressed public key
   */
  using PublicKey = base::Blob<constants::kGeneralPublicKeySize>;

  /**
   * secp256k1 RSV-signature
   */
  using RSVSignature = base::Blob<constants::kCompactSignatureSize>;

  /**
   * 32-byte sequence of bytes (presumably blake2s hash)
   */
  using MessageHash = base::Hash256;
}  // namespace sgns::crypto::secp256k1

#endif  // SUPERGENIUS_SRC_CRYPTO_SECP256K1_TYPES_HPP
