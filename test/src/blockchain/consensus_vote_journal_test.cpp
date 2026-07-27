/**
 * @file consensus_vote_journal_test.cpp
 * @brief Deterministic persistent-store harness for Phase 10 vote-lock tests.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "blockchain/Consensus.hpp"
#include "blockchain/ConsensusStateStore.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "storage/database_error.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    /**
     * Test-only access surface for the private durable-vote hooks added by
     * later Phase 10 plans. Production fault-injection APIs are intentionally
     * not part of this harness.
     */
    class ConsensusVoteJournalTestAccess
    {
    public:
        static constexpr std::string_view Scope()
        {
            return "durable consensus vote journal";
        }

        static void FailQueries( ConsensusStateStore &store )
        {
            store.query_ = []( const base::Buffer & ) -> outcome::result<storage::rocksdb::QueryResult>
            { return outcome::failure( storage::DatabaseError::IO_ERROR ); };
        }
    };
} // namespace sgns

namespace
{
    constexpr auto kSystemClockNow =
        std::chrono::system_clock::time_point( std::chrono::milliseconds( 1'750'000'000'000LL ) );
    constexpr auto kSteadyClockNow =
        std::chrono::steady_clock::time_point( std::chrono::milliseconds( 42'000 ) );

    std::vector<uint8_t> DummySignature( std::vector<uint8_t> payload )
    {
        payload.push_back( 0x10 );
        return payload;
    }

    std::string HashText( char value )
    {
        return std::string( 64, value );
    }

    sgns::ConsensusStateStore::VoteRecord MakeVoteRecord( std::string validator = "validator-vote-journal",
                                                           char        slot_char = '1',
                                                           char        proposal_char = '2',
                                                           uint64_t    generation = 1 )
    {
        const auto slot = HashText( slot_char );
        const auto proposal_id = HashText( proposal_char );

        sgns::ConsensusVote vote;
        vote.set_proposal_id( proposal_id );
        vote.set_voter_id( validator );
        vote.set_approve( true );
        vote.set_timestamp( 1'750'000'000'000ULL );
        vote.set_signature( "signed-vote\0bytes", 17 );
        std::string signed_vote;
        EXPECT_TRUE( vote.SerializeToString( &signed_vote ) );

        sgns::ConsensusMessage envelope;
        *envelope.mutable_vote() = vote;
        std::string envelope_bytes;
        EXPECT_TRUE( envelope.SerializeToString( &envelope_bytes ) );

        sgns::ConsensusProposal proposal;
        proposal.set_proposal_id( proposal_id );
        proposal.set_proposer_id( "proposer" );
        proposal.set_timestamp( 1'750'000'000'000ULL );
        proposal.set_registry_cid( "registry-cid" );
        proposal.set_registry_epoch( 7 );
        proposal.set_signature( "proposal-signature" );
        std::string proposal_bytes;
        EXPECT_TRUE( proposal.SerializeToString( &proposal_bytes ) );

        sgns::ConsensusStateStore::VoteRecord record;
        record.set_schema_version( 2 );
        record.set_state( sgns::ConsensusStateStore::VoteRecord::ACTIVE );
        record.set_slot_id( slot );
        record.set_proposal_id( proposal_id );
        record.set_validator_id( validator );
        record.set_signed_vote_bytes( signed_vote );
        record.set_outbound_envelope_bytes( envelope_bytes );
        record.set_signed_proposal_bytes( proposal_bytes );
        record.set_registry_cid( "registry-cid" );
        record.set_registry_epoch( 7 );
        record.set_generation( generation );
        record.set_created_at_ms( 1'750'000'000'000ULL );
        record.set_acceptance_horizon_ms( 1'750'000'300'000ULL );
        return record;
    }

    sgns::base::Buffer AsBuffer( std::string_view value )
    {
        sgns::base::Buffer out;
        out.put( value );
        return out;
    }

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

    struct VoteJournalCounters
    {
        std::atomic<uint64_t> signer{ 0 };
        std::atomic<uint64_t> raw_publish{ 0 };
        std::atomic<uint64_t> subscription{ 0 };
        std::atomic<uint64_t> timer{ 0 };
        std::atomic<uint64_t> certificate_filter{ 0 };

        void Reset()
        {
            signer.store( 0 );
            raw_publish.store( 0 );
            subscription.store( 0 );
            timer.store( 0 );
            certificate_filter.store( 0 );
        }
    };

    class ConsensusVoteJournalHarness : public test::CRDTFixture
    {
    public:
        ConsensusVoteJournalHarness() : CRDTFixture( "consensus_vote_journal_test" ) {}

    protected:
        std::shared_ptr<sgns::ValidatorRegistry> MakeRegistry()
        {
            auto registry = sgns::ValidatorRegistry::New(
                db_,
                1,
                1,
                sgns::ValidatorRegistry::WeightConfig{},
                validator_id_,
                []( const std::string &, std::function<void( outcome::result<std::string> )> callback )
                { callback( outcome::failure( std::errc::not_supported ) ); } );
            EXPECT_TRUE( registry );
            if ( !registry )
            {
                return nullptr;
            }

            EXPECT_TRUE( registry->StoreGenesisRegistry( { validator_id_ }, DummySignature ).has_value() );
            ASSERT_WAIT_FOR_CONDITION(
                [&registry]()
                {
                    auto loaded = registry->LoadCurrentRegistry();
                    return loaded.has_value() && !registry->GetRegistryCid().empty();
                },
                std::chrono::milliseconds( 2000 ),
                "vote journal registry initialized",
                nullptr );
            return registry;
        }

        std::shared_ptr<sgns::ConsensusManager> MakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry )
        {
            auto manager = sgns::ConsensusManager::New(
                registry,
                db_,
                pubs_,
                [this]( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
                {
                    ++counters_.signer;
                    return DummySignature( std::move( payload ) );
                },
                validator_id_ );
            EXPECT_TRUE( manager );
            if ( manager )
            {
                managers_.push_back( manager );
            }
            return manager;
        }

        void CloseManagers()
        {
            for ( auto &manager : managers_ )
            {
                if ( manager )
                {
                    manager->Close();
                }
            }
            managers_.clear();
        }

        std::shared_ptr<sgns::storage::rocksdb> CloseGlobalDBAndReopenStorage()
        {
            CloseManagers();
            db_->ShutdownNow();
            db_.reset();

            auto reopened = sgns::storage::rocksdb::create( db_path_ );
            EXPECT_TRUE( reopened.has_value() );
            return reopened.has_value() ? reopened.value() : nullptr;
        }

        VoteJournalCounters counters_;
        const std::chrono::system_clock::time_point system_clock_now_{ kSystemClockNow };
        const std::chrono::steady_clock::time_point steady_clock_now_{ kSteadyClockNow };

    private:
        const std::string validator_id_{ "validator-vote-journal" };
        std::vector<std::shared_ptr<sgns::ConsensusManager>> managers_;
    };
} // namespace

TEST_F( ConsensusVoteJournalHarness, PersistentDatabaseReopensCleanly )
{
    const ScopedReset reset_counters( [this]() { counters_.Reset(); } );
    ASSERT_LT( steady_clock_now_.time_since_epoch(), system_clock_now_.time_since_epoch() );

    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );

    sgns::base::Buffer key;
    key.put( "/consensus/local/v2/harness/reopen" );
    sgns::base::Buffer value;
    value.put( "same-path-marker" );
    auto first_store = db_->GetDataStore();
    ASSERT_TRUE( first_store );
    ASSERT_TRUE( first_store->put( key, value ).has_value() );
    first_store.reset();
    manager.reset();
    registry.reset();

    auto reopened = CloseGlobalDBAndReopenStorage();
    ASSERT_TRUE( reopened );
    auto stored = reopened->get( key );
    ASSERT_TRUE( stored.has_value() );
    EXPECT_EQ( stored.value().toString(), "same-path-marker" );
}

TEST_F( ConsensusVoteJournalHarness, VoteBytesAndPrivateNamespaceSurviveExactReopen )
{
    auto datastore = db_->GetDataStore();
    ASSERT_TRUE( datastore );
    sgns::ConsensusStateStore store( datastore );
    const auto record = MakeVoteRecord();
    ASSERT_TRUE( store.PutActiveVote( record ).has_value() );
    ASSERT_TRUE( store.UpdatePublication( record.validator_id(), record.slot_id(), 1'750'000'000'100ULL, true )
                     .has_value() );

    auto local_raw = datastore->query( AsBuffer( "/consensus/local/v2/" ) );
    ASSERT_TRUE( local_raw.has_value() );
    ASSERT_EQ( local_raw.value().size(), 1 );
    EXPECT_EQ( local_raw.value().begin()->first.toString(),
               sgns::ConsensusStateStore::VoteKey( record.validator_id(), record.slot_id() ) );
    auto crdt_raw = db_->QueryKeyValues( "/consensus/local/v2/" );
    ASSERT_TRUE( crdt_raw.has_value() );
    EXPECT_TRUE( crdt_raw.value().empty() );

    const auto vote_bytes = record.signed_vote_bytes();
    const auto envelope_bytes = record.outbound_envelope_bytes();
    datastore.reset();
    auto reopened = CloseGlobalDBAndReopenStorage();
    ASSERT_TRUE( reopened );
    sgns::ConsensusStateStore reopened_store( reopened );
    auto restored = reopened_store.GetVote( record.validator_id(), record.slot_id() );
    ASSERT_TRUE( restored.has_value() );
    ASSERT_TRUE( restored.value().has_value() );
    EXPECT_EQ( restored.value()->signed_vote_bytes(), vote_bytes );
    EXPECT_EQ( restored.value()->outbound_envelope_bytes(), envelope_bytes );
    EXPECT_EQ( restored.value()->publication_count(), 1 );
}

TEST_F( ConsensusVoteJournalHarness, ActiveVoteIsExactIdempotentAndRejectsOccupiedIdentity )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    auto record = MakeVoteRecord();
    ASSERT_TRUE( store.PutActiveVote( record ).has_value() );
    EXPECT_TRUE( store.PutActiveVote( record ).has_value() );

