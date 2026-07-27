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
#include "account/GeniusAccount.hpp"
#include "account/TokenID.hpp"
#include "base/hexutil.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
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

        static void ObserveStartup( std::function<void( std::string_view )> observer )
        {
            ConsensusManager::startup_event_observer_ = std::move( observer );
        }

        static void OverrideRawPublish( std::function<outcome::result<void>( std::string_view )> publisher )
        {
            ConsensusManager::raw_publish_override_ = std::move( publisher );
        }

        static void FailStartupQueries()
        {
            ConsensusManager::startup_local_query_override_ =
                []( const base::Buffer & ) -> outcome::result<storage::rocksdb::QueryResult>
            { return outcome::failure( storage::DatabaseError::IO_ERROR ); };
        }

        static void ResetStartupHooks()
        {
            ConsensusManager::startup_event_observer_ = {};
            ConsensusManager::raw_publish_override_ = {};
            ConsensusManager::startup_local_query_override_ = {};
        }

        static outcome::result<std::string> Slot( const ConsensusManager::Proposal &proposal )
        {
            return ConsensusManager::GetSlotKey( proposal );
        }

        static outcome::result<std::string> Winner( const ConsensusManager::Proposal &proposal )
        {
            return ConsensusManager::GetSubjectHash( proposal.subject() );
        }

        static std::chrono::milliseconds VoteSelectionWindow( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager->config_.vote_selection_window;
        }

        static bool SafetyStopped( const std::shared_ptr<ConsensusManager> &manager, const std::string &slot )
        {
            return manager->restored_safety_slots_.count( slot ) != 0;
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
        ~ConsensusVoteJournalHarness() override
        {
            CloseManagers();
        }

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
                    "c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00c0ffee00",
                    boost::filesystem::path( db_path_ ) / "vote-journal-account",
                    false );
                EXPECT_TRUE( account_ );
                if ( account_ ) validator_id_ = account_->GetAddress();
            }
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
            const std::shared_ptr<sgns::ValidatorRegistry> &registry,
            sgns::ConsensusConfig config = sgns::ConsensusConfig{} )
        {
            auto manager = sgns::ConsensusManager::New(
                registry,
                db_,
                pubs_,
                [this]( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
                {
                    ++counters_.signer;
                    return account_ ? outcome::result<std::vector<uint8_t>>( account_->Sign( payload ) )
                                    : outcome::result<std::vector<uint8_t>>(
                                          outcome::failure( std::errc::owner_dead ) );
                },
                validator_id_,
                "",
                config );
            EXPECT_TRUE( manager );
            if ( manager )
            {
                managers_.push_back( manager );
            }
            return manager;
        }

        std::shared_ptr<sgns::ConsensusManager> TryMakeManager(
            const std::shared_ptr<sgns::ValidatorRegistry> &registry )
        {
            return sgns::ConsensusManager::New(
                registry,
                db_,
                pubs_,
                [this]( std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
                {
                    ++counters_.signer;
                    return account_->Sign( std::move( payload ) );
                },
                validator_id_ );
        }

        sgns::ConsensusStateStore::VoteRecord MakeSignedVoteRecord(
            const std::shared_ptr<sgns::ConsensusManager> &manager,
            const std::shared_ptr<sgns::ValidatorRegistry> &registry )
        {
            auto subject = sgns::ConsensusManager::CreateGenericSubject(
                validator_id_, "sgns.vote-journal.startup.v1", { 1, 2, 3 } );
            EXPECT_TRUE( subject );
            auto proposal = manager->CreateProposal( subject.value(),
                                                     validator_id_,
                                                     registry->GetRegistryCid(),
                                                     registry->GetRegistryEpoch() );
            EXPECT_TRUE( proposal );
            const auto &proposal_value = proposal.value();
            auto vote = manager->CreateVote(
                proposal_value.proposal_id(),
                validator_id_,
                true,
                [this]( std::vector<uint8_t> bytes ) { return account_->Sign( std::move( bytes ) ); } );
            EXPECT_TRUE( vote );
            const auto &vote_value = vote.value();
            auto slot = sgns::ConsensusVoteJournalTestAccess::Slot( proposal_value );
            EXPECT_TRUE( slot );
            sgns::ConsensusMessage envelope;
            *envelope.mutable_vote() = vote_value;
            std::string vote_bytes;
            std::string proposal_bytes;
            std::string envelope_bytes;
            EXPECT_TRUE( vote_value.SerializeToString( &vote_bytes ) );
            EXPECT_TRUE( proposal_value.SerializeToString( &proposal_bytes ) );
            EXPECT_TRUE( envelope.SerializeToString( &envelope_bytes ) );
            sgns::ConsensusStateStore::VoteRecord record;
            record.set_schema_version( 2 );
            record.set_state( sgns::ConsensusStateStore::VoteRecord::ACTIVE );
            record.set_slot_id( slot.value() );
            record.set_proposal_id( proposal_value.proposal_id() );
            record.set_validator_id( validator_id_ );
            record.set_signed_vote_bytes( vote_bytes );
            record.set_outbound_envelope_bytes( envelope_bytes );
            record.set_signed_proposal_bytes( proposal_bytes );
            record.set_registry_cid( proposal_value.registry_cid() );
            record.set_registry_epoch( proposal_value.registry_epoch() );
            record.set_generation( 1 );
            record.set_created_at_ms( vote_value.timestamp() );
            record.set_acceptance_horizon_ms( vote_value.timestamp() + 300'000 );
            return record;
        }

        sgns::ConsensusManager::Certificate PersistCertificate(
            const std::shared_ptr<sgns::ConsensusManager> &manager,
            const sgns::ConsensusStateStore::VoteRecord &record )
        {
            sgns::ConsensusManager::Proposal proposal;
            sgns::ConsensusManager::Vote vote;
            EXPECT_TRUE( proposal.ParseFromString( record.signed_proposal_bytes() ) );
            EXPECT_TRUE( vote.ParseFromString( record.signed_vote_bytes() ) );
            auto certificate = manager->CreateCertificate( proposal, { vote } );
            EXPECT_TRUE( certificate );
            auto winner = sgns::ConsensusVoteJournalTestAccess::Winner( proposal );
            EXPECT_TRUE( winner );
            std::string bytes;
            EXPECT_TRUE( certificate.value().SerializeToString( &bytes ) );
            std::vector<sgns::crdt::GlobalDB::DataPair> records;
            records.emplace_back( sgns::crdt::HierarchicalKey{ "/cert/v2/slot/" + record.slot_id() },
                                  AsBuffer( bytes ) );
            records.emplace_back( sgns::crdt::HierarchicalKey{ "/cert/v2/tx/" + winner.value() },
                                  AsBuffer( record.slot_id() ) );
            EXPECT_TRUE( db_->Put( records, {} ).has_value() );
            return certificate.value();
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

    protected:
        std::string validator_id_{ "validator-vote-journal" };
        std::shared_ptr<sgns::GeniusAccount> account_;
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

TEST_F( ConsensusVoteJournalHarness, ConfigIsFixedBeforeManagerStartup )
{
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry, sgns::ConsensusConfig{ std::chrono::milliseconds( 1777 ) } );
    ASSERT_TRUE( manager );
    EXPECT_EQ( sgns::ConsensusVoteJournalTestAccess::VoteSelectionWindow( manager ),
               std::chrono::milliseconds( 1777 ) );
}

TEST_F( ConsensusVoteJournalHarness, RestartReplaysExactStoredEnvelopeWithoutSigning )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    auto record = MakeSignedVoteRecord( manager, registry );
    auto datastore = db_->GetDataStore();
    ASSERT_TRUE( datastore );
    sgns::ConsensusStateStore store( datastore );
    ASSERT_TRUE( store.PutActiveVote( record ).has_value() );
    manager->Close();
    manager.reset();
    counters_.Reset();

    std::string replayed;
    bool lock_existed_before_replay = false;
    std::vector<std::string> events;
    sgns::ConsensusVoteJournalTestAccess::ObserveStartup(
        [&]( std::string_view event ) { events.emplace_back( event ); } );
    sgns::ConsensusVoteJournalTestAccess::OverrideRawPublish(
        [&]( std::string_view bytes ) -> outcome::result<void>
        {
            auto durable = store.GetVote( record.validator_id(), record.slot_id() );
            lock_existed_before_replay = durable && durable.value().has_value();
            replayed.assign( bytes );
            ++counters_.raw_publish;
            return outcome::success();
        } );
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    EXPECT_TRUE( lock_existed_before_replay );
    EXPECT_EQ( replayed, record.outbound_envelope_bytes() );
    EXPECT_EQ( counters_.raw_publish.load(), 1 );
    EXPECT_EQ( counters_.signer.load(), 0 );
    EXPECT_EQ( events,
               ( std::vector<std::string>{ "restored", "subscribe", "certificate-filter", "timer", "publish" } ) );
}

TEST_F( ConsensusVoteJournalHarness, FailedRawReplayKeepsExactActiveRecordRetryable )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    auto record = MakeSignedVoteRecord( manager, registry );
    auto datastore = db_->GetDataStore();
    sgns::ConsensusStateStore store( datastore );
    ASSERT_TRUE( store.PutActiveVote( record ).has_value() );
    manager->Close();
    manager.reset();
    counters_.Reset();

    sgns::ConsensusVoteJournalTestAccess::OverrideRawPublish(
        [&]( std::string_view bytes ) -> outcome::result<void>
        {
            EXPECT_EQ( bytes, record.outbound_envelope_bytes() );
            ++counters_.raw_publish;
            return outcome::failure( std::errc::io_error );
        } );
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    auto retryable = store.GetVote( record.validator_id(), record.slot_id() );
    ASSERT_TRUE( retryable && retryable.value().has_value() );
    EXPECT_EQ( retryable.value()->state(), sgns::ConsensusStateStore::VoteRecord::ACTIVE );
    EXPECT_EQ( retryable.value()->outbound_envelope_bytes(), record.outbound_envelope_bytes() );
    EXPECT_EQ( retryable.value()->publication_count(), 1 );
    EXPECT_FALSE( retryable.value()->last_publication_succeeded() );
    EXPECT_EQ( counters_.signer.load(), 0 );
}

TEST_F( ConsensusVoteJournalHarness, StartupQueryFailureHasNoConsensusSideEffects )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    std::vector<std::string> events;
    sgns::ConsensusVoteJournalTestAccess::ObserveStartup(
        [&]( std::string_view event ) { events.emplace_back( event ); } );
    sgns::ConsensusVoteJournalTestAccess::FailStartupQueries();
    EXPECT_FALSE( TryMakeManager( registry ) );
    EXPECT_TRUE( events.empty() );
    EXPECT_EQ( counters_.signer.load(), 0 );
}

