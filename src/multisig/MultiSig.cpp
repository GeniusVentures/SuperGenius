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
        // NOTE: stub for Task 1 (VerifyPayloadSignature scope only); full dedup+verify
        // loop implemented in Task 2.
        (void)signer_set;
        (void)threshold;
        (void)collected_signatures;
        (void)payload;
        return QuorumResult{};
    }
} // namespace sgns::multisig
