/**
 * @file       BridgeConsensusAdapter.cpp
 * @brief      Bridge-owned consensus subject helpers for EVM bridge event claims.
 */
#include "account/BridgeConsensusAdapter.hpp"

#include <system_error>
#include <utility>
#include <vector>

#include <gsl/span>
#include <eth/bridge_observation.hpp>

#include "crypto/hasher/hasher_impl.hpp"

namespace sgns
{
    namespace
    {
        bool CheckOpaqueSubjectPayload( const ConsensusManager::Subject &subject )
        {
            if ( subject.account_id().empty() || !subject.has_subject_type_hash() ||
                 subject.subject_type_hash().hash().empty() || subject.payload().empty() ||
                 subject.payload_hash().empty() )
            {
                return false;
            }

            sgns::crypto::HasherImpl hasher;
            auto                     payload_hash = hasher.sha2_256( subject.payload().data(), subject.payload().size() );
            return subject.payload_hash() == std::string(
                                                 reinterpret_cast<const char *>( payload_hash.data() ),
                                                 payload_hash.size() );
        }
    } // namespace

    outcome::result<ConsensusManager::Subject> CreateBridgeEventConsensusSubject(
        const std::string           &account_id,
        const eth::BridgeEventClaim &claim )
    {
        return ConsensusManager::CreateGenericSubject(
            account_id,
            std::string( kBridgeEventSubjectType ),
            eth::bridge_event_claim_payload( claim ) );
    }

    outcome::result<eth::BridgeEventClaim> DecodeBridgeEventConsensusSubject(
        const ConsensusManager::Subject &subject )
    {
        if ( !ConsensusManager::SubjectTypeMatches( subject, std::string( kBridgeEventSubjectType ) ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( !CheckOpaqueSubjectPayload( subject ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        const auto payload = std::vector<uint8_t>( subject.payload().begin(), subject.payload().end() );
        auto       claim   = eth::decode_bridge_event_claim_payload( payload );
        if ( !claim.has_value() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return claim.value();
    }

    ConsensusManager::SubjectHandler MakeBridgeEventConsensusHandler(
        BridgeEventConsensusHandler handler )
    {
        return [handler = std::move( handler )](
                   const ConsensusManager::Subject &subject ) -> outcome::result<ConsensusManager::Check>
        {
            if ( !handler )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            auto claim = DecodeBridgeEventConsensusSubject( subject );
            if ( claim.has_error() )
            {
                return outcome::failure( claim.error() );
            }
            return handler( claim.value(), subject );
        };
    }

    bool RegisterBridgeEventConsensusHandler(
        const std::shared_ptr<Blockchain> &blockchain,
        BridgeEventConsensusHandler       handler )
    {
        if ( !blockchain || !handler )
        {
            return false;
        }
        return blockchain->RegisterSubjectHandler(
            std::string( kBridgeEventSubjectType ),
            MakeBridgeEventConsensusHandler( std::move( handler ) ) );
    }
} // namespace sgns