    auto other_proposal = MakeVoteRecord( record.validator_id(), '1', '3', 2 );
    auto conflict = store.PutActiveVote( other_proposal );
    ASSERT_TRUE( conflict.has_error() );
    EXPECT_EQ( conflict.error(), sgns::ConsensusStateStoreError::Conflict );
}

TEST_F( ConsensusVoteJournalHarness, RetirementIsDurableBeforeNextGeneration )
{
    auto datastore = db_->GetDataStore();
    auto record = MakeVoteRecord();
    {
        sgns::ConsensusStateStore store( datastore );
        ASSERT_TRUE( store.PutActiveVote( record ).has_value() );
        EXPECT_TRUE( store.RetireVote( record.validator_id(), record.slot_id(), record.acceptance_horizon_ms() )
                         .has_error() );
        ASSERT_TRUE( store.RetireVote( record.validator_id(),
                                      record.slot_id(),
                                      record.acceptance_horizon_ms() + 1 )
                         .has_value() );
    }
    datastore.reset();
    auto reopened = CloseGlobalDBAndReopenStorage();
    ASSERT_TRUE( reopened );
    sgns::ConsensusStateStore reopened_store( reopened );
    auto retired = reopened_store.GetVote( record.validator_id(), record.slot_id() );
    ASSERT_TRUE( retired.has_value() && retired.value().has_value() );
    EXPECT_EQ( retired.value()->state(), sgns::ConsensusStateStore::VoteRecord::RETIRED );
    auto next = MakeVoteRecord( record.validator_id(), '1', '3', 2 );
    EXPECT_TRUE( reopened_store.PutActiveVote( next ).has_value() );
}

