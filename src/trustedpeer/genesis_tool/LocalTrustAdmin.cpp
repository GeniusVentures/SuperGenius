#include "trustedpeer/genesis_tool/LocalTrustAdmin.hpp"

#include <system_error>
#include <utility>

namespace sgns::trustedpeer
{
    LocalTrustAdmin::LocalTrustAdmin( std::shared_ptr<TrustedPeerRegistry> registry,
                                      std::shared_ptr<sgns::account::BurnConfig> burn_config,
                                      std::string policy_domain,
                                      std::string burn_domain ) :
        registry_( std::move( registry ) ),
        burn_config_( std::move( burn_config ) ),
        policy_domain_( std::move( policy_domain ) ),
        burn_domain_( std::move( burn_domain ) )
    {
    }

    outcome::result<std::vector<LocalTrustAdmin::CandidateSummary>> LocalTrustAdmin::ListCandidates() const
    {
        if ( !registry_ || !burn_config_ )
            return outcome::failure( std::errc::not_connected );

        BOOST_OUTCOME_TRY( auto policies, registry_->ListPendingPolicyCandidates() );
        BOOST_OUTCOME_TRY( auto burns, burn_config_->ListPendingBurnCandidates() );
        std::vector<CandidateSummary> result;
        result.reserve( policies.size() + burns.size() );
        for ( auto &id : policies )
            result.push_back( { CandidateType::Policy, std::move( id ) } );
        for ( auto &id : burns )
            result.push_back( { CandidateType::Burn, std::move( id ) } );
        return result;
    }

    outcome::result<sgns::securecrdt::CandidateId> LocalTrustAdmin::ProposePolicy(
        const QuorumPolicyState &candidate )
    {
        if ( !registry_ || !burn_config_ )
            return outcome::failure( std::errc::not_connected );
        if ( !burn_config_->IsEconomicallyReady() )
            return outcome::failure( std::errc::operation_not_permitted );
        auto proposed = registry_->ProposePolicyCandidate( candidate );
        if ( proposed.has_value() )
        {
            auto activated = registry_->TryActivatePolicyCandidate( proposed.value() );
            if ( activated.has_error() )
                return activated.error();
        }
        return proposed;
    }

    outcome::result<sgns::securecrdt::CandidateId> LocalTrustAdmin::ProposeBurn( uint64_t basis_points )
    {
        if ( !burn_config_ )
            return outcome::failure( std::errc::not_connected );
        auto proposed = burn_config_->ProposeBurnCandidate( basis_points );
        if ( proposed.has_value() )
        {
            auto activated = burn_config_->TryActivateBurnCandidate( proposed.value() );
            if ( activated.has_error() )
                return activated.error();
        }
        return proposed;
    }

    outcome::result<sgns::securecrdt::CandidateId> LocalTrustAdmin::Approve(
        const sgns::securecrdt::CandidateId &candidate_id )
    {
        if ( candidate_id.domain == policy_domain_ && registry_ )
        {
            if ( !burn_config_ )
                return outcome::failure( std::errc::not_connected );
            if ( !burn_config_->IsEconomicallyReady() )
                return outcome::failure( std::errc::operation_not_permitted );
            auto approved = registry_->ApprovePolicyCandidate( candidate_id );
            if ( approved.has_value() )
            {
                auto activated = registry_->TryActivatePolicyCandidate( approved.value() );
                if ( activated.has_error() )
                    return activated.error();
            }
            return approved;
        }
        if ( candidate_id.domain == burn_domain_ && burn_config_ )
        {
            auto approved = burn_config_->ApproveBurnCandidate( candidate_id );
            if ( approved.has_value() )
            {
                auto activated = burn_config_->TryActivateBurnCandidate( approved.value() );
                if ( activated.has_error() )
                    return activated.error();
            }
            return approved;
        }
        return outcome::failure( std::errc::invalid_argument );
    }
} // namespace sgns::trustedpeer
