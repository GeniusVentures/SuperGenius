#include "account/TrustStartupController.hpp"

#include <algorithm>
#include <iterator>

#include "account/BurnConfig.hpp"
#include "securecrdt/SecureCrdt.hpp"

namespace sgns::account
{
    namespace
    {
        std::vector<std::string> ConflictingFields( const sgns::trustedpeer::GenesisManifest &configured,
                                                    const sgns::trustedpeer::GenesisManifest &persisted )
        {
            std::vector<std::string> fields;
            auto                     configured_peers = configured.peers;
            std::sort( configured_peers.begin(), configured_peers.end() );
            if ( configured_peers != persisted.peers )
            {
                fields.emplace_back( "trusted_peers" );
            }
            if ( configured.bootstrapper_public_key != persisted.bootstrapper_public_key )
            {
                fields.emplace_back( "bootstrapper_node" );
            }
            if ( configured.membership_threshold != persisted.membership_threshold )
            {
                fields.emplace_back( "trusted_peer_quorum_threshold" );
            }
            if ( configured.burn_threshold != persisted.burn_threshold )
            {
                fields.emplace_back( "burn_config_quorum_threshold" );
            }
            return fields;
        }
    } // namespace

    outcome::result<std::shared_ptr<TrustStartupController>> TrustStartupController::New(
        std::shared_ptr<sgns::securecrdt::SecureCrdt>        secure_crdt,
        std::shared_ptr<sgns::trustedpeer::TrustStateStore>  trust_store,
        std::optional<sgns::trustedpeer::GenesisManifest>    diagnostic_manifest,
        std::string                                          local_signer_address,
        sgns::trustedpeer::TrustedPeerRegistry::SignCallback sign_callback,
        EventCallback                                        event_callback,
        StateCallback                                        state_callback )
    {
        if ( !secure_crdt || !trust_store )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        auto instance             = std::shared_ptr<TrustStartupController>( new TrustStartupController );
        instance->secure_crdt_    = std::move( secure_crdt );
        instance->trust_store_    = std::move( trust_store );
        instance->local_signer_address_ = local_signer_address;
        instance->event_callback_ = std::move( event_callback );
        instance->state_callback_ = std::move( state_callback );

        auto persisted = instance->trust_store_->LoadAndVerify();
        if ( persisted.has_value() )
        {
            if ( diagnostic_manifest && diagnostic_manifest->network_id != persisted.value().genesis.network_id )
            {
                instance->manifest_ = persisted.value().genesis;
                instance->SetState( State::FatalMismatch );
                instance->Emit( EventCode::TRUST_NETWORK_MISMATCH );
                return outcome::failure( sgns::trustedpeer::TrustStateStore::Error::NETWORK_MISMATCH );
            }
            if ( diagnostic_manifest )
            {
                auto fields = ConflictingFields( *diagnostic_manifest, persisted.value().genesis );
                if ( !fields.empty() )
                {
                    instance->manifest_ = persisted.value().genesis;
                    instance->Emit( EventCode::TRUST_CONFIG_CONFLICT, std::move( fields ) );
                }
            }
            instance->manifest_ = persisted.value().genesis;
        }
        else if ( persisted.error() == sgns::trustedpeer::TrustStateStore::Error::NOT_FOUND )
        {
            if ( !diagnostic_manifest || !diagnostic_manifest->Canonicalized() )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            instance->manifest_ = diagnostic_manifest->Canonicalized().value();
        }
        else
        {
            instance->SetState( State::FatalMismatch );
            instance->Emit( persisted.error() == sgns::trustedpeer::TrustStateStore::Error::NETWORK_MISMATCH
                                ? EventCode::TRUST_NETWORK_MISMATCH
                                : EventCode::TRUST_LOCAL_STATE_CORRUPT );
            return persisted.error();
        }

        BOOST_OUTCOME_TRY( instance->registry_,
                           sgns::trustedpeer::TrustedPeerRegistry::NewProduction( instance->secure_crdt_,
                                                                                  instance->trust_store_,
                                                                                  instance->manifest_,
                                                                                  {},
                                                                                  local_signer_address,
                                                                                  sign_callback ) );
        BOOST_OUTCOME_TRY( instance->burn_config_,
                           BurnConfig::NewProduction( instance->secure_crdt_,
                                                      instance->registry_,
                                                      instance->trust_store_,
                                                      std::move( local_signer_address ),
                                                      std::move( sign_callback ) ) );
        if ( !instance->secure_crdt_->RegisterFilters() )
        {
            return outcome::failure( std::errc::operation_not_permitted );
        }

        const std::weak_ptr<TrustStartupController> weak = instance;
        const auto enqueue_candidate = [weak]( const auto &id )
        {
            if ( auto self = weak.lock() )
            {
                {
                    std::lock_guard<std::mutex> lock( self->candidate_mutex_ );
                    auto &failed = id.domain == "trusted-peer" ? self->failed_policy_candidates_
                                                               : self->failed_burn_candidates_;
                    auto &pending = id.domain == "trusted-peer" ? self->pending_policy_candidates_
                                                                : self->pending_burn_candidates_;
                    if ( std::find( failed.begin(), failed.end(), id ) == failed.end() &&
                         std::find( pending.begin(), pending.end(), id ) == pending.end() )
                    {
                        pending.push_back( id );
                    }
                }
                self->RequestRefresh();
            }
        };
        if ( !instance->secure_crdt_->RegisterCandidateCallback(
                 "trusted-peer-genesis",
                 [weak]( const auto &id, const auto & )
                 {
                     if ( auto self = weak.lock() )
                     {
                         self->RequestRefresh();
                     }
                 },
                 &instance->callback_owner_token_ ) ||
             !instance->secure_crdt_->RegisterCandidateCallback(
                 "trusted-peer",
                 [weak, enqueue_candidate]( const auto &id, const auto &approval )
                 {
                     if ( auto self = weak.lock(); self && approval.signer != self->local_signer_address_ )
                     {
                         enqueue_candidate( id );
                     }
                 },
                 &instance->callback_owner_token_ ) ||
             !instance->secure_crdt_->RegisterCandidateCallback(
                 "burn-config",
                 [weak, enqueue_candidate]( const auto &id, const auto &approval )
                 {
                     if ( auto self = weak.lock(); self && approval.signer != self->local_signer_address_ )
                     {
                         enqueue_candidate( id );
                     }
                 },
                 &instance->callback_owner_token_ ) )
        {
            return outcome::failure( std::errc::operation_not_permitted );
        }

        auto *worker_self = instance.get();
        instance->refresh_worker_ = std::thread( [worker_self]
        {
            std::unique_lock<std::mutex> lock( worker_self->refresh_worker_mutex_ );
            while ( !worker_self->stop_refresh_worker_ )
            {
                worker_self->refresh_worker_condition_.wait(
                    lock, [&] { return worker_self->stop_refresh_worker_ || worker_self->refresh_requested_; } );
                if ( worker_self->stop_refresh_worker_ ) break;
                worker_self->refresh_requested_ = false;
                lock.unlock();
                (void) worker_self->Refresh();
                lock.lock();
            }
        } );

        BOOST_OUTCOME_TRY( instance->Refresh() );
        return instance;
    }

