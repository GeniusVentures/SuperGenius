/**
 * @file       tx_lookup_tri_state_test.cpp
 * @brief      D-03 / SURV-02 local half: the TransactionManager's tri-state
 *             lookup (found / not-found / pruned) proven on a real CRDT
 *             stack — all three verdicts plus the loud
 *             value-plus-marker invariant-violation case (Phase 19, plan 05).
 * @details    The fixture is the tx_pruner_test family's stack (GlobalDB +
 *             pubsub + temp dir, GeniusAccount + Blockchain +
 *             TransactionManager::Light, no GeniusNode); this file owns its
 *             own copy and includes no sibling test. Seeding always goes
 *             through the REAL publish path on the fixture's CRDT topic
 *             (never raw local puts — the Phase-16 lesson); the pruned state
 *             is armed only through the sanctioned driver
 *             (EraseTransactionEntryPruned — the marker joins the erase
 *             batch), and the resurrected invariant-violation state is armed
 *             exactly like the SweepReprunes recipe (the same element
 *             re-published over the real topic after the erase). Ordering
 *             truth under test: a present value outranks a marker — the
 *             violation is reported loudly (verdict
 *             VALUE_PRESENT_INVARIANT_VIOLATION) while the found tx is still
 *             returned, and PRUNED is never silently conflated with
 *             not-found.
 * @date       2026-10-23
 */

#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "account/GeniusAccount.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "transaction/PruneEligibility.hpp"
#include "transaction/TransactionManager.hpp"

using namespace sgns;

namespace
{
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// The fixture's pre-seeded CRDT topic: puts on it create real blocks,
    /// heads, and seals (an empty-topics Put creates no destinations).
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// An arbitrary well-formed hash that nothing ever seeds: the plain
    /// not-found arm.
    constexpr char kUnknownHash[] = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
} // namespace

/**
 * @brief Real-store fixture for the tri-state lookup: GlobalDB + Blockchain +
 *        TransactionManager (NodeType::Light, no GeniusNode) — the
 *        tx_pruner_test stack with only the members these suites drive.
 */
class TxLookupTriStateTest : public ::test::CRDTFixture
{
public:
    TxLookupTriStateTest() : ::test::CRDTFixture( "tx_lookup_tri_state_test" )
    {
    }

    void SetUp() override
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        assert( account_ != nullptr );

        // Load the UTXOManager's DB so the manager can store/restore UTXOs.
        auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
        assert( load_result.has_value() );

        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        assert( blockchain_ != nullptr );

        MakeManager();
    }

    ~TxLookupTriStateTest() override
    {
        /*
         * Teardown invariant (asio), copied verbatim from the
         * TxPrunerTest/PruneEligibilityTest family: stop the blockchain FIRST
         * so the ConsensusManager round-timer thread joins before any
         * db-backed handle is released; the base ~CRDTFixture stays the FINAL
         * host release (Linux CI segfault otherwise).
         */
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        tm_.reset();
        blockchain_.reset();
        account_.reset();
    }

    /// Constructs the manager under test (and the fresh-instance arm of the
    /// FoundCrdt case: same live db/account/blockchain, empty tracked map).
    void MakeManager()
    {
        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

        tm_ = TransactionManager::New( db_,
                                       io_,
                                       account_,
                                       blockchain_,
                                       NodeType::Light,
                                       0,
                                       kTimestampTolerance,
                                       kMutabilityWindow );
        ASSERT_NE( tm_, nullptr );
    }

    std::shared_ptr<GeniusAccount>      account_;
    std::shared_ptr<Blockchain>         blockchain_;
    std::shared_ptr<TransactionManager> tm_;

    //
    // Seeding helpers — the real publish path only (the tx_pruner recipes):
    // the CRDT value goes through the topic Put, the tx->CID map entry
    // through the raw StoreTransactionCID shapes.
    //

    /// Transfer with @p output_count outputs owned by this account.
    std::shared_ptr<TransferTransaction> MakeTransfer( uint64_t nonce, size_t output_count )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );

        std::vector<OutputDestInfo> destinations;
        destinations.reserve( output_count );
        for ( size_t i = 0; i < output_count; ++i )
        {
            destinations.push_back( { 100 + i, account_->GetAddress(), kTestTokenId } );
        }
        return std::make_shared<TransferTransaction>(
            TransferTransaction::New( {}, std::move( destinations ), std::move( dag ) ) );
    }

    /// Real-topic value Put + raw tx->CID map entry (StoreTransactionCID
    /// shapes), followed by the head-retirement driver put: the tx's delta is
    /// still the topic HEAD right after its Put, and GATE-03 refuses to erase
    /// a head delta — a plain later put advances the head past it (the
    /// tx_pruner family's seeding recipe; the map entry still names the TX's
    /// own delta CID).
    void SeedTransaction( const std::shared_ptr<GeniusTransaction> &transaction )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *transaction );

        crdt::GlobalDB::Buffer serialized;
        serialized.put( transaction->SerializeByteVector() );
        auto put_result = db_->Put( { tx_path }, serialized, { kSeedTopic } );
        ASSERT_TRUE( put_result.has_value() );

        crdt::GlobalDB::Buffer key_buffer;
        key_buffer.put( tx_path );
        crdt::GlobalDB::Buffer value_buffer;
        value_buffer.put( put_result.value().toString().value() );
        ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );

        seed_sequence_                    += 1;
        const std::string      driver_key  = "/seed/txlookup/tri-state-" + std::to_string( seed_sequence_ );
        crdt::GlobalDB::Buffer driver;
        driver.put( "driver-" + std::to_string( seed_sequence_ ) );
        ASSERT_TRUE( db_->Put( { driver_key }, driver, { kSeedTopic } ).has_value() );
    }

    /// Driver-put sequence (one per seeded tx — unique keys, double-run safe).
    uint64_t seed_sequence_ = 0;
};

