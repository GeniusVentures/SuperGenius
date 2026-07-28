/**
 * @file consensus_burn_reservation_test.cpp
 * @brief Deterministic persistent-storage harness for Phase 11 burn reservations.
 */

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "base/buffer.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ConsensusStateStore.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "account/GeniusAccount.hpp"
#include "account/TokenID.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    /**
     * Test-only surface reserved for private Phase 11 hooks. Production hooks
     * added by later plans must friend this class rather than becoming public.
     */
    class ConsensusBurnReservationTestAccess
    {
    public:
        static constexpr std::string_view Scope()
        {
            return "slot-owned bridge burn reservation";
        }

        static void FailCommits( ConsensusStateStore &store )
        {
            store.commit_ = []( storage::BufferBatch & ) -> outcome::result<void>
            {
                return outcome::failure( ConsensusStateStoreError::Storage );
            };
        }

        static void ObserveStartup( std::function<void( std::string_view )> observer )
        {
            ConsensusManager::startup_event_observer_ = std::move( observer );
        }

        static void ResetStartupHooks()
        {
            ConsensusManager::startup_event_observer_ = {};
        }

        static outcome::result<std::string> Slot( const ConsensusManager::Subject &subject )
        {
            return ConsensusManager::GetSlotKey( subject );
        }

        static outcome::result<std::string> Winner( const ConsensusManager::Subject &subject )
        {
            return ConsensusManager::GetSubjectHash( subject );
        }

        static void HandleProposal( const std::shared_ptr<ConsensusManager> &manager,
                                    const ConsensusManager::Proposal &proposal )
        {
            manager->HandleProposal( proposal );
        }

        static bool HasCandidate( const std::shared_ptr<ConsensusManager> &manager,
                                  const std::string &proposal_id )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            return manager->proposals_.count( proposal_id ) != 0;
        }

        static std::string BestCandidate( const std::shared_ptr<ConsensusManager> &manager,
                                         const std::string &slot_id )
        {
            std::lock_guard lock( manager->proposals_mutex_ );
            auto it = manager->slot_states_.find( slot_id );
            return it == manager->slot_states_.end() ? std::string{} : it->second.best_proposal_id;
        }

        static void FailAdmissionCommits( const std::shared_ptr<ConsensusManager> &manager )
        {
            FailCommits( *manager->state_store_ );
        }

        static ConsensusManager::FinalizeResult Finalize(
            const std::shared_ptr<ConsensusManager> &manager,
            const ConsensusManager::Certificate &certificate,
            ConsensusManager::DeliverySource source = ConsensusManager::DeliverySource::Recovery )
        {
            return manager->FinalizeSlot( certificate, source );
        }

        static void ObserveFinalization( const std::shared_ptr<ConsensusManager> &manager,
                                         std::function<void( std::string_view )> observer )
        {
            manager->finalization_stage_observer_ = std::move( observer );
        }

        static void FireCleanup( const std::shared_ptr<ConsensusManager> &manager,
                                 const ConsensusManager::Proposal &proposal )
        {
            manager->FireProposalCleanupCallbacks( proposal );
        }
    };
} // namespace sgns

