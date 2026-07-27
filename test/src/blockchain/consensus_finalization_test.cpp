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
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

        static void SetStageObserver( const std::shared_ptr<ConsensusManager> &manager,
                                      std::function<void( std::string_view )> observer )
        {
            manager->finalization_stage_observer_ = std::move( observer );
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

        outcome::result<sgns::ConsensusManager::Certificate> MakeCertificate(
            const std::shared_ptr<sgns::ConsensusManager>  &manager,
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const std::shared_ptr<sgns::GeniusAccount>     &account,
            std::string                                      winner )
        {
            sgns::UTXOTransitionCommitment commitment;
            commitment.set_consumed_outpoints_root( std::string( 32, '\x01' ) );
            commitment.set_produced_outputs_root( std::string( 32, '\x02' ) );
            BOOST_OUTCOME_TRY( auto subject,
                sgns::ConsensusManager::CreateNonceSubject( account->GetAddress(), 7, std::move( winner ),
                    sgns::EmbeddedTransaction{}, commitment, sgns::UTXOWitness{} ) );
            BOOST_OUTCOME_TRY( auto proposal,
                sgns::ConsensusManager::CreateProposal( subject, account->GetAddress(), registry->GetRegistryCid(),
                    registry->GetRegistryEpoch(),
                    [account]( std::vector<uint8_t> bytes ) { return account->Sign( std::move( bytes ) ); } ) );
            BOOST_OUTCOME_TRY( auto vote,
                manager->CreateVote( proposal.proposal_id(), account->GetAddress(), true,
                    [account]( std::vector<uint8_t> bytes ) { return account->Sign( std::move( bytes ) ); } ) );
            return manager->CreateCertificate( proposal, { vote } );
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const std::shared_ptr<sgns::GeniusAccount>     &account )
        {
            return sgns::ConsensusManager::New( registry, db_, pubs_,
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
