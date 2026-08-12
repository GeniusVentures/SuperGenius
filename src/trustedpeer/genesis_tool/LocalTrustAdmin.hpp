/**
 * @file LocalTrustAdmin.hpp
 * @brief Explicit, process-local trusted-peer policy administration.
 */
#ifndef SGNS_TRUSTEDPEER_GENESIS_TOOL_LOCAL_TRUST_ADMIN_HPP
#define SGNS_TRUSTEDPEER_GENESIS_TOOL_LOCAL_TRUST_ADMIN_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "account/BurnConfig.hpp"
#include "outcome/outcome.hpp"
#include "securecrdt/SecureCrdtCandidate.hpp"
#include "trustedpeer/QuorumPolicy.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"

namespace sgns::trustedpeer
{
    class LocalTrustAdmin
    {
    public:
        enum class CandidateType : uint8_t
        {
            Policy,
            Burn,
        };

        struct CandidateSummary
        {
            CandidateType type = CandidateType::Policy;
            sgns::securecrdt::CandidateId id;
        };

        LocalTrustAdmin( std::shared_ptr<TrustedPeerRegistry> registry,
                         std::shared_ptr<sgns::account::BurnConfig> burn_config,
                         std::string policy_domain = "trusted-peer",
                         std::string burn_domain = "burn-config" );

        /** @brief Lists authenticated current-head candidates without signing. */
        outcome::result<std::vector<CandidateSummary>> ListCandidates() const;

        /** @brief Explicitly proposes and signs one exact policy successor. */
        outcome::result<sgns::securecrdt::CandidateId> ProposePolicy( const QuorumPolicyState &candidate );

        /** @brief Explicitly proposes and signs one exact burn successor. */
        outcome::result<sgns::securecrdt::CandidateId> ProposeBurn( uint64_t basis_points );

        /** @brief Explicitly approves only the supplied content-addressed candidate. */
        outcome::result<sgns::securecrdt::CandidateId> Approve(
            const sgns::securecrdt::CandidateId &candidate_id );

    private:
        std::shared_ptr<TrustedPeerRegistry> registry_;
        std::shared_ptr<sgns::account::BurnConfig> burn_config_;
        std::string policy_domain_;
        std::string burn_domain_;
    };
} // namespace sgns::trustedpeer

#endif // SGNS_TRUSTEDPEER_GENESIS_TOOL_LOCAL_TRUST_ADMIN_HPP