// ---------------------------------------------------------------------------
// The tri-state verdicts (D-03)
// ---------------------------------------------------------------------------

/**
 * A processed (tracked) transaction answers FOUND from the tracked map with
 * the tx payload and a clean verdict (no marker exists — NOT_FOUND is the
 * marker-evidence state on a found value, never a pruned answer).
 */
TEST_F( TxLookupTriStateTest, FoundTrackedAnswersFromTrackedMapWithPayload )
{
    auto tx = MakeTransfer( 1, 1 );
    ASSERT_NE( tx, nullptr );
    SeedTransaction( tx );

    // The REAL intake path places the tx in the tracked map (the public
    // status scan reads exactly that map; INVALID == not tracked).
    ASSERT_TRUE( tm_->FetchAndProcessTransaction( TransactionManager::GetTransactionPath( *tx ) ).has_value() );
    ASSERT_NE( tm_->GetTransactionStatusByTxId( tx->GetHash() ), TransactionManager::TransactionStatus::INVALID );

    auto lookup = tm_->LookupTransaction( tx->GetHash() );
    ASSERT_FALSE( lookup.has_error() );
    EXPECT_NE( lookup.value().tx, nullptr ) << "tracked tx must answer FOUND";
    ASSERT_NE( lookup.value().tx, nullptr );
    EXPECT_EQ( lookup.value().tx->GetHash(), tx->GetHash() );
    EXPECT_EQ( lookup.value().verdict, PrunedLookup::NOT_FOUND ) << "clean found carries no marker evidence";
    EXPECT_FALSE( lookup.value().record.has_value() );
}

/**
 * A transaction present in the CRDT but not in the tracked map (a fresh
 * manager instance over the same live DB) answers FOUND through the CRDT
 * found-source: the tracked map is proven empty first, so only the
 * exact-hash CRDT probe can produce the payload.
 */
TEST_F( TxLookupTriStateTest, FoundCrdtAnswersThroughCrdtSourceOnFreshManager )
{
    auto tx = MakeTransfer( 2, 1 );
    ASSERT_NE( tx, nullptr );
    SeedTransaction( tx );

    // Fresh manager over the SAME live db/account/blockchain: durable CRDT
    // value intact, tracked map empty by construction.
    tm_.reset();
    MakeManager();
    ASSERT_EQ( tm_->GetTransactionStatusByTxId( tx->GetHash() ), TransactionManager::TransactionStatus::INVALID )
        << "negative proof: tracked map is empty";

    auto lookup = tm_->LookupTransaction( tx->GetHash() );
    ASSERT_FALSE( lookup.has_error() );
    EXPECT_NE( lookup.value().tx, nullptr ) << "CRDT-resident tx must answer FOUND";
    ASSERT_NE( lookup.value().tx, nullptr );
    EXPECT_EQ( lookup.value().tx->GetHash(), tx->GetHash() );
    EXPECT_EQ( lookup.value().verdict, PrunedLookup::NOT_FOUND );
    EXPECT_FALSE( lookup.value().record.has_value() );
}

