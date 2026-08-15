/**
 * @file       MultiSig.cpp
 * @brief      Implementation of the standalone multi-signature primitive.
 * @date       2026-07-21
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "multisig/MultiSig.hpp"

#include "account/GeniusAccount.hpp"

namespace sgns::multisig
{
    bool VerifyPayloadSignature( const std::string          &address,
                                 const SignatureBytes       &signature,
                                 const std::vector<uint8_t> &payload )
    {
        return sgns::GeniusAccount::VerifySignature( address, signature, payload );
    }

    MultiSig::MultiSig( const std::vector<std::string> &signer_set, uint64_t required_signatures ) :
        signer_set_( signer_set.begin(), signer_set.end() ), required_signatures_( required_signatures )
    {
    }

    bool MultiSig::IsValid() const
    {
        return required_signatures_ > 0 && required_signatures_ <= signer_set_.size();
    }

    uint64_t MultiSig::RequiredSignatures() const
    {
        return required_signatures_;
    }

    size_t MultiSig::AuthorizedSignerCount() const
    {
        return signer_set_.size();
    }

    QuorumResult MultiSig::EvaluateQuorum( const CollectedSignatures  &collected_signatures,
                                           const std::vector<uint8_t> &payload ) const
    {
        QuorumResult result;
        if ( !IsValid() )
        {
            return result;
        }

        std::unordered_set<std::string> valid_unique_signers;

        for ( const auto &[address, signature] : collected_signatures )
        {
            if ( valid_unique_signers.count( address ) != 0 )
            {
                continue; // dedup-first: already counted this signer, skip before verification
            }
            if ( signer_set_.count( address ) == 0 )
            {
                continue; // unauthorized signer
            }
            if ( !VerifyPayloadSignature( address, signature, payload ) )
            {
                continue; // invalid signature, skip silently
            }
            valid_unique_signers.insert( address );
        }

        result.valid_unique_count = valid_unique_signers.size();
        result.has_quorum         = valid_unique_signers.size() >= required_signatures_;
        return result;
    }
} // namespace sgns::multisig
