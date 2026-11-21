/**
 * @file       validator_admission_equivocation_test.cpp
 * @brief      TRUST-02 enforcement proofs: D-04 ACTIVE-addition cap and the
 *             cross-certificate equivocation guard.
 * @details    Adversarial + compat pairs (VER-01 discipline) for the two cheap
 *             guards the 2026-10 admission/equivocation audit shipped:
 *             (1) a certified registry update inserting more than the
 *             configured cap of new ACTIVE validators stops at the cap, and
 *             the config-wired value (max_new_validators_per_update, 21-03)
 *             overrides the default 10; (2) a signer provably committed to two
 *             conflicting certificates at one canonical slot gets the
 *             conflicting certificate rejected and a durable equivocation
 *             marker recorded, while disjoint-signer conflicts keep today's
 *             lower-hash ordering exactly.
 * @date       2026-10-21
 * @author     Henrique A Klein (hklein@gnus.ai)
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "account/GeniusAccount.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/TokenID.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"
#include "crdt/crdt_data_filter.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    /**
     * @brief Friend accessor for the private admission/equivocation surfaces.
     * @details Declared as a friend in Consensus.hpp (FilterCertificate,
     *          EquivocationRecordStorageKey) and ValidatorRegistry.hpp
     *          (InsertNewValidators).
     */
    class ValidatorAdmissionEquivocationTestAccess
    {
    public:
        static void InsertNewValidators( ValidatorRegistry                           &registry,
                                         ValidatorRegistry::Registry                 &target,
                                         const std::unordered_map<std::string, bool> &unregistered_votes )
        {
            registry.InsertNewValidators( target, unregistered_votes );
        }

        static crdt::CRDTDataFilter::ElementFilterResult FilterCertificate(
            const std::shared_ptr<ConsensusManager> &manager,
            const crdt::pb::Element                 &element )
        {
            return manager->FilterCertificate( element );
        }

        static std::string EquivocationRecordStorageKey( std::string_view slot_key, std::string_view voter_id )
        {
            return ConsensusManager::EquivocationRecordStorageKey( slot_key, voter_id );
        }
    };
} // namespace sgns

namespace
{
    using namespace sgns;

    using FilterDecision = crdt::CRDTDataFilter::ElementFilterResult::Decision;

    /// @brief Test token identifier (same value the admission and bridge suites use).
    const TokenID kTestTokenId = TokenID::FromBytes( { 0x00 } );

    /// @brief Source chain id of the fixture mints (a real public chain id).
    constexpr const char *kSourceChainId = "11155111";

    /// @brief Deterministic private keys so both validators are stable per run.
    constexpr const char *kValidatorAPrivateKey = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    constexpr const char *kValidatorBPrivateKey = "1badc0de1badc0de1badc0de1badc0de1badc0de1badc0de1badc0de1badc0de";

    constexpr std::string_view kCertificatePathPrefix = "/cert/";