TEST_F( ConsensusVoteJournalHarness, ProcessingLifecycleIsStrictAndIdentityBound )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    sgns::ConsensusStateStore::ProcessRecord process;
    process.set_schema_version( 2 );
    process.set_state( sgns::ConsensusStateStore::ProcessRecord::PENDING );
    process.set_slot_id( HashText( '4' ) );
    process.set_certificate_digest( HashText( '5' ) );
    process.set_proposal_id( HashText( '6' ) );
    process.set_winner_id( "winner-identity" );
    process.set_updated_at_ms( 100 );
    ASSERT_TRUE( store.PutPendingProcess( process ).has_value() );
    EXPECT_TRUE( store.PutPendingProcess( process ).has_value() );
    ASSERT_TRUE( store.MarkProcessing( process.slot_id(), 300, 200 ).has_value() );
    ASSERT_TRUE( store.MarkComplete( process.slot_id(), 400 ).has_value() );
    EXPECT_TRUE( store.MarkComplete( process.slot_id(), 500 ).has_value() );
    auto restored = store.GetProcess( process.slot_id() );
    ASSERT_TRUE( restored.has_value() && restored.value().has_value() );
    EXPECT_EQ( restored.value()->state(), sgns::ConsensusStateStore::ProcessRecord::COMPLETE );
    EXPECT_EQ( restored.value()->attempt_count(), 1 );

    auto mismatch = process;
    mismatch.set_certificate_digest( HashText( '7' ) );
    auto conflict = store.PutPendingProcess( mismatch );
    ASSERT_TRUE( conflict.has_error() );
    EXPECT_EQ( conflict.error(), sgns::ConsensusStateStoreError::Conflict );
}

