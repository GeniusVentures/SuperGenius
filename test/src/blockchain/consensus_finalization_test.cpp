/**
 * @file consensus_finalization_test.cpp
 * @brief Deterministic multi-ingress harness for Phase 10 finalization tests.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>

#include "account/GeniusAccount.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    /** Friend-only access surface reserved for later finalization hooks. */
    class ConsensusFinalizationTestAccess
    {
    public:
        using TraceEvent = ConsensusManager::ConsensusTraceEvent;

        static constexpr std::string_view Scope()
        {
            return "consensus finalization state machine";
        }

        static ConsensusManager::FinalizeResult Finalize( const std::shared_ptr<ConsensusManager> &manager,
                                                          const ConsensusManager::Certificate      &certificate,
                                                          ConsensusManager::DeliverySource          source )
        {
            return manager->FinalizeSlot( certificate, source );
        }

        static outcome::result<std::string> Slot( const ConsensusManager::Certificate &certificate )
        {
            return ConsensusManager::GetSlotKey( certificate.proposal() );
        }

        static std::optional<ConsensusStateStore::ProcessRecord> Process(
            const std::shared_ptr<ConsensusManager> &manager,
            const std::string                       &slot )
        {
            auto process = manager->state_store_->GetProcess( slot );
            return process && process.value() ? process.value() : std::optional<ConsensusStateStore::ProcessRecord>{};
        }

        static bool HasProposal( const std::shared_ptr<ConsensusManager> &manager,
                                 const std::string                       &proposal_id )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            return manager->proposals_.count( proposal_id ) != 0;
        }

        static std::vector<ConsensusStateStore::ConflictRecord> Conflicts(
            const std::shared_ptr<ConsensusManager> &manager )
        {
            auto conflicts = manager->state_store_->ScanConflicts();
            return conflicts ? conflicts.value() : std::vector<ConsensusStateStore::ConflictRecord>{};
        }

        static bool SafetyStopped( const std::shared_ptr<ConsensusManager> &manager,
                                   const std::string                       &slot )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            return manager->restored_safety_slots_.count( slot ) != 0 &&
                   manager->slot_states_[slot].lifecycle == ConsensusManager::SlotState::Lifecycle::SafetyViolation;
        }

        static uint64_t UniqueConflictPairs( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager->certificate_conflict_unique_pairs_.load();
        }

        static void SetConflictObserver(
            const std::shared_ptr<ConsensusManager> &manager,
            std::function<void( const ConsensusStateStore::ConflictRecord &, bool )> observer )
        {
            manager->certificate_conflict_observer_ = std::move( observer );
        }

        static void SetPublishObserver( const std::shared_ptr<ConsensusManager> &manager,
                                        std::function<void()> observer )
        {
            manager->certificate_publish_observer_ = std::move( observer );
        }

        static void SetStageObserver( const std::shared_ptr<ConsensusManager> &manager,
                                      std::function<void( std::string_view )> observer )
        {
            manager->finalization_stage_observer_ = std::move( observer );
        }

        static void SetTraceObserver( const std::shared_ptr<ConsensusManager> &manager,
                                      std::function<void( const TraceEvent & )> observer )
        {
            manager->consensus_trace_observer_ = std::move( observer );
        }

        static outcome::result<std::string> SubjectHash( const ConsensusManager::Proposal &proposal )
        {
            return ConsensusManager::GetSubjectHash( proposal.subject() );
        }

        static bool InstallAndReplayVote( const std::shared_ptr<ConsensusManager> &manager,
                                          const ConsensusManager::Certificate     &certificate,
                                          uint64_t                                  generation )
        {
            const auto &proposal = certificate.proposal();
            const auto &vote = certificate.votes( 0 );
            auto slot = ConsensusManager::GetSlotKey( proposal );
            if ( !slot ) return false;

            ConsensusMessage envelope;
            *envelope.mutable_vote() = vote;
            std::string vote_bytes;
            std::string proposal_bytes;
            std::string envelope_bytes;
            if ( !vote.SerializeToString( &vote_bytes ) ||
                 !proposal.SerializeToString( &proposal_bytes ) ||
                 !envelope.SerializeToString( &envelope_bytes ) )
                return false;

            ConsensusStateStore::VoteRecord record;
            record.set_schema_version( 2 );
            record.set_state( ConsensusStateStore::VoteRecord::ACTIVE );
            record.set_slot_id( slot.value() );
            record.set_proposal_id( proposal.proposal_id() );
            record.set_validator_id( vote.voter_id() );
            record.set_signed_vote_bytes( vote_bytes );
            record.set_outbound_envelope_bytes( envelope_bytes );
            record.set_signed_proposal_bytes( proposal_bytes );
            record.set_registry_cid( proposal.registry_cid() );
            record.set_registry_epoch( proposal.registry_epoch() );
            record.set_generation( generation );
            record.set_created_at_ms( vote.timestamp() );
            record.set_acceptance_horizon_ms( vote.timestamp() + 300'000 );
            if ( !manager->state_store_->PutActiveVote( record ) ) return false;
            {
                std::lock_guard lock( manager->proposals_mutex_ );
                auto &state = manager->slot_states_[slot.value()];
                state.lifecycle = ConsensusManager::SlotState::Lifecycle::Voted;
                state.generation = generation;
                state.durable_generation = generation;
                state.durable_proposal_id = proposal.proposal_id();
            }
            return manager->ReplayDurableVote( slot.value(), generation );
        }

        static bool ReplayVote( const std::shared_ptr<ConsensusManager> &manager,
                                const std::string                       &slot,
                                uint64_t                                  generation )
        {
            return manager->ReplayDurableVote( slot, generation );
        }

        static void SetSigningPublishing( const std::shared_ptr<ConsensusManager> &manager,
                                          const std::string                       &slot )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            auto &state = manager->slot_states_[slot];
            state.generation = 1;
            state.lifecycle = ConsensusManager::SlotState::Lifecycle::SigningPublishing;
        }

        static void CompletePublication( const std::shared_ptr<ConsensusManager> &manager,
                                         const std::string                       &slot,
                                         const std::function<void()>             &before_notify )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            auto &state = manager->slot_states_[slot];
            state.lifecycle = ConsensusManager::SlotState::Lifecycle::Voted;
            before_notify();
            manager->slot_cv_.notify_all();
        }

        static bool TryStartSigning( const std::shared_ptr<ConsensusManager> &manager,
                                     const std::string                       &slot )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            auto &state = manager->slot_states_[slot];
            if ( state.lifecycle == ConsensusManager::SlotState::Lifecycle::Finalizing ||
                 state.lifecycle == ConsensusManager::SlotState::Lifecycle::FinalizedPendingApplication ||
                 state.lifecycle == ConsensusManager::SlotState::Lifecycle::Applied ||
                 state.lifecycle == ConsensusManager::SlotState::Lifecycle::SafetyViolation )
                return false;
            state.lifecycle = ConsensusManager::SlotState::Lifecycle::SigningPublishing;
            return true;
        }

        static void WaitUntilClosing( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::unique_lock lock( manager->activity_state_->mutex );
            manager->activity_state_->cv.wait(
                lock, [&]() { return manager->activity_state_->closing; } );
        }

        static std::size_t ActiveOperations( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->activity_state_->mutex );
            return manager->activity_state_->active;
        }
    };
} // namespace sgns