TEST_F( ConsensusVoteJournalHarness, MalformedVoteStateHasNoConsensusSideEffects )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto datastore = db_->GetDataStore();
    auto record = MakeVoteRecord( validator_id_ );
    ASSERT_TRUE( datastore->put( AsBuffer( sgns::ConsensusStateStore::VoteKey( validator_id_, record.slot_id() ) ),
                                 AsBuffer( "\x80" ) )
                     .has_value() );
    std::vector<std::string> events;
    sgns::ConsensusVoteJournalTestAccess::ObserveStartup(
        [&]( std::string_view event ) { events.emplace_back( event ); } );
    EXPECT_FALSE( TryMakeManager( registry ) );
    EXPECT_TRUE( events.empty() );
}

TEST_F( ConsensusVoteJournalHarness, WrongValidatorAndBadSignatureFailBeforeSideEffects )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto datastore = db_->GetDataStore();
    auto wrong = MakeVoteRecord( "another-validator" );
    std::string wrong_bytes;
    ASSERT_TRUE( wrong.SerializeToString( &wrong_bytes ) );
    ASSERT_TRUE( datastore->put( AsBuffer( sgns::ConsensusStateStore::VoteKey( wrong.validator_id(), wrong.slot_id() ) ),
                                 AsBuffer( wrong_bytes ) )
                     .has_value() );
    std::vector<std::string> events;
    sgns::ConsensusVoteJournalTestAccess::ObserveStartup(
        [&]( std::string_view event ) { events.emplace_back( event ); } );
    EXPECT_FALSE( TryMakeManager( registry ) );
    EXPECT_TRUE( events.empty() );
}