TEST_F( ConsensusVoteJournalHarness, ConflictPairIsSortedDeduplicatedAndBatchedWithSafety )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    sgns::ConsensusStateStore::ConflictRecord conflict;
    conflict.set_schema_version( 2 );
    conflict.set_slot_id( HashText( '8' ) );
    conflict.set_low_certificate_digest( HashText( 'b' ) );
    conflict.set_high_certificate_digest( HashText( 'a' ) );
    conflict.set_low_proposal_id( HashText( 'd' ) );
    conflict.set_high_proposal_id( HashText( 'c' ) );
    conflict.set_sources_bitset( 1 );
    conflict.set_first_seen_at_ms( 100 );
    conflict.set_last_seen_at_ms( 100 );
    conflict.set_observation_count( 1 );

    sgns::ConsensusStateStore::SafetyRecord safety;
    safety.set_schema_version( 2 );
    safety.set_state( sgns::ConsensusStateStore::SafetyRecord::SAFETY_VIOLATION );
    safety.set_slot_id( conflict.slot_id() );
    safety.set_authoritative_certificate_digest( HashText( 'a' ) );
    safety.set_authoritative_proposal_id( HashText( 'c' ) );
    safety.set_updated_at_ms( 100 );
    ASSERT_TRUE( store.RecordConflictAndSafety( conflict, safety ).has_value() );

    conflict.set_low_certificate_digest( HashText( 'a' ) );
    conflict.set_high_certificate_digest( HashText( 'b' ) );
    conflict.set_low_proposal_id( HashText( 'c' ) );
    conflict.set_high_proposal_id( HashText( 'd' ) );
    conflict.set_sources_bitset( 2 );
    conflict.set_last_seen_at_ms( 200 );
    ASSERT_TRUE( store.RecordConflictAndSafety( conflict, safety ).has_value() );

    auto conflicts = store.ScanConflicts();
    auto safeties = store.ScanSafety();
    ASSERT_TRUE( conflicts.has_value() && safeties.has_value() );
    ASSERT_EQ( conflicts.value().size(), 1 );
    ASSERT_EQ( safeties.value().size(), 1 );
    EXPECT_EQ( conflicts.value()[0].low_certificate_digest(), HashText( 'a' ) );
    EXPECT_EQ( conflicts.value()[0].high_certificate_digest(), HashText( 'b' ) );
    EXPECT_EQ( conflicts.value()[0].sources_bitset(), 3 );
    EXPECT_EQ( conflicts.value()[0].observation_count(), 2 );
    EXPECT_EQ( conflicts.value()[0].GetDescriptor()->FindFieldByName( "certificate" ), nullptr );

    auto raw = datastore->query( AsBuffer( "/consensus/local/v2/" ) );
    ASSERT_TRUE( raw.has_value() );
    EXPECT_EQ( raw.value().size(), 2 );
}