namespace
{
    constexpr auto kSystemClockNow =
        std::chrono::system_clock::time_point( std::chrono::milliseconds( 1'750'000'000'000LL ) );
    constexpr auto kSteadyClockNow =
        std::chrono::steady_clock::time_point( std::chrono::milliseconds( 84'000 ) );

    class ScopedReset
    {
    public:
        explicit ScopedReset( std::function<void()> reset ) : reset_( std::move( reset ) ) {}

        ScopedReset( const ScopedReset & )            = delete;
        ScopedReset &operator=( const ScopedReset & ) = delete;

        ~ScopedReset()
        {
            if ( reset_ )
            {
                reset_();
            }
        }

    private:
        std::function<void()> reset_;
    };

    class DeterministicBarrier
    {
    public:
        void ArriveAndWait()
        {
            std::unique_lock lock( mutex_ );
            arrived_ = true;
            cv_.notify_all();
            cv_.wait( lock, [this]() { return released_; } );
        }

        void WaitUntilArrived()
        {
            std::unique_lock lock( mutex_ );
            cv_.wait( lock, [this]() { return arrived_; } );
        }

        void Release()
        {
            std::lock_guard lock( mutex_ );
            released_ = true;
            cv_.notify_all();
        }

    private:
        std::mutex              mutex_;
        std::condition_variable cv_;
        bool                    arrived_{ false };
        bool                    released_{ false };
    };

    class ScopedWorker
    {
    public:
        ScopedWorker( std::thread worker, DeterministicBarrier &barrier )
            : worker_( std::move( worker ) ), barrier_( barrier )
        {
        }

        ScopedWorker( const ScopedWorker & )            = delete;
        ScopedWorker &operator=( const ScopedWorker & ) = delete;

        ~ScopedWorker()
        {
            barrier_.Release();
            Join();
        }

        void Join()
        {
            if ( worker_.joinable() )
            {
                worker_.join();
            }
        }

    private:
        std::thread           worker_;
        DeterministicBarrier &barrier_;
    };

    class JoiningThread
    {
    public:
        explicit JoiningThread( std::thread worker ) : worker_( std::move( worker ) ) {}
        JoiningThread( const JoiningThread & ) = delete;
        JoiningThread &operator=( const JoiningThread & ) = delete;
        ~JoiningThread()
        {
            if ( worker_.joinable() ) worker_.join();
        }
        void Join()
        {
            if ( worker_.joinable() ) worker_.join();
        }
    private:
        std::thread worker_;
    };

    struct FinalizationCounters
    {
        std::atomic<uint64_t> signer{ 0 };
        std::atomic<uint64_t> raw_publish{ 0 };
        std::atomic<uint64_t> handler{ 0 };
        std::atomic<uint64_t> cleanup{ 0 };
        std::atomic<uint64_t> subscription{ 0 };
        std::atomic<uint64_t> timer{ 0 };
        std::atomic<uint64_t> certificate_filter{ 0 };
        std::atomic<uint64_t> durable_certificate{ 0 };
        std::atomic<uint64_t> processing{ 0 };
        std::atomic<uint64_t> conflict{ 0 };
        std::atomic<uint64_t> publication{ 0 };

        void Reset()
        {
            signer.store( 0 );
            raw_publish.store( 0 );
            handler.store( 0 );
            cleanup.store( 0 );
            subscription.store( 0 );
            timer.store( 0 );
            certificate_filter.store( 0 );
            durable_certificate.store( 0 );
            processing.store( 0 );
            conflict.store( 0 );
            publication.store( 0 );
        }
    };

    class OrderedEvents
    {
    public:
        void Record( std::string event )
        {
            std::lock_guard lock( mutex_ );
            events_.push_back( std::move( event ) );
        }

        std::vector<std::string> Snapshot() const
        {
            std::lock_guard lock( mutex_ );
            return events_;
        }

        void Reset()
        {
            std::lock_guard lock( mutex_ );
            events_.clear();
        }

    private:
        mutable std::mutex       mutex_;
        std::vector<std::string> events_;
    };

    class ConsensusFinalizationHarness : public test::CRDTFixture
    {
    public:
        ConsensusFinalizationHarness() : CRDTFixture( "consensus_finalization_test" )
        {
            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
                { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
        }

    protected:
        std::shared_ptr<sgns::GeniusAccount> MakeAccount()
        {
            return sgns::GeniusAccount::NewFromPrivateKey(
                sgns::TokenID::FromBytes( { 0 } ),
                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                boost::filesystem::path( db_path_ ) / "finalizer-account",
                false );
        }

        std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry( const std::shared_ptr<sgns::GeniusAccount> &account )
        {
            auto registry = sgns::ValidatorRegistry::New(
                db_, 1, 1, {}, account->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> cb )
                { cb( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            EXPECT_TRUE( registry->StoreGenesisRegistry(
                { account->GetAddress() },
                [account]( std::vector<uint8_t> bytes ) { return account->Sign( std::move( bytes ) ); } ) );
            ASSERT_WAIT_FOR_CONDITION(
                [&]() { return registry->LoadCurrentRegistry().has_value() && !registry->GetRegistryCid().empty(); },
                std::chrono::seconds( 2 ), "registry", nullptr );
            return registry;
        }

        std::shared_ptr<sgns::crdt::GlobalDB> MakeIsolatedDatabase( const std::string &path )
        {
            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( io_ ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
            auto network = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>(
                pubs_->GetHost(), scheduler );
            auto created = sgns::crdt::GlobalDB::New(
                io_, path, pubs_, sgns::crdt::CrdtOptions::DefaultOptions(), network, scheduler, generator );
            EXPECT_TRUE( created );
            if ( !created ) return nullptr;
            auto database = created.value();
            database->AddListenTopic( "consensus-finalization-isolated" );
            database->AddBroadcastTopic( "consensus-finalization-isolated" );
            database->Start();
            return database;
        }

        outcome::result<sgns::ConsensusManager::Certificate> MakeCertificate(
            const std::shared_ptr<sgns::ConsensusManager>  &manager,
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const std::shared_ptr<sgns::GeniusAccount>     &account,
            std::string                                      winner,
            const sgns::ConsensusManager::Subject           *existing_subject = nullptr,
            const std::shared_ptr<sgns::GeniusAccount>      &proposer = nullptr,
            uint64_t                                          nonce = 7 )
        {
            sgns::ConsensusManager::Subject subject;
            if ( existing_subject ) subject = *existing_subject;
            else
            {
                sgns::UTXOTransitionCommitment commitment;
                commitment.set_consumed_outpoints_root( std::string( 32, '\x01' ) );
                commitment.set_produced_outputs_root( std::string( 32, '\x02' ) );
                BOOST_OUTCOME_TRY( auto created,
                    sgns::ConsensusManager::CreateNonceSubject( account->GetAddress(), nonce, std::move( winner ),
                        sgns::EmbeddedTransaction{}, commitment, sgns::UTXOWitness{} ) );
                subject = std::move( created );
            }
            const auto proposal_account = proposer ? proposer : account;
            BOOST_OUTCOME_TRY( auto proposal,
                sgns::ConsensusManager::CreateProposal( subject, proposal_account->GetAddress(), registry->GetRegistryCid(),
                    registry->GetRegistryEpoch(),
                    [proposal_account]( std::vector<uint8_t> bytes )
                    { return proposal_account->Sign( std::move( bytes ) ); } ) );
            BOOST_OUTCOME_TRY( auto vote,
                manager->CreateVote( proposal.proposal_id(), account->GetAddress(), true,
                    [account]( std::vector<uint8_t> bytes ) { return account->Sign( std::move( bytes ) ); } ) );
            return manager->CreateCertificate( proposal, { vote } );
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const std::shared_ptr<sgns::GeniusAccount>     &account,
            const std::shared_ptr<sgns::crdt::GlobalDB>    &database = nullptr )
        {
            return sgns::ConsensusManager::New( registry, database ? database : db_, pubs_,
                [account]( std::vector<uint8_t> bytes ) { return account->Sign( std::move( bytes ) ); },
                account->GetAddress() );
        }

        FinalizationCounters counters_;
        OrderedEvents        events_;
        const std::chrono::system_clock::time_point system_clock_now_{ kSystemClockNow };
        const std::chrono::steady_clock::time_point steady_clock_now_{ kSteadyClockNow };
    };
} // namespace

TEST_F( ConsensusFinalizationHarness, ConcurrentBarrierJoinsCleanly )
{
    const ScopedReset reset_harness(
        [this]()
        {
            counters_.Reset();
            events_.Reset();
        } );
    ASSERT_LT( steady_clock_now_.time_since_epoch(), system_clock_now_.time_since_epoch() );

    DeterministicBarrier barrier;
    std::atomic<bool>     worker_finished{ false };
    ScopedWorker worker(
        std::thread(
            [&]()
            {
                events_.Record( "handler-blocked" );
                ++counters_.handler;
                barrier.ArriveAndWait();
                events_.Record( "handler-released" );
                worker_finished.store( true );
            } ),
        barrier );

    barrier.WaitUntilArrived();
    EXPECT_FALSE( worker_finished.load() );
    EXPECT_EQ( counters_.handler.load(), 1U );
    barrier.Release();
    worker.Join();

    EXPECT_TRUE( worker_finished.load() );
    EXPECT_EQ( events_.Snapshot(),
               ( std::vector<std::string>{ "handler-blocked", "handler-released" } ) );
}

TEST_F( ConsensusFinalizationHarness, StructuredTraceIsPerManagerAndBehaviorNeutral )
{
    using Access = sgns::ConsensusFinalizationTestAccess;
    using Event = Access::TraceEvent;
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    ASSERT_TRUE( registry );
    const auto isolated_path = db_path_ + ".trace-isolated";
    auto isolated_db = MakeIsolatedDatabase( isolated_path );
    ASSERT_TRUE( isolated_db );
    auto first = MakeManager( registry, account );
    auto second = MakeManager( registry, account, isolated_db );
    ASSERT_TRUE( first );
    ASSERT_TRUE( second );
    const ScopedReset close_managers(
        [&]()
        {
            Access::SetTraceObserver( first, {} );
            Access::SetTraceObserver( second, {} );
            first->Close();
            second->Close();
            isolated_db->ShutdownNow();
            boost::filesystem::remove_all( isolated_path );
        } );

    auto first_certificate = MakeCertificate(
        first, registry, account,
        "789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456", nullptr, nullptr, 21 );
    auto second_certificate = MakeCertificate(
        second, registry, account,
        "89abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234567", nullptr, nullptr, 22 );
    ASSERT_TRUE( first_certificate );
    ASSERT_TRUE( second_certificate );

    std::vector<Event> first_events;
    std::vector<Event> second_events;
    Access::SetTraceObserver( first, [&]( const Event &event ) { first_events.push_back( event ); } );
    Access::SetTraceObserver( second, [&]( const Event &event ) { second_events.push_back( event ); } );
    ASSERT_TRUE( first->SubmitProposal( first_certificate.value().proposal(), false ) );
    ASSERT_TRUE( second->SubmitProposal( second_certificate.value().proposal(), false ) );
    ASSERT_EQ( first_events.size(), 1U );
    ASSERT_EQ( second_events.size(), 1U );
    EXPECT_EQ( first_events.front().stage, Event::Stage::LocalProposalPublished );
    EXPECT_EQ( second_events.front().stage, Event::Stage::LocalProposalPublished );
    EXPECT_EQ( first_events.front().proposal_id, first_certificate.value().proposal_id() );
    EXPECT_EQ( second_events.front().proposal_id, second_certificate.value().proposal_id() );
    EXPECT_NE( first_events.front().proposal_id, second_events.front().proposal_id );
    Access::SetTraceObserver( first, {} );
    Access::SetTraceObserver( second, {} );

    std::atomic<uint64_t> first_applications{ 0 };
    std::atomic<uint64_t> second_applications{ 0 };
    ASSERT_TRUE( first->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::Check>
        {
            ++first_applications;
            return sgns::ConsensusManager::Check::Approve;
        } ) );
    ASSERT_TRUE( second->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::Check>
        {
            ++second_applications;
            return sgns::ConsensusManager::Check::Approve;
        } ) );
    const auto without_observer = Access::Finalize(
        first, first_certificate.value(), sgns::ConsensusManager::DeliverySource::Local );
    ASSERT_EQ( without_observer, sgns::ConsensusManager::FinalizeResult::Applied );
    auto first_slot = Access::Slot( first_certificate.value() );
    ASSERT_TRUE( first_slot );
    auto authority_without = first->GetCertificateBySlotId( first_slot.value() );
    ASSERT_TRUE( authority_without );
    EXPECT_EQ( authority_without.value().SerializeAsString(), first_certificate.value().SerializeAsString() );
    auto process_without = Access::Process( first, first_slot.value() );
    ASSERT_TRUE( process_without );
    EXPECT_EQ( process_without->state(), sgns::ConsensusStateStore::ProcessRecord::COMPLETE );
    EXPECT_EQ( first_applications.load(), 1U );

    bool reentrant_read_saw_authority = false;
    Access::SetTraceObserver(
        second,
        [&]( const Event &event )
        {
            if ( event.stage != Event::Stage::AuthorityEstablished ) return;
            auto authority = second->GetCertificateBySlotId( event.slot_id );
            reentrant_read_saw_authority = authority &&
                authority.value().proposal_id() == event.proposal_id;
        } );
    const auto with_observer = Access::Finalize(
        second, second_certificate.value(), sgns::ConsensusManager::DeliverySource::Recovery );
    ASSERT_EQ( with_observer, sgns::ConsensusManager::FinalizeResult::Applied );
    EXPECT_EQ( with_observer, without_observer );
    auto second_slot = Access::Slot( second_certificate.value() );
    ASSERT_TRUE( second_slot );
    auto authority_with = second->GetCertificateBySlotId( second_slot.value() );
    ASSERT_TRUE( authority_with );
    EXPECT_EQ( authority_with.value().SerializeAsString(), second_certificate.value().SerializeAsString() );
    auto process_with = Access::Process( second, second_slot.value() );
    ASSERT_TRUE( process_with );
    EXPECT_EQ( process_with->state(), process_without->state() );
    EXPECT_TRUE( reentrant_read_saw_authority );
    EXPECT_EQ( second_applications.load(), 1U );

    Access::SetTraceObserver( second, []( const Event & ) { throw std::runtime_error( "ignored" ); } );
    EXPECT_EQ( Access::Finalize(
                   second, second_certificate.value(), sgns::ConsensusManager::DeliverySource::Recovery ),
               sgns::ConsensusManager::FinalizeResult::AlreadyFinalized );
    EXPECT_EQ( second_applications.load(), 1U );
    auto authority_after_throw = second->GetCertificateBySlotId( second_slot.value() );
    ASSERT_TRUE( authority_after_throw );
    EXPECT_EQ( authority_after_throw.value().SerializeAsString(), second_certificate.value().SerializeAsString() );
}

TEST_F( ConsensusFinalizationHarness, StructuredTraceUsesStableIdentityForExactReplay )
{
    using Access = sgns::ConsensusFinalizationTestAccess;
    using Event = Access::TraceEvent;
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry, account );
    ASSERT_TRUE( manager );
    const ScopedReset close_manager(
        [&]()
        {
            Access::SetTraceObserver( manager, {} );
            manager->Close();
        } );
    auto certificate = MakeCertificate(
        manager, registry, account,
        "9abcdef0123456789abcdef0123456789abcdef0123456789abcdef012345678", nullptr, nullptr, 31 );
    ASSERT_TRUE( certificate );
    auto competitor = MakeCertificate(
        manager, registry, account,
        "9abcdef0123456789abcdef0123456789abcdef0123456789abcdef012345678",
        &certificate.value().proposal().subject(), nullptr, 31 );
    ASSERT_TRUE( competitor );

    std::vector<Event> votes;
    Access::SetTraceObserver(
        manager,
        [&]( const Event &event )
        {
            if ( event.stage == Event::Stage::VotePublished ) votes.push_back( event );
        } );
    ASSERT_TRUE( Access::InstallAndReplayVote( manager, certificate.value(), 1 ) );
    auto slot = Access::Slot( certificate.value() );
    ASSERT_TRUE( slot );
    ASSERT_TRUE( Access::ReplayVote( manager, slot.value(), 1 ) );
    ASSERT_EQ( votes.size(), 2U );

    const auto identity = []( const Event &event )
    {
        return std::make_tuple( event.validator_id, event.slot_id, event.proposal_id, event.payload_digest );
    };
    EXPECT_EQ( identity( votes[0] ), identity( votes[1] ) );
    EXPECT_EQ( votes[0].slot_id, slot.value() );
    EXPECT_EQ( votes[0].proposal_id, certificate.value().proposal_id() );
    EXPECT_EQ( votes[0].subject_hash,
               Access::SubjectHash( certificate.value().proposal() ).value() );
    auto different_target = identity( votes[0] );
    std::get<2>( different_target ) = competitor.value().proposal_id();
    EXPECT_NE( different_target, identity( votes[0] ) );
}

TEST_F( ConsensusFinalizationHarness, CloseWakesFinalizerWaitingForPublicationReservation )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    auto manager = MakeManager( registry, account );
    ASSERT_TRUE( manager );
    auto certificate = MakeCertificate(
        manager, registry, account,
        "0abcdef0123456789abcdef0123456789abcdef0123456789abcdef012345678" );
    ASSERT_TRUE( certificate );
    auto slot = sgns::ConsensusFinalizationTestAccess::Slot( certificate.value() );
    ASSERT_TRUE( slot );
    sgns::ConsensusFinalizationTestAccess::SetSigningPublishing( manager, slot.value() );

    DeterministicBarrier waiting;
    sgns::ConsensusFinalizationTestAccess::SetStageObserver(
        manager,
        [&]( std::string_view stage )
        {
            if ( stage == "waiting-publication" ) waiting.ArriveAndWait();
        } );
    std::atomic<bool> finished{ false };
    ScopedWorker finalizer(
        std::thread(
            [&]()
            {
                (void) sgns::ConsensusFinalizationTestAccess::Finalize(
                    manager, certificate.value(), sgns::ConsensusManager::DeliverySource::Recovery );
                finished.store( true );
            } ),
        waiting );

    waiting.WaitUntilArrived();
    waiting.Release();
    manager->Close();
    finalizer.Join();
    EXPECT_TRUE( finished.load() );
    EXPECT_EQ( sgns::ConsensusFinalizationTestAccess::ActiveOperations( manager ), 0U );
}

TEST_F( ConsensusFinalizationHarness, CloseWaitsForBlockedHandlerAndDrainsBeforeDestruction )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    auto manager = MakeManager( registry, account );
    ASSERT_TRUE( manager );
    auto certificate = MakeCertificate(
        manager, registry, account,
        "1abcdef0123456789abcdef0123456789abcdef0123456789abcdef012345678" );
    ASSERT_TRUE( certificate );

    DeterministicBarrier handler_barrier;
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::Check>
        {
            handler_barrier.ArriveAndWait();
            ++counters_.handler;
            return sgns::ConsensusManager::Check::Approve;
        } ) );

    ScopedWorker finalizer(
        std::thread(
            [&]()
            {
                (void) sgns::ConsensusFinalizationTestAccess::Finalize(
                    manager, certificate.value(), sgns::ConsensusManager::DeliverySource::Local );
            } ),
        handler_barrier );
    handler_barrier.WaitUntilArrived();

    std::atomic<bool> close_returned{ false };
    JoiningThread closer( std::thread(
        [&]()
        {
            manager->Close();
            close_returned.store( true );
        } ) );
    sgns::ConsensusFinalizationTestAccess::WaitUntilClosing( manager );
    EXPECT_FALSE( close_returned.load() );
    EXPECT_GE( sgns::ConsensusFinalizationTestAccess::ActiveOperations( manager ), 1U );

    handler_barrier.Release();
    finalizer.Join();
    closer.Join();
    EXPECT_TRUE( close_returned.load() );
    EXPECT_EQ( counters_.handler.load(), 1U );
    EXPECT_EQ( sgns::ConsensusFinalizationTestAccess::ActiveOperations( manager ), 0U );

    std::weak_ptr<sgns::ConsensusManager> weak = manager;
    manager.reset();
    EXPECT_TRUE( weak.expired() );
    EXPECT_EQ( counters_.handler.load(), 1U );
}

