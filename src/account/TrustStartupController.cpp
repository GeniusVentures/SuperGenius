#include "account/TrustStartupController.hpp"

#include <algorithm>

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
            if ( configured.peers != persisted.peers )
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
            instance->Emit( EventCode::TRUST_LOCAL_STATE_CORRUPT );
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
        if ( !instance->secure_crdt_->RegisterCandidateCallback(
                 "trusted-peer-genesis",
                 [weak]( const auto &id, const auto & )
                 {
                     if ( auto self = weak.lock() )
                     {
                         (void) self->registry_->TryActivateReviewedGenesisCandidate( id );
                         (void) self->Refresh();
                     }
                 },
                 &instance->callback_owner_token_ ) ||
             !instance->secure_crdt_->RegisterCandidateCallback(
                 "burn-config",
                 [weak]( const auto &id, const auto & )
                 {
                     if ( auto self = weak.lock() )
                     {
                         {
                             std::lock_guard<std::mutex> lock( self->candidate_mutex_ );
                             if ( std::find( self->pending_burn_candidates_.begin(),
                                             self->pending_burn_candidates_.end(),
                                             id ) == self->pending_burn_candidates_.end() )
                             {
                                 self->pending_burn_candidates_.push_back( id );
                             }
                         }
                         (void) self->burn_config_->TryActivateBurnCandidate( id );
                         (void) self->Refresh();
                     }
                 },
                 &instance->callback_owner_token_ ) )
        {
            return outcome::failure( std::errc::operation_not_permitted );
        }

        BOOST_OUTCOME_TRY( instance->Refresh() );
        return instance;
    }

    TrustStartupController::~TrustStartupController()
    {
        if ( secure_crdt_ )
        {
            secure_crdt_->UnregisterCandidateCallbackIf( "burn-config", &callback_owner_token_ );
            secure_crdt_->UnregisterCandidateCallbackIf( "trusted-peer-genesis", &callback_owner_token_ );
        }
    }

    outcome::result<void> TrustStartupController::Refresh()
    {
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
            if ( candidate )
            {
                (void) registry_->TryActivateReviewedGenesisCandidate( *candidate );
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

        if ( burn_config_->IsEconomicallyReady() )
        {
            SetState( State::ConfirmedReady );
            return outcome::success();
        }

        std::vector<sgns::securecrdt::CandidateId> pending;
        {
            std::lock_guard<std::mutex> lock( candidate_mutex_ );
            pending = pending_burn_candidates_;
        }
        for ( const auto &candidate : pending )
        {
            auto activated = burn_config_->TryActivateBurnCandidate( candidate );
            if ( activated.has_value() && burn_config_->IsEconomicallyReady() )
            {
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
        return state_.load() != State::FreshWaitingForGenesis && state_.load() != State::FatalMismatch;
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
} // namespace sgns::account
