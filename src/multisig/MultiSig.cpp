/**
 * @file       MultiSig.cpp
 * @brief      Implementation of the standalone multi-signature primitive.
 * @date       2026-07-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "multisig/MultiSig.hpp"

#include <unordered_set>

#include "account/GeniusAccount.hpp"

namespace sgns::multisig
{
    bool VerifyPayloadSignature( const std::string          &address,
                                 std::string_view            signature,
                                 const std::vector<uint8_t> &payload )
    {
        return sgns::GeniusAccount::VerifySignature( address, signature, payload );
    }

    QuorumResult EvaluateQuorum( const std::vector<std::string>                         &signer_set,
                                 uint64_t                                                threshold,
                                 const std::vector<std::pair<std::string, std::string>> &collected_signatures,
                                 const std::vector<uint8_t>                             &payload )
    {
        const std::unordered_set<std::string> signer_lookup( signer_set.begin(), signer_set.end() );
        std::unordered_set<std::string>       valid_unique_signers;

        for ( const auto &[address, signature] : collected_signatures )
        {
            if ( valid_unique_signers.count( address ) != 0 )
            {
                continue; // dedup-first: already counted this signer, skip before verification
            }
            if ( signer_lookup.count( address ) == 0 )
            {
                continue; // unauthorized signer
            }
            if ( !VerifyPayloadSignature( address, signature, payload ) )
            {
                continue; // invalid signature, skip silently
            }
            valid_unique_signers.insert( address );
        }

        QuorumResult result;
        result.valid_unique_count = valid_unique_signers.size();
        result.has_quorum         = valid_unique_signers.size() >= threshold;
        return result;
    }
} // namespace sgns::multisig
