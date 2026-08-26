/**
 * @file       BurnConfig.cpp
 * @brief      Implementation of the genesis-seeded, quorum-signed,
 *             cache-refresh-via-quorum-re-derivation `BURN_BASIS_POINTS`
 *             value (BURN-01, BURN-02, BURN-03).
 * @date       2026-07-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/BurnConfig.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <system_error>

#include "account/GeniusAccount.hpp"
#include "securecrdt/QuorumThresholdValidation.hpp"

namespace sgns::account
{
    bool ConfirmedBurnValueProvider::IsReady() const
    {
        return ready_.load( std::memory_order_acquire );
    }

    uint64_t ConfirmedBurnValueProvider::GetBasisPoints() const
    {
        return basis_points_.load( std::memory_order_relaxed );
    }

    BurnConfigPayload::BurnConfigPayload( uint64_t basis_points ) : basis_points_( basis_points )
    {
    }

    std::vector<uint8_t> BurnConfigPayload::SerializeToBytes() const
    {
        const std::string encoded = std::to_string( basis_points_ );
        return std::vector<uint8_t>( encoded.begin(), encoded.end() );
    }

    bool BurnConfigPayload::DeserializeFromBytes( const std::vector<uint8_t> &bytes )
    {
        if ( bytes.empty() )
        {
            return false;
        }

        uint64_t   parsed = 0;
        const auto begin  = reinterpret_cast<const char *>( bytes.data() );
        const auto end    = begin + bytes.size();
        auto [ptr, ec]    = std::from_chars( begin, end, parsed );
        if ( ec != std::errc() || ptr != end )
        {
            return false;
        }

        basis_points_ = parsed;
        return true;
    }

    bool BurnConfigPayload::Verify( const std::vector<uint8_t> &payload ) const
    {
        if ( payload.empty() )
        {
            return false;
        }

        uint64_t   parsed = 0;
        const auto begin  = reinterpret_cast<const char *>( payload.data() );
        const auto end    = begin + payload.size();
        auto [ptr, ec]    = std::from_chars( begin, end, parsed );
        if ( ec != std::errc() || ptr != end )
        {
            return false;
        }

        return parsed <= BASIS_POINTS_TOTAL;
    }

    void BurnConfigPayload::Apply()
    {
        applied_ = true;
    }

    BurnConfig::BurnConfig( std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
                            std::shared_ptr<sgns::crdt::GlobalDB>                  db,
                            std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,
                            uint64_t                                                quorum_threshold,
                            std::shared_ptr<sgns::GeniusAccount>          account,
                            sgns::crdt::HierarchicalKey                             base_key ) :
        secure_crdt_( std::move( secure_crdt ) ),
        db_( std::move( db ) ),
        trusted_peer_registry_( std::move( trusted_peer_registry ) ),
        quorum_threshold_( quorum_threshold ),
        account_( std::move( account ) ),
        base_key_( std::move( base_key ) )
    {
    }

    BurnConfig::~BurnConfig()
    {
        Unregister();
    }

    outcome::result<std::shared_ptr<BurnConfig>> BurnConfig::New(
        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
        std::shared_ptr<sgns::crdt::GlobalDB>                   db,
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,
        uint64_t                                                 quorum_threshold,
        std::shared_ptr<sgns::GeniusAccount>           account,
        sgns::crdt::HierarchicalKey                              base_key )
    {
        auto validation_result = sgns::securecrdt::ValidateBurnQuorumThreshold(
            quorum_threshold, trusted_peer_registry->GetCurrentPeers().size() );
        if ( validation_result.has_error() )
        {
            return validation_result.error();
        }

        auto instance = std::make_shared<BurnConfig>( std::move( secure_crdt ), std::move( db ),
                                                       std::move( trusted_peer_registry ), quorum_threshold,
                                                       std::move( account ), std::move( base_key ) );
        if ( !instance->RegisterSignerSetSource() )
        {
            return outcome::failure( std::errc::file_exists );
        }
        instance->RegisterCrdtChangeCallback();
        instance->TrySeedGenesisIfEligible();

        // Seed the cache from whatever is synchronously confirmable right
        // now (BURN-03): falls back to GENESIS_DEFAULT_BASIS_POINTS (already
        // the member-initializer default) if nothing is confirmed yet.
        instance->OnCrdtElementChanged();

        return instance;
    }

    outcome::result<std::shared_ptr<BurnConfig>> BurnConfig::NewProduction(
        std::shared_ptr<sgns::securecrdt::SecureCrdt>           secure_crdt,
        std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> trusted_peer_registry,
        std::shared_ptr<sgns::trustedpeer::TrustStateStore>     trust_store,
        std::string                                             local_signer_address,
        SignCallback                                            sign_callback,
        std::string                                             candidate_domain )
    {
        if ( !secure_crdt || !trusted_peer_registry || !trust_store || candidate_domain.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        auto instance = std::make_shared<BurnConfig>( std::move( secure_crdt ),
                                                       nullptr,
                                                       std::move( trusted_peer_registry ),
                                                       0,
                                                       nullptr,
                                                       sgns::crdt::HierarchicalKey( candidate_domain ) );
        instance->production_mode_      = true;
        instance->trust_store_          = std::move( trust_store );
        instance->local_signer_address_ = std::move( local_signer_address );
        instance->sign_callback_        = std::move( sign_callback );
        instance->candidate_domain_     = std::move( candidate_domain );
        if ( !instance->RegisterProductionDomain() )
        {
            return outcome::failure( std::errc::file_exists );
        }

        auto snapshot = instance->trusted_peer_registry_->GetConfirmedSnapshot();
        if ( snapshot.has_value() &&
             snapshot.value().burn_authorization == sgns::trustedpeer::BurnAuthorizationKind::PeerQuorum )
        {
            instance->PublishConfirmedBurn( snapshot.value() );
        }
        return instance;
    }

    bool BurnConfig::RegisterProductionDomain()
    {
        return secure_crdt_->Registry().RegisterCandidateDomain(
            candidate_domain_,
            sgns::securecrdt::CandidateDomainEntry{
                candidate_domain_,
                sgns::securecrdt::CandidateKind::BurnConfig,
                [weak_self = weak_from_this()]()
                    -> outcome::result<sgns::securecrdt::CandidateAuthorizationSnapshot>
                {
                    auto self = weak_self.lock();
                    if ( !self ) return outcome::failure( sgns::trustedpeer::TrustedPeerRegistry::Error::NOT_CONFIRMED );
                    return self->ResolveBurnAuthorization();
                },
                &registry_token_ } );
    }

    outcome::result<sgns::securecrdt::CandidateAuthorizationSnapshot> BurnConfig::ResolveBurnAuthorization() const
    {
        auto snapshot = trusted_peer_registry_->GetConfirmedSnapshot();
        if ( snapshot.has_error() ) return snapshot.error();
        const auto policy_hash = snapshot.value().policy.Hash();
        if ( !policy_hash ) return outcome::failure( std::errc::invalid_argument );

        uint64_t next_version = 1;
        std::string predecessor = sgns::trustedpeer::BurnGenesisAnchorHash( snapshot.value().genesis_fingerprint );
        if ( IsEconomicallyReady() )
        {
            if ( snapshot.value().burn.version == std::numeric_limits<uint64_t>::max() )
                return outcome::failure( std::errc::value_too_large );
            next_version = snapshot.value().burn.version + 1;
            predecessor = snapshot.value().burn.Hash().value();
        }
        return sgns::securecrdt::CandidateAuthorizationSnapshot{
            snapshot.value().policy.network_id,
            sgns::securecrdt::CandidateKind::BurnConfig,
            next_version,
            predecessor,
            *policy_hash,
            snapshot.value().policy.peers
        };
    }

    std::optional<sgns::securecrdt::CandidateCore> BurnConfig::BurnCandidateCore(
        const sgns::trustedpeer::ConfirmedBurnState &candidate, const std::string &domain )
    {
        auto bytes = candidate.CanonicalBytes();
        if ( !bytes || domain.empty() ) return std::nullopt;
        return sgns::securecrdt::CandidateCore{
            sgns::securecrdt::CandidateCore::ENCODING_VERSION,
            domain,
            candidate.network_id,
            sgns::securecrdt::CandidateKind::BurnConfig,
            candidate.version,
            candidate.expected_previous_hash,
            candidate.authorizing_policy_hash,
            std::move( *bytes )
        };
    }

    outcome::result<sgns::securecrdt::CandidateId> BurnConfig::SubmitLocalApproval(
        const sgns::securecrdt::CandidateCore &core )
    {
        if ( !sign_callback_ || local_signer_address_.empty() )
            return outcome::failure( std::errc::operation_not_permitted );
        const auto bytes = core.CanonicalBytes();
        const auto id = sgns::securecrdt::CandidateId::FromCore( core );
        if ( !bytes || !id ) return outcome::failure( std::errc::invalid_argument );
        auto existing = secure_crdt_->ReadCandidateApprovals( *id );
        if ( existing.has_value() &&
             std::any_of( existing.value().begin(), existing.value().end(), [&]( const auto &approval ) {
                 return approval.signer == local_signer_address_;
             } ) ) return *id;
        return secure_crdt_->SubmitCandidateApproval( {
            sgns::securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
            core,
            local_signer_address_,
            sign_callback_( *bytes )
        } );
    }

    outcome::result<sgns::securecrdt::CandidateId> BurnConfig::OnTrustedPeerGenesisConfirmed()
    {
        auto snapshot = trusted_peer_registry_->GetConfirmedSnapshot();
        if ( snapshot.has_error() ) return snapshot.error();
        if ( automatic_genesis_candidate_ ) return *automatic_genesis_candidate_;
        if ( std::find( snapshot.value().policy.peers.begin(),
                        snapshot.value().policy.peers.end(),
                        local_signer_address_ ) == snapshot.value().policy.peers.end() )
            return outcome::failure( std::errc::operation_not_permitted );
        const auto policy_hash = snapshot.value().policy.Hash();
        if ( !policy_hash || snapshot.value().policy.version != 1 || snapshot.value().burn.version != 1 ||
             snapshot.value().burn.basis_points != GENESIS_DEFAULT_BASIS_POINTS ||
             snapshot.value().burn.expected_previous_hash !=
                 sgns::trustedpeer::BurnGenesisAnchorHash( snapshot.value().genesis_fingerprint ) ||
             snapshot.value().burn.authorizing_policy_hash != *policy_hash )
            return outcome::failure( std::errc::invalid_argument );
        auto core = BurnCandidateCore( snapshot.value().burn, candidate_domain_ );
        if ( !core ) return outcome::failure( std::errc::invalid_argument );
        auto submitted = SubmitLocalApproval( *core );
        if ( submitted.has_value() ) automatic_genesis_candidate_ = submitted.value();
        return submitted;
    }

    outcome::result<std::vector<sgns::securecrdt::CandidateId>> BurnConfig::ListPendingBurnCandidates() const
    {
        auto authorization = ResolveBurnAuthorization();
        if ( authorization.has_error() ) return authorization.error();
        return secure_crdt_->ListCandidates( candidate_domain_, authorization.value().expected_previous_hash );
    }

    outcome::result<sgns::securecrdt::CandidateId> BurnConfig::ProposeBurnCandidate( uint64_t basis_points )
    {
        if ( !IsEconomicallyReady() )
            return outcome::failure( sgns::trustedpeer::TrustedPeerRegistry::Error::NOT_CONFIRMED );
        if ( basis_points > BurnConfigPayload::BASIS_POINTS_TOTAL )
            return outcome::failure( std::errc::invalid_argument );
        auto snapshot = trusted_peer_registry_->GetConfirmedSnapshot();
        if ( snapshot.has_error() ) return snapshot.error();
        if ( snapshot.value().burn.version == std::numeric_limits<uint64_t>::max() )
            return outcome::failure( std::errc::value_too_large );
        sgns::trustedpeer::ConfirmedBurnState candidate;
        candidate.network_id = snapshot.value().policy.network_id;
        candidate.version = snapshot.value().burn.version + 1;
        candidate.expected_previous_hash = snapshot.value().burn.Hash().value();
        candidate.authorizing_policy_hash = snapshot.value().policy.Hash().value();
        candidate.basis_points = basis_points;
        auto core = BurnCandidateCore( candidate, candidate_domain_ );
        if ( !core ) return outcome::failure( std::errc::invalid_argument );
        return SubmitLocalApproval( *core );
    }

    outcome::result<sgns::securecrdt::CandidateId> BurnConfig::ApproveBurnCandidate(
        const sgns::securecrdt::CandidateId &candidate_id )
    {
        if ( candidate_id.domain != candidate_domain_ ) return outcome::failure( std::errc::invalid_argument );
        auto approvals = secure_crdt_->ReadCandidateApprovals( candidate_id );
        if ( approvals.has_error() ) return approvals.error();
        if ( approvals.value().empty() ) return outcome::failure( std::errc::invalid_argument );
        // Already activated by a concurrent refresh before this approval could be
        // submitted — the authorization context has advanced, so submitting would be
        // rejected as a context mismatch. The approval is redundant; succeed.
        auto snapshot = trusted_peer_registry_->GetConfirmedSnapshot();
        if ( snapshot.has_value() &&
             snapshot.value().burn_authorization == sgns::trustedpeer::BurnAuthorizationKind::PeerQuorum &&
             snapshot.value().burn.Hash() == std::optional<std::string>( candidate_id.content_hash ) &&
             snapshot.value().burn.version == approvals.value().front().core.version )
        {
            return candidate_id;
        }
        return SubmitLocalApproval( approvals.value().front().core );
    }

    outcome::result<bool> BurnConfig::TryActivateBurnCandidate(
        const sgns::securecrdt::CandidateId &candidate_id )
    {
        auto snapshot = trusted_peer_registry_->GetConfirmedSnapshot();
        if ( snapshot.has_error() ) return snapshot.error();
        auto approvals = secure_crdt_->ReadCandidateApprovals( candidate_id );
        if ( approvals.has_error() ) return approvals.error();
        if ( approvals.value().empty() ) return outcome::failure( std::errc::invalid_argument );
        const auto &core = approvals.value().front().core;
        auto candidate = sgns::trustedpeer::ConfirmedBurnState::DecodeCanonical( core.payload );
        auto expected_core = candidate ? BurnCandidateCore( *candidate, candidate_domain_ ) : std::nullopt;
        const auto policy_hash = snapshot.value().policy.Hash();
        if ( candidate_id.domain != candidate_domain_ || !candidate || !expected_core || !( *expected_core == core ) ||
             !policy_hash || candidate->authorizing_policy_hash != *policy_hash )
            return outcome::failure( std::errc::invalid_argument );
        // Already the durable peer-confirmed burn (admin/refresh activation race) —
        // idempotent. The initial burn (BootstrapOnly) must NOT take this path: its
        // record exists but still needs the quorum-proof commit to become active.
        const auto current_burn_hash = snapshot.value().burn.Hash();
        const auto candidate_hash    = candidate->Hash();
        if ( snapshot.value().burn_authorization == sgns::trustedpeer::BurnAuthorizationKind::PeerQuorum &&
             current_burn_hash && candidate_hash && *current_burn_hash == *candidate_hash &&
             snapshot.value().burn.version == candidate->version )
            return false;
        if ( IsEconomicallyReady() )
        {
            if ( candidate->version != snapshot.value().burn.version + 1 ||
                 candidate->expected_previous_hash != snapshot.value().burn.Hash().value() )
                return outcome::failure( std::errc::invalid_argument );
        }
        else if ( candidate->version != 1 || candidate->basis_points != GENESIS_DEFAULT_BASIS_POINTS ||
                  candidate->expected_previous_hash !=
                      sgns::trustedpeer::BurnGenesisAnchorHash( snapshot.value().genesis_fingerprint ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        multisig::CollectedSignatures proof;
        for ( const auto &approval : approvals.value() ) proof.emplace_back( approval.signer, approval.signature );
        if ( proof.size() < snapshot.value().policy.burn_threshold ) return false;
        const auto authorization_bytes = core.CanonicalBytes();
        if ( !authorization_bytes ) return outcome::failure( std::errc::invalid_argument );
        auto committed = trust_store_->CommitBurnSuccessor( *candidate, proof, *authorization_bytes );
        if ( committed.has_error() ) return committed.error();
        PublishConfirmedBurn( committed.value() );
        return true;
    }

    void BurnConfig::PublishConfirmedBurn( const sgns::trustedpeer::ConfirmedTrustSnapshot &snapshot )
    {
        const auto previous = cached_basis_points_.load( std::memory_order_relaxed );
        confirmed_value_provider_->basis_points_.store( snapshot.burn.basis_points, std::memory_order_relaxed );
        confirmed_value_provider_->ready_.store( true, std::memory_order_release );
        cached_basis_points_.store( snapshot.burn.basis_points, std::memory_order_relaxed );
        if ( previous == snapshot.burn.basis_points ) return;
        std::vector<RefreshCallback> callbacks_copy;
        {
            std::lock_guard<std::mutex> lock( refresh_callbacks_mutex_ );
            callbacks_copy = refresh_callbacks_;
        }
        for ( const auto &callback : callbacks_copy ) callback( snapshot.burn.basis_points );
    }

    bool BurnConfig::IsEconomicallyReady() const
    {
        return confirmed_value_provider_->IsReady();
    }

    std::shared_ptr<const ConfirmedBurnValueProvider> BurnConfig::GetConfirmedValueProvider() const
    {
        return confirmed_value_provider_;
    }

    bool BurnConfig::RegisterSignerSetSource()
    {
        sgns::securecrdt::SecureCrdtRegistryEntry entry;
        entry.signer_set_source =
            [weak_self = weak_from_this()]( const std::string & ) -> outcome::result<sgns::securecrdt::SignerSetSnapshot>
        {
            auto self = weak_self.lock();
            if ( !self )
            {
                return sgns::securecrdt::SignerSetSnapshot{};
            }
            return sgns::securecrdt::SignerSetSnapshot{ self->trusted_peer_registry_->GetCurrentPeers(),
                                                        self->quorum_threshold_ };
        };
        entry.make_instance = []() -> std::shared_ptr<sgns::securecrdt::ISignedCRDTData>
        {
            return std::make_shared<BurnConfigPayload>();
        };
        entry.owner_token = &registry_token_;

        return secure_crdt_->Registry().Register( base_key_.GetKey(), std::move( entry ) );
    }

    void BurnConfig::RegisterCrdtChangeCallback()
    {
        const std::string pattern = "/?" + base_key_.GetKey() + "(/sig/.*)?";
        auto               weak_self = weak_from_this();
        db_->RegisterNewElementCallback( pattern,
                                         [weak_self]( sgns::crdt::CRDTCallbackManager::NewDataPair, const std::string & )
                                         {
                                             if ( auto self = weak_self.lock() )
                                             {
                                                 self->OnCrdtElementChanged();
                                             }
                                         } );
    }

    void BurnConfig::OnCrdtElementChanged()
    {
        auto read_result = secure_crdt_->ReadIfQuorum( base_key_ );
        if ( read_result.has_error() || !read_result.value().has_value() )
        {
            return;
        }

        const auto      bytes = read_result.value()->toVector();
        BurnConfigPayload payload;
        if ( !payload.DeserializeFromBytes( bytes ) || !payload.Verify( bytes ) )
        {
            logger_->error( "{}: confirmed value failed deserialize/verify", __func__ );
            return;
        }

        const uint64_t new_value = payload.GetBasisPoints();
        if ( new_value == cached_basis_points_.load( std::memory_order_relaxed ) )
        {
            return;
        }

        cached_basis_points_.store( new_value, std::memory_order_relaxed );

        std::vector<RefreshCallback> callbacks_copy;
        {
            std::lock_guard<std::mutex> lock( refresh_callbacks_mutex_ );
            callbacks_copy = refresh_callbacks_;
        }
        for ( const auto &cb : callbacks_copy )
        {
            cb( new_value );
        }

        logger_->info( "{}: cached basis points refreshed to {}", __func__, new_value );
    }

    void BurnConfig::TrySeedGenesisIfEligible()
    {
        auto read_result = secure_crdt_->ReadIfQuorum( base_key_ );
        if ( read_result.has_error() || read_result.value().has_value() )
        {
            // Either an error occurred, or a confirmed value already exists --
            // never auto-seed in either case.
            return;
        }

        const auto current_peers = trusted_peer_registry_->GetCurrentPeers();
        const auto self_address  = account_->GetAddress();
        const bool is_eligible =
            std::find( current_peers.begin(), current_peers.end(), self_address ) != current_peers.end();
        if ( !is_eligible )
        {
            return;
        }

        const BurnConfigPayload genesis_payload( GENESIS_DEFAULT_BASIS_POINTS );
        const auto               serialized = genesis_payload.SerializeToBytes();

        auto propose_result = secure_crdt_->ProposeValue( base_key_, serialized );
        if ( propose_result.has_error() )
        {
            logger_->error( "{}: ProposeValue failed", __func__ );
            return;
        }

        const auto signature_bytes = account_->Sign( serialized );
        auto sign_result = secure_crdt_->AddSignature( base_key_, self_address, signature_bytes );
        if ( sign_result.has_error() )
        {
            logger_->error( "{}: AddSignature failed", __func__ );
            return;
        }

        logger_->info( "{}: genesis burn-config default seeded ({} basis points)", __func__,
                       GENESIS_DEFAULT_BASIS_POINTS );
    }

    uint64_t BurnConfig::GetCachedBasisPoints() const
    {
        return cached_basis_points_.load( std::memory_order_relaxed );
    }

    void BurnConfig::RegisterRefreshCallback( RefreshCallback cb )
    {
        std::lock_guard<std::mutex> lock( refresh_callbacks_mutex_ );
        refresh_callbacks_.push_back( std::move( cb ) );
    }

    void BurnConfig::Unregister()
    {
        if ( production_mode_ )
        {
            secure_crdt_->Registry().UnregisterCandidateDomainIf( candidate_domain_, &registry_token_ );
        }
        else
        {
            secure_crdt_->Registry().UnregisterIf( base_key_.GetKey(), &registry_token_ );
        }
    }
} // namespace sgns::account
