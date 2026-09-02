/**
 * @file       TrustedPeerRegistry.cpp
 * @brief      Implementation of the genesis-seeded, quorum-updatable
 *             trusted-peer set (TPR-01, TPR-02, TPR-03).
 * @date       2026-07-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "trustedpeer/TrustedPeerRegistry.hpp"

#include <system_error>
#include <limits>
#include <unordered_set>

#include "base/hexutil.hpp"
#include "securecrdt/QuorumThresholdValidation.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::trustedpeer, TrustedPeerRegistry::Error, e )
{
    using Error = sgns::trustedpeer::TrustedPeerRegistry::Error;
    switch ( e )
    {
        case Error::NOT_CONFIRMED:
            return "trusted-peer genesis is not durably confirmed";
        case Error::INVALID_CANDIDATE:
            return "trusted-peer candidate is invalid for the durable policy";
        case Error::SIGNING_UNAVAILABLE:
            return "explicit local signing is unavailable";
    }
    return "unknown TrustedPeerRegistry::Error";
}

namespace sgns::trustedpeer
{
    TrustedPeerListPayload::TrustedPeerListPayload( std::vector<std::string> peers ) : peers_( std::move( peers ) )
    {
    }

    std::vector<uint8_t> TrustedPeerListPayload::SerializeToBytes() const
    {
        std::string joined;
        for ( size_t i = 0; i < peers_.size(); ++i )
        {
            if ( i > 0 )
            {
                joined.push_back( '\n' );
            }
            joined += peers_[i];
        }
        return std::vector<uint8_t>( joined.begin(), joined.end() );
    }

    std::optional<TrustedPeerListPayload> TrustedPeerListPayload::FromBytes( const std::vector<uint8_t> &bytes )
    {
        if ( bytes.empty() )
        {
            return std::nullopt;
        }

        std::string              raw( bytes.begin(), bytes.end() );
        std::vector<std::string> parsed;
        size_t                   start = 0;
        while ( start <= raw.size() )
        {
            const auto pos = raw.find( '\n', start );
            if ( pos == std::string::npos )
            {
                parsed.push_back( raw.substr( start ) );
                break;
            }
            parsed.push_back( raw.substr( start, pos - start ) );
            start = pos + 1;
        }

        return TrustedPeerListPayload( std::move( parsed ) );
    }

    bool TrustedPeerListPayload::DeserializeFromBytes( const std::vector<uint8_t> &bytes )
    {
        auto payload = FromBytes( bytes );
        if ( !payload )
        {
            return false;
        }

        peers_ = std::move( payload->peers_ );
        return true;
    }

    bool TrustedPeerListPayload::Verify( const std::vector<uint8_t> &payload ) const
    {
        if ( payload.empty() )
        {
            return false;
        }

        std::string              raw( payload.begin(), payload.end() );
        std::vector<std::string> entries;
        size_t                   start = 0;
        while ( start <= raw.size() )
        {
            const auto pos = raw.find( '\n', start );
            if ( pos == std::string::npos )
            {
                entries.push_back( raw.substr( start ) );
                break;
            }
            entries.push_back( raw.substr( start, pos - start ) );
            start = pos + 1;
        }

        if ( entries.empty() )
        {
            return false;
        }

        std::unordered_set<std::string> unique_entries;
        for ( const auto &entry : entries )
        {
            if ( !sgns::base::IsHexAddress( entry ) )
            {
                return false;
            }
            if ( !unique_entries.insert( entry ).second )
            {
                return false; // duplicate entry
            }
        }

        return true;
    }

    void TrustedPeerListPayload::Apply()
    {
    }

    TrustedPeerRegistry::TrustedPeerRegistry( std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
                                              std::vector<std::string>                      genesis_peers,
                                              std::string                                   bootstrapper_address,
                                              uint64_t                                      quorum_threshold,
                                              sgns::crdt::HierarchicalKey                   base_key ) :
        secure_crdt_( std::move( secure_crdt ) ),
        base_key_( std::move( base_key ) ),
        bootstrapper_address_( std::move( bootstrapper_address ) ),
        quorum_threshold_( quorum_threshold ),
        cached_peers_( std::move( genesis_peers ) )
    {
    }

    TrustedPeerRegistry::~TrustedPeerRegistry()
    {
        Unregister();
    }

    outcome::result<std::shared_ptr<TrustedPeerRegistry>> TrustedPeerRegistry::New(
        std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
        std::vector<std::string>                      genesis_peers,
        std::string                                   bootstrapper_address,
        uint64_t                                      quorum_threshold,
        sgns::crdt::HierarchicalKey                   base_key )
    {
        auto validation_result = sgns::securecrdt::ValidateMembershipQuorumThreshold( quorum_threshold,
                                                                                      genesis_peers.size() );
        if ( validation_result.has_error() )
        {
            return validation_result.error();
        }

        auto instance = std::make_shared<TrustedPeerRegistry>( std::move( secure_crdt ),
                                                               std::move( genesis_peers ),
                                                               std::move( bootstrapper_address ),
                                                               quorum_threshold,
                                                               std::move( base_key ) );
        if ( !instance->RegisterSignerSetSource() )
        {
            return outcome::failure( std::errc::file_exists );
        }
        return instance;
    }

    outcome::result<std::shared_ptr<TrustedPeerRegistry>> TrustedPeerRegistry::NewProduction(
        std::shared_ptr<sgns::securecrdt::SecureCrdt> secure_crdt,
        std::shared_ptr<TrustStateStore>              trust_store,
        GenesisManifest                               reviewed_manifest,
        std::vector<uint8_t>                          bootstrap_manifest_signature,
        std::string                                   local_signer_address,
        SignCallback                                  sign_callback,
        std::string                                   policy_domain )
    {
        const auto canonical = reviewed_manifest.Canonicalized();
        const auto bytes     = reviewed_manifest.CanonicalBytes();
        if ( !secure_crdt || !trust_store || !canonical || !bytes || policy_domain.empty() )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }

        auto instance                           = std::make_shared<TrustedPeerRegistry>( std::move( secure_crdt ),
                                                               std::vector<std::string>{},
                                                               canonical->bootstrapper_public_key,
                                                               canonical->membership_threshold,
                                                               sgns::crdt::HierarchicalKey( policy_domain ) );
        instance->production_mode_              = true;
        instance->trust_store_                  = std::move( trust_store );
        instance->reviewed_manifest_            = *canonical;
        instance->bootstrap_manifest_signature_ = std::move( bootstrap_manifest_signature );
        instance->local_signer_address_         = std::move( local_signer_address );
        instance->sign_callback_                = std::move( sign_callback );
        instance->policy_domain_                = std::move( policy_domain );
        instance->genesis_domain_               = instance->policy_domain_ + "-genesis";

        if ( !instance->RegisterProductionDomains() )
        {
            return outcome::failure( std::errc::file_exists );
        }
        auto restored = instance->trust_store_->LoadAndVerify();
        if ( restored.has_value() )
        {
            if ( restored.value().genesis_fingerprint != instance->reviewed_manifest_.Fingerprint().value() )
            {
                instance->Unregister();
                return outcome::failure( TrustStateStore::Error::CORRUPT_FINGERPRINT );
            }
            instance->PublishSnapshot( restored.value() );
        }
        else if ( restored.error() != TrustStateStore::Error::NOT_FOUND )
        {
            instance->Unregister();
            return restored.error();
        }
        return instance;
    }

    bool TrustedPeerRegistry::RegisterProductionDomains()
    {
        const bool genesis_registered = secure_crdt_->Registry().RegisterCandidateDomain(
            genesis_domain_,
            sgns::securecrdt::CandidateDomainEntry{
                genesis_domain_,
                sgns::securecrdt::CandidateKind::TrustedPeerGenesis,
                [weak_self = weak_from_this()]() -> outcome::result<sgns::securecrdt::CandidateAuthorizationSnapshot>
                {
                    auto self = weak_self.lock();
                    if ( !self )
                    {
                        return outcome::failure( Error::NOT_CONFIRMED );
                    }
                    return self->ResolveGenesisAuthorization();
                },
                &registry_token_ } );
        const bool policy_registered = secure_crdt_->Registry().RegisterCandidateDomain(
            policy_domain_,
            sgns::securecrdt::CandidateDomainEntry{
                policy_domain_,
                sgns::securecrdt::CandidateKind::TrustPolicy,
                [weak_self = weak_from_this()]() -> outcome::result<sgns::securecrdt::CandidateAuthorizationSnapshot>
                {
                    auto self = weak_self.lock();
                    if ( !self )
                    {
                        return outcome::failure( Error::NOT_CONFIRMED );
                    }
                    return self->ResolvePolicyAuthorization();
                },
                &registry_token_ } );
        if ( !genesis_registered || !policy_registered )
        {
            secure_crdt_->Registry().UnregisterCandidateDomainIf( genesis_domain_, &registry_token_ );
            secure_crdt_->Registry().UnregisterCandidateDomainIf( policy_domain_, &registry_token_ );
            return false;
        }
        return true;
    }

    outcome::result<sgns::securecrdt::CandidateAuthorizationSnapshot> TrustedPeerRegistry::ResolveGenesisAuthorization()
        const
    {
        const auto fingerprint = reviewed_manifest_.Fingerprint();
        if ( !fingerprint )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        return sgns::securecrdt::CandidateAuthorizationSnapshot{ reviewed_manifest_.network_id,
                                                                 sgns::securecrdt::CandidateKind::TrustedPeerGenesis,
                                                                 reviewed_manifest_.policy_version,
                                                                 *fingerprint,
                                                                 *fingerprint,
                                                                 { reviewed_manifest_.bootstrapper_public_key } };
    }

    outcome::result<sgns::securecrdt::CandidateAuthorizationSnapshot> TrustedPeerRegistry::ResolvePolicyAuthorization()
        const
    {
        auto snapshot = GetConfirmedSnapshot();
        if ( snapshot.has_error() )
        {
            return snapshot.error();
        }
        const auto policy_hash = snapshot.value().policy.Hash();
        if ( !policy_hash || snapshot.value().policy.version == std::numeric_limits<uint64_t>::max() )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        return sgns::securecrdt::CandidateAuthorizationSnapshot{ snapshot.value().policy.network_id,
                                                                 sgns::securecrdt::CandidateKind::TrustPolicy,
                                                                 snapshot.value().policy.version + 1,
                                                                 *policy_hash,
                                                                 *policy_hash,
                                                                 snapshot.value().policy.peers };
    }

    std::optional<sgns::securecrdt::CandidateCore> TrustedPeerRegistry::PolicyCandidateCore(
        const QuorumPolicyState &candidate,
        const std::string       &domain )
    {
        auto bytes = candidate.CanonicalBytes();
        if ( !bytes || domain.empty() )
        {
            return std::nullopt;
        }
        return sgns::securecrdt::CandidateCore{ sgns::securecrdt::CandidateCore::ENCODING_VERSION,
                                                domain,
                                                candidate.network_id,
                                                sgns::securecrdt::CandidateKind::TrustPolicy,
                                                candidate.version,
                                                candidate.expected_previous_hash,
                                                candidate.authorizing_policy_hash,
                                                std::move( *bytes ) };
    }

    outcome::result<sgns::securecrdt::CandidateId> TrustedPeerRegistry::SubmitLocalApproval(
        const sgns::securecrdt::CandidateCore &core )
    {
        if ( !sign_callback_ || local_signer_address_.empty() )
        {
            return outcome::failure( Error::SIGNING_UNAVAILABLE );
        }
        const auto bytes = core.CanonicalBytes();
        if ( !bytes )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        const auto id = sgns::securecrdt::CandidateId::FromCore( core );
        if ( !id )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        auto existing = secure_crdt_->ReadCandidateApprovals( *id );
        if ( existing.has_value() &&
             std::any_of( existing.value().begin(),
                          existing.value().end(),
                          [&]( const auto &approval ) { return approval.signer == local_signer_address_; } ) )
        {
            return *id;
        }
        sgns::securecrdt::CandidateApprovalRecord approval{ sgns::securecrdt::CandidateApprovalRecord::ENCODING_VERSION,
                                                            core,
                                                            local_signer_address_,
                                                            sign_callback_( *bytes ) };
        return secure_crdt_->SubmitCandidateApproval( approval );
    }

    outcome::result<sgns::securecrdt::CandidateId> TrustedPeerRegistry::SubmitReviewedGenesisApproval()
    {
        if ( !production_mode_ || local_signer_address_ != reviewed_manifest_.bootstrapper_public_key )
        {
            return outcome::failure( Error::SIGNING_UNAVAILABLE );
        }
        const auto fingerprint = reviewed_manifest_.Fingerprint();
        const auto payload     = reviewed_manifest_.CanonicalBytes();
        if ( !fingerprint || !payload )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        auto core = GenesisCandidateCore( reviewed_manifest_, *payload, *fingerprint, genesis_domain_ );
        auto submitted = SubmitLocalApproval( core );
        if ( submitted.has_error() )
        {
            return submitted.error();
        }
        auto committed = trust_store_->CommitGenesis( reviewed_manifest_, bootstrap_manifest_signature_ );
        if ( committed.has_error() )
        {
            return committed.error();
        }
        PublishSnapshot( committed.value() );
        return submitted.value();
    }

    outcome::result<bool> TrustedPeerRegistry::TryActivateReviewedGenesisCandidate(
        const sgns::securecrdt::CandidateId &candidate_id )
    {
        if ( !production_mode_ || !trust_store_ )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        const auto fingerprint = reviewed_manifest_.Fingerprint();
        const auto payload     = reviewed_manifest_.CanonicalBytes();
        if ( !fingerprint || !payload )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        const auto expected = GenesisCandidateCore( reviewed_manifest_, *payload, *fingerprint, genesis_domain_ );
        const auto expected_id = sgns::securecrdt::CandidateId::FromCore( expected );
        if ( !expected_id || !( *expected_id == candidate_id ) )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        auto approvals = secure_crdt_->ReadCandidateApprovals( candidate_id );
        if ( approvals.has_error() )
        {
            return approvals.error();
        }
        const auto approval = std::find_if(
            approvals.value().begin(),
            approvals.value().end(),
            [&]( const auto &record )
            { return record.core == expected && record.signer == reviewed_manifest_.bootstrapper_public_key; } );
        if ( approval == approvals.value().end() )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        const auto authorization_bytes = expected.CanonicalBytes();
        if ( !authorization_bytes )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        auto committed = trust_store_->CommitGenesis( reviewed_manifest_, approval->signature, *authorization_bytes );
        if ( committed.has_error() )
        {
            if ( committed.error() == TrustStateStore::Error::ALREADY_INITIALIZED )
            {
                auto existing = trust_store_->LoadAndVerify();
                if ( existing.has_error() || existing.value().genesis_fingerprint != *fingerprint )
                {
                    return committed.error();
                }
                PublishSnapshot( existing.value() );
                return false;
            }
            return committed.error();
        }
        PublishSnapshot( committed.value() );
        return true;
    }

    outcome::result<std::vector<sgns::securecrdt::CandidateId>> TrustedPeerRegistry::ListPendingPolicyCandidates() const
    {
        auto snapshot = GetConfirmedSnapshot();
        if ( snapshot.has_error() )
        {
            return snapshot.error();
        }
        const auto hash = snapshot.value().policy.Hash();
        if ( !hash )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        return secure_crdt_->ListCandidates( policy_domain_, *hash );
    }

    outcome::result<sgns::securecrdt::CandidateId> TrustedPeerRegistry::ProposePolicyCandidate(
        const QuorumPolicyState &candidate )
    {
        auto current = GetConfirmedSnapshot();
        if ( current.has_error() )
        {
            return current.error();
        }
        if ( !ValidatePolicySuccessor( current.value().policy, candidate ) )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        auto core = PolicyCandidateCore( candidate, policy_domain_ );
        if ( !core )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        return SubmitLocalApproval( *core );
    }

    outcome::result<sgns::securecrdt::CandidateId> TrustedPeerRegistry::ApprovePolicyCandidate(
        const sgns::securecrdt::CandidateId &candidate_id )
    {
        if ( candidate_id.domain != policy_domain_ )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        auto approvals = secure_crdt_->ReadCandidateApprovals( candidate_id );
        if ( approvals.has_error() )
        {
            return approvals.error();
        }
        if ( approvals.value().empty() )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        // Already activated by a concurrent refresh before this approval could be
        // submitted — the authorization context has advanced, so submitting would be
        // rejected as a context mismatch. The approval is redundant; succeed.
        auto current = GetConfirmedSnapshot();
        if ( current.has_value() &&
             current.value().policy.Hash() == std::optional<std::string>( candidate_id.content_hash ) )
        {
            return candidate_id;
        }
        auto submitted = SubmitLocalApproval( approvals.value().front().core );
        if ( submitted.has_error() &&
             submitted.error() == sgns::securecrdt::SecureCrdt::Error::CANDIDATE_CONTEXT_MISMATCH )
        {
            // The refresh activated the candidate while this approval was in flight;
            // the authorization context moved past it. Re-check durability.
            current = GetConfirmedSnapshot();
            if ( current.has_value() &&
                 current.value().policy.Hash() == std::optional<std::string>( candidate_id.content_hash ) )
            {
                return candidate_id;
            }
        }
        return submitted;
    }

    outcome::result<bool> TrustedPeerRegistry::TryActivatePolicyCandidate(
        const sgns::securecrdt::CandidateId &candidate_id )
    {
        auto current = GetConfirmedSnapshot();
        if ( current.has_error() )
        {
            return current.error();
        }
        auto approvals = secure_crdt_->ReadCandidateApprovals( candidate_id );
        if ( approvals.has_error() )
        {
            return approvals.error();
        }
        if ( approvals.value().empty() )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        const auto &core          = approvals.value().front().core;
        auto        candidate     = QuorumPolicyState::DecodeCanonical( core.payload );
        auto        expected_core = candidate ? PolicyCandidateCore( *candidate, policy_domain_ ) : std::nullopt;
        if ( candidate_id.domain != policy_domain_ || !candidate || !expected_core || !( *expected_core == core ) )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        // Already the durable policy (admin/refresh activation race) — idempotent.
        const auto current_policy_hash = current.value().policy.Hash();
        const auto candidate_hash      = candidate->Hash();
        if ( current_policy_hash && candidate_hash && *current_policy_hash == *candidate_hash &&
             current.value().policy.version == candidate->version )
        {
            return false;
        }
        if ( !ValidatePolicySuccessor( current.value().policy, *candidate ) )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        multisig::CollectedSignatures proof;
        for ( const auto &approval : approvals.value() )
        {
            proof.emplace_back( approval.signer, approval.signature );
        }
        if ( proof.size() < current.value().policy.membership_threshold )
        {
            return false;
        }
        const auto authorization_bytes = core.CanonicalBytes();
        if ( !authorization_bytes )
        {
            return outcome::failure( Error::INVALID_CANDIDATE );
        }
        auto committed = trust_store_->CommitPolicySuccessor( *candidate, proof, *authorization_bytes );
        if ( committed.has_error() )
        {
            return committed.error();
        }
        PublishSnapshot( committed.value() );
        return true;
    }

    outcome::result<ConfirmedTrustSnapshot> TrustedPeerRegistry::GetConfirmedSnapshot() const
    {
        if ( !production_mode_ || !trust_store_ )
        {
            return outcome::failure( Error::NOT_CONFIRMED );
        }
        auto loaded = trust_store_->LoadAndVerify();
        if ( loaded.has_error() && loaded.error() == TrustStateStore::Error::NOT_FOUND )
        {
            return outcome::failure( Error::NOT_CONFIRMED );
        }
        return loaded;
    }

    void TrustedPeerRegistry::PublishSnapshot( const ConfirmedTrustSnapshot &snapshot )
    {
        std::unique_lock<std::shared_mutex> lock( cache_mutex_ );
        cached_peers_      = snapshot.policy.peers;
        quorum_threshold_  = snapshot.policy.membership_threshold;
        genesis_confirmed_ = true;
    }

    bool TrustedPeerRegistry::RegisterSignerSetSource()
    {
        sgns::securecrdt::SecureCrdtRegistryEntry entry;
        entry.signer_set_source = [weak_self = weak_from_this()](
                                      const std::string & ) -> outcome::result<sgns::securecrdt::SignerSetSnapshot>
        {
            auto self = weak_self.lock();
            if ( !self )
            {
                return sgns::securecrdt::SignerSetSnapshot{};
            }
            return self->ResolveSignerSet();
        };
        entry.make_instance = []() -> std::shared_ptr<sgns::securecrdt::ISignedCRDTData>
        { return std::make_shared<TrustedPeerListPayload>(); };
        entry.owner_token = &registry_token_;

        return secure_crdt_->Registry().Register( base_key_.GetKey(), std::move( entry ) );
    }

    outcome::result<sgns::securecrdt::SignerSetSnapshot> TrustedPeerRegistry::ResolveSignerSet() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        if ( !genesis_confirmed_ )
        {
            return sgns::securecrdt::SignerSetSnapshot{ { bootstrapper_address_ }, 1 };
        }
        return sgns::securecrdt::SignerSetSnapshot{ cached_peers_, quorum_threshold_ };
    }

    outcome::result<void> TrustedPeerRegistry::SeedGenesis( const std::vector<std::string> &genesis_peers,
                                                            const std::vector<uint8_t>     &ephemeral_signature )
    {
        logger_->info( "{}: seeding genesis trusted-peer list ({} peers)", __func__, genesis_peers.size() );

        TrustedPeerListPayload payload( genesis_peers );
        auto                   propose_result = secure_crdt_->ProposeValue( base_key_, payload.SerializeToBytes() );
        if ( propose_result.has_error() )
        {
            logger_->error( "{}: ProposeValue failed", __func__ );
            return propose_result.error();
        }

        auto sign_result = secure_crdt_->AddSignature(
            base_key_,
            bootstrapper_address_,
            std::vector<uint8_t>( ephemeral_signature.begin(), ephemeral_signature.end() ) );
        if ( sign_result.has_error() )
        {
            logger_->error( "{}: AddSignature failed", __func__ );
            return sign_result.error();
        }

        return outcome::success();
    }

    outcome::result<void> TrustedPeerRegistry::ProposeMembershipChange( const std::vector<std::string> &new_peers )
    {
        logger_->info( "{}: proposing membership change ({} peers)", __func__, new_peers.size() );
        TrustedPeerListPayload payload( new_peers );
        return secure_crdt_->ProposeValue( base_key_, payload.SerializeToBytes() );
    }

    outcome::result<void> TrustedPeerRegistry::SignMembershipChange( const std::string          &signer_address,
                                                                     const std::vector<uint8_t> &signature )
    {
        logger_->info( "{}: signing membership change (signer={})", __func__, signer_address );
        return secure_crdt_->AddSignature( base_key_,
                                           signer_address,
                                           std::vector<uint8_t>( signature.begin(), signature.end() ) );
    }

    outcome::result<bool> TrustedPeerRegistry::TryConfirm()
    {
        auto read_result = secure_crdt_->ReadIfQuorum( base_key_ );
        if ( read_result.has_error() )
        {
            return read_result.error();
        }

        if ( !read_result.value().has_value() )
        {
            return outcome::success( false );
        }

        const auto bytes   = read_result.value()->toVector();
        auto       payload = TrustedPeerListPayload::FromBytes( bytes );
        if ( !payload )
        {
            logger_->error( "{}: confirmed value failed to deserialize", __func__ );
            return outcome::failure( std::errc::bad_message );
        }
        if ( !payload->Verify( bytes ) )
        {
            logger_->error( "{}: confirmed value failed structural verification", __func__ );
            return outcome::failure( std::errc::bad_message );
        }
        payload->Apply();

        {
            std::unique_lock<std::shared_mutex> lock( cache_mutex_ );
            cached_peers_      = payload->GetPeers();
            genesis_confirmed_ = true;
        }

        logger_->info( "{}: confirmed trusted-peer set ({} peers)", __func__, payload->GetPeers().size() );
        return outcome::success( true );
    }

    std::vector<std::string> TrustedPeerRegistry::GetCurrentPeers() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        return cached_peers_;
    }

    bool TrustedPeerRegistry::IsGenesisConfirmed() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        return genesis_confirmed_;
    }

    void TrustedPeerRegistry::Unregister()
    {
        secure_crdt_->Registry().UnregisterIf( base_key_.GetKey(), &registry_token_ );
        if ( production_mode_ )
        {
            secure_crdt_->Registry().UnregisterCandidateDomainIf( genesis_domain_, &registry_token_ );
            secure_crdt_->Registry().UnregisterCandidateDomainIf( policy_domain_, &registry_token_ );
        }
    }
} // namespace sgns::trustedpeer