    std::string SerializedCertificateHash( std::string_view serialized )
    {
        const auto hash = crypto::sha2_256( serialized.data(), serialized.size() );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    int64_t NowMs()
    {
        return static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    /**
     * @brief Single-node harness over CRDTFixture: a real GlobalDB, a genesis
     *        registry signed by validator A (authority) containing validators A
     *        and B (each weight 50000, quorum 1/2 so a single-signer
     *        certificate reaches quorum), and a ConsensusManager. The
     *        nonce-subject slot-key handler is the fixture's own mapping: same
     *        account + same nonce = one canonical slot, while the transaction
     *        hash and embedded mint may differ — exactly the contention shape
     *        the conflicting-certificate branch arbitrates.
     */
    class ValidatorAdmissionEquivocationTest : public ::test::CRDTFixture
    {
    public:
        ValidatorAdmissionEquivocationTest() : ::test::CRDTFixture( "validator_admission_equivocation_test" )
        {
        }

        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );

            RegisterSlotKeyHandlerOnce();

            validator_a_ = MakeAccount( kValidatorAPrivateKey, "validator-a" );
            validator_b_ = MakeAccount( kValidatorBPrivateKey, "validator-b" );
            ASSERT_TRUE( validator_a_ );
            ASSERT_TRUE( validator_b_ );

            registry_ = ValidatorRegistry::New(
                db_,
                1, // quorum numerator: a single validator's 50000 weight
                2, // quorum denominator: reaches the 100000 total threshold
                ValidatorRegistry::WeightConfig{},
                validator_a_->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
                { cb( outcome::failure( std::errc::not_supported ) ); } );
            ASSERT_TRUE( registry_ );

            // Genesis registry: validator A signs (it is the genesis authority),
            // both A and B enter as ACTIVE GENESIS validators of weight 50000.
            auto store_result = registry_->StoreGenesisRegistry(
                { validator_a_->GetAddress(), validator_b_->GetAddress() },
                [this]( std::vector<uint8_t> payload ) { return validator_a_->Sign( std::move( payload ) ); } );
            ASSERT_FALSE( store_result.has_error() );

            std::chrono::milliseconds waited( 0 );
            ASSERT_WAIT_FOR_CONDITION(
                [this]()
                {
                    auto load = registry_->LoadCurrentRegistry();
                    return load.has_value() && !registry_->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "genesis registry stored",
                &waited );
            genesis_registry_cid_ = registry_->GetRegistryCid();
            ASSERT_FALSE( genesis_registry_cid_.empty() );

            manager_ = ConsensusManager::New(
                registry_,
                db_,
                pubs_,
                [this]( std::vector<uint8_t> payload ) { return validator_a_->Sign( std::move( payload ) ); },
                validator_a_->GetAddress() );
            ASSERT_TRUE( manager_ );
        }

        void TearDown() override
        {
            // Join the ConsensusManager round-timer thread before the base
            // fixture releases the pubsub host/io_context underneath it (same
            // teardown invariant as consensus_pending_lifecycle_test.cpp).
            if ( manager_ )
            {
                manager_->Close();
            }
            manager_.reset();
            registry_.reset();
            validator_b_.reset();
            validator_a_.reset();
        }

        /// @brief Builds a signed mint-v2 whose hash is unique per @p amount.
        std::shared_ptr<MintTransactionV2> MakeMintV2( uint64_t amount )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "mint-v2" );
            dag.set_source_addr( validator_a_->GetAddress() );
            dag.set_nonce( 0 );
            dag.set_timestamp( NowMs() );
            const auto burn_hash = base::Hash256::fromReadableString( std::string( 64, 'a' ) );
            EXPECT_TRUE( burn_hash.has_value() );
            auto mint = std::make_shared<MintTransactionV2>( MintTransactionV2::New( amount,
                                                                                     kSourceChainId,
                                                                                     kTestTokenId,
                                                                                     std::move( dag ),
                                                                                     { { burn_hash.value(), 0, {} } },
                                                                                     validator_a_->GetAddress() ) );
            EXPECT_TRUE( mint != nullptr );
            mint->MakeSignature( *validator_a_ );
            return mint;
        }