namespace
{
    constexpr auto kFixedSystemNow =
        std::chrono::system_clock::time_point( std::chrono::milliseconds( 1'750'000'000'000LL ) );
    constexpr auto kFixedSteadyNow =
        std::chrono::steady_clock::time_point( std::chrono::milliseconds( 126'000 ) );

    enum class HarnessCaseGroup : uint8_t
    {
        Store,
        Admission,
        Restart,
        Final,
        Application,
        Race,
        Stale,
        ABA,
        Horizon,
    };

    constexpr std::array<std::string_view, 9> kHarnessCaseGroups = {
        "Store", "Admission", "Restart", "Final", "Application",
        "Race", "Stale", "ABA", "Horizon"
    };

    sgns::base::Buffer BufferOf( std::string_view value )
    {
        sgns::base::Buffer buffer;
        buffer.put( value );
        return buffer;
    }

    sgns::ConsensusStateStore::BurnOutpoint MakeOutpoint( uint32_t index = 7 )
    {
        return { "11155111", std::string( 63, '0' ) + "1", index };
    }

    std::string SlotFor( const sgns::ConsensusStateStore::BurnOutpoint &outpoint )
    {
        const auto preimage = std::string( "mint-v2:" ) + outpoint.source_chain + ":" + outpoint.burn_hash + ":" +
                              std::to_string( outpoint.receipt_log_index );
        const auto hash = sgns::crypto::sha2_256( preimage.data(), preimage.size() );
        return sgns::base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    sgns::ConsensusStateStore::BurnReservationRecord MakeReservedRecord(
        const sgns::ConsensusStateStore::BurnOutpoint &outpoint,
        std::string generation = std::string( 64, 'a' ) )
    {
        sgns::ConsensusStateStore::BurnReservationRecord record;
        record.set_schema_version( 2 );
        record.set_state( sgns::ConsensusStateStore::BurnReservationRecord::RESERVED );
        record.set_slot_id( SlotFor( outpoint ) );
        record.set_source_chain( outpoint.source_chain );
        record.set_burn_hash( outpoint.burn_hash );
        record.set_receipt_log_index( outpoint.receipt_log_index );
        record.set_generation( std::move( generation ) );
        record.set_candidate_acceptance_horizon_ms( 1'750'000'001'000ULL );
        record.set_created_at_ms( 1'750'000'000'000ULL );
        record.set_updated_at_ms( 1'750'000'000'000ULL );
        return record;
    }

    sgns::ConsensusStateStore::BurnOutpointIndex MakeIndex(
        const sgns::ConsensusStateStore::BurnReservationRecord &record )
    {
        sgns::ConsensusStateStore::BurnOutpointIndex index;
        index.set_schema_version( 2 );
        index.set_slot_id( record.slot_id() );
        index.set_source_chain( record.source_chain() );
        index.set_burn_hash( record.burn_hash() );
        index.set_receipt_log_index( record.receipt_log_index() );
        index.set_generation( record.generation() );
        return index;
    }

    class ScopedHookReset
    {
    public:
        explicit ScopedHookReset( std::function<void()> reset ) : reset_( std::move( reset ) ) {}

        ScopedHookReset( const ScopedHookReset & )            = delete;
        ScopedHookReset &operator=( const ScopedHookReset & ) = delete;

        ~ScopedHookReset()
        {
            if ( reset_ )
            {
                reset_();
            }
        }

    private:
        std::function<void()> reset_;
    };

    class PredicateBarrier
    {
    public:
        void ArriveAndWait()
        {
            std::unique_lock lock( mutex_ );
            arrived_ = true;
            condition_.notify_all();
            condition_.wait( lock, [this]() { return released_; } );
        }

        void WaitUntilArrived()
        {
            std::unique_lock lock( mutex_ );
            condition_.wait( lock, [this]() { return arrived_; } );
        }

        void Release()
        {
            std::lock_guard lock( mutex_ );
            released_ = true;
            condition_.notify_all();
        }

    private:
        std::mutex              mutex_;
        std::condition_variable condition_;
        bool                    arrived_{ false };
        bool                    released_{ false };
    };

    class ScopedWorker
    {
    public:
        ScopedWorker( std::thread worker, PredicateBarrier &barrier )
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
        std::thread       worker_;
        PredicateBarrier &barrier_;
    };

    struct BurnReservationCounters
    {
        std::atomic<uint64_t> subscribe{ 0 };
        std::atomic<uint64_t> timer{ 0 };
        std::atomic<uint64_t> replay{ 0 };
        std::atomic<uint64_t> admission{ 0 };
        std::atomic<uint64_t> handler{ 0 };
        std::atomic<uint64_t> cleanup{ 0 };
        std::atomic<uint64_t> application{ 0 };

        void Reset()
        {
            subscribe.store( 0 );
            timer.store( 0 );
            replay.store( 0 );
            admission.store( 0 );
            handler.store( 0 );
            cleanup.store( 0 );
            application.store( 0 );
        }
    };

    class ConsensusBurnReservationHarness : public test::CRDTFixture
    {
    public:
        ConsensusBurnReservationHarness() : CRDTFixture( "consensus_burn_reservation_test" ) {}
        ~ConsensusBurnReservationHarness() override { CloseManagers(); }

    protected:
        std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry()
        {
            if ( !account_ )
            {
                sgns::GeniusAccount::SetSecureStorageFactory(
                    []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
                account_ = sgns::GeniusAccount::NewFromPrivateKey(
                    sgns::TokenID::FromBytes( { 0x00 } ),
                    "d0ffee00d0ffee00d0ffee00d0ffee00d0ffee00d0ffee00d0ffee00d0ffee00",
                    boost::filesystem::path( db_path_ ) / "burn-reservation-account", false );
                EXPECT_TRUE( account_ );
            }
            sgns::ValidatorRegistry::WeightConfig weights;
            weights.slot_public_min_group_ = 1;
            auto registry = sgns::ValidatorRegistry::New(
                db_, 1, 1, weights, account_->GetAddress(),
                []( const std::string &, std::function<void( outcome::result<std::string> )> callback )
                { callback( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            if ( !registry ) return nullptr;
            EXPECT_TRUE( registry->StoreGenesisRegistry(
                { account_->GetAddress() },
                []( std::vector<uint8_t> payload ) { payload.push_back( 0x10 ); return payload; } ).has_value() );
            ASSERT_WAIT_FOR_CONDITION(
                [&registry]() { return registry->LoadCurrentRegistry().has_value() &&
                                        !registry->GetRegistryCid().empty(); },
                std::chrono::milliseconds( 2000 ), "burn reservation registry initialized", nullptr );
            return registry;
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry )
        {
            auto manager = sgns::ConsensusManager::New(
                registry, db_, pubs_,
                [this]( std::vector<uint8_t> payload ) { return account_->Sign( std::move( payload ) ); },
                account_->GetAddress() );
            if ( manager ) managers_.push_back( manager );
            return manager;
        }

        sgns::ConsensusManager::Subject MakeMintSubject(
            const sgns::ConsensusStateStore::BurnOutpoint &outpoint,
            std::string tx_hash = std::string( 64, '9' ) )
        {
            sgns::EmbeddedTransaction embedded;
            auto *mint = embedded.mutable_mint_v2();
            mint->set_chain_id( outpoint.source_chain );
            auto *input = mint->mutable_utxo_params()->add_inputs();
            input->set_tx_id_hash( outpoint.burn_hash );
            input->set_output_index( outpoint.receipt_log_index );
            auto subject = sgns::ConsensusManager::CreateNonceSubject(
                account_->GetAddress(), 7, std::move( tx_hash ), embedded,
                std::nullopt, std::nullopt );
            EXPECT_TRUE( subject );
            return subject.value();
        }

        outcome::result<sgns::ConsensusManager::Certificate> MakeMintCertificate(
            const std::shared_ptr<sgns::ConsensusManager> &manager,
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            const sgns::ConsensusStateStore::BurnOutpoint &outpoint,
            std::string tx_hash = std::string( 64, '9' ) )
        {
            manager->SetSlotHashPopulator( []( sgns::ConsensusVote &vote )
            {
                vote.set_slot_0_hash( std::string( 32, '\x01' ) );
                vote.set_slot_1_hash( std::string( 32, '\x02' ) );
                vote.set_slot_2_hash( std::string( 32, '\x03' ) );
            } );
            auto proposal = manager->CreateProposal(
                MakeMintSubject( outpoint, std::move( tx_hash ) ), account_->GetAddress(),
                registry->GetRegistryCid(), registry->GetRegistryEpoch() );
            if ( !proposal ) return outcome::failure( proposal.error() );
            auto vote = manager->CreateVote(
                proposal.value().proposal_id(), account_->GetAddress(), true,
                [this]( std::vector<uint8_t> bytes ) { return account_->Sign( std::move( bytes ) ); } );
            if ( !vote ) return outcome::failure( vote.error() );
            return manager->CreateCertificate( proposal.value(), { vote.value() } );
        }

        void CloseManagers()
        {
            for ( auto &manager : managers_ ) if ( manager ) manager->Close();
            managers_.clear();
        }

        std::shared_ptr<sgns::storage::rocksdb> CloseOwnersAndReopenSamePath()
        {
            const auto exact_path = db_path_;
            db_->ShutdownNow();
            db_.reset();

            auto reopened = sgns::storage::rocksdb::create( exact_path );
            EXPECT_TRUE( reopened.has_value() );
            return reopened ? reopened.value() : nullptr;
        }

        sgns::storage::rocksdb::QueryResult InspectRaw(
            const std::shared_ptr<sgns::storage::rocksdb> &storage,
            std::string_view prefix )
        {
            auto raw = storage->query( BufferOf( prefix ) );
            EXPECT_TRUE( raw.has_value() );
            return raw ? raw.value() : sgns::storage::rocksdb::QueryResult{};
        }

        BurnReservationCounters counters_;
        std::shared_ptr<sgns::GeniusAccount> account_;
        std::vector<std::shared_ptr<sgns::ConsensusManager>> managers_;
        const std::chrono::system_clock::time_point system_now_{ kFixedSystemNow };
        const std::chrono::steady_clock::time_point steady_now_{ kFixedSteadyNow };
    };
} // namespace

TEST_F( ConsensusBurnReservationHarness, PersistentDatabaseAndBarrierReopenCleanly )
{
    const ScopedHookReset reset_hooks( [this]() { counters_.Reset(); } );
    ASSERT_EQ( kHarnessCaseGroups.size(), 9U );
    EXPECT_EQ( static_cast<uint8_t>( HarnessCaseGroup::Horizon ), 8U );
    EXPECT_EQ( sgns::ConsensusBurnReservationTestAccess::Scope(),
               "slot-owned bridge burn reservation" );
    ASSERT_LT( steady_now_.time_since_epoch(), system_now_.time_since_epoch() );

    auto datastore = db_->GetDataStore();
    ASSERT_TRUE( datastore );
    sgns::ConsensusStateStore store( datastore );

    sgns::ConsensusStateStore::ProcessRecord marker;
    marker.set_schema_version( 2 );
    marker.set_state( sgns::ConsensusStateStore::ProcessRecord::PENDING );
    marker.set_slot_id( std::string( 64, '1' ) );
    marker.set_certificate_digest( std::string( 64, '2' ) );
    marker.set_proposal_id( std::string( 64, '3' ) );
    marker.set_winner_id( "phase-11-harness-marker" );
    marker.set_updated_at_ms( 1'750'000'000'000ULL );
    ASSERT_TRUE( store.PutPendingProcess( marker ).has_value() );

    const auto process_key = sgns::ConsensusStateStore::ProcessKey( marker.slot_id() );
    const auto raw_before  = InspectRaw( datastore, process_key );
    ASSERT_EQ( raw_before.size(), 1U );
    datastore.reset();

    auto reopened = CloseOwnersAndReopenSamePath();
    ASSERT_TRUE( reopened );
    sgns::ConsensusStateStore reopened_store( reopened );
    auto restored = reopened_store.GetProcess( marker.slot_id() );
    ASSERT_TRUE( restored.has_value() );
    ASSERT_TRUE( restored.value().has_value() );
    EXPECT_EQ( restored.value()->winner_id(), marker.winner_id() );
    EXPECT_EQ( InspectRaw( reopened, process_key ).size(), 1U );

    PredicateBarrier barrier;
    std::atomic<bool> worker_finished{ false };
    ScopedWorker worker(
        std::thread(
            [&]()
            {
                ++counters_.handler;
                barrier.ArriveAndWait();
                worker_finished.store( true );
            } ),
        barrier );

    barrier.WaitUntilArrived();
    EXPECT_FALSE( worker_finished.load() );
    EXPECT_EQ( counters_.handler.load(), 1U );
    barrier.Release();
    worker.Join();
    EXPECT_TRUE( worker_finished.load() );
}

TEST_F( ConsensusBurnReservationHarness, BurnReservationStoreCreatesReciprocalAndJoinsOneGeneration )
{
    auto datastore = db_->GetDataStore();
    ASSERT_TRUE( datastore );
    sgns::ConsensusStateStore store( datastore );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    constexpr uint64_t now = 1'750'000'000'000ULL;

    auto created = store.CreateOrJoinBurnReservation( slot, outpoint, now + 100, now );
    ASSERT_TRUE( created.has_value() );
    EXPECT_TRUE( created.value().created );
    EXPECT_EQ( created.value().record.generation().size(), 64U );
    EXPECT_EQ( InspectRaw( datastore, sgns::ConsensusStateStore::BurnSlotKey( slot ) ).size(), 1U );
    EXPECT_EQ( InspectRaw( datastore, sgns::ConsensusStateStore::BurnOutpointKey( outpoint ) ).size(), 1U );

    auto joined = store.CreateOrJoinBurnReservation( slot, outpoint, now + 50, now + 1 );
    ASSERT_TRUE( joined.has_value() );
    EXPECT_FALSE( joined.value().created );
    EXPECT_EQ( joined.value().record.generation(), created.value().record.generation() );
    EXPECT_EQ( joined.value().record.candidate_acceptance_horizon_ms(), now + 100 );

    auto extended = store.CreateOrJoinBurnReservation( slot, outpoint, now + 500, now + 2 );
    ASSERT_TRUE( extended.has_value() );
    EXPECT_EQ( extended.value().record.generation(), created.value().record.generation() );
    EXPECT_EQ( extended.value().record.candidate_acceptance_horizon_ms(), now + 500 );
    auto scanned = store.ScanBurnReservations();
    ASSERT_TRUE( scanned.has_value() );
    ASSERT_EQ( scanned.value().size(), 1U );
}

TEST_F( ConsensusBurnReservationHarness, BurnReservationStoreAtomicCreationFailureLeavesNoReciprocalHalf )
{
    auto datastore = db_->GetDataStore();
    ASSERT_TRUE( datastore );
    sgns::ConsensusStateStore store( datastore );
    sgns::ConsensusBurnReservationTestAccess::FailCommits( store );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    constexpr uint64_t now = 1'750'000'000'000ULL;

    auto failed = store.CreateOrJoinBurnReservation( slot, outpoint, now + 100, now );
    ASSERT_TRUE( failed.has_error() );
    EXPECT_EQ( failed.error(), sgns::ConsensusStateStoreError::Storage );
    EXPECT_EQ( InspectRaw( datastore, sgns::ConsensusStateStore::BurnSlotKey( slot ) ).size(), 0U );
    EXPECT_EQ( InspectRaw( datastore, sgns::ConsensusStateStore::BurnOutpointKey( outpoint ) ).size(), 0U );
}

TEST_F( ConsensusBurnReservationHarness, BurnReservationStoreRejectsIdentityAliasesAndContradictions )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    constexpr uint64_t now = 1'750'000'000'000ULL;
    ASSERT_TRUE( store.CreateOrJoinBurnReservation( slot, outpoint, now + 100, now ).has_value() );

    auto other = outpoint;
    other.receipt_log_index++;
    auto wrong_slot = store.CreateOrJoinBurnReservation( slot, other, now + 100, now );
    ASSERT_TRUE( wrong_slot.has_error() );
    EXPECT_EQ( wrong_slot.error(), sgns::ConsensusStateStoreError::Conflict );
    auto wrong_outpoint_slot = store.CreateOrJoinBurnReservation( std::string( 64, 'b' ), outpoint, now + 100, now );
    ASSERT_TRUE( wrong_outpoint_slot.has_error() );
    EXPECT_EQ( wrong_outpoint_slot.error(), sgns::ConsensusStateStoreError::Conflict );

    auto noncanonical_chain = outpoint;
    noncanonical_chain.source_chain = "011155111";
    auto chain_alias = store.CreateOrJoinBurnReservation( SlotFor( noncanonical_chain ), noncanonical_chain,
                                                          now + 100, now );
    ASSERT_TRUE( chain_alias.has_error() );
    EXPECT_EQ( chain_alias.error(), sgns::ConsensusStateStoreError::InvalidArgument );
    auto zero_burn = outpoint;
    zero_burn.burn_hash.assign( 64, '0' );
    auto zero = store.CreateOrJoinBurnReservation( SlotFor( zero_burn ), zero_burn, now + 100, now );
    ASSERT_TRUE( zero.has_error() );
    EXPECT_EQ( zero.error(), sgns::ConsensusStateStoreError::InvalidArgument );
}

TEST_F( ConsensusBurnReservationHarness, BurnReservationStoreStrictDecodeCorruptionMatrixFailsClosed )
{
    auto datastore = db_->GetDataStore();
    ASSERT_TRUE( datastore );
    sgns::ConsensusStateStore store( datastore );
    const auto outpoint = MakeOutpoint();
    auto record = MakeReservedRecord( outpoint );
    auto index = MakeIndex( record );
    const auto slot_key = sgns::ConsensusStateStore::BurnSlotKey( record.slot_id() );
    const auto index_key = sgns::ConsensusStateStore::BurnOutpointKey( outpoint );

    auto put_pair = [&]( const auto &slot_record, const auto &outpoint_index )
    {
        ASSERT_TRUE( datastore->put( BufferOf( slot_key ), BufferOf( slot_record.SerializeAsString() ) ).has_value() );
        ASSERT_TRUE( datastore->put( BufferOf( index_key ), BufferOf( outpoint_index.SerializeAsString() ) ).has_value() );
    };
    auto expect_integrity = [&]()
    {
        auto scan = store.ScanBurnReservations();
        ASSERT_TRUE( scan.has_error() );
        EXPECT_EQ( scan.error(), sgns::ConsensusStateStoreError::Integrity );
    };

    put_pair( record, index );
    ASSERT_EQ( store.ScanBurnReservations().value().size(), 1U );

    auto bad = record;
    bad.set_schema_version( 3 );
    put_pair( bad, index );
    expect_integrity();
    bad = record;
    bad.set_state( sgns::ConsensusStateStore::BurnReservationRecord::STATE_UNSPECIFIED );
    put_pair( bad, index );
    expect_integrity();
    bad = record;
    bad.set_source_chain( "01" );
    put_pair( bad, index );
    expect_integrity();
    bad = record;
    bad.set_burn_hash( std::string( 64, '0' ) );
    put_pair( bad, index );
    expect_integrity();
    bad = record;
    bad.set_generation( std::string( 63, 'a' ) + "A" );
    put_pair( bad, index );
    expect_integrity();
    bad = record;
    bad.set_certificate_digest( std::string( 64, 'b' ) );
    put_pair( bad, index );
    expect_integrity();

    put_pair( record, index );
    auto malformed = record.SerializeAsString();
    malformed.append( "\x78\x01", 2 );
    ASSERT_TRUE( datastore->put( BufferOf( slot_key ), BufferOf( malformed ) ).has_value() );
    expect_integrity();
    auto noncanonical = record.SerializeAsString();
    noncanonical.append( "\x08\x02", 2 );
    ASSERT_TRUE( datastore->put( BufferOf( slot_key ), BufferOf( noncanonical ) ).has_value() );
    expect_integrity();
}

TEST_F( ConsensusBurnReservationHarness, BurnReservationStoreRejectsMissingOrMismatchedReciprocalHalf )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    const auto outpoint = MakeOutpoint();
    auto record = MakeReservedRecord( outpoint );
    auto index = MakeIndex( record );
    ASSERT_TRUE( datastore->put( BufferOf( sgns::ConsensusStateStore::BurnSlotKey( record.slot_id() ) ),
                                 BufferOf( record.SerializeAsString() ) ).has_value() );
    auto half = store.ScanBurnReservations();
    ASSERT_TRUE( half.has_error() );
    EXPECT_EQ( half.error(), sgns::ConsensusStateStoreError::Integrity );

    index.set_generation( std::string( 64, 'b' ) );
    ASSERT_TRUE( datastore->put( BufferOf( sgns::ConsensusStateStore::BurnOutpointKey( outpoint ) ),
                                 BufferOf( index.SerializeAsString() ) ).has_value() );
    auto mismatch = store.GetBurnReservation( record.slot_id() );
    ASSERT_TRUE( mismatch.has_error() );
    EXPECT_EQ( mismatch.error(), sgns::ConsensusStateStoreError::Integrity );
}

TEST_F( ConsensusBurnReservationHarness, BurnReservationStoreFinalityTransitionsAreMonotonic )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    constexpr uint64_t now = 1'750'000'000'000ULL;
    const std::string certificate( 64, 'b' );
    const std::string proposal( 64, 'c' );
    const std::string winner( 64, 'd' );
    auto created = store.CreateOrJoinBurnReservation( slot, outpoint, now + 100, now );
    ASSERT_TRUE( created.has_value() );
    const auto generation = created.value().record.generation();

    auto finalized = store.FinalizeBurnReservation( slot, outpoint, certificate, proposal, winner, now + 1 );
    ASSERT_TRUE( finalized.has_value() );
    EXPECT_EQ( finalized.value().state(), sgns::ConsensusStateStore::BurnReservationRecord::FINALIZED_PENDING_APPLICATION );
    auto rejoin = store.CreateOrJoinBurnReservation( slot, outpoint, now + 200, now + 2 );
    ASSERT_TRUE( rejoin.has_error() );
    EXPECT_EQ( rejoin.error(), sgns::ConsensusStateStoreError::Conflict );
    auto release = store.DeleteReservedBurnReservation( slot, generation );
    ASSERT_TRUE( release.has_error() );
    EXPECT_EQ( release.error(), sgns::ConsensusStateStoreError::Conflict );

    auto batch = datastore->batch();
    auto consumed = store.PrepareConsumedBurnReservation( *batch, slot, outpoint, generation,
                                                           certificate, proposal, winner, now + 2 );
    ASSERT_TRUE( consumed.has_value() );
    ASSERT_TRUE( batch->commit().has_value() );
    auto durable = store.GetBurnReservation( slot );
    ASSERT_TRUE( durable.has_value() && durable.value().has_value() );
    EXPECT_EQ( durable.value()->state(), sgns::ConsensusStateStore::BurnReservationRecord::CONSUMED );
    EXPECT_TRUE( store.FinalizeBurnReservation( slot, outpoint, certificate, proposal, winner, now + 3 ).has_value() );
    auto conflicting = store.FinalizeBurnReservation( slot, outpoint, std::string( 64, 'e' ), proposal, winner, now + 3 );
    ASSERT_TRUE( conflicting.has_error() );
    EXPECT_EQ( conflicting.error(), sgns::ConsensusStateStoreError::Conflict );
}

TEST_F( ConsensusBurnReservationHarness, BurnReservationSafetyErrorCannotRegressOrRelease )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    constexpr uint64_t now = 1'750'000'000'000ULL;
    const std::string certificate( 64, 'b' );
    const std::string proposal( 64, 'c' );
    const std::string winner( 64, 'd' );
    auto finalized = store.FinalizeBurnReservation( slot, outpoint, certificate, proposal, winner, now );
    ASSERT_TRUE( finalized.has_value() );
    auto safety = store.MarkBurnReservationSafetyError( slot, finalized.value().generation(), certificate,
                                                        proposal, winner, "different durable winner", now + 1 );
    ASSERT_TRUE( safety.has_value() );
    EXPECT_EQ( safety.value().state(), sgns::ConsensusStateStore::BurnReservationRecord::SAFETY_ERROR );
    auto release = store.DeleteReservedBurnReservation( slot, safety.value().generation() );
    ASSERT_TRUE( release.has_error() );
    EXPECT_EQ( release.error(), sgns::ConsensusStateStoreError::Conflict );
    auto consume_batch = datastore->batch();
    auto consume = store.PrepareConsumedBurnReservation( *consume_batch, slot, outpoint, safety.value().generation(),
                                                          certificate, proposal, winner, now + 2 );
    ASSERT_TRUE( consume.has_error() );
    EXPECT_EQ( consume.error(), sgns::ConsensusStateStoreError::Conflict );
}

TEST_F( ConsensusBurnReservationHarness, BurnReservationGenerationReleaseIsConditionalAndRecreationIsFresh )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    constexpr uint64_t now = 1'750'000'000'000ULL;
    auto first = store.CreateOrJoinBurnReservation( slot, outpoint, now + 100, now );
    ASSERT_TRUE( first.has_value() );
    const auto first_generation = first.value().record.generation();

    auto stale = store.DeleteReservedBurnReservation( slot, std::string( 64, 'f' ) );
    ASSERT_TRUE( stale.has_value() );
    EXPECT_EQ( stale.value(), sgns::ConsensusStateStore::BurnDeleteResult::GenerationMismatch );
    EXPECT_TRUE( store.GetBurnReservation( slot ).value().has_value() );
    auto released = store.DeleteReservedBurnReservation( slot, first_generation );
    ASSERT_TRUE( released.has_value() );
    EXPECT_EQ( released.value(), sgns::ConsensusStateStore::BurnDeleteResult::Deleted );
    EXPECT_FALSE( store.GetBurnReservation( slot ).value().has_value() );
    EXPECT_EQ( InspectRaw( datastore, sgns::ConsensusStateStore::BurnSlotKey( slot ) ).size(), 0U );
    EXPECT_EQ( InspectRaw( datastore, sgns::ConsensusStateStore::BurnOutpointKey( outpoint ) ).size(), 0U );

    auto second = store.CreateOrJoinBurnReservation( slot, outpoint, now + 300, now + 200 );
    ASSERT_TRUE( second.has_value() );
    EXPECT_NE( second.value().record.generation(), first_generation );
    auto stale_again = store.DeleteReservedBurnReservation( slot, first_generation );
    ASSERT_TRUE( stale_again.has_value() );
    EXPECT_EQ( stale_again.value(), sgns::ConsensusStateStore::BurnDeleteResult::GenerationMismatch );
    EXPECT_TRUE( store.GetBurnReservation( slot ).value().has_value() );
}

TEST_F( ConsensusBurnReservationHarness, AdmissionPersistsBeforeCandidateVisibility )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    ASSERT_TRUE( manager->RegisterSubjectHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const auto & ) -> outcome::result<sgns::ConsensusManager::ValidationResult>
        { return sgns::ConsensusManager::ValidationResult::Approve(); } ) );

    std::string proposal_id;
    ASSERT_TRUE( manager->RegisterResourceAdmissionHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const auto &, const std::string &resolved_slot )
            -> outcome::result<std::optional<sgns::ConsensusStateStore::BurnOutpoint>>
        {
            EXPECT_EQ( resolved_slot, slot );
            EXPECT_FALSE( sgns::ConsensusBurnReservationTestAccess::HasCandidate( manager, proposal_id ) );
            EXPECT_FALSE( store.GetBurnReservation( slot ).value().has_value() );
            return std::optional<sgns::ConsensusStateStore::BurnOutpoint>{ outpoint };
        } ) );
    auto proposal = manager->CreateProposal(
        MakeMintSubject( outpoint ), account_->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal );
    proposal_id = proposal.value().proposal_id();
    sgns::ConsensusBurnReservationTestAccess::HandleProposal( manager, proposal.value() );

    auto durable = store.GetBurnReservation( slot );
    ASSERT_TRUE( durable && durable.value() );
    EXPECT_TRUE( sgns::ConsensusBurnReservationTestAccess::HasCandidate( manager, proposal_id ) );
    EXPECT_EQ( sgns::ConsensusBurnReservationTestAccess::BestCandidate( manager, slot ), proposal_id );
}

