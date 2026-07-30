/**
 * @file consensus_finality_race_test.cpp
 * @brief Deterministic external-ingress coverage for the finality/application gap.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    class ConsensusFinalityRaceTestAccess
    {
    public:
        static void SetStageObserver( const std::shared_ptr<ConsensusManager> &manager,
                                      std::function<void( std::string_view )> observer )
        {
            manager->finalization_stage_observer_ = std::move( observer );
        }

        static void HandleCertificate( const std::shared_ptr<ConsensusManager> &manager,
                                       const ConsensusManager::Certificate &certificate )
        {
            manager->HandleCertificate( certificate );
        }

        static void RecoverPendingCertificateWork( const std::shared_ptr<ConsensusManager> &manager )
        {
            manager->RecoverPendingCertificateWork();
        }

        static bool IsFinalizedPendingApplication(
            const std::shared_ptr<ConsensusManager> &manager, const std::string &slot )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            return manager->slot_states_[slot].lifecycle ==
                   ConsensusManager::SlotState::Lifecycle::FinalizedPendingApplication;
        }

        static std::optional<ConsensusStateStore::ProcessRecord> Process(
            const std::shared_ptr<ConsensusManager> &manager, const std::string &slot )
        {
            auto process = manager->state_store_->GetProcess( slot );
            return process && process.value() ? process.value()
                                              : std::optional<ConsensusStateStore::ProcessRecord>{};
        }

        static std::vector<ConsensusStateStore::ConflictRecord> Conflicts(
            const std::shared_ptr<ConsensusManager> &manager )
        {
            auto conflicts = manager->state_store_->ScanConflicts();
            return conflicts ? conflicts.value() : std::vector<ConsensusStateStore::ConflictRecord>{};
        }

        static uint64_t UniqueConflictPairs( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager->certificate_conflict_unique_pairs_.load();
        }

        static outcome::result<std::string> Slot( const ConsensusManager::Certificate &certificate )
        {
            return ConsensusManager::GetSlotKey( certificate.proposal() );
        }
    };
}

namespace
{
    class DeterministicBarrier
    {
    public:
        void ArriveAndWait()
        {
            std::unique_lock lock( mutex_ );
            arrived_ = true;
            cv_.notify_all();
            cv_.wait( lock, [&] { return released_; } );
        }
        void WaitUntilArrived()
        {
            std::unique_lock lock( mutex_ );
            cv_.wait( lock, [&] { return arrived_; } );
        }
        void Release()
        {
            std::lock_guard lock( mutex_ );
            released_ = true;
            cv_.notify_all();
        }
    private:
        std::mutex mutex_;
        std::condition_variable cv_;
        bool arrived_{ false };
        bool released_{ false };
    };

    class ScopedWorker
    {
    public:
        ScopedWorker( std::thread worker, DeterministicBarrier &barrier )
            : worker_( std::move( worker ) ), barrier_( barrier ) {}
        ~ScopedWorker() { barrier_.Release(); Join(); }
        void Join() { if ( worker_.joinable() ) worker_.join(); }
        ScopedWorker( const ScopedWorker & ) = delete;
        ScopedWorker &operator=( const ScopedWorker & ) = delete;
    private:
        std::thread worker_;
        DeterministicBarrier &barrier_;
    };

    class ConsensusFinalityRaceHarness : public test::CRDTFixture
    {
    public:
        ConsensusFinalityRaceHarness() : CRDTFixture( "consensus_finality_race_test" )
        {
            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &id ) -> std::shared_ptr<sgns::ISecureStorage>
                { return std::make_shared<sgns::MemorySecureStorage>( id ); } );
        }

    protected:
        std::shared_ptr<sgns::GeniusAccount> MakeAccount()
        {
            return sgns::GeniusAccount::NewFromPrivateKey(
                sgns::TokenID::FromBytes( { 0 } ),
                "facebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                boost::filesystem::path( db_path_ ) / "race-account", false );
        }

        std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry(
            const std::shared_ptr<sgns::GeniusAccount> &account )
        {
            auto registry = sgns::ValidatorRegistry::New(
                db_, 1, 1, {}, account->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
                { cb( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            EXPECT_TRUE( registry->StoreGenesisRegistry(
                { account->GetAddress() }, [account]( std::vector<uint8_t> bytes )
                { return account->Sign( std::move( bytes ) ); } ) );
            ASSERT_WAIT_FOR_CONDITION(
                [&] { return registry->LoadCurrentRegistry().has_value() && !registry->GetRegistryCid().empty(); },
                std::chrono::seconds( 2 ), "registry", nullptr );
            return registry;
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const std::shared_ptr<sgns::GeniusAccount> &account )
        {
            return sgns::ConsensusManager::New(
                registry, db_, pubs_, [account]( std::vector<uint8_t> bytes )
                { return account->Sign( std::move( bytes ) ); }, account->GetAddress() );
        }

        outcome::result<sgns::ConsensusManager::Certificate> MakeCertificate(
            const std::shared_ptr<sgns::ConsensusManager> &manager,
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const std::shared_ptr<sgns::GeniusAccount> &validator,
            std::string winner,
            const sgns::ConsensusManager::Subject *existing_subject = nullptr,
            const std::shared_ptr<sgns::GeniusAccount> &proposer = nullptr )
        {
            sgns::ConsensusManager::Subject subject;
            if ( existing_subject ) subject = *existing_subject;
            else
            {
                sgns::UTXOTransitionCommitment commitment;
                commitment.set_consumed_outpoints_root( std::string( 32, '\x11' ) );
                commitment.set_produced_outputs_root( std::string( 32, '\x22' ) );
                BOOST_OUTCOME_TRY( auto created,
                    sgns::ConsensusManager::CreateNonceSubject(
                        validator->GetAddress(), 17, std::move( winner ),
                        sgns::EmbeddedTransaction{}, commitment, sgns::UTXOWitness{} ) );
                subject = std::move( created );
            }
            auto proposal_account = proposer ? proposer : validator;
            BOOST_OUTCOME_TRY( auto proposal,
                sgns::ConsensusManager::CreateProposal(
                    subject, proposal_account->GetAddress(), registry->GetRegistryCid(),
                    registry->GetRegistryEpoch(), [proposal_account]( std::vector<uint8_t> bytes )
                    { return proposal_account->Sign( std::move( bytes ) ); } ) );
            BOOST_OUTCOME_TRY( auto vote,
                manager->CreateVote( proposal.proposal_id(), validator->GetAddress(), true,
                    [validator]( std::vector<uint8_t> bytes )
                    { return validator->Sign( std::move( bytes ) ); } ) );
            return manager->CreateCertificate( proposal, { vote } );
        }
    };
}

TEST_F( ConsensusFinalityRaceHarness, HarnessIsDiscoverable )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = sgns::ConsensusManager::New(
        registry, db_, pubs_, [account]( std::vector<uint8_t> bytes )
        { return account->Sign( std::move( bytes ) ); }, account->GetAddress() );
    ASSERT_TRUE( manager );
    manager->Close();
}

TEST_F( ConsensusFinalityRaceHarness, HandleCertificateBeforeCrdtApplicationBlocksCompetingCertificate )
{
    auto account = MakeAccount(); ASSERT_TRUE( account );
    auto registry = MakeRegistry( account ); ASSERT_TRUE( registry );
    auto manager = MakeManager( registry, account ); ASSERT_TRUE( manager );
    auto winner = MakeCertificate( manager, registry, account, std::string( 64, 'a' ) );
    ASSERT_TRUE( winner );
    auto competitor_account = sgns::GeniusAccount::NewFromPrivateKey(
        sgns::TokenID::FromBytes( { 0 } ),
        "feedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        boost::filesystem::path( db_path_ ) / "competitor", false );
    ASSERT_TRUE( competitor_account );
    auto competitor = MakeCertificate( manager, registry, account, std::string( 64, 'b' ),
                                       &winner.value().proposal().subject(), competitor_account );
    ASSERT_TRUE( competitor );
    auto slot = sgns::ConsensusFinalityRaceTestAccess::Slot( winner.value() ); ASSERT_TRUE( slot );

    DeterministicBarrier barrier;
    sgns::ConsensusFinalityRaceTestAccess::SetStageObserver(
        manager, [&]( std::string_view stage )
        { if ( stage == "authority-established" ) barrier.ArriveAndWait(); } );
    ScopedWorker worker( std::thread( [&]
        { sgns::ConsensusFinalityRaceTestAccess::HandleCertificate( manager, winner.value() ); } ), barrier );
    barrier.WaitUntilArrived();

    auto stored = manager->GetCertificateBySlotId( slot.value() );
    ASSERT_TRUE( stored );
    EXPECT_EQ( stored.value().SerializeAsString(), winner.value().SerializeAsString() );
    EXPECT_TRUE( sgns::ConsensusFinalityRaceTestAccess::IsFinalizedPendingApplication(
        manager, slot.value() ) );
    // Late candidate traffic may remain observable, but it cannot replace the
    // authority already installed at this boundary.
    (void) manager->SubmitProposal( competitor.value().proposal(), false );
    (void) manager->SubmitVote( competitor.value().votes( 0 ), false );
    (void) manager->CreateCertificate( competitor.value().proposal(), { competitor.value().votes( 0 ) } );
    auto conflict = manager->SubmitCertificate( competitor.value() );
    EXPECT_TRUE( conflict.has_error() );
    EXPECT_EQ( manager->GetCertificateBySlotId( slot.value() ).value().SerializeAsString(),
               winner.value().SerializeAsString() );

    barrier.Release();
    worker.Join();
    manager->Close();
}

TEST_F( ConsensusFinalityRaceHarness, IdenticalCertificateAllIngressPathsApplyAndCleanupOnce )
{
    auto account = MakeAccount(); ASSERT_TRUE( account );
    auto registry = MakeRegistry( account ); ASSERT_TRUE( registry );
    auto manager = MakeManager( registry, account ); ASSERT_TRUE( manager );
    auto certificate = MakeCertificate( manager, registry, account, std::string( 64, 'c' ) );
    ASSERT_TRUE( certificate );
    auto slot = sgns::ConsensusFinalityRaceTestAccess::Slot( certificate.value() ); ASSERT_TRUE( slot );
    std::atomic<uint64_t> applications{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::Check>
        { ++applications; return sgns::ConsensusManager::Check::Approve; } ) );

    ASSERT_TRUE( manager->SubmitCertificate( certificate.value() ) );
    sgns::ConsensusFinalityRaceTestAccess::HandleCertificate( manager, certificate.value() );
    sgns::ConsensusFinalityRaceTestAccess::RecoverPendingCertificateWork( manager );
    EXPECT_EQ( applications.load(), 1U );
    auto complete = sgns::ConsensusFinalityRaceTestAccess::Process( manager, slot.value() );
    ASSERT_TRUE( complete );
    EXPECT_EQ( complete->state(), sgns::ConsensusStateStore::ProcessRecord::COMPLETE );
    const auto authority_bytes = manager->GetCertificateBySlotId( slot.value() ).value().SerializeAsString();
    manager->Close();
    manager.reset();

    auto restarted = MakeManager( registry, account ); ASSERT_TRUE( restarted );
    ASSERT_TRUE( restarted->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::Check>
        { ++applications; return sgns::ConsensusManager::Check::Approve; } ) );
    sgns::ConsensusFinalityRaceTestAccess::RecoverPendingCertificateWork( restarted );
    EXPECT_EQ( applications.load(), 1U );
    EXPECT_EQ( restarted->GetCertificateBySlotId( slot.value() ).value().SerializeAsString(), authority_bytes );
    restarted->Close();
}

TEST_F( ConsensusFinalityRaceHarness, ConflictingCertificateAllIngressPathsPreserveWinnerAndDeduplicateEvidence )
{
    auto account = MakeAccount(); ASSERT_TRUE( account );
    auto registry = MakeRegistry( account ); ASSERT_TRUE( registry );
    auto manager = MakeManager( registry, account ); ASSERT_TRUE( manager );
    auto winner = MakeCertificate( manager, registry, account, std::string( 64, 'd' ) ); ASSERT_TRUE( winner );
    auto proposer = sgns::GeniusAccount::NewFromPrivateKey(
        sgns::TokenID::FromBytes( { 0 } ),
        "cafe0000deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        boost::filesystem::path( db_path_ ) / "conflict", false );
    ASSERT_TRUE( proposer );
    auto conflict = MakeCertificate( manager, registry, account, std::string( 64, 'e' ),
                                     &winner.value().proposal().subject(), proposer );
    ASSERT_TRUE( conflict );
    auto slot = sgns::ConsensusFinalityRaceTestAccess::Slot( winner.value() ); ASSERT_TRUE( slot );
    std::atomic<uint64_t> applications{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::Check>
        { ++applications; return sgns::ConsensusManager::Check::Approve; } ) );
    ASSERT_TRUE( manager->SubmitCertificate( winner.value() ) );
    const auto bytes = manager->GetCertificateBySlotId( slot.value() ).value().SerializeAsString();
    EXPECT_TRUE( manager->SubmitCertificate( conflict.value() ).has_error() );
    sgns::ConsensusFinalityRaceTestAccess::HandleCertificate( manager, conflict.value() );
    EXPECT_TRUE( manager->SubmitCertificate( conflict.value() ).has_error() );
    EXPECT_EQ( applications.load(), 1U );
    EXPECT_EQ( manager->GetCertificateBySlotId( slot.value() ).value().SerializeAsString(), bytes );
    auto records = sgns::ConsensusFinalityRaceTestAccess::Conflicts( manager );
    ASSERT_EQ( records.size(), 1U );
    EXPECT_LT( records.front().low_certificate_digest(), records.front().high_certificate_digest() );
    EXPECT_EQ( sgns::ConsensusFinalityRaceTestAccess::UniqueConflictPairs( manager ), 1U );
    EXPECT_GE( records.front().observation_count(), 3U );
    manager->Close();
}
