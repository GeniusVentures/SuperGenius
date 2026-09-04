/**
 * @file       validator_registry_batch_slot_test.cpp
 * @brief      Slot-authoritative registry batch subject contract tests.
 * @details    Successor of the deleted validator_registry_certificate_lookup_test:
 *             registry batch subjects now carry the canonical certificate slot of
 *             every member, so pending members, the batch root, and member
 *             lookups are observable through public APIs without friend accessors.
 * @date       2026-09-04
 */

#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace sgns
{
    class ValidatorRegistryBatchSlotTest : public ::test::CRDTFixture
    {
    public:
        ValidatorRegistryBatchSlotTest() : ::test::CRDTFixture( "validator_registry_batch_slot_test" )
        {
        }

    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            // slot_key_handlers_ is process-global: registration must pair with the
            // TearDown unregister so other suites never observe this handler.
            ConsensusManager::RegisterSlotKeyHandler(
                NONCE_SUBJECT_TYPE,
                []( const ConsensusManager::Subject &subject )
                {
                    const auto nonce = ConsensusManager::DecodeNonceSubject( subject );
                    if ( nonce.has_error() || nonce.value().tx_hash().empty() )
                    {
                        return std::string{};
                    }
                    return "canonical-" + nonce.value().tx_hash();
                } );
        }

        void TearDown() override
        {
            ConsensusManager::UnregisterSlotKeyHandler( NONCE_SUBJECT_TYPE );
        }

        static std::string SlotFor( const std::string &tx_hash )
        {
            return "canonical-" + tx_hash;
        }

        std::shared_ptr<GeniusAccount> MakeAccount()
        {
            auto account = GeniusAccount::NewFromPrivateKey(
                TokenID::FromBytes( { 0x00 } ), kPrivateKey, getPathString(), false );
            EXPECT_TRUE( account );
            return account;
        }

        std::shared_ptr<ValidatorRegistry> MakeRegistry( const std::shared_ptr<GeniusAccount> &account )
        {
            auto registry = ValidatorRegistry::New(
                db_,
                1,
                1,
                ValidatorRegistry::WeightConfig{},
                account->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> callback )
                { callback( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            if ( !registry )
            {
                return nullptr;
            }

            auto stored = registry->StoreGenesisRegistry(
                account->GetAddress(), [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
            EXPECT_TRUE( stored.has_value() );
            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto current = registry->LoadCurrentRegistry();
                    return current.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "registry initialized",
                nullptr );
            return registry;
        }

        std::shared_ptr<ConsensusManager> MakeManager( const std::shared_ptr<ValidatorRegistry> &registry,
                                                       const std::shared_ptr<GeniusAccount>     &account )
        {
            auto manager = ConsensusManager::New(
                registry,
                db_,
                pubs_,
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); },
                account->GetAddress() );
            EXPECT_TRUE( manager );
            return manager;
        }

        outcome::result<ConsensusCertificate> MakeMemberCertificate(
            const std::shared_ptr<ConsensusManager>  &manager,
            const std::shared_ptr<ValidatorRegistry> &registry,
            const std::shared_ptr<GeniusAccount>     &account,
            const std::string                        &tx_hash,
            uint64_t                                  nonce )
        {
            auto subject = ConsensusManager::CreateNonceSubject(
                account->GetAddress(), nonce, tx_hash, EmbeddedTransaction{}, std::nullopt, std::nullopt );
            if ( subject.has_error() )
            {
                return outcome::failure( subject.error() );
            }
            auto proposal = manager->CreateProposal(
                subject.value(), account->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
            if ( proposal.has_error() )
            {
                return outcome::failure( proposal.error() );
            }
            auto vote = manager->CreateVote(
                proposal.value().proposal_id(),
                account->GetAddress(),
                true,
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
            if ( vote.has_error() )
            {
                return outcome::failure( vote.error() );
            }
            return manager->CreateCertificate( proposal.value(), { vote.value() } );
        }

        outcome::result<ConsensusManager::Certificate> MakeBatchCertificate(
            const std::shared_ptr<ConsensusManager>      &manager,
            const std::shared_ptr<ValidatorRegistry>     &registry,
            const std::shared_ptr<GeniusAccount>         &account,
            const ConsensusManager::Subject              &batch_subject )
        {
            auto proposal = manager->CreateProposal(
                batch_subject, account->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
            if ( proposal.has_error() )
            {
                return outcome::failure( proposal.error() );
            }
            auto vote = manager->CreateVote(
                proposal.value().proposal_id(),
                account->GetAddress(),
                true,
                [account]( std::vector<uint8_t> payload ) { return account->Sign( std::move( payload ) ); } );
            if ( vote.has_error() )
            {
                return outcome::failure( vote.error() );
            }
            return manager->CreateCertificate( proposal.value(), { vote.value() } );
        }

        static std::string BatchSubjectHash( const ConsensusManager::Subject &subject )
        {
            auto payload = ConsensusManager::DecodeRegistryBatchSubject( subject );
            if ( payload.has_error() )
            {
                return {};
            }
            return std::string( payload.value().batch_root() );
        }

        void CaptureBatchSubjects( const std::shared_ptr<ValidatorRegistry> &registry )
        {
            registry->SetBatchSubjectSubmitter(
                [this]( const ConsensusSubject &subject ) -> outcome::result<void>
                {
                    submitted_subjects_.push_back( subject );
                    return outcome::success();
                } );
        }

        void WriteCertificateAtKey( const std::string &key, const ConsensusCertificate &certificate )
        {
            std::string serialized;
            ASSERT_TRUE( certificate.SerializeToString( &serialized ) );
            crdt::GlobalDB::Buffer value;
            value.put( serialized );
            ASSERT_TRUE( db_->Put( { key }, value, {} ).has_value() );
        }

        void WriteRawAtKey( const std::string &key, const std::string &bytes )
        {
            crdt::GlobalDB::Buffer value;
            value.put( bytes );
            ASSERT_TRUE( db_->Put( { key }, value, {} ).has_value() );
        }

        std::vector<ConsensusSubject> submitted_subjects_;

    private:
        static constexpr const char *kPrivateKey =
            "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    };
} // namespace sgns

using namespace sgns;

namespace
{
    TEST( ValidatorRegistryBatchSlotContractTest, CreateRegistryBatchSubjectRejectsEmptyAndMismatchedSlotLists )
    {
        /**
         * Given registry batches must carry one canonical slot per member,
         * When a batch subject is created with an empty, undersized, or
         * empty-string slot list,
         * Then construction fails closed instead of producing a hash-only subject.
         */
        const std::string cid = "registry-cid";

        auto empty = ConsensusManager::CreateRegistryBatchSubject( "account", cid, 0, 1, 2, "00", {} );
        EXPECT_TRUE( empty.has_error() );

        auto undersized = ConsensusManager::CreateRegistryBatchSubject( "account", cid, 0, 1, 2, "00", { "slot-a" } );
        EXPECT_TRUE( undersized.has_error() );

        auto with_empty_slot = ConsensusManager::CreateRegistryBatchSubject( "account", cid, 0, 1, 2, "00", { "slot-a", "" } );
        EXPECT_TRUE( with_empty_slot.has_error() );

        const std::vector<std::string> slots = { "slot-b", "slot-a" };
        auto root = ConsensusManager::ComputeBatchRoot( slots );
        ASSERT_TRUE( root.has_value() );

        auto subject = ConsensusManager::CreateRegistryBatchSubject( "account", cid, 0, 1, 2, root.value(), slots );
        ASSERT_TRUE( subject.has_value() );
        auto payload = ConsensusManager::DecodeRegistryBatchSubject( subject.value() );
        ASSERT_TRUE( payload.has_value() );
        EXPECT_EQ( payload.value().member_certificate_slots_size(), 2 );
        EXPECT_EQ( payload.value().member_certificate_slots( 0 ), "slot-b" );
        EXPECT_EQ( payload.value().member_certificate_slots( 1 ), "slot-a" );
        EXPECT_EQ( std::string( payload.value().batch_root() ), root.value() );
    }

    TEST_F( ValidatorRegistryBatchSlotTest, BatchSubjectCarriesSortedCanonicalMemberSlotsWithMatchingRoot )
    {
        /**
         * Given finalized certificates are authoritative only at their canonical
         * slot, When enough members finalize against one base registry, Then the
         * submitted batch subject carries their sorted canonical slots and its
         * batch root is the deterministic root over those slots.
         */
        auto account  = MakeAccount();
        auto registry = MakeRegistry( account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, account );
        ASSERT_TRUE( manager );

        registry->SetCertificatesPerBatch( 2 );
        CaptureBatchSubjects( registry );

        const std::string tx_hash_b = "0xbatch-member-b";
        const std::string tx_hash_a = "0xbatch-member-a";
        auto member_b = MakeMemberCertificate( manager, registry, account, tx_hash_b, 200 );
        auto member_a = MakeMemberCertificate( manager, registry, account, tx_hash_a, 201 );
        ASSERT_TRUE( member_b.has_value() );
        ASSERT_TRUE( member_a.has_value() );

        auto first_finalize = registry->OnFinalizedCertificate( member_b.value() );
        ASSERT_TRUE( first_finalize.has_error() );
        EXPECT_EQ( first_finalize.error(), std::errc::resource_unavailable_try_again );
        EXPECT_TRUE( submitted_subjects_.empty() );

        ASSERT_TRUE( registry->OnFinalizedCertificate( member_a.value() ).has_value() );
        ASSERT_EQ( submitted_subjects_.size(), 1U );

        auto payload = ConsensusManager::DecodeRegistryBatchSubject( submitted_subjects_.front() );
        ASSERT_TRUE( payload.has_value() );
        EXPECT_EQ( payload.value().certificate_count(), 2U );
        ASSERT_EQ( payload.value().member_certificate_slots_size(), 2 );
        EXPECT_EQ( payload.value().member_certificate_slots( 0 ), ValidatorRegistryBatchSlotTest::SlotFor( tx_hash_a ) );
        EXPECT_EQ( payload.value().member_certificate_slots( 1 ), ValidatorRegistryBatchSlotTest::SlotFor( tx_hash_b ) );

        auto root = ConsensusManager::ComputeBatchRoot(
            { ValidatorRegistryBatchSlotTest::SlotFor( tx_hash_a ),
              ValidatorRegistryBatchSlotTest::SlotFor( tx_hash_b ) } );
        ASSERT_TRUE( root.has_value() );
        EXPECT_EQ( std::string( payload.value().batch_root() ), root.value() );

        // The batch identity is slot-authoritative: no member is identified by its
        // legacy subject-hash (/cert/<tx_hash>) certificate key.
        EXPECT_NE( payload.value().member_certificate_slots( 0 ), tx_hash_a );

        manager->Close();
    }

    TEST_F( ValidatorRegistryBatchSlotTest, EvaluateBatchSubjectProgressesPendingToApproveAndRejectsTamperedRoot )
    {
        /**
         * Given a slot-carrying batch subject, When the pending member set is
         * incomplete the subject stays Pending, once complete it Approves, and a
         * subject whose root points at different members is Rejected.
         */
        auto account  = MakeAccount();
        auto registry = MakeRegistry( account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, account );
        ASSERT_TRUE( manager );

        registry->SetCertificatesPerBatch( 2 );
        CaptureBatchSubjects( registry );

        const std::string tx_hash_a = "0xprogress-member-a";
        const std::string tx_hash_b = "0xprogress-member-b";
        auto member_a = MakeMemberCertificate( manager, registry, account, tx_hash_a, 210 );
        auto member_b = MakeMemberCertificate( manager, registry, account, tx_hash_b, 211 );
        ASSERT_TRUE( member_a.has_value() );
        ASSERT_TRUE( member_b.has_value() );

        const std::vector<std::string> slots = { SlotFor( tx_hash_a ), SlotFor( tx_hash_b ) };
        auto root = ConsensusManager::ComputeBatchRoot( slots );
        ASSERT_TRUE( root.has_value() );
        auto subject = ConsensusManager::CreateRegistryBatchSubject( account->GetAddress(),
                                                                     registry->GetRegistryCid(),
                                                                     0,
                                                                     1,
                                                                     2,
                                                                     root.value(),
                                                                     slots );
        ASSERT_TRUE( subject.has_value() );

        auto empty_pending = registry->EvaluateBatchSubject( subject.value() );
        ASSERT_TRUE( empty_pending.has_value() );
        EXPECT_EQ( empty_pending.value(), ValidatorRegistry::BatchSubjectDecision::Pending );

        auto first_finalize = registry->OnFinalizedCertificate( member_a.value() );
        ASSERT_TRUE( first_finalize.has_error() );
        EXPECT_EQ( first_finalize.error(), std::errc::resource_unavailable_try_again );

        auto partial = registry->EvaluateBatchSubject( subject.value() );
        ASSERT_TRUE( partial.has_value() );
        EXPECT_EQ( partial.value(), ValidatorRegistry::BatchSubjectDecision::Pending );

        ASSERT_TRUE( registry->OnFinalizedCertificate( member_b.value() ).has_value() );
        ASSERT_EQ( submitted_subjects_.size(), 1U );

        auto complete = registry->EvaluateBatchSubject( subject.value() );
        ASSERT_TRUE( complete.has_value() );
        EXPECT_EQ( complete.value(), ValidatorRegistry::BatchSubjectDecision::Approve );

        const std::vector<std::string> tampered_slots = { SlotFor( tx_hash_a ), SlotFor( "0xtampered-member" ) };
        auto tampered_root = ConsensusManager::ComputeBatchRoot( tampered_slots );
        ASSERT_TRUE( tampered_root.has_value() );
        auto tampered = ConsensusManager::CreateRegistryBatchSubject( account->GetAddress(),
                                                                      registry->GetRegistryCid(),
                                                                      0,
                                                                      1,
                                                                      2,
                                                                      tampered_root.value(),
                                                                      tampered_slots );
        ASSERT_TRUE( tampered.has_value() );
        auto tampered_decision = registry->EvaluateBatchSubject( tampered.value() );
        ASSERT_TRUE( tampered_decision.has_value() );
        EXPECT_EQ( tampered_decision.value(), ValidatorRegistry::BatchSubjectDecision::Reject );

        manager->Close();
    }

    TEST_F( ValidatorRegistryBatchSlotTest, HandleBatchCertificateStallsWhileMemberSlotRecordIsAbsent )
    {
        /**
         * Given a batch certificate whose member has no durable /cert/<slot>
         * record, When the member is missing or only a legacy /cert/<subject-hash>
         * record exists, Then handling stays retryable (Stalled) and no registry
         * update is applied.
         */
        auto account  = MakeAccount();
        auto registry = MakeRegistry( account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, account );
        ASSERT_TRUE( manager );

        registry->SetCertificatesPerBatch( 1 );
        CaptureBatchSubjects( registry );

        const std::string tx_hash = "0xhandle-missing-a";
        auto member = MakeMemberCertificate( manager, registry, account, tx_hash, 300 );
        ASSERT_TRUE( member.has_value() );
        ASSERT_TRUE( registry->OnFinalizedCertificate( member.value() ).has_value() );
        ASSERT_EQ( submitted_subjects_.size(), 1U );

        auto batch_certificate = MakeBatchCertificate( manager, registry, account, submitted_subjects_.front() );
        ASSERT_TRUE( batch_certificate.has_value() );
        const auto subject_hash = BatchSubjectHash( submitted_subjects_.front() );
        ASSERT_FALSE( subject_hash.empty() );

        auto missing = registry->HandleBatchCertificate( subject_hash, batch_certificate.value() );
        ASSERT_TRUE( missing.has_value() );
        EXPECT_EQ( missing.value(), ValidatorRegistry::BatchCertificateDecision::Stalled );
        EXPECT_EQ( registry->GetRegistryEpoch(), 0U );

        // A legacy subject-hash (/cert/<tx_hash>) record never becomes authority.
        WriteCertificateAtKey( "/cert/" + tx_hash, member.value() );
        auto legacy = registry->HandleBatchCertificate( subject_hash, batch_certificate.value() );
        ASSERT_TRUE( legacy.has_value() );
        EXPECT_EQ( legacy.value(), ValidatorRegistry::BatchCertificateDecision::Stalled );
        EXPECT_EQ( registry->GetRegistryEpoch(), 0U );

        manager->Close();
    }

    TEST_F( ValidatorRegistryBatchSlotTest, HandleBatchCertificateRejectsCorruptMemberSlotRecord )
    {
        /**
         * Given the durable /cert/<slot> record does not parse as a certificate,
         * When the batch is handled, Then it is permanently Rejected.
         */
        auto account  = MakeAccount();
        auto registry = MakeRegistry( account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, account );
        ASSERT_TRUE( manager );

        registry->SetCertificatesPerBatch( 1 );
        CaptureBatchSubjects( registry );

        const std::string tx_hash = "0xhandle-corrupt-a";
        auto member = MakeMemberCertificate( manager, registry, account, tx_hash, 301 );
        ASSERT_TRUE( member.has_value() );
        ASSERT_TRUE( registry->OnFinalizedCertificate( member.value() ).has_value() );
        ASSERT_EQ( submitted_subjects_.size(), 1U );

        auto batch_certificate = MakeBatchCertificate( manager, registry, account, submitted_subjects_.front() );
        ASSERT_TRUE( batch_certificate.has_value() );

        WriteRawAtKey( "/cert/" + SlotFor( tx_hash ), "not-a-certificate" );

        auto decision = registry->HandleBatchCertificate( BatchSubjectHash( submitted_subjects_.front() ),
                                                          batch_certificate.value() );
        ASSERT_TRUE( decision.has_value() );
        EXPECT_EQ( decision.value(), ValidatorRegistry::BatchCertificateDecision::Reject );
        EXPECT_EQ( registry->GetRegistryEpoch(), 0U );

        manager->Close();
    }

    TEST_F( ValidatorRegistryBatchSlotTest, HandleBatchCertificateRejectsMismatchedMemberSlotRecord )
    {
        /**
         * Given the durable /cert/<slot> record holds a valid certificate bound to
         * a different canonical slot, When the batch is handled, Then it is
         * permanently Rejected.
         */
        auto account  = MakeAccount();
        auto registry = MakeRegistry( account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, account );
        ASSERT_TRUE( manager );

        registry->SetCertificatesPerBatch( 1 );
        CaptureBatchSubjects( registry );

        const std::string tx_hash_a = "0xhandle-mismatch-a";
        const std::string tx_hash_b = "0xhandle-mismatch-b";
        auto member_a = MakeMemberCertificate( manager, registry, account, tx_hash_a, 302 );
        auto member_b = MakeMemberCertificate( manager, registry, account, tx_hash_b, 303 );
        ASSERT_TRUE( member_a.has_value() );
        ASSERT_TRUE( member_b.has_value() );
        // Only member_a finalizes, so the batch references SlotFor( tx_hash_a ).
        ASSERT_TRUE( registry->OnFinalizedCertificate( member_a.value() ).has_value() );
        ASSERT_EQ( submitted_subjects_.size(), 1U );
        auto payload = ConsensusManager::DecodeRegistryBatchSubject( submitted_subjects_.front() );
        ASSERT_TRUE( payload.has_value() );
        ASSERT_EQ( payload.value().member_certificate_slots_size(), 1 );
        EXPECT_EQ( payload.value().member_certificate_slots( 0 ), SlotFor( tx_hash_a ) );

        auto batch_certificate = MakeBatchCertificate( manager, registry, account, submitted_subjects_.front() );
        ASSERT_TRUE( batch_certificate.has_value() );

        // A valid certificate for another slot parked under this slot's key.
        WriteCertificateAtKey( "/cert/" + SlotFor( tx_hash_a ), member_b.value() );

        auto decision = registry->HandleBatchCertificate( BatchSubjectHash( submitted_subjects_.front() ),
                                                          batch_certificate.value() );
        ASSERT_TRUE( decision.has_value() );
        EXPECT_EQ( decision.value(), ValidatorRegistry::BatchCertificateDecision::Reject );
        EXPECT_EQ( registry->GetRegistryEpoch(), 0U );

        manager->Close();
    }

    TEST_F( ValidatorRegistryBatchSlotTest, HandleBatchCertificateAppliesUpdateFromCanonicalSlotMembers )
    {
        /**
         * Given the member certificate is durably present under its canonical
         * slot, When the batch certificate is handled, Then the recomputed
         * registry update is applied and carries the authoritative member slots
         * (with legacy subject hashes as informational metadata only).
         */
        auto account  = MakeAccount();
        auto registry = MakeRegistry( account );
        ASSERT_TRUE( registry );
        auto manager = MakeManager( registry, account );
        ASSERT_TRUE( manager );

        registry->SetCertificatesPerBatch( 1 );
        CaptureBatchSubjects( registry );

        const std::string tx_hash = "0xhandle-apply-a";
        auto member = MakeMemberCertificate( manager, registry, account, tx_hash, 304 );
        ASSERT_TRUE( member.has_value() );
        ASSERT_TRUE( registry->OnFinalizedCertificate( member.value() ).has_value() );
        ASSERT_EQ( submitted_subjects_.size(), 1U );

        auto batch_certificate = MakeBatchCertificate( manager, registry, account, submitted_subjects_.front() );
        ASSERT_TRUE( batch_certificate.has_value() );

        WriteCertificateAtKey( "/cert/" + SlotFor( tx_hash ), member.value() );

        auto decision = registry->HandleBatchCertificate( BatchSubjectHash( submitted_subjects_.front() ),
                                                          batch_certificate.value() );
        ASSERT_TRUE( decision.has_value() );
        EXPECT_EQ( decision.value(), ValidatorRegistry::BatchCertificateDecision::Approve );

        ASSERT_WAIT_FOR_CONDITION(
            [&registry]() { return registry->GetRegistryEpoch() == 1; },
            std::chrono::milliseconds( 5000 ),
            "batch registry update applied",
            nullptr );

        auto update = registry->LoadRegistryUpdate();
        ASSERT_FALSE( update.has_error() );
        EXPECT_EQ( update.value().registry().epoch(), 1U );
        ASSERT_EQ( update.value().batch_certificate_slots_size(), 1 );
        EXPECT_EQ( update.value().batch_certificate_slots( 0 ), SlotFor( tx_hash ) );
        ASSERT_EQ( update.value().batch_certificate_subject_hashes_size(), 1 );
        EXPECT_EQ( update.value().batch_certificate_subject_hashes( 0 ), tx_hash );

        manager->Close();
    }
} // namespace