TEST_F( ConsensusBurnReservationHarness, AdmissionStoreFailureLeavesNoCandidateOrReservation )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    ASSERT_TRUE( manager->RegisterSubjectHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const auto & ) -> outcome::result<sgns::ConsensusManager::ValidationResult>
        { return sgns::ConsensusManager::ValidationResult::Approve(); } ) );
    ASSERT_TRUE( manager->RegisterResourceAdmissionHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [outpoint]( const auto &, const std::string & )
            -> outcome::result<std::optional<sgns::ConsensusStateStore::BurnOutpoint>>
        { return std::optional<sgns::ConsensusStateStore::BurnOutpoint>{ outpoint }; } ) );
    sgns::ConsensusBurnReservationTestAccess::FailAdmissionCommits( manager );
    auto proposal = manager->CreateProposal(
        MakeMintSubject( outpoint ), account_->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal );
    sgns::ConsensusBurnReservationTestAccess::HandleProposal( manager, proposal.value() );

    EXPECT_FALSE( sgns::ConsensusBurnReservationTestAccess::HasCandidate( manager, proposal.value().proposal_id() ) );
    EXPECT_FALSE( sgns::ConsensusStateStore( db_->GetDataStore() ).GetBurnReservation( slot ).value().has_value() );
}

TEST_F( ConsensusBurnReservationHarness, PendingAndRejectedAdmissionRemainSideEffectFree )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    sgns::ConsensusManager::Check current_decision = sgns::ConsensusManager::Check::Pending;
    std::atomic_uint64_t descriptors{ 0 };
    ASSERT_TRUE( manager->RegisterSubjectHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const auto & ) -> outcome::result<sgns::ConsensusManager::ValidationResult>
        { return sgns::ConsensusManager::ValidationResult{ current_decision }; } ) );
    ASSERT_TRUE( manager->RegisterResourceAdmissionHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const auto &, const std::string & )
            -> outcome::result<std::optional<sgns::ConsensusStateStore::BurnOutpoint>>
        { ++descriptors; return std::optional<sgns::ConsensusStateStore::BurnOutpoint>{ MakeOutpoint() }; } ) );

    for ( const auto decision : { sgns::ConsensusManager::Check::Pending, sgns::ConsensusManager::Check::Reject } )
    {
        current_decision = decision;
        const auto outpoint = MakeOutpoint( decision == sgns::ConsensusManager::Check::Pending ? 31 : 32 );
        auto proposal = manager->CreateProposal(
            MakeMintSubject( outpoint ), account_->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
        ASSERT_TRUE( proposal );
        sgns::ConsensusBurnReservationTestAccess::HandleProposal( manager, proposal.value() );
        EXPECT_EQ( descriptors.load(), 0U );
        EXPECT_FALSE( sgns::ConsensusStateStore( db_->GetDataStore() )
                          .GetBurnReservation( SlotFor( outpoint ) ).value().has_value() );
    }
}

