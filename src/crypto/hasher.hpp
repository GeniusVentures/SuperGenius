#ifndef SUPERGENIUS_SRC_HASHER_HASHER_HPP_
#define SUPERGENIUS_SRC_HASHER_HASHER_HPP_

#include <cstddef>

#include <gsl/span>

#include "base/blob.hpp"

namespace sgns::crypto
{
    [[nodiscard]] base::Hash64  twox_64( gsl::span<const uint8_t> buffer );
    [[nodiscard]] base::Hash128 twox_128( gsl::span<const uint8_t> buffer );
    [[nodiscard]] base::Hash128 blake2b_128( gsl::span<const uint8_t> buffer );
    [[nodiscard]] base::Hash256 twox_256( gsl::span<const uint8_t> buffer );
    [[nodiscard]] base::Hash256 blake2b_256( gsl::span<const uint8_t> buffer );
    [[nodiscard]] base::Hash256 keccak_256( gsl::span<const uint8_t> buffer );
    [[nodiscard]] base::Hash256 blake2s_256( gsl::span<const uint8_t> buffer );
    [[nodiscard]] base::Hash256 sha2_256( gsl::span<const uint8_t> buffer );

    [[nodiscard]] inline base::Hash256 sha2_256( const void *data, size_t size )
    {
        return sha2_256( gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( data ), size ) );
    }
}

#endif