TEST_F( ConsensusVoteJournalHarness, PendingMarkerWithoutCertificateFailsBeforeSideEffects )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    sgns::ConsensusStateStore::ProcessRecord process;
    process.set_schema_version( 2 );
    process.set_state( sgns::ConsensusStateStore::ProcessRecord::PENDING );
    process.set_slot_id( HashText( 'a' ) );
    process.set_certificate_digest( HashText( 'b' ) );
    process.set_proposal_id( HashText( 'c' ) );
    process.set_winner_id( "missing-winner" );
    process.set_updated_at_ms( 100 );
    ASSERT_TRUE( store.PutPendingProcess( process ).has_value() );
    std::vector<std::string> events;
    sgns::ConsensusVoteJournalTestAccess::ObserveStartup(
        [&]( std::string_view event ) { events.emplace_back( event ); } );
    EXPECT_FALSE( TryMakeManager( registry ) );
    EXPECT_TRUE( events.empty() );
}

TEST_F( ConsensusVoteJournalHarness, BadSignatureAndEnvelopeFailBeforeSideEffects )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    auto record = MakeSignedVoteRecord( manager, registry );
    manager->Close();
    manager.reset();
    sgns::ConsensusVote vote;
    ASSERT_TRUE( vote.ParseFromString( record.signed_vote_bytes() ) );
    vote.set_signature( std::string( 64, '\x01' ) );
    ASSERT_TRUE( vote.SerializeToString( record.mutable_signed_vote_bytes() ) );
    sgns::ConsensusMessage envelope;
    *envelope.mutable_vote() = vote;
    ASSERT_TRUE( envelope.SerializeToString( record.mutable_outbound_envelope_bytes() ) );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    ASSERT_TRUE( store.PutActiveVote( record ).has_value() );
    std::vector<std::string> events;
    sgns::ConsensusVoteJournalTestAccess::ObserveStartup(
        [&]( std::string_view event ) { events.emplace_back( event ); } );
    EXPECT_FALSE( TryMakeManager( registry ) );
    EXPECT_TRUE( events.empty() );
}