TEST_F( ConsensusBurnReservationHarness, ContendersJoinOneGenerationAcrossCandidateIdentities )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    ASSERT_TRUE( manager->RegisterSubjectHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const auto & ) -> outcome::result<sgns::ConsensusManager::ValidationResult>
        { return sgns::ConsensusManager::ValidationResult::Approve(); } ) );
    ASSERT_TRUE( manager->RegisterResourceAdmissionHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [outpoint]( const auto &, const std::string & )
            -> outcome::result<std::optional<sgns::ConsensusStateStore::BurnOutpoint>>
        { return std::optional<sgns::ConsensusStateStore::BurnOutpoint>{ outpoint }; } ) );
    auto first = manager->CreateProposal(
        MakeMintSubject( outpoint, std::string( 64, '8' ) ), account_->GetAddress(),
        registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    auto second = manager->CreateProposal(
        MakeMintSubject( outpoint, std::string( 64, '9' ) ), account_->GetAddress(),
        registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( first && second );
    sgns::ConsensusBurnReservationTestAccess::HandleProposal( manager, first.value() );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    auto initial = store.GetBurnReservation( slot );
    ASSERT_TRUE( initial && initial.value() );
    const auto generation = initial.value()->generation();
    sgns::ConsensusBurnReservationTestAccess::HandleProposal( manager, second.value() );
    auto joined = store.GetBurnReservation( slot );
    ASSERT_TRUE( joined && joined.value() );
    EXPECT_EQ( joined.value()->generation(), generation );
    EXPECT_TRUE( sgns::ConsensusBurnReservationTestAccess::HasCandidate( manager, first.value().proposal_id() ) );
    EXPECT_TRUE( sgns::ConsensusBurnReservationTestAccess::HasCandidate( manager, second.value().proposal_id() ) );
}

TEST_F( ConsensusBurnReservationHarness, CleanupCallbacksCannotReleaseSharedReservation )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint();
    const auto slot = SlotFor( outpoint );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    auto created = store.CreateOrJoinBurnReservation( slot, outpoint, 1'900'000'000'000ULL, 1'800'000'000'000ULL );
    ASSERT_TRUE( created );
    std::atomic_uint64_t cleanup{ 0 };
    ASSERT_TRUE( manager->RegisterProposalCleanupHandler(
        sgns::NONCE_SUBJECT_TYPE, [&]( const std::string & ) { ++cleanup; } ) );
    auto proposal = manager->CreateProposal(
        MakeMintSubject( outpoint ), account_->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal );
    sgns::ConsensusBurnReservationTestAccess::FireCleanup( manager, proposal.value() );
    auto unchanged = store.GetBurnReservation( slot );
    ASSERT_TRUE( unchanged && unchanged.value() );
    EXPECT_EQ( unchanged.value()->generation(), created.value().record.generation() );
    EXPECT_EQ( cleanup.load(), 1U );
}