        /**
         * @brief Builds a fully signed certificate whose nonce subject embeds
         *        @p mint and carries @p tx_hash, signed by @p signers.
         *
         * Mirrors consensus_pending_lifecycle_test.cpp
         * MakeCertificateForRegistry (the proposal id replicates the private
         * CreateProposalId derivation: ProposalSigningBytes + sha2_256 +
         * hex_lower of the id-cleared copy).
         */
        ConsensusManager::Certificate MakeCertificateAtSlot( const std::vector<std::shared_ptr<GeniusAccount>> &signers,
                                                             uint64_t                                           nonce,
                                                             const std::string                                 &tx_hash,
                                                             const EmbeddedTransaction &embedded )
        {
            const auto now_ms = NowMs();

            auto subject = ConsensusManager::CreateNonceSubject( validator_a_->GetAddress(),
                                                                 nonce,
                                                                 tx_hash,
                                                                 embedded,
                                                                 std::nullopt,
                                                                 std::nullopt );
            EXPECT_TRUE( subject.has_value() );

            ConsensusManager::Proposal proposal;
            *proposal.mutable_subject() = subject.value();
            proposal.set_proposer_id( validator_a_->GetAddress() );
            proposal.set_registry_cid( genesis_registry_cid_ );
            proposal.set_registry_epoch( 0 );
            proposal.set_timestamp( now_ms );
            ConsensusManager::Proposal id_copy = proposal;
            id_copy.clear_proposal_id();
            auto id_signing = ConsensusManager::ProposalSigningBytes( id_copy );
            EXPECT_TRUE( id_signing.has_value() );
            auto id_hash = crypto::sha2_256( id_signing.value().data(), id_signing.value().size() );
            proposal.set_proposal_id( base::hex_lower( gsl::span<const uint8_t>( id_hash.data(), id_hash.size() ) ) );
            auto proposal_signing = ConsensusManager::ProposalSigningBytes( proposal );
            EXPECT_TRUE( proposal_signing.has_value() );
            auto proposal_signature = validator_a_->Sign( proposal_signing.value() );
            proposal.set_signature( proposal_signature.data(), proposal_signature.size() );

            ConsensusManager::Certificate certificate;
            certificate.set_proposal_id( proposal.proposal_id() );
            certificate.set_registry_cid( genesis_registry_cid_ );
            certificate.set_registry_epoch( 0 );
            certificate.set_total_weight( 100000 );
            certificate.set_approved_weight( 50000 );
            certificate.set_timestamp( now_ms );
            *certificate.mutable_proposal() = proposal;
            for ( const auto &signer : signers )
            {
                ConsensusManager::Vote vote;
                vote.set_proposal_id( proposal.proposal_id() );
                vote.set_voter_id( signer->GetAddress() );
                vote.set_approve( true );
                vote.set_timestamp( now_ms );
                auto vote_signing = ConsensusManager::VoteSigningBytes( vote );
                EXPECT_TRUE( vote_signing.has_value() );
                auto vote_signature = signer->Sign( vote_signing.value() );
                vote.set_signature( vote_signature.data(), vote_signature.size() );
                *certificate.add_votes() = vote;
            }
            return certificate;
        }

        /// @brief Writes the durable canonical /cert/<slot> record exactly the
        ///        way the 21-06 fixture does (the record shape
        ///        SubmitCertificate writes post-Approve).
        void StoreDurableCertificate( const ConsensusManager::Certificate &certificate )
        {
            const auto durable_key = ConsensusManager::GetExpectedCertificateSlotKey( certificate );
            ASSERT_FALSE( durable_key.empty() );
            std::string serialized;
            ASSERT_TRUE( certificate.SerializeToString( &serialized ) );
            crdt::GlobalDB::Buffer value;
            value.put( serialized );
            ASSERT_TRUE( db_->Put( { durable_key }, value, {} ).has_value() );
        }

        /// @brief The CRDT element shape the certificate filter sees on a delta.
        crdt::pb::Element MakeCertificateElement( const ConsensusManager::Certificate &certificate,
                                                  std::string                         *serialized_out = nullptr )
        {
            std::string serialized;
            EXPECT_TRUE( certificate.SerializeToString( &serialized ) );
            if ( serialized_out != nullptr )
            {
                *serialized_out = serialized;
            }
            crdt::pb::Element element;
            element.set_key( ConsensusManager::GetExpectedCertificateSlotKey( certificate ) );
            element.set_value( serialized );
            return element;
        }

        /// @brief Reads the durable equivocation marker for @p voter at @p slot.
        bool HasEquivocationMarker( const std::string &slot_key,
                                    const std::string &voter_id,
                                    ActiveVoteRecord  *marker_out = nullptr )
        {
            auto datastore = db_->GetDataStore();
            EXPECT_TRUE( datastore != nullptr );
            crdt::GlobalDB::Buffer key;
            key.put( ValidatorAdmissionEquivocationTestAccess::EquivocationRecordStorageKey( slot_key, voter_id ) );
            auto value = datastore->get( key );
            if ( value.has_error() )
            {
                return false;
            }
            if ( marker_out != nullptr )
            {
                EXPECT_TRUE( marker_out->ParseFromString( value.value().toString() ) );
            }
            return true;
        }