    TrustStartupController::~TrustStartupController()
    {
        {
            std::lock_guard<std::mutex> lock( refresh_worker_mutex_ );
            stop_refresh_worker_ = true;
        }
        refresh_worker_condition_.notify_one();
        if ( refresh_worker_.joinable() )
        {
            refresh_worker_.join();
        }
        if ( secure_crdt_ )
        {
            secure_crdt_->UnregisterCandidateCallbackIf( "trusted-peer", &callback_owner_token_ );
            secure_crdt_->UnregisterCandidateCallbackIf( "burn-config", &callback_owner_token_ );
            secure_crdt_->UnregisterCandidateCallbackIf( "trusted-peer-genesis", &callback_owner_token_ );
        }
    }

    outcome::result<void> TrustStartupController::Refresh()
    {
        std::lock_guard<std::mutex> refresh_lock( refresh_execution_mutex_ );
        auto snapshot = trust_store_->LoadAndVerify();
        if ( snapshot.has_error() && snapshot.error() == sgns::trustedpeer::TrustStateStore::Error::NOT_FOUND )
        {
            const auto fingerprint = manifest_.Fingerprint();
            const auto payload     = manifest_.CanonicalBytes();
            if ( !fingerprint || !payload )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            const sgns::securecrdt::CandidateCore core{ sgns::securecrdt::CandidateCore::ENCODING_VERSION,
                                                        "trusted-peer-genesis",
                                                        manifest_.network_id,
                                                        sgns::securecrdt::CandidateKind::TrustedPeerGenesis,
                                                        manifest_.policy_version,
                                                        *fingerprint,
                                                        *fingerprint,
                                                        *payload };
            const auto                            candidate = sgns::securecrdt::CandidateId::FromCore( core );
            if ( candidate && ( !failed_genesis_candidate_ || !( *failed_genesis_candidate_ == *candidate ) ) )
            {
                auto approvals = secure_crdt_->ReadCandidateApprovals( *candidate );
                if ( approvals.has_error() )
                {
                    return approvals.error();
                }
                if ( !approvals.value().empty() )
                {
                    auto activated = registry_->TryActivateReviewedGenesisCandidate( *candidate );
                    if ( activated.has_error() )
                    {
                        failed_genesis_candidate_ = *candidate;
                        Emit( EventCode::TRUST_ACTIVATION_FAILED,
                              { candidate->domain,
                                std::to_string( candidate->version ),
                                candidate->content_hash,
                                activated.error().message() } );
                        return activated.error();
                    }
                }
            }
            snapshot = trust_store_->LoadAndVerify();
        }
        if ( snapshot.has_error() )
        {
            if ( snapshot.error() == sgns::trustedpeer::TrustStateStore::Error::NOT_FOUND )
            {
                SetState( State::FreshWaitingForGenesis );
                return outcome::success();
            }
            SetState( State::FatalMismatch );
            Emit( EventCode::TRUST_LOCAL_STATE_CORRUPT );
            return snapshot.error();
        }

        auto discovered_policies = registry_->ListPendingPolicyCandidates();
        if ( discovered_policies.has_error() )
        {
            return discovered_policies.error();
        }
        {
            std::lock_guard<std::mutex> lock( candidate_mutex_ );
            for ( const auto &candidate : discovered_policies.value() )
            {
                if ( std::find( failed_policy_candidates_.begin(), failed_policy_candidates_.end(), candidate ) ==
                         failed_policy_candidates_.end() &&
                     std::find( pending_policy_candidates_.begin(), pending_policy_candidates_.end(), candidate ) ==
                         pending_policy_candidates_.end() )
                {
                    pending_policy_candidates_.push_back( candidate );
                }
            }
        }
        std::vector<sgns::securecrdt::CandidateId> policy_candidates;
        {
            std::lock_guard<std::mutex> lock( candidate_mutex_ );
            policy_candidates = pending_policy_candidates_;
        }
        std::sort( policy_candidates.begin(),
                   policy_candidates.end(),
                   []( const auto &left, const auto &right )
                   {
                       return left.version == right.version ? left.content_hash < right.content_hash
                                                            : left.version < right.version;
                   } );
        policy_candidates.erase( std::unique( policy_candidates.begin(), policy_candidates.end() ),
                                 policy_candidates.end() );
        for ( const auto &candidate : policy_candidates )
        {
            auto activated = registry_->TryActivatePolicyCandidate( candidate );
            if ( activated.has_error() )
            {
                {
                    std::lock_guard<std::mutex> lock( candidate_mutex_ );
                    pending_policy_candidates_.erase(
                        std::remove( pending_policy_candidates_.begin(),
                                     pending_policy_candidates_.end(),
                                     candidate ),
                        pending_policy_candidates_.end() );
                    if ( std::find( failed_policy_candidates_.begin(), failed_policy_candidates_.end(), candidate ) ==
                         failed_policy_candidates_.end() )
                    {
                        failed_policy_candidates_.push_back( candidate );
                    }
                }
                Emit( EventCode::TRUST_ACTIVATION_FAILED,
                      { candidate.domain,
                        std::to_string( candidate.version ),
                        candidate.content_hash,
                        activated.error().message() } );
                return activated.error();
            }
            if ( activated.value() )
            {
                {
                    std::lock_guard<std::mutex> lock( candidate_mutex_ );
                    pending_policy_candidates_.clear();
                }
                snapshot = trust_store_->LoadAndVerify();
                if ( snapshot.has_error() )
                {
                    Emit( EventCode::TRUST_ACTIVATION_FAILED,
                          { candidate.domain,
                            std::to_string( candidate.version ),
                            candidate.content_hash,
                            snapshot.error().message() } );
                    return snapshot.error();
                }
                // All candidates in this pass were authorized by the predecessor
                // that just advanced. Rediscover against the new durable head on
                // the next refresh rather than reporting ordinary stale losers.
                break;
            }
        }

        std::vector<sgns::securecrdt::CandidateId> pending;
        if ( snapshot.value().burn_authorization == sgns::trustedpeer::BurnAuthorizationKind::BootstrapOnly &&
             std::find( snapshot.value().policy.peers.begin(),
                        snapshot.value().policy.peers.end(),
                        local_signer_address_ ) != snapshot.value().policy.peers.end() )
        {
            auto initiated = burn_config_->OnTrustedPeerGenesisConfirmed();
            if ( initiated.has_error() )
            {
                auto core = BurnConfig::BurnCandidateCore( snapshot.value().burn );
                auto id = core ? sgns::securecrdt::CandidateId::FromCore( *core ) : std::nullopt;
                Emit( EventCode::TRUST_ACTIVATION_FAILED,
                      { id ? id->domain : "burn-config",
                        id ? std::to_string( id->version ) : std::to_string( snapshot.value().burn.version ),
                        id ? id->content_hash : "",
                        initiated.error().message() } );
                return initiated.error();
            }
            {
                std::lock_guard<std::mutex> lock( candidate_mutex_ );
                if ( std::find( failed_burn_candidates_.begin(),
                                failed_burn_candidates_.end(),
                                initiated.value() ) == failed_burn_candidates_.end() &&
                     std::find( pending_burn_candidates_.begin(),
                                pending_burn_candidates_.end(),
                                initiated.value() ) == pending_burn_candidates_.end() )
                {
                    pending_burn_candidates_.push_back( initiated.value() );
                }
            }
        }
        auto discovered = burn_config_->ListPendingBurnCandidates();
        if ( discovered.has_error() )
        {
            return discovered.error();
        }
        {
            std::lock_guard<std::mutex> lock( candidate_mutex_ );
            for ( const auto &candidate : discovered.value() )
            {
                if ( std::find( failed_burn_candidates_.begin(), failed_burn_candidates_.end(), candidate ) ==
                         failed_burn_candidates_.end() &&
                     std::find( pending_burn_candidates_.begin(), pending_burn_candidates_.end(), candidate ) ==
                         pending_burn_candidates_.end() )
                {
                    pending_burn_candidates_.push_back( candidate );
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock( candidate_mutex_ );
            std::copy_if( pending_burn_candidates_.begin(),
                          pending_burn_candidates_.end(),
                          std::back_inserter( pending ),
                          []( const auto &candidate ) { return candidate.domain == "burn-config"; } );
        }
        std::sort( pending.begin(), pending.end(), []( const auto &left, const auto &right ) {
            if ( left.domain != right.domain ) return left.domain < right.domain;
            return left.version == right.version ? left.content_hash < right.content_hash
                                                 : left.version < right.version;
        } );
        pending.erase( std::unique( pending.begin(), pending.end() ), pending.end() );
        for ( const auto &candidate : pending )
        {
            auto activated = burn_config_->TryActivateBurnCandidate( candidate );
            if ( activated.has_error() )
            {
                {
                    std::lock_guard<std::mutex> lock( candidate_mutex_ );
                    pending_burn_candidates_.erase(
                        std::remove( pending_burn_candidates_.begin(), pending_burn_candidates_.end(), candidate ),
                        pending_burn_candidates_.end() );
                    if ( std::find( failed_burn_candidates_.begin(), failed_burn_candidates_.end(), candidate ) ==
                         failed_burn_candidates_.end() )
                    {
                        failed_burn_candidates_.push_back( candidate );
                    }
                }
                Emit( EventCode::TRUST_ACTIVATION_FAILED,
                      { candidate.domain,
                        std::to_string( candidate.version ),
                        candidate.content_hash,
                        activated.error().message() } );
                return activated.error();
            }
            if ( activated.value() )
            {
                {
                    std::lock_guard<std::mutex> lock( candidate_mutex_ );
                    pending_burn_candidates_.erase(
                        std::remove( pending_burn_candidates_.begin(), pending_burn_candidates_.end(), candidate ),
                        pending_burn_candidates_.end() );
                }
                snapshot = trust_store_->LoadAndVerify();
                if ( snapshot.has_error() )
                {
                    Emit( EventCode::TRUST_ACTIVATION_FAILED,
                          { candidate.domain,
                            std::to_string( candidate.version ),
                            candidate.content_hash,
                            snapshot.error().message() } );
                    return snapshot.error();
                }
                // The durable predecessor changed. Any remaining IDs were eligible for
                // the previous head, so discover again on the next refresh instead of
                // treating them as actionable stale candidates.
                break;
            }
        }
        SetState( burn_config_->IsEconomicallyReady() ? State::ConfirmedReady : State::WaitingForInitialBurn );
        return outcome::success();
    }

    outcome::result<void> TrustStartupController::ObserveReplicatedSnapshot(
        const std::optional<sgns::trustedpeer::ConfirmedTrustSnapshot> &replicated )
    {
        BOOST_OUTCOME_TRY( auto durable, trust_store_->LoadAndVerify() );
        if ( !replicated )
        {
            Emit( EventCode::TRUST_CRDT_MISSING );
            return outcome::success();
        }
        const auto durable_policy = durable.policy.Hash();
        const auto remote_policy  = replicated->policy.Hash();
        const auto durable_burn   = durable.burn.Hash();
        const auto remote_burn    = replicated->burn.Hash();
        if ( replicated->policy.version < durable.policy.version || replicated->burn.version < durable.burn.version )
        {
            Emit( EventCode::TRUST_CRDT_ROLLBACK );
        }
        else if ( replicated->policy.version == durable.policy.version &&
                  replicated->burn.version == durable.burn.version &&
                  ( remote_policy != durable_policy || remote_burn != durable_burn ) )
        {
            Emit( EventCode::TRUST_CRDT_FORK );
        }
        return outcome::success();
    }

    TrustStartupController::State TrustStartupController::GetState() const noexcept
    {
        return state_.load();
    }

    bool TrustStartupController::CanApproveSuccessors() const noexcept
    {
        return state_.load() == State::ConfirmedReady;
    }

    bool TrustStartupController::IsEconomicallyReady() const noexcept
    {
        return state_.load() == State::ConfirmedReady && burn_config_ && burn_config_->IsEconomicallyReady();
    }

    std::vector<std::string> TrustStartupController::GetCurrentPeers() const
    {
        return registry_ ? registry_->GetCurrentPeers() : std::vector<std::string>{};
    }

    std::shared_ptr<sgns::trustedpeer::TrustedPeerRegistry> TrustStartupController::registry() const
    {
        return registry_;
    }

    std::shared_ptr<BurnConfig> TrustStartupController::burn_config() const
    {
        return burn_config_;
    }

    void TrustStartupController::SetState( State state )
    {
        const auto previous = state_.exchange( state );
        if ( previous != state && state_callback_ )
        {
            state_callback_( state );
        }
    }

    void TrustStartupController::Emit( EventCode code, std::vector<std::string> fields ) const
    {
        if ( !event_callback_ )
        {
            return;
        }
        const auto fingerprint = manifest_.Fingerprint();
        event_callback_( Event{ code, std::move( fields ), fingerprint.value_or( "" ) } );
    }

    void TrustStartupController::RequestRefresh()
    {
        {
            std::lock_guard<std::mutex> lock( refresh_worker_mutex_ );
            if ( stop_refresh_worker_ ) return;
            refresh_requested_ = true;
        }
        refresh_worker_condition_.notify_one();
    }
} // namespace sgns::account