TEST_F( ConsensusBurnReservationHarness, RestartRestoresReservedBurnBeforeStartupWithoutCandidates )
{
    const ScopedHookReset reset( []()
    {
        sgns::ConsensusBurnReservationTestAccess::ResetStartupHooks();
        sgns::ConsensusManager::UnregisterSlotKeyHandler( sgns::NONCE_SUBJECT_TYPE );
        EXPECT_TRUE( sgns::ConsensusManager::EnsureBuiltinSlotKeyHandlers() );
    } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    const auto outpoint = MakeOutpoint();
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    auto created = store.CreateOrJoinBurnReservation(
        SlotFor( outpoint ), outpoint, 1'750'000'001'000ULL, 1'750'000'000'000ULL );
    ASSERT_TRUE( created );

    sgns::ConsensusManager::UnregisterSlotKeyHandler( sgns::NONCE_SUBJECT_TYPE );
    std::vector<std::string> events;
    sgns::ConsensusBurnReservationTestAccess::ObserveStartup(
        [&]( std::string_view event ) { events.emplace_back( event ); } );
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    ASSERT_FALSE( events.empty() );
    EXPECT_EQ( events.front(), "restored" );
    EXPECT_EQ( events.at( 1 ), "subscribe" );
    auto durable = store.GetBurnReservation( SlotFor( outpoint ) );
    ASSERT_TRUE( durable && durable.value() );
    EXPECT_EQ( durable.value()->generation(), created.value().record.generation() );
    EXPECT_EQ( durable.value()->state(), sgns::ConsensusStateStore::BurnReservationRecord::RESERVED );

    auto subject = MakeMintSubject( outpoint );
    auto slot = sgns::ConsensusBurnReservationTestAccess::Slot( subject );
    ASSERT_TRUE( slot );
    EXPECT_EQ( slot.value(), SlotFor( outpoint ) );
}

TEST_F( ConsensusBurnReservationHarness, StartupReconcileCorruptReciprocalReservationHasZeroSideEffects )
{
    const ScopedHookReset reset( []() { sgns::ConsensusBurnReservationTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto record = MakeReservedRecord( MakeOutpoint() );
    ASSERT_TRUE( db_->GetDataStore()->put(
        BufferOf( sgns::ConsensusStateStore::BurnSlotKey( record.slot_id() ) ),
        BufferOf( record.SerializeAsString() ) ).has_value() );
    std::vector<std::string> events;
    sgns::ConsensusBurnReservationTestAccess::ObserveStartup(
        [&]( std::string_view event ) { events.emplace_back( event ); } );
    EXPECT_FALSE( MakeManager( registry ) );
    EXPECT_TRUE( events.empty() );
}

TEST_F( ConsensusBurnReservationHarness, StartupResolverRegistrationRequiresExplicitRemovalToOverwrite )
{
    sgns::ConsensusManager::UnregisterSlotKeyHandler( sgns::NONCE_SUBJECT_TYPE );
    ASSERT_TRUE( sgns::ConsensusManager::EnsureBuiltinSlotKeyHandlers() );
    EXPECT_FALSE( sgns::ConsensusManager::RegisterSlotKeyHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const sgns::ConsensusManager::Subject & ) -> outcome::result<std::string>
        { return std::string( 64, 'f' ); } ) );
    sgns::ConsensusManager::UnregisterSlotKeyHandler( sgns::NONCE_SUBJECT_TYPE );
    EXPECT_TRUE( sgns::ConsensusManager::RegisterSlotKeyHandler(
        sgns::NONCE_SUBJECT_TYPE,
        []( const sgns::ConsensusManager::Subject & ) -> outcome::result<std::string>
        { return std::string( 64, 'f' ); } ) );
    sgns::ConsensusManager::UnregisterSlotKeyHandler( sgns::NONCE_SUBJECT_TYPE );
    EXPECT_TRUE( sgns::ConsensusManager::EnsureBuiltinSlotKeyHandlers() );
}

TEST_F( ConsensusBurnReservationHarness, RestartCertificateReconcileCreatesFinalProtectionBeforeHandler )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    manager->SetSlotHashPopulator( []( sgns::ConsensusVote &vote )
    {
        vote.set_slot_0_hash( std::string( 32, '\x01' ) );
        vote.set_slot_1_hash( std::string( 32, '\x02' ) );
        vote.set_slot_2_hash( std::string( 32, '\x03' ) );
    } );
    const auto outpoint = MakeOutpoint();
    auto subject = MakeMintSubject( outpoint );
    auto proposal = manager->CreateProposal(
        subject, account_->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal );
    auto vote = manager->CreateVote(
        proposal.value().proposal_id(), account_->GetAddress(), true,
        [this]( std::vector<uint8_t> bytes ) { return account_->Sign( std::move( bytes ) ); } );
    ASSERT_TRUE( vote );
    auto certificate = manager->CreateCertificate( proposal.value(), { vote.value() } );
    ASSERT_TRUE( certificate );
    auto winner = sgns::ConsensusBurnReservationTestAccess::Winner( subject );
    ASSERT_TRUE( winner );
    std::string bytes;
    ASSERT_TRUE( certificate.value().SerializeToString( &bytes ) );
    std::vector<sgns::crdt::GlobalDB::DataPair> records;
    records.emplace_back( sgns::crdt::HierarchicalKey{ "/cert/v2/slot/" + SlotFor( outpoint ) },
                          BufferOf( bytes ) );
    records.emplace_back( sgns::crdt::HierarchicalKey{ "/cert/v2/tx/" + winner.value() },
                          BufferOf( SlotFor( outpoint ) ) );
    ASSERT_TRUE( db_->Put( records, {} ).has_value() );
    CloseManagers();

    sgns::ConsensusStateStore store( db_->GetDataStore() );
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    auto protected_burn = store.GetBurnReservation( SlotFor( outpoint ) );
    ASSERT_TRUE( protected_burn && protected_burn.value() );
    EXPECT_EQ( protected_burn.value()->state(),
               sgns::ConsensusStateStore::BurnReservationRecord::FINALIZED_PENDING_APPLICATION );
    EXPECT_EQ( protected_burn.value()->proposal_id(), proposal.value().proposal_id() );
    auto process = store.GetProcess( SlotFor( outpoint ) );
    ASSERT_TRUE( process && process.value() );
    EXPECT_EQ( process.value()->state(), sgns::ConsensusStateStore::ProcessRecord::PENDING );
}