        /// @brief Canonical slot of a certificate (its durable key minus /cert/).
        static std::string SlotKeyOf( const ConsensusManager::Certificate &certificate )
        {
            const auto durable_key = ConsensusManager::GetExpectedCertificateSlotKey( certificate );
            return durable_key.substr( kCertificatePathPrefix.size() );
        }

    protected:
        std::shared_ptr<GeniusAccount>     validator_a_;
        std::shared_ptr<GeniusAccount>     validator_b_;
        std::shared_ptr<ValidatorRegistry> registry_;
        std::shared_ptr<ConsensusManager>  manager_;
        std::string                        genesis_registry_cid_;

    private:
        std::shared_ptr<GeniusAccount> MakeAccount( const char *private_key, const std::string &name )
        {
            return GeniusAccount::NewFromPrivateKey( kTestTokenId, private_key, getPathString() + "/" + name );
        }

        static void RegisterSlotKeyHandlerOnce()
        {
            static const bool registered = []()
            {
                ConsensusManager::RegisterSlotKeyHandler(
                    NONCE_SUBJECT_TYPE,
                    []( const ConsensusManager::Subject &subject ) -> std::string
                    {
                        auto nonce = ConsensusManager::DecodeNonceSubject( subject );
                        return subject.account_id() + ":" +
                               std::to_string( nonce.has_value() ? nonce.value().nonce() : 0ULL );
                    } );
                return true;
            }();
            (void) registered;
        }
    };

    // -------------------------------------------------------------------------
    // F1: ACTIVE-addition cap enforcement (D-04)
    // -------------------------------------------------------------------------

    // VER-01 adversarial case.
    // Attacker authority: a registry-update author stacking sympathetic
    // validators — a certified update carrying cap+1 new ACTIVE validators.
    // Expected: exactly DefaultMaxNewValidatorsPerUpdate (10) entries inserted,
    // all REGULAR/ACTIVE, deterministically truncated in sorted id order (every
    // peer derives the same truncated set). Actual behavior: the cap loop
    // existed but its enforcement against the config-wired value had never been
    // proven — this pair pins it (21-03 wired the config; this test is the
    // enforcement proof the audit's F1 disposition names).
    TEST_F( ValidatorAdmissionEquivocationTest, OverCapUpdateInsertsExactlyCapValidators )
    {
        std::unordered_map<std::string, bool> unregistered_votes;
        for ( uint64_t i = 0; i < ValidatorRegistry::DefaultMaxNewValidatorsPerUpdate + 1; ++i )
        {
            unregistered_votes.emplace( "stacked-validator-" + std::to_string( 100 + i ), i % 2 == 0 );
        }

        ValidatorRegistry::Registry next;
        next.set_epoch( 1 );
        ValidatorAdmissionEquivocationTestAccess::InsertNewValidators( *registry_, next, unregistered_votes );

        // Expected: exactly the cap lands.
        ASSERT_EQ( next.validators_size(), static_cast<int>( ValidatorRegistry::DefaultMaxNewValidatorsPerUpdate ) );
        for ( const auto &entry : next.validators() )
        {
            EXPECT_EQ( entry.status(), validator::ACTIVE );
            EXPECT_EQ( entry.role(), validator::REGULAR );
        }
        // Deterministic sorted-order truncation: 100..109 in, 110 out.
        EXPECT_TRUE( ValidatorRegistry::FindValidator( next, "stacked-validator-100" ) != nullptr );
        EXPECT_TRUE( ValidatorRegistry::FindValidator( next, "stacked-validator-109" ) != nullptr );
        EXPECT_TRUE( ValidatorRegistry::FindValidator( next, "stacked-validator-110" ) == nullptr );
    }

    // VER-01 adversarial companion.
    // Attacker authority: same update author; the deployment tightened
    // max_new_validators_per_update to 3 in network_config.json (21-03 wiring:
    // GeniusNode applies it via SetMaxNewValidatorsPerUpdate after
    // Blockchain::New). Expected: the wired value overrides the default 10 — a
    // 4-candidate update inserts exactly 3. Actual behavior: the setter value
    // flows into the same cap loop (proven here, not assumed).
    TEST_F( ValidatorAdmissionEquivocationTest, ConfigWiredCapOverridesDefault )
    {
        registry_->SetMaxNewValidatorsPerUpdate( 3 );

        std::unordered_map<std::string, bool> unregistered_votes;
        for ( uint64_t i = 0; i < 4; ++i )
        {
            unregistered_votes.emplace( "wired-cap-validator-" + std::to_string( i ), true );
        }

        ValidatorRegistry::Registry next;
        ValidatorAdmissionEquivocationTestAccess::InsertNewValidators( *registry_, next, unregistered_votes );

        ASSERT_EQ( next.validators_size(), 3 );
        EXPECT_TRUE( ValidatorRegistry::FindValidator( next, "wired-cap-validator-2" ) != nullptr );
        EXPECT_TRUE( ValidatorRegistry::FindValidator( next, "wired-cap-validator-3" ) == nullptr );
    }

    // VER-01 compat twin (F1).
    // Honest admission flow: a sub-cap certified update (two new validators
    // under the wired cap of three) must insert ALL of its candidates — the
    // cap truncates only over-cap stacking, never honest growth. Expected: both
    // validators enter as REGULAR/ACTIVE with the weight policy's REGULAR
    // weight and the vote-derived penalty bookkeeping (approve -> penalty 0,
    // reject -> penalty 1). Actual behavior: unchanged by the cap proof — this
    // is the F1 guard's no-false-truncation proof.
    TEST_F( ValidatorAdmissionEquivocationTest, SubCapHonestUpdateInsertsAllNewValidators )
    {
        registry_->SetMaxNewValidatorsPerUpdate( 3 );

        std::unordered_map<std::string, bool> unregistered_votes;
        unregistered_votes.emplace( "honest-validator-yes", true );
        unregistered_votes.emplace( "honest-validator-no", false );

        ValidatorRegistry::Registry next;
        ValidatorAdmissionEquivocationTestAccess::InsertNewValidators( *registry_, next, unregistered_votes );

        ASSERT_EQ( next.validators_size(), 2 );
        const auto *yes_entry = ValidatorRegistry::FindValidator( next, "honest-validator-yes" );
        ASSERT_TRUE( yes_entry != nullptr );
        EXPECT_EQ( yes_entry->status(), validator::ACTIVE );
        EXPECT_EQ( yes_entry->role(), validator::REGULAR );
        EXPECT_EQ( yes_entry->penalty_score(), 0 );
        const auto *no_entry = ValidatorRegistry::FindValidator( next, "honest-validator-no" );
        ASSERT_TRUE( no_entry != nullptr );
        EXPECT_EQ( no_entry->status(), validator::ACTIVE );
        EXPECT_EQ( no_entry->role(), validator::REGULAR );
        EXPECT_EQ( no_entry->penalty_score(), 1 );
    }

    // -------------------------------------------------------------------------
    // F2: cross-certificate equivocation guard
    // -------------------------------------------------------------------------

    // VER-01 adversarial case.
    // Attacker authority: a byzantine validator (validator A's key) signing
    // votes for TWO conflicting certificates at one canonical slot — same
    // account+nonce slot, different verified mint-v2 transaction hashes. The
    // earlier certificate is durably stored at /cert/<slot>; the conflicting
    // candidate arrives as a sync delta. Expected: the candidate is REJECTED
    // (not merely hash-ordered) and a durable equivocation marker for the
    // signer exists under the active-vote ledger's storage conventions.
    // Actual pre-fix behavior: critical log, then lower-hash ordering — the
    // equivocator paid nothing and could win its slot by hash.
    TEST_F( ValidatorAdmissionEquivocationTest, EquivocatingSignerConflictingCertificateRejected )
    {
        auto mint_a = MakeMintV2( 42 );
        auto mint_b = MakeMintV2( 43 );
        ASSERT_NE( mint_a->GetHash(), mint_b->GetHash() );

        auto existing = MakeCertificateAtSlot( { validator_a_ },
                                               7,
                                               mint_a->GetHash(),
                                               mint_a->SerializeToEmbeddedTransaction() );
        StoreDurableCertificate( existing );

        auto candidate = MakeCertificateAtSlot( { validator_a_ },
                                                7,
                                                mint_b->GetHash(),
                                                mint_b->SerializeToEmbeddedTransaction() );
        ASSERT_EQ( ConsensusManager::GetExpectedCertificateSlotKey( candidate ),
                   ConsensusManager::GetExpectedCertificateSlotKey( existing ) )
            << "Both certificates must compete for the same canonical slot";

        const auto result = ValidatorAdmissionEquivocationTestAccess::FilterCertificate(
            manager_,
            MakeCertificateElement( candidate ) );

        // Expected: the provable equivocation is rejected outright.
        EXPECT_EQ( result.decision, FilterDecision::kReject );

        // Expected: the durable marker records the signer's PRIOR commitment
        // (its verified vote from the earlier certificate).
        const auto       slot_key = SlotKeyOf( existing );
        ActiveVoteRecord marker;
        EXPECT_TRUE( HasEquivocationMarker( slot_key, validator_a_->GetAddress(), &marker ) );
        EXPECT_EQ( marker.canonical_slot(), slot_key );
        ConsensusManager::Vote marker_vote;
        ASSERT_TRUE( marker_vote.ParseFromString( marker.vote_bytes() ) );
        EXPECT_EQ( marker_vote.voter_id(), validator_a_->GetAddress() );
        EXPECT_EQ( marker_vote.proposal_id(), existing.proposal_id() );
    }

    // VER-01 compat twin (T-21-0705).
    // Honest view-change contention: two conflicting certificates at one slot
    // with DISJOINT signer sets — no signer committed to both, so nothing is
    // provable and the guard must not fire. Expected: today's lower-hash
    // ordering EXACTLY (Reject when the stored certificate's serialized hash is
    // lower than the candidate's, Accept otherwise) and NO equivocation marker
    // for either signer. Actual behavior: unchanged by the guard — this is its
    // no-false-positive proof.
    TEST_F( ValidatorAdmissionEquivocationTest, DisjointSignerConflictKeepsLowerHashOrdering )
    {
        auto mint_a = MakeMintV2( 52 );
        auto mint_b = MakeMintV2( 53 );

        auto existing = MakeCertificateAtSlot( { validator_a_ },
                                               9,
                                               mint_a->GetHash(),
                                               mint_a->SerializeToEmbeddedTransaction() );
        StoreDurableCertificate( existing );

        auto candidate = MakeCertificateAtSlot( { validator_b_ },
                                                9,
                                                mint_b->GetHash(),
                                                mint_b->SerializeToEmbeddedTransaction() );
        ASSERT_EQ( ConsensusManager::GetExpectedCertificateSlotKey( candidate ),
                   ConsensusManager::GetExpectedCertificateSlotKey( existing ) )
            << "Both certificates must compete for the same canonical slot";

        std::string candidate_serialized;
        const auto  result = ValidatorAdmissionEquivocationTestAccess::FilterCertificate(
            manager_,
            MakeCertificateElement( candidate, &candidate_serialized ) );

        // Expected: the production ordering rule decides — mirror it here.
        std::string existing_serialized;
        ASSERT_TRUE( existing.SerializeToString( &existing_serialized ) );
        const bool candidate_hash_is_higher = SerializedCertificateHash( existing_serialized ) <
                                              SerializedCertificateHash( candidate_serialized );
        EXPECT_EQ( result.decision, candidate_hash_is_higher ? FilterDecision::kReject : FilterDecision::kAccept )
            << "Disjoint-signer conflicts must keep today's lower-hash ordering";

        // Expected: no equivocation marker for either honest signer.
        const auto slot_key = SlotKeyOf( existing );
        EXPECT_FALSE( HasEquivocationMarker( slot_key, validator_a_->GetAddress() ) );
        EXPECT_FALSE( HasEquivocationMarker( slot_key, validator_b_->GetAddress() ) );
    }

} // namespace
