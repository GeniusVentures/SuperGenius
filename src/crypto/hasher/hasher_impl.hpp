#ifndef SUPERGENIUS_SRC_CRYPTO_HASHER_HASHER_IMPL_HPP_
#define SUPERGENIUS_SRC_CRYPTO_HASHER_HASHER_IMPL_HPP_

#include "crypto/hasher.hpp"

namespace sgns::crypto
{
    class HasherImpl : public Hasher
    {
    public:
        ~HasherImpl() override = default;

        [[nodiscard]] Hash64 twox_64( gsl::span<const uint8_t> buffer ) const override;

        [[nodiscard]] Hash128 twox_128( gsl::span<const uint8_t> buffer ) const override;

        [[nodiscard]] Hash128 blake2b_128( gsl::span<const uint8_t> buffer ) const override;

        [[nodiscard]] Hash256 twox_256( gsl::span<const uint8_t> buffer ) const override;

        [[nodiscard]] Hash256 blake2b_256( gsl::span<const uint8_t> buffer ) const override;

        [[nodiscard]] Hash256 keccak_256( gsl::span<const uint8_t> buffer ) const override;

        [[nodiscard]] Hash256 blake2s_256( gsl::span<const uint8_t> buffer ) const override;

        [[nodiscard]] Hash256 sha2_256( gsl::span<const uint8_t> buffer ) const override;

        std::string GetName() override
        {
            return "HasherImpl";
        }
    };
}

#endif