TEST_F( ConsensusBurnReservationHarness, RestartActiveVoteHorizonExtendsReservedProtectionAtEquality )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint();
    auto subject = MakeMintSubject( outpoint );
    auto proposal = manager->CreateProposal(
        subject, account_->GetAddress(), registry->GetRegistryCid(), registry->GetRegistryEpoch() );
    ASSERT_TRUE( proposal );
    auto vote = manager->CreateVote(
        proposal.value().proposal_id(), account_->GetAddress(), true,
        [this]( std::vector<uint8_t> bytes ) { return account_->Sign( std::move( bytes ) ); } );
    ASSERT_TRUE( vote );
    sgns::ConsensusMessage envelope;
    *envelope.mutable_vote() = vote.value();
    sgns::ConsensusStateStore::VoteRecord record;
    record.set_schema_version( 2 );
    record.set_state( sgns::ConsensusStateStore::VoteRecord::ACTIVE );
    record.set_slot_id( SlotFor( outpoint ) );
    record.set_proposal_id( proposal.value().proposal_id() );
    record.set_validator_id( account_->GetAddress() );
    ASSERT_TRUE( vote.value().SerializeToString( record.mutable_signed_vote_bytes() ) );
    ASSERT_TRUE( proposal.value().SerializeToString( record.mutable_signed_proposal_bytes() ) );
    ASSERT_TRUE( envelope.SerializeToString( record.mutable_outbound_envelope_bytes() ) );
    record.set_registry_cid( proposal.value().registry_cid() );
    record.set_registry_epoch( proposal.value().registry_epoch() );
    record.set_generation( 1 );
    record.set_created_at_ms( vote.value().timestamp() );
    record.set_acceptance_horizon_ms( vote.value().timestamp() + 300'000 );
    CloseManagers();

    sgns::ConsensusStateStore store( db_->GetDataStore() );
    ASSERT_TRUE( store.CreateOrJoinBurnReservation(
        SlotFor( outpoint ), outpoint, record.acceptance_horizon_ms() - 1, vote.value().timestamp() ) );
    ASSERT_TRUE( store.PutActiveVote( record ) );
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    auto durable = store.GetBurnReservation( SlotFor( outpoint ) );
    ASSERT_TRUE( durable && durable.value() );
    EXPECT_EQ( durable.value()->candidate_acceptance_horizon_ms(), record.acceptance_horizon_ms() );
}