TEST_F( ConsensusVoteJournalHarness, AuthoritativeFinalitySuppressesStoredVoteReplay )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    auto record = MakeSignedVoteRecord( manager, registry );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    ASSERT_TRUE( store.PutActiveVote( record ).has_value() );
    auto certificate = PersistCertificate( manager, record );
    (void) certificate;
    manager->Close();
    manager.reset();
    counters_.Reset();
    sgns::ConsensusVoteJournalTestAccess::OverrideRawPublish(
        [&]( std::string_view ) -> outcome::result<void>
        {
            ++counters_.raw_publish;
            return outcome::success();
        } );
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    EXPECT_EQ( counters_.raw_publish.load(), 0 );
    EXPECT_EQ( counters_.signer.load(), 0 );
}

TEST_F( ConsensusVoteJournalHarness, RestoredSafetyViolationSuppressesStoredVoteReplay )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    auto record = MakeSignedVoteRecord( manager, registry );
    auto certificate = PersistCertificate( manager, record );
    std::string certificate_bytes;
    ASSERT_TRUE( certificate.SerializeToString( &certificate_bytes ) );
    auto digest_bytes = sgns::crypto::sha2_256( certificate_bytes.data(), certificate_bytes.size() );
    const auto digest = sgns::base::hex_lower(
        gsl::span<const uint8_t>( digest_bytes.data(), digest_bytes.size() ) );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    ASSERT_TRUE( store.PutActiveVote( record ).has_value() );
    sgns::ConsensusStateStore::ConflictRecord conflict;
    conflict.set_schema_version( 2 );
    conflict.set_slot_id( record.slot_id() );
    conflict.set_low_certificate_digest( digest );
    conflict.set_high_certificate_digest( HashText( 'f' ) );
    conflict.set_low_proposal_id( record.proposal_id() );
    conflict.set_high_proposal_id( HashText( 'e' ) );
    conflict.set_sources_bitset( 1 );
    conflict.set_first_seen_at_ms( 100 );
    conflict.set_last_seen_at_ms( 100 );
    conflict.set_observation_count( 1 );
    sgns::ConsensusStateStore::SafetyRecord safety;
    safety.set_schema_version( 2 );
    safety.set_state( sgns::ConsensusStateStore::SafetyRecord::SAFETY_VIOLATION );
    safety.set_slot_id( record.slot_id() );
    safety.set_authoritative_certificate_digest( digest );
    safety.set_authoritative_proposal_id( record.proposal_id() );
    safety.set_updated_at_ms( 100 );
    ASSERT_TRUE( store.RecordConflictAndSafety( conflict, safety ).has_value() );
    manager->Close();
    manager.reset();
    counters_.Reset();
    sgns::ConsensusVoteJournalTestAccess::OverrideRawPublish(
        [&]( std::string_view ) -> outcome::result<void>
        {
            ++counters_.raw_publish;
            return outcome::success();
        } );
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    EXPECT_TRUE( sgns::ConsensusVoteJournalTestAccess::SafetyStopped( restarted, record.slot_id() ) );
    EXPECT_EQ( counters_.raw_publish.load(), 0 );
    EXPECT_EQ( counters_.signer.load(), 0 );
}

