

#ifndef SUPERGENIUS_CRYPTO_SECP256K1_TYPES_HPP
#define SUPERGENIUS_CRYPTO_SECP256K1_TYPES_HPP

#include "common/blob.hpp"

namespace sgns::crypto::secp256k1 {
  namespace constants {
    static constexpr size_t kUncompressedPublicKeySize = 65u;
    static constexpr size_t kCompressedPublicKeySize = 33u;
    static constexpr size_t kCompactSignatureSize = 65u;
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
  using CompressedPublicKey = common::Blob<constants::kCompressedPublicKeySize>;
  /**
   * uncompressed form of public key
   */
  using ExpandedPublicKey = common::Blob<constants::kUncompressedPublicKeySize>;

  /**
   * secp256k1 RSV-signature
   */
  using RSVSignature = common::Blob<constants::kCompactSignatureSize>;

  /**
   * 32-byte sequence of bytes (presumably blake2s hash)
   */
  using MessageHash = common::Hash256;
}  // namespace sgns::crypto::secp256k1

#endif  // SUPERGENIUS_CRYPTO_SECP256K1_TYPES_HPP