TEST_F( ConsensusBurnReservationHarness, CertificateOnlyFinalProtectionPrecedesHandlerAndCleanup )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint( 31 );
    auto certificate = MakeMintCertificate( manager, registry, outpoint, std::string( 64, '7' ) );
    ASSERT_TRUE( certificate );
    const auto slot = SlotFor( outpoint );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    std::vector<std::string> stages;
    sgns::ConsensusBurnReservationTestAccess::ObserveFinalization(
        manager, [&]( std::string_view stage ) { stages.emplace_back( stage ); } );
    std::atomic<uint64_t> handler_count{ 0 };
    std::atomic<uint64_t> cleanup_count{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateApplicationHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &winner, const sgns::ConsensusManager::Certificate &observed )
            -> outcome::result<sgns::ConsensusManager::ApplicationDisposition>
        {
            ++handler_count;
            auto protected_burn = store.GetBurnReservation( slot );
            EXPECT_TRUE( protected_burn && protected_burn.value() );
            EXPECT_EQ( protected_burn.value()->state(),
                       sgns::ConsensusStateStore::BurnReservationRecord::FINALIZED_PENDING_APPLICATION );
            EXPECT_EQ( protected_burn.value()->proposal_id(), observed.proposal_id() );
            EXPECT_EQ( protected_burn.value()->winner_id(), winner );
            return sgns::ConsensusManager::ApplicationDisposition::Applied;
        } ) );
    ASSERT_TRUE( manager->RegisterProposalCleanupHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string & )
        {
            ++cleanup_count;
            auto protected_burn = store.GetBurnReservation( slot );
            EXPECT_TRUE( protected_burn && protected_burn.value() );
        } ) );

    EXPECT_EQ( sgns::ConsensusBurnReservationTestAccess::Finalize( manager, certificate.value() ),
               sgns::ConsensusManager::FinalizeResult::Applied );
    EXPECT_EQ( handler_count.load(), 1U );
    EXPECT_EQ( cleanup_count.load(), 1U );
    EXPECT_NE( std::find( stages.begin(), stages.end(), "burn-finalized" ), stages.end() );
}