enum class VoteCorruption
{
    MalformedValue,
    UnknownVersion,
    UnknownState,
    UnknownField,
    KeyMismatch,
    EnvelopeMismatch,
};

class ConsensusVoteCorruptionTest : public ConsensusVoteJournalHarness,
                                    public testing::WithParamInterface<VoteCorruption>
{};

TEST_P( ConsensusVoteCorruptionTest, ScanFailsClosed )
{
    auto datastore = db_->GetDataStore();
    auto record = MakeVoteRecord();
    std::string key = sgns::ConsensusStateStore::VoteKey( record.validator_id(), record.slot_id() );
    std::string value;
    switch ( GetParam() )
    {
        case VoteCorruption::MalformedValue:
            value = "\x80";
            break;
        case VoteCorruption::UnknownVersion:
            record.set_schema_version( 99 );
            ASSERT_TRUE( record.SerializeToString( &value ) );
            break;
        case VoteCorruption::UnknownState:
            record.set_state( static_cast<sgns::ConsensusStateStore::VoteRecord::State>( 99 ) );
            ASSERT_TRUE( record.SerializeToString( &value ) );
            break;
        case VoteCorruption::UnknownField:
            record.GetReflection()->MutableUnknownFields( &record )->AddVarint( 99, 1 );
            ASSERT_TRUE( record.SerializeToString( &value ) );
            break;
        case VoteCorruption::KeyMismatch:
            key = sgns::ConsensusStateStore::VoteKey( record.validator_id(), HashText( '9' ) );
            ASSERT_TRUE( record.SerializeToString( &value ) );
            break;
        case VoteCorruption::EnvelopeMismatch:
            record.set_outbound_envelope_bytes( "not-an-envelope" );
            ASSERT_TRUE( record.SerializeToString( &value ) );
            break;
    }
    ASSERT_TRUE( datastore->put( AsBuffer( key ), AsBuffer( value ) ).has_value() );
    sgns::ConsensusStateStore store( datastore );
    auto scan = store.ScanVotes();
    EXPECT_TRUE( scan.has_error() );
    if ( scan.has_error() ) EXPECT_EQ( scan.error(), sgns::ConsensusStateStoreError::Integrity );
}

INSTANTIATE_TEST_SUITE_P( MalformedLocalState,
                          ConsensusVoteCorruptionTest,
                          testing::Values( VoteCorruption::MalformedValue,
                                           VoteCorruption::UnknownVersion,
                                           VoteCorruption::UnknownState,
                                           VoteCorruption::UnknownField,
                                           VoteCorruption::KeyMismatch,
                                           VoteCorruption::EnvelopeMismatch ) );

TEST_F( ConsensusVoteJournalHarness, QueryErrorsAreNeverReportedAsEmptyScans )
{
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    sgns::ConsensusVoteJournalTestAccess::FailQueries( store );
    auto votes = store.ScanVotes();
    auto processes = store.ScanProcesses();
    auto conflicts = store.ScanConflicts();
    auto safety = store.ScanSafety();
    EXPECT_TRUE( votes.has_error() );
    EXPECT_TRUE( processes.has_error() );
    EXPECT_TRUE( conflicts.has_error() );
    EXPECT_TRUE( safety.has_error() );
}