TEST_F( ConsensusFinalizationHarness, MissingHandlerLeavesQueryableFinalityAndRegistrationCompletesBeforeCleanup )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    auto manager = MakeManager( registry, account );
    ASSERT_TRUE( manager );
    auto certificate = MakeCertificate(
        manager, registry, account,
        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789" );
    ASSERT_TRUE( certificate );
    auto slot = sgns::ConsensusFinalizationTestAccess::Slot( certificate.value() );
    ASSERT_TRUE( slot );

    EXPECT_EQ( sgns::ConsensusFinalizationTestAccess::Finalize(
                   manager, certificate.value(), sgns::ConsensusManager::DeliverySource::Local ),
               sgns::ConsensusManager::FinalizeResult::PendingApplication );
    EXPECT_TRUE( manager->GetCertificateBySlotId( slot.value() ) );
    auto pending = sgns::ConsensusFinalizationTestAccess::Process( manager, slot.value() );
    ASSERT_TRUE( pending );
    EXPECT_EQ( pending->state(), sgns::ConsensusStateStore::ProcessRecord::PENDING );

    int handler_count = 0;
    int cleanup_count = 0;
    bool complete_seen_before_cleanup = false;
    ASSERT_TRUE( manager->RegisterProposalCleanupHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string & )
        {
            ++cleanup_count;
            auto process = sgns::ConsensusFinalizationTestAccess::Process( manager, slot.value() );
            complete_seen_before_cleanup = process &&
                process->state() == sgns::ConsensusStateStore::ProcessRecord::COMPLETE;
        } ) );
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &winner, const sgns::ConsensusManager::Certificate &observed )
            -> outcome::result<sgns::ConsensusManager::Check>
        {
            ++handler_count;
            EXPECT_EQ( winner,
                       "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789" );
            EXPECT_EQ( observed.proposal_id(), certificate.value().proposal_id() );
            return sgns::ConsensusManager::Check::Approve;
        } ) );

    auto complete = sgns::ConsensusFinalizationTestAccess::Process( manager, slot.value() );
    ASSERT_TRUE( complete );
    EXPECT_EQ( complete->state(), sgns::ConsensusStateStore::ProcessRecord::COMPLETE );
    EXPECT_EQ( handler_count, 1 );
    EXPECT_EQ( cleanup_count, 1 );
    EXPECT_TRUE( complete_seen_before_cleanup );
    manager->Close();
}