/**
 * A pruned transaction answers PRUNED — never not-found — and carries the
 * durable PrunedTransactionRecord payload (the erased path and the recorded
 * CID), not just the enum.
 */
TEST_F( TxLookupTriStateTest, PrunedAnswersWithRecordPayload )
{
    auto tx = MakeTransfer( 3, 1 );
    ASSERT_NE( tx, nullptr );
    SeedTransaction( tx );

    // The sanctioned pruned-state driver: the marker joins the erase batch.
    ASSERT_TRUE( tm_->EraseTransactionEntryPruned( tx->GetHash() ).has_value() );

    auto lookup = tm_->LookupTransaction( tx->GetHash() );
    ASSERT_FALSE( lookup.has_error() );
    EXPECT_EQ( lookup.value().tx, nullptr ) << "pruned tx must NOT answer FOUND";
    EXPECT_EQ( lookup.value().verdict, PrunedLookup::PRUNED ) << "pruned is distinct from not-found";
    ASSERT_TRUE( lookup.value().record.has_value() ) << "PRUNED carries the record payload";
    // The production marker record names the RESOLVED erased path (its one
    // durable payload field — BuildPruneMarkerEntry sets tx_path only).
    EXPECT_EQ( lookup.value().record->tx_path(), TransactionManager::GetTransactionPath( *tx ) );
}

/**
 * An arbitrary unknown hash answers plain NOT_FOUND: no tx, no marker, no
 * record — the never-existed state, distinct from PRUNED.
 */
TEST_F( TxLookupTriStateTest, NotFoundForUnknownHash )
{
    auto lookup = tm_->LookupTransaction( kUnknownHash );
    ASSERT_FALSE( lookup.has_error() );
    EXPECT_EQ( lookup.value().tx, nullptr );
    EXPECT_EQ( lookup.value().verdict, PrunedLookup::NOT_FOUND );
    EXPECT_FALSE( lookup.value().record.has_value() );
}

/**
 * The value-plus-marker invariant violation (the SweepReprunes resurrected
 * state: the same element re-published over the real topic after a pruned
 * erase) is reported LOUDLY and OBSERVABLY — the verdict carries
 * VALUE_PRESENT_INVARIANT_VIOLATION — while the found tx is still returned:
 * a present value outranks a marker, never a silent PRUNED and never a
 * fabricated not-found.
 */
TEST_F( TxLookupTriStateTest, InvariantViolationLoudWhileReturningFoundTx )
{
    auto tx = MakeTransfer( 4, 1 );
    ASSERT_NE( tx, nullptr );
    SeedTransaction( tx );

    ASSERT_TRUE( tm_->EraseTransactionEntryPruned( tx->GetHash() ).has_value() );
    {
        // Pristine armed state: the clean PRUNED verdict.
        auto lookup = tm_->LookupTransaction( tx->GetHash() );
        ASSERT_FALSE( lookup.has_error() );
        EXPECT_EQ( lookup.value().verdict, PrunedLookup::PRUNED );
    }

    // The add-wins re-merge simulation (the SweepReprunes recipe): the same
    // element again through the REAL publish path — value present again
    // while the marker survived.
    crdt::GlobalDB::Buffer serialized;
    serialized.put( tx->SerializeByteVector() );
    ASSERT_TRUE(
        db_->Put( { TransactionManager::GetTransactionPath( *tx ) }, serialized, { kSeedTopic } ).has_value() );
    {
        auto marker = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( marker.has_value() );
        ASSERT_EQ( marker.value().first, PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION )
            << "armed state check: marker AND value both present";
    }

    auto lookup = tm_->LookupTransaction( tx->GetHash() );
    ASSERT_FALSE( lookup.has_error() );
    EXPECT_EQ( lookup.value().verdict, PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION )
        << "the violation is observable on the result, not only in logs";
    EXPECT_NE( lookup.value().tx, nullptr ) << "the found tx is still returned — never guessed away";
    ASSERT_NE( lookup.value().tx, nullptr );
    EXPECT_EQ( lookup.value().tx->GetHash(), tx->GetHash() );
    EXPECT_FALSE( lookup.value().record.has_value() ) << "record is the PRUNED payload only";
}
