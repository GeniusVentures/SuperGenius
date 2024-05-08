#ifndef SUPERGENIUS_CRYPTO_TWOX_HPP
#define SUPERGENIUS_CRYPTO_TWOX_HPP

#include "base/blob.hpp"

namespace sgns::crypto
{
    base::Hash64 make_twox64( gsl::span<const uint8_t> buf );

    base::Hash128 make_twox128( gsl::span<const uint8_t> buf );

    base::Hash256 make_twox256( gsl::span<const uint8_t> buf );
}

#endif