TEST_F( ConsensusFinalizationHarness, ConcurrentDeliverySourcesApplyExactWinnerOnce )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    auto manager = MakeManager( registry, account );
    ASSERT_TRUE( manager );
    auto certificate = MakeCertificate(
        manager, registry, account,
        "123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0" );
    ASSERT_TRUE( certificate );

    std::atomic<int> handler_count{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::Check>
        {
            ++handler_count;
            return sgns::ConsensusManager::Check::Approve;
        } ) );

    const std::array sources = {
        sgns::ConsensusManager::DeliverySource::Local,
        sgns::ConsensusManager::DeliverySource::PubSub,
        sgns::ConsensusManager::DeliverySource::CRDT,
        sgns::ConsensusManager::DeliverySource::Recovery };
    std::vector<std::thread> workers;
    for ( auto source : sources )
        workers.emplace_back( [&, source]()
        {
            (void) sgns::ConsensusFinalizationTestAccess::Finalize( manager, certificate.value(), source );
        } );
    for ( auto &worker : workers ) worker.join();

    auto slot = sgns::ConsensusFinalizationTestAccess::Slot( certificate.value() );
    ASSERT_TRUE( slot );
    auto complete = sgns::ConsensusFinalizationTestAccess::Process( manager, slot.value() );
    ASSERT_TRUE( complete );
    EXPECT_EQ( complete->state(), sgns::ConsensusStateStore::ProcessRecord::COMPLETE );
    EXPECT_EQ( handler_count.load(), 1 );
    manager->Close();
}

