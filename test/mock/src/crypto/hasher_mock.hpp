

#ifndef SUPERGENIUS_TEST_MOCK_CRYPTO_HASHER_HPP
#define SUPERGENIUS_TEST_MOCK_CRYPTO_HASHER_HPP

#include "crypto/hasher.hpp"

#include <gmock/gmock.h>

namespace sgns::crypto {
  class HasherMock : public Hasher {
   public:
    ~HasherMock() override = default;

    MOCK_CONST_METHOD1(twox_64, Hash64(gsl::span<const uint8_t>));

    MOCK_CONST_METHOD1(blake2b_128, Hash128(gsl::span<const uint8_t>));

    MOCK_CONST_METHOD1(twox_128, Hash128(gsl::span<const uint8_t>));

    MOCK_CONST_METHOD1(twox_256, Hash256(gsl::span<const uint8_t>));

    MOCK_CONST_METHOD1(blake2b_256, Hash256(gsl::span<const uint8_t>));

    MOCK_CONST_METHOD1(blake2s_256, Hash256(gsl::span<const uint8_t>));

    MOCK_CONST_METHOD1(keccak_256, Hash256(gsl::span<const uint8_t>));

    MOCK_CONST_METHOD1(sha2_256, Hash256(gsl::span<const uint8_t>));
  };
}  // namespace sgns::crypto

#endif  // SUPERGENIUS_TEST_MOCK_CRYPTO_HASHER_HPP
