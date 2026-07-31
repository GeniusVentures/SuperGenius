/**
 * @file ConsensusSlot.hpp
 * @brief Canonical consensus-slot identity helpers shared by blockchain and transactions.
 */
#ifndef SGNS_CONSENSUS_SLOT_HPP
#define SGNS_CONSENSUS_SLOT_HPP

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

#include <gsl/span>

#include "base/hexutil.hpp"
#include "crypto/hasher.hpp"
#include "outcome/outcome.hpp"

namespace sgns::consensus_slot
{
    /** Build the canonical readable identity for a normal nonce slot. */
    inline outcome::result<std::string> MakeNoncePreimage( std::string_view source_address, uint64_t nonce )
    {
        if ( source_address.size() != 128 ||
             !std::all_of(
                 source_address.begin(),
                 source_address.end(),
                 []( unsigned char c )
                 {
                     return std::isdigit( c ) != 0 || ( c >= 'a' && c <= 'f' );
                 } ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        std::string preimage( source_address );
        preimage += ':';
        preimage += std::to_string( nonce );
        return preimage;
    }

    /** Hash a canonical readable preimage into the operational slot ID. */
    inline std::string HashPreimage( std::string_view preimage )
    {
        const auto hash = crypto::sha2_256( preimage.data(), preimage.size() );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }
} // namespace sgns::consensus_slot

#endif // SGNS_CONSENSUS_SLOT_HPP