TEST_F( ConsensusFinalizationHarness, ValidConflictIsCanonicalDurableSlotLocalAndOriginalWinnerCanRetry )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    auto manager = MakeManager( registry, account );
    ASSERT_TRUE( manager );
    auto authoritative = MakeCertificate(
        manager, registry, account,
        "456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123" );
    ASSERT_TRUE( authoritative );
    auto proposer2 = sgns::GeniusAccount::NewFromPrivateKey(
        sgns::TokenID::FromBytes( { 0 } ),
        "deedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        boost::filesystem::path( db_path_ ) / "conflict-proposer", false );
    ASSERT_TRUE( proposer2 );
    auto incoming = MakeCertificate(
        manager, registry, account,
        "56789abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234",
        &authoritative.value().proposal().subject(), proposer2 );
    ASSERT_TRUE( incoming );
    ASSERT_NE( authoritative.value().proposal_id(), incoming.value().proposal_id() );
    auto slot = sgns::ConsensusFinalizationTestAccess::Slot( authoritative.value() );
    ASSERT_TRUE( slot );
    ASSERT_EQ( slot.value(), sgns::ConsensusFinalizationTestAccess::Slot( incoming.value() ).value() );

    std::atomic<uint64_t> publish_count{ 0 };
    std::vector<sgns::ConsensusStateStore::ConflictRecord> observations;
    std::mutex observations_mutex;
    sgns::ConsensusFinalizationTestAccess::SetPublishObserver(
        manager, [&]() { ++publish_count; } );
    sgns::ConsensusFinalizationTestAccess::SetConflictObserver(
        manager,
        [&]( const auto &record, bool )
        {
            std::lock_guard lock( observations_mutex );
            observations.push_back( record );
        } );

    EXPECT_EQ( sgns::ConsensusFinalizationTestAccess::Finalize(
                   manager, authoritative.value(), sgns::ConsensusManager::DeliverySource::Local ),
               sgns::ConsensusManager::FinalizeResult::PendingApplication );
    EXPECT_EQ( publish_count.load(), 1U );
    for ( const auto source : { sgns::ConsensusManager::DeliverySource::Local,
                               sgns::ConsensusManager::DeliverySource::PubSub,
                               sgns::ConsensusManager::DeliverySource::Recovery } )
        EXPECT_EQ( sgns::ConsensusFinalizationTestAccess::Finalize( manager, incoming.value(), source ),
                   sgns::ConsensusManager::FinalizeResult::Conflict );

    const auto conflicts = sgns::ConsensusFinalizationTestAccess::Conflicts( manager );
    ASSERT_EQ( conflicts.size(), 1U );
    const auto &conflict = conflicts.front();
    EXPECT_LT( conflict.low_certificate_digest(), conflict.high_certificate_digest() );
    EXPECT_EQ( conflict.authoritative_proposal_id(), authoritative.value().proposal_id() );
    EXPECT_EQ( conflict.incoming_proposal_id(), incoming.value().proposal_id() );
    EXPECT_EQ( conflict.first_source(), 1U );
    EXPECT_EQ( conflict.sources_bitset(), 1U | 2U | 8U );
    EXPECT_EQ( conflict.observation_count(), 3U );
    ASSERT_EQ( observations.size(), 3U );
    EXPECT_EQ( conflict.first_seen_at_ms(), observations.front().first_seen_at_ms() );
    EXPECT_GE( conflict.last_seen_at_ms(), conflict.first_seen_at_ms() );
    EXPECT_EQ( conflict.GetDescriptor()->FindFieldByName( "certificate" ), nullptr );
    EXPECT_EQ( sgns::ConsensusFinalizationTestAccess::UniqueConflictPairs( manager ), 1U );
    EXPECT_TRUE( sgns::ConsensusFinalizationTestAccess::SafetyStopped( manager, slot.value() ) );
    EXPECT_EQ( publish_count.load(), 1U );
    EXPECT_TRUE( manager->SubmitProposal( incoming.value().proposal(), false ).has_error() );
    EXPECT_TRUE( manager->SubmitVote( incoming.value().votes( 0 ), false ).has_error() );
    EXPECT_TRUE( manager->CreateCertificate(
        incoming.value().proposal(), { incoming.value().votes( 0 ) } ).has_error() );
    auto unrelated = MakeCertificate(
        manager, registry, account,
        "6789abcdef0123456789abcdef0123456789abcdef0123456789abcdef012345",
        nullptr, nullptr, 8 );
    ASSERT_TRUE( unrelated );
    EXPECT_TRUE( manager->SubmitProposal( unrelated.value().proposal(), false ) );

    EXPECT_EQ( sgns::ConsensusFinalizationTestAccess::Finalize(
                   manager, authoritative.value(), sgns::ConsensusManager::DeliverySource::Recovery ),
               sgns::ConsensusManager::FinalizeResult::PendingApplication );
    std::atomic<uint64_t> handler_count{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &winner, const sgns::ConsensusManager::Certificate &certificate )
            -> outcome::result<sgns::ConsensusManager::Check>
        {
            ++handler_count;
            EXPECT_EQ( winner,
                       "456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123" );
            EXPECT_EQ( certificate.proposal_id(), authoritative.value().proposal_id() );
            return sgns::ConsensusManager::Check::Approve;
        } ) );
    EXPECT_EQ( handler_count.load(), 1U );
    EXPECT_TRUE( sgns::ConsensusFinalizationTestAccess::SafetyStopped( manager, slot.value() ) );
    manager->Close();
}