TEST_F( ConsensusBurnReservationHarness, FinalReservationWriteFailureInvokesNoHandlerOrCleanup )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint( 32 );
    auto certificate = MakeMintCertificate( manager, registry, outpoint, std::string( 64, '6' ) );
    ASSERT_TRUE( certificate );
    std::atomic<uint64_t> handler_count{ 0 };
    std::atomic<uint64_t> cleanup_count{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateApplicationHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::ApplicationDisposition>
        { ++handler_count; return sgns::ConsensusManager::ApplicationDisposition::Applied; } ) );
    ASSERT_TRUE( manager->RegisterProposalCleanupHandler(
        sgns::NONCE_SUBJECT_TYPE, [&]( const std::string & ) { ++cleanup_count; } ) );
    sgns::ConsensusBurnReservationTestAccess::FailAdmissionCommits( manager );

    EXPECT_EQ( sgns::ConsensusBurnReservationTestAccess::Finalize( manager, certificate.value() ),
               sgns::ConsensusManager::FinalizeResult::StorageFailure );
    EXPECT_EQ( handler_count.load(), 0U );
    EXPECT_EQ( cleanup_count.load(), 0U );
    EXPECT_TRUE( manager->GetCertificateBySlotId( SlotFor( outpoint ) ) );
    EXPECT_FALSE( sgns::ConsensusStateStore( db_->GetDataStore() )
                      .GetBurnReservation( SlotFor( outpoint ) ).value().has_value() );
}

TEST_F( ConsensusBurnReservationHarness, FinalRetryableApplicationRetainsExactWinnerForRetry )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint( 33 );
    auto certificate = MakeMintCertificate( manager, registry, outpoint, std::string( 64, '5' ) );
    ASSERT_TRUE( certificate );
    std::atomic<uint64_t> attempts{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateApplicationHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::ApplicationDisposition>
        {
            return ++attempts == 1 ? sgns::ConsensusManager::ApplicationDisposition::Retryable
                                   : sgns::ConsensusManager::ApplicationDisposition::Applied;
        } ) );
    EXPECT_EQ( sgns::ConsensusBurnReservationTestAccess::Finalize( manager, certificate.value() ),
               sgns::ConsensusManager::FinalizeResult::PendingApplication );
    auto pending = sgns::ConsensusStateStore( db_->GetDataStore() ).GetBurnReservation( SlotFor( outpoint ) );
    ASSERT_TRUE( pending && pending.value() );
    EXPECT_EQ( pending.value()->proposal_id(), certificate.value().proposal_id() );
    manager->Close();
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    ASSERT_TRUE( restarted->RegisterCertificateApplicationHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::ApplicationDisposition>
        { ++attempts; return sgns::ConsensusManager::ApplicationDisposition::Applied; } ) );
    EXPECT_EQ( attempts.load(), 2U );
    auto exact = sgns::ConsensusStateStore( db_->GetDataStore() ).GetBurnReservation( SlotFor( outpoint ) );
    ASSERT_TRUE( exact && exact.value() );
    EXPECT_EQ( exact.value()->proposal_id(), certificate.value().proposal_id() );
}

TEST_F( ConsensusBurnReservationHarness, FinalIrreconcilableApplicationPersistsSafetyErrorAndStopsRetry )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint( 34 );
    auto certificate = MakeMintCertificate( manager, registry, outpoint, std::string( 64, '4' ) );
    ASSERT_TRUE( certificate );
    std::atomic<uint64_t> attempts{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateApplicationHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::ApplicationDisposition>
        { ++attempts; return sgns::ConsensusManager::ApplicationDisposition::Irreconcilable; } ) );

    EXPECT_EQ( sgns::ConsensusBurnReservationTestAccess::Finalize( manager, certificate.value() ),
               sgns::ConsensusManager::FinalizeResult::AlreadyFinalized );
    auto safety = sgns::ConsensusStateStore( db_->GetDataStore() ).GetBurnReservation( SlotFor( outpoint ) );
    ASSERT_TRUE( safety && safety.value() );
    EXPECT_EQ( safety.value()->state(), sgns::ConsensusStateStore::BurnReservationRecord::SAFETY_ERROR );
    EXPECT_FALSE( safety.value()->safety_error().empty() );
    EXPECT_EQ( sgns::ConsensusBurnReservationTestAccess::Finalize( manager, certificate.value() ),
               sgns::ConsensusManager::FinalizeResult::AlreadyFinalized );
    EXPECT_EQ( attempts.load(), 1U );
}

TEST_F( ConsensusBurnReservationHarness, FinalDuplicateIngressSharesOneExactWinnerHandlerLease )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    const auto outpoint = MakeOutpoint( 35 );
    auto certificate = MakeMintCertificate( manager, registry, outpoint, std::string( 64, '3' ) );
    ASSERT_TRUE( certificate );
    std::atomic<uint64_t> attempts{ 0 };
    ASSERT_TRUE( manager->RegisterCertificateApplicationHandler(
        sgns::NONCE_SUBJECT_TYPE,
        [&]( const std::string &, const sgns::ConsensusManager::Certificate & )
            -> outcome::result<sgns::ConsensusManager::ApplicationDisposition>
        { ++attempts; return sgns::ConsensusManager::ApplicationDisposition::Applied; } ) );
    const std::array sources = {
        sgns::ConsensusManager::DeliverySource::Local,
        sgns::ConsensusManager::DeliverySource::PubSub,
        sgns::ConsensusManager::DeliverySource::CRDT,
        sgns::ConsensusManager::DeliverySource::Recovery };
    std::vector<std::thread> workers;
    for ( auto source : sources )
        workers.emplace_back( [&, source]()
        { (void) sgns::ConsensusBurnReservationTestAccess::Finalize( manager, certificate.value(), source ); } );
    for ( auto &worker : workers ) worker.join();

    EXPECT_EQ( attempts.load(), 1U );
    auto protected_burn = sgns::ConsensusStateStore( db_->GetDataStore() )
                              .GetBurnReservation( SlotFor( outpoint ) );
    ASSERT_TRUE( protected_burn && protected_burn.value() );
    EXPECT_EQ( protected_burn.value()->proposal_id(), certificate.value().proposal_id() );
}
