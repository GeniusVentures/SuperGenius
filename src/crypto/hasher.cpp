#include "crypto/hasher.hpp"

#include <crypto/blake2/blake2b.h>
#include <crypto/blake2/blake2s.h>

#include "crypto/keccak/keccak.h"
#include "crypto/sha/sha256.hpp"
#include "crypto/twox/twox.hpp"

namespace sgns::crypto
{
    base::Hash64 twox_64( gsl::span<const uint8_t> buffer )
    {
        return make_twox64( buffer );
    }

    base::Hash128 twox_128( gsl::span<const uint8_t> buffer )
    {
        return make_twox128( buffer );
    }

    base::Hash128 blake2b_128( gsl::span<const uint8_t> buffer )
    {
        base::Hash128 out;
        sgns_blake2b( out.data(), out.size(), nullptr, 0, buffer.data(), buffer.size() );
        return out;
    }

    base::Hash256 twox_256( gsl::span<const uint8_t> buffer )
    {
        return make_twox256( buffer );
    }

    base::Hash256 blake2b_256( gsl::span<const uint8_t> buffer )
    {
        base::Hash256 out;
        sgns_blake2b( out.data(), out.size(), nullptr, 0, buffer.data(), buffer.size() );
        return out;
    }

    base::Hash256 keccak_256( gsl::span<const uint8_t> buffer )
    {
        base::Hash256 out;
        sha3_HashBuffer( 256, SHA3_FLAGS::SHA3_FLAGS_KECCAK, buffer.data(), buffer.size(), out.data(), out.size() );
        return out;
    }

    base::Hash256 blake2s_256( gsl::span<const uint8_t> buffer )
    {
        base::Hash256 out;
        blake2s( out.data(), out.size(), nullptr, 0, buffer.data(), buffer.size() );
        return out;
    }

    base::Hash256 sha2_256( gsl::span<const uint8_t> buffer )
    {
        return sha256( buffer );
    }
} // namespace sgns::crypto