TEST_F( ConsensusFinalizationHarness, PublicationReservationCompletesBeforeFinalizationReserves )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    auto manager = MakeManager( registry, account );
    ASSERT_TRUE( manager );
    auto certificate = MakeCertificate(
        manager, registry, account,
        "23456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef01" );
    ASSERT_TRUE( certificate );
    auto slot = sgns::ConsensusFinalizationTestAccess::Slot( certificate.value() );
    ASSERT_TRUE( slot );

    DeterministicBarrier wait_barrier;
    OrderedEvents events;
    sgns::ConsensusFinalizationTestAccess::SetSigningPublishing( manager, slot.value() );
    sgns::ConsensusFinalizationTestAccess::SetStageObserver(
        manager,
        [&]( std::string_view stage )
        {
            events.Record( std::string( stage ) );
            if ( stage == "waiting-publication" ) wait_barrier.ArriveAndWait();
        } );

    std::atomic<bool> finished{ false };
    ScopedWorker worker(
        std::thread( [&]()
        {
            (void) sgns::ConsensusFinalizationTestAccess::Finalize(
                manager, certificate.value(), sgns::ConsensusManager::DeliverySource::Recovery );
            finished.store( true );
        } ),
        wait_barrier );

    wait_barrier.WaitUntilArrived();
    EXPECT_FALSE( finished.load() );
    wait_barrier.Release();
    sgns::ConsensusFinalizationTestAccess::CompletePublication(
        manager, slot.value(), [&]() { events.Record( "publication-complete" ); } );
    worker.Join();

    EXPECT_TRUE( finished.load() );
    EXPECT_EQ( events.Snapshot(),
               ( std::vector<std::string>{ "waiting-publication", "publication-complete", "reserved" } ) );
    manager->Close();
}

