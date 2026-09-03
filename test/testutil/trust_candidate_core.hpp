/**
 * @file       trust_candidate_core.hpp
 * @brief      Test-only twin of BurnConfig::BurnCandidateCore, for tests
 *             (trustedpeer, securecrdt) that must not link the account
 *             library that owns it. Policy candidates have no such
 *             constraint — call TrustedPeerRegistry::PolicyCandidateCore
 *             directly.
 */
#ifndef SGNS_TESTUTIL_TRUST_CANDIDATE_CORE_HPP
#define SGNS_TESTUTIL_TRUST_CANDIDATE_CORE_HPP

#include <optional>

#include "securecrdt/SecureCrdtCandidate.hpp"
#include "trustedpeer/TrustStateStore.hpp"

namespace sgns::testutil
{
    [[nodiscard]] inline std::optional<securecrdt::CandidateCore> BurnCandidateCore(
        const trustedpeer::ConfirmedBurnState &candidate )
    {
        auto bytes = candidate.CanonicalBytes();
        if ( !bytes )
        {
            return std::nullopt;
        }
        return securecrdt::CandidateCore{ securecrdt::CandidateCore::ENCODING_VERSION,
                                          "burn-config",
                                          candidate.network_id,
                                          securecrdt::CandidateKind::BurnConfig,
                                          candidate.version,
                                          candidate.expected_previous_hash,
                                          candidate.authorizing_policy_hash,
                                          std::move( *bytes ) };
    }
} // namespace sgns::testutil

#endif // SGNS_TESTUTIL_TRUST_CANDIDATE_CORE_HPP
