

#ifndef SUPERGENIUS_CORE_CRYPTO_HASHER_HASHER_IMPL_HPP_
#define SUPERGENIUS_CORE_CRYPTO_HASHER_HASHER_IMPL_HPP_

#include "crypto/hasher.hpp"

namespace sgns::crypto {

  class HasherImpl : public Hasher {
   public:
    ~HasherImpl() override = default;

    Hash64 twox_64(gsl::span<const uint8_t> buffer) const override;

    Hash128 twox_128(gsl::span<const uint8_t> buffer) const override;

    Hash128 blake2b_128(gsl::span<const uint8_t> buffer) const override;

    Hash256 twox_256(gsl::span<const uint8_t> buffer) const override;

    Hash256 blake2b_256(gsl::span<const uint8_t> buffer) const override;

    Hash256 keccak_256(gsl::span<const uint8_t> buffer) const override;

    Hash256 blake2s_256(gsl::span<const uint8_t> buffer) const override;

    Hash256 sha2_256(gsl::span<const uint8_t> buffer) const override;
  };

}  // namespace sgns::hash

#endif  // SUPERGENIUS_CORE_CRYPTO_HASHER_HASHER_IMPL_HPP_