TEST_F( ConsensusFinalizationHarness, FinalizationReservationSuppressesLaterSigningBeforePersistence )
{
    auto account = MakeAccount();
    ASSERT_TRUE( account );
    auto registry = MakeRegistry( account );
    auto manager = MakeManager( registry, account );
    ASSERT_TRUE( manager );
    auto certificate = MakeCertificate(
        manager, registry, account,
        "3456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef012" );
    ASSERT_TRUE( certificate );
    auto slot = sgns::ConsensusFinalizationTestAccess::Slot( certificate.value() );
    ASSERT_TRUE( slot );

    DeterministicBarrier reservation_barrier;
    sgns::ConsensusFinalizationTestAccess::SetStageObserver(
        manager,
        [&]( std::string_view stage )
        {
            if ( stage == "reserved" ) reservation_barrier.ArriveAndWait();
        } );

    ScopedWorker worker(
        std::thread( [&]()
        {
            (void) sgns::ConsensusFinalizationTestAccess::Finalize(
                manager, certificate.value(), sgns::ConsensusManager::DeliverySource::Recovery );
        } ),
        reservation_barrier );

    reservation_barrier.WaitUntilArrived();
    EXPECT_FALSE( sgns::ConsensusFinalizationTestAccess::TryStartSigning( manager, slot.value() ) );
    auto before_persistence = manager->GetCertificateBySlotId( slot.value() );
    EXPECT_TRUE( before_persistence.has_error() );
    reservation_barrier.Release();
    worker.Join();

    EXPECT_TRUE( manager->GetCertificateBySlotId( slot.value() ) );
    manager->Close();
}