TEST_F( ConsensusVoteJournalHarness, StaleProcessingRestoresPendingAndHandlerRegistrationWakesIt )
{
    const ScopedReset reset_hooks( []() { sgns::ConsensusVoteJournalTestAccess::ResetStartupHooks(); } );
    auto registry = MakeRegistry();
    ASSERT_TRUE( registry );
    auto manager = MakeManager( registry );
    ASSERT_TRUE( manager );
    auto record = MakeSignedVoteRecord( manager, registry );
    auto certificate = PersistCertificate( manager, record );
    std::string certificate_bytes;
    ASSERT_TRUE( certificate.SerializeToString( &certificate_bytes ) );
    auto digest_bytes = sgns::crypto::sha2_256( certificate_bytes.data(), certificate_bytes.size() );
    const auto digest = sgns::base::hex_lower(
        gsl::span<const uint8_t>( digest_bytes.data(), digest_bytes.size() ) );
    auto proposal = certificate.proposal();
    auto winner = sgns::ConsensusVoteJournalTestAccess::Winner( proposal );
    ASSERT_TRUE( winner );
    sgns::ConsensusStateStore store( db_->GetDataStore() );
    sgns::ConsensusStateStore::ProcessRecord process;
    process.set_schema_version( 2 );
    process.set_state( sgns::ConsensusStateStore::ProcessRecord::PENDING );
    process.set_slot_id( record.slot_id() );
    process.set_certificate_digest( digest );
    process.set_proposal_id( record.proposal_id() );
    process.set_winner_id( winner.value() );
    process.set_updated_at_ms( 100 );
    ASSERT_TRUE( store.PutPendingProcess( process ).has_value() );
    ASSERT_TRUE( store.MarkProcessing( record.slot_id(), 300, 200 ).has_value() );
    manager->Close();
    manager.reset();
    auto restarted = MakeManager( registry );
    ASSERT_TRUE( restarted );
    auto pending = store.GetProcess( record.slot_id() );
    ASSERT_TRUE( pending && pending.value().has_value() );
    EXPECT_EQ( pending.value()->state(), sgns::ConsensusStateStore::ProcessRecord::PENDING );
    EXPECT_TRUE( restarted->RegisterCertificateHandler(
        "sgns.vote-journal.startup.v1",
        []( const std::string &, const sgns::ConsensusManager::Certificate & )
        { return outcome::result<sgns::ConsensusManager::Check>( sgns::ConsensusManager::Check::Approve ); } ) );
    auto complete = store.GetProcess( record.slot_id() );
    ASSERT_TRUE( complete && complete.value().has_value() );
    EXPECT_EQ( complete.value()->state(), sgns::ConsensusStateStore::ProcessRecord::COMPLETE );
}
