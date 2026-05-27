/**
 * @file       BridgeConsensusAdapter.cpp
 * @brief      Bridge-owned consensus subject helpers for EVM bridge event claims.
 */
#include "account/BridgeConsensusAdapter.hpp"

#include <system_error>
#include <utility>
#include <vector>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>

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

        template <typename Bytes>
        std::string HexLower( const Bytes &bytes )
        {
            std::ostringstream out;
            out << std::hex << std::setfill( '0' );
            for ( auto byte : bytes )
            {
                out << std::setw( 2 ) << static_cast<unsigned>( byte );
            }
            return out.str();
        }

        outcome::result<uint64_t> BridgeAmountToUint64( const eth::BridgeEventClaim &claim )
        {
            const auto max_amount = intx::uint256{ std::numeric_limits<uint64_t>::max() };
            if ( claim.amount > max_amount )
            {
                return outcome::failure( std::errc::value_too_large );
            }
            return static_cast<uint64_t>( claim.amount );
        }

        TokenID BridgeTokenId( const eth::BridgeEventClaim &claim )
        {
            std::array<uint8_t, 32> bytes{};
            intx::be::unsafe::store( bytes.data(), claim.token_id_or_nonce );
            return TokenID::FromBytes( bytes.data(), bytes.size() );
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

    outcome::result<BridgeEventMintRequest> CreateBridgeEventMintRequest(
        const eth::BridgeEventClaim &claim )
    {
        if ( claim.src_chain_id == 0 || claim.dest_chain_id == 0 )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        auto amount = BridgeAmountToUint64( claim );
        if ( amount.has_error() )
        {
            return outcome::failure( amount.error() );
        }
        BridgeEventMintRequest request;
        request.amount           = amount.value();
        request.transaction_hash = HexLower( eth::bridge_event_claim_hash( claim ) );
        request.chain_id         = std::to_string( claim.src_chain_id );
        request.token_id         = BridgeTokenId( claim );
        request.destination      = HexLower( claim.recipient );
        return request;
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

    ConsensusManager::CertificateSubjectHandler MakeBridgeEventConsensusCertificateHandler(
        BridgeEventConsensusCertificateHandler handler )
    {
        return [handler = std::move( handler )](
                   const std::string                 &subject_hash,
                   const ConsensusManager::Certificate &certificate ) -> outcome::result<ConsensusManager::Check>
        {
            if ( !handler || !certificate.has_proposal() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            auto claim = DecodeBridgeEventConsensusSubject( certificate.proposal().subject() );
            if ( claim.has_error() )
            {
                return ConsensusManager::Check::Reject;
            }
            return handler( claim.value(), subject_hash, certificate );
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

    bool RegisterBridgeEventConsensusCertificateHandler(
        const std::shared_ptr<Blockchain>      &blockchain,
        BridgeEventConsensusCertificateHandler  handler )
    {
        if ( !blockchain || !handler )
        {
            return false;
        }
        return blockchain->RegisterCertificateHandler(
            std::string( kBridgeEventSubjectType ),
            MakeBridgeEventConsensusCertificateHandler( std::move( handler ) ) );
    }
} // namespace sgns
