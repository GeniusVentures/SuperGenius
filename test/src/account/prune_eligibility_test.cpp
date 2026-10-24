/**
 * @file       prune_eligibility_test.cpp
 * @brief      SC-1 (exact eligible set), SC-2 (multi-element block gate),
 *             SC-4 (fail-closed gates), the D-04 pruned-marker contract, and
 *             the PruningConfig proofs for the PRUNE-01/03 dry-run evaluator.
 * @details    Group A seeds the three-way window discriminator plus every spend
 *             discriminator through the REAL publish path on the fixture's CRDT
 *             topic (never raw local puts — Phase-16 lesson) and asserts the
 *             eligible set is EXACTLY the expected transaction, including the
 *             owner's counterexample: an ancient-creation tx spent recently is
 *             NEVER eligible (the window reads spend recency, never creation
 *             age, D-02). Group B proves construction refuses Full/Archive and
 *             the allowlist rejects five crafted out-of-prefix keys. Group C
 *             pins the PruningConfig parse/validate contract (GATE-06).
 *             Group D proves SC-2: a delta block is erasable ONLY when every
 *             element AND tombstone key it carries (parsed from the block
 *             itself) is eligible and allowlist-conformant — real batched
 *             blocks with block-presence asserted before gating (Pitfall 11).
 *             Group E pins the D-04 marker contract: PRUNED vs NOT_FOUND vs
 *             the value-XOR-marker invariant violation, plus the
 *             spender-pruned window refinement driven through the REAL
 *             Phase-16 erase flow. Group F proves the production marker
 *             join (D-C1): BuildPruneMarkerEntry's buffers, the staged-entry
 *             EraseEntry atomicity (one commit: value gone, block gone,
 *             marker PRUNED), and the seam-interrupted all-or-nothing proof.
 * @date       2026-10-19
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "account/GeniusAccount.hpp"
#include "account/RegistrationTransaction.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "base/sgns_version.hpp"
#include "blockchain/Blockchain.hpp"
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
}

/**
 * @brief Real-store fixture: GlobalDB + Blockchain + TransactionManager
 *        (NodeType::Light, no GeniusNode, no network sync) — the
 *        CertificateFallbackTest stack with its load-bearing teardown order.
 */
class PruneEligibilityTest : public ::test::CRDTFixture
{
public:
    PruneEligibilityTest() : ::test::CRDTFixture( "prune_eligibility_test" )
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
        assert( tm_ != nullptr );
    }

    ~PruneEligibilityTest() override
    {
        /*
         * Teardown invariant (asio), copied verbatim from
         * CertificateFallbackTest: stop the blockchain FIRST so the
         * ConsensusManager round-timer thread joins before any db-backed
         * handle is released; the base ~CRDTFixture stays the FINAL host
         * release (Linux CI segfault otherwise).
         */
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        tm_.reset();
        blockchain_.reset();
        account_.reset();
    }

    std::shared_ptr<GeniusAccount>      account_;
    std::shared_ptr<Blockchain>         blockchain_;
    std::shared_ptr<TransactionManager> tm_;

    //
    // Seeding helpers — every transaction goes through the REAL topic Put so
    // its CRDT value, delta CID, head, and per-key priority all exist, then
    // gets a raw tx->CID map entry in the exact StoreTransactionCID shapes.
    //

    /// Transfer with @p output_count outputs owned by this account (empty for spenders).
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

    /// Registration (a tx type with NO UTXO parameters — GetProducedUTXOs nullopt).
    std::shared_ptr<RegistrationTransaction> MakeRegistration( uint64_t nonce )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "registration" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );
        return std::make_shared<RegistrationTransaction>(
            RegistrationTransaction::New( account_->GetAddress(), nonce, {}, std::move( dag ) ) );
    }

    /// Real-topic value Put + raw tx->CID map entry (StoreTransactionCID shapes).
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
    }

    /// Commits several transactions' values in ONE delta block (a batched
    /// AtomicTransaction on the seed topic — the multi-element-block recipe)
    /// and records each tx->CID map entry against that block's CID.
    /// Returns the block CID string ("" on failure — callers assert on it).
    std::string SeedTransactionsInOneBlock( const std::vector<std::shared_ptr<GeniusTransaction>> &transactions )
    {
        auto batch = db_->BeginTransaction();
        EXPECT_NE( batch, nullptr );
        if ( batch == nullptr )
        {
            return "";
        }
        for ( const auto &transaction : transactions )
        {
            crdt::GlobalDB::Buffer serialized;
            serialized.put( transaction->SerializeByteVector() );
            EXPECT_TRUE(
                batch->Put( { TransactionManager::GetTransactionPath( *transaction ) }, serialized ).has_value() );
        }
        auto commit = batch->Commit( { kSeedTopic } );
        EXPECT_TRUE( commit.has_value() );
        if ( !commit.has_value() )
        {
            return "";
        }
        const std::string block_cid = commit.value().toString().value();

        for ( const auto &transaction : transactions )
        {
            crdt::GlobalDB::Buffer key_buffer;
            key_buffer.put( TransactionManager::GetTransactionPath( *transaction ) );
            crdt::GlobalDB::Buffer value_buffer;
            value_buffer.put( block_cid );
            EXPECT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
        }
        return block_cid;
    }

    /// One delta block carrying @p transaction's element PLUS a non-tx
    /// element at @p extra_key (the out-of-prefix smuggling shape, SC-2 A3).
    /// Returns the block CID string ("" on failure).
    std::string SeedBlockWithOutOfPrefixKey( const std::shared_ptr<GeniusTransaction> &transaction,
                                             const std::string                        &extra_key )
    {
        auto batch = db_->BeginTransaction();
        EXPECT_NE( batch, nullptr );
        if ( batch == nullptr )
        {
            return "";
        }
        crdt::GlobalDB::Buffer serialized;
        serialized.put( transaction->SerializeByteVector() );
        EXPECT_TRUE( batch->Put( { TransactionManager::GetTransactionPath( *transaction ) }, serialized ).has_value() );
        crdt::GlobalDB::Buffer extra_value;
        extra_value.put( "non-tx-payload" );
        EXPECT_TRUE( batch->Put( { extra_key }, extra_value ).has_value() );
        auto commit = batch->Commit( { kSeedTopic } );
        EXPECT_TRUE( commit.has_value() );
        return commit.has_value() ? commit.value().toString().value() : "";
    }

    /// One delta block carrying @p transaction's element PLUS a TOMBSTONE of
    /// @p tombstone_target (previously committed) — the Pitfall-4 shape: an
    /// elements-only gate would never see the tombstone key.
    /// Returns the block CID string ("" on failure).
    std::string SeedBlockWithTombstone( const std::shared_ptr<GeniusTransaction> &transaction,
                                        const std::string                        &tombstone_target )
    {
        auto batch = db_->BeginTransaction();
        EXPECT_NE( batch, nullptr );
        if ( batch == nullptr )
        {
            return "";
        }
        crdt::GlobalDB::Buffer serialized;
        serialized.put( transaction->SerializeByteVector() );
        EXPECT_TRUE( batch->Put( { TransactionManager::GetTransactionPath( *transaction ) }, serialized ).has_value() );
        EXPECT_TRUE( batch->Remove( { tombstone_target } ).has_value() );
        auto commit = batch->Commit( { kSeedTopic } );
        EXPECT_TRUE( commit.has_value() );
        return commit.has_value() ? commit.value().toString().value() : "";
    }

    /// Plain topic Put of a raw string at @p key (driver heights, garbage
    /// values, tombstone targets) — real publish path, never a local-only put.
    void PutSeedValue( const std::string &key, const std::string &value )
    {
        crdt::GlobalDB::Buffer buffer;
        buffer.put( value );
        ASSERT_TRUE( db_->Put( { key }, buffer, { kSeedTopic } ).has_value() );
    }

    /// Records the tx->CID map entry (StoreTransactionCID shapes) for @p hash
    /// against @p cid — the block-gate input wiring for hand-built blocks.
    void SeedTxCidEntry( const std::string &tx_hash, const std::string &cid )
    {
        crdt::GlobalDB::Buffer key_buffer;
        key_buffer.put( TransactionManager::GetTransactionPath( tx_hash ) );
        crdt::GlobalDB::Buffer value_buffer;
        value_buffer.put( cid );
        ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
    }

    /// Tracks a candidate's produced outputs in the UTXO manager (PutProducedUTXOs shape).
    void TrackOutputs( const GeniusTransaction &transaction )
    {
        auto outputs = transaction.GetProducedUTXOs();
        ASSERT_TRUE( outputs.has_value() );
        for ( const auto &output : *outputs )
        {
            auto put = account_->GetUTXOManager().PutUTXO( output, output.GetOwnerAddress() );
            ASSERT_TRUE( put.has_value() );
            ASSERT_TRUE( put.value() );
        }
    }

    /// Spends one output; returns ConsumeUTXOs's verdict (false = synthesized tombstone).
    bool ConsumeOutput( const GeniusTransaction &transaction, uint32_t output_idx, const GeniusTransaction &spender )
    {
        return ConsumeOutputOpt( transaction, output_idx, SpenderHash( spender ) );
    }

    bool ConsumeOutputOpt( const GeniusTransaction            &transaction,
                           uint32_t                            output_idx,
                           const std::optional<base::Hash256> &spender )
    {
        auto txid = base::Hash256::fromReadableString( transaction.GetHash() );
        EXPECT_FALSE( txid.has_error() );
        auto consumed = account_->GetUTXOManager().ConsumeUTXOs( { { txid.value(), output_idx, {} } },
                                                                 account_->GetAddress(),
                                                                 UTXOManager::UTXOType::UTXO_NORMAL,
                                                                 spender );
        EXPECT_TRUE( consumed.has_value() );
        return consumed.has_value() && consumed.value();
    }

    static base::Hash256 SpenderHash( const GeniusTransaction &transaction )
    {
        return base::Hash256::fromReadableString( transaction.GetHash() ).value();
    }

    /// Extracts the tx hash from a /bc-<net>/tx/<hash> candidate path.
    static std::string HashOfPath( const std::string &tx_path )
    {
        const std::string prefix = TransactionManager::GetBlockChainBase() + "tx/";
        return tx_path.substr( prefix.size() );
    }

    /// Finds the structured verdict of @p transaction in an evaluation result.
    static const PruneCandidateResult *FindVerdict( const PruneEvaluationResult &result,
                                                    const GeniusTransaction     &transaction )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( transaction );
        for ( const auto &candidate : result.candidates )
        {
            if ( candidate.tx_path == tx_path )
            {
                return &candidate;
            }
        }
        return nullptr;
    }

    static bool HasReason( const PruneCandidateResult &candidate, PruneReason reason )
    {
        return std::find( candidate.reasons.begin(), candidate.reasons.end(), reason ) != candidate.reasons.end();
    }

    static bool HasReason( const BlockGateResult &report, PruneReason reason )
    {
        return std::find( report.reasons.begin(), report.reasons.end(), reason ) != report.reasons.end();
    }

    /// Finds the block-gate verdict for @p cid in an evaluation result.
    static const BlockGateResult *FindBlockReport( const PruneEvaluationResult &result, const std::string &cid )
    {
        for ( const auto &report : result.block_reports )
        {
            if ( report.cid == cid )
            {
                return &report;
            }
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Group A: SC-1 — the exact eligible set on the three-way discriminator
// ---------------------------------------------------------------------------

/**
 * The phase's core proof. Put order on the seed topic sets each key's CRDT
 * priority (creation arrival height); the LAST put is the recent spender S_R,
 * so maxHeight - priority(S_R) == 1 < N while maxHeight - priority(S_E) is far
 * beyond N. The eligible set must be EXACTLY {E}: the ancient-creation tx all
 * of whose outputs were spent anciently. The owner's counterexample C (ANCIENT
 * creation, RECENT spend) must be ineligible — the window is spend-recency,
 * never creation age (D-02).
 */
TEST_F( PruneEligibilityTest, ExactEligibleSet_ThreeWayWindowAndSpendDiscriminators )
{
    constexpr uint64_t kWindow = 2;

    // Candidates (creation order sets arrival heights; C and E are ancient).
    auto counterexample   = MakeTransfer( 1, 1 );  // C: ancient creation, spent recently
    auto eligible_tx      = MakeTransfer( 2, 2 );  // E: ancient creation, spent anciently
    auto unspent_tx       = MakeTransfer( 3, 2 );  // U: outputs never consumed
    auto split_tx         = MakeTransfer( 4, 2 );  // P: output0 ancient, output1 recent
    auto tombstone_tx     = MakeTransfer( 5, 2 );  // T: outputs consumed but never tracked
    auto nospender_tx     = MakeTransfer( 6, 1 );  // NR: consumed without spender id
    auto unresolved_tx    = MakeTransfer( 7, 1 );  // SU: spent by a tx with no entry
    auto registration     = MakeRegistration( 8 ); // R: no UTXO parameters at all
    auto ancient_spender  = MakeTransfer( 9, 0 );  // S_E: ancient (put early, below)
    auto recent_candidate = MakeTransfer( 10, 1 ); // A3: recent creation, recent spend
    auto recent_spender   = MakeTransfer( 11, 0 ); // S_R: recent (the LAST put)

    // Ancient side first: C, E (ancient creations), then the ANCIENT spender
    // S_E — put early so its arrival height sits far below maxHeight. The
    // remaining candidates' creation heights are irrelevant by design (D-02:
    // the window reads spend recency, never creation age).
    SeedTransaction( counterexample );
    SeedTransaction( eligible_tx );
    SeedTransaction( ancient_spender );
    SeedTransaction( unspent_tx );
    SeedTransaction( split_tx );
    SeedTransaction( tombstone_tx );
    SeedTransaction( nospender_tx );
    SeedTransaction( unresolved_tx );
    SeedTransaction( registration );

    // Track every genuine output BEFORE consuming (mirrors PutProducedUTXOs).
    TrackOutputs( *eligible_tx );
    TrackOutputs( *counterexample );
    TrackOutputs( *split_tx );
    TrackOutputs( *nospender_tx );
    TrackOutputs( *unresolved_tx );
    TrackOutputs( *recent_candidate );
    // unspent_tx: deliberately NOT consumed; tombstone_tx: deliberately NOT
    // tracked so its consume synthesizes zero-amount tombstones (D-C2 case).

    // Spends: E's two outputs <- ancient S_E (the eligible shape).
    ASSERT_TRUE( ConsumeOutput( *eligible_tx, 0, *ancient_spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_tx, 1, *ancient_spender ) );
    // Counterexample + recent candidate <- recent S_R.
    ASSERT_TRUE( ConsumeOutput( *counterexample, 0, *recent_spender ) );
    // Split-time: output0 ancient, output1 recent — the most-recent binds.
    ASSERT_TRUE( ConsumeOutput( *split_tx, 0, *ancient_spender ) );
    // NR: genuine consume with NO spender identity recorded.
    ASSERT_TRUE( ConsumeOutputOpt( *nospender_tx, 0, std::nullopt ) );
    // SU: genuine consume by a hash with NO tx->CID entry anywhere (T-17-0403).
    const auto ghost_spender = base::Hash256::fromReadableString( std::string( 64, 'f' ) ).value();
    ASSERT_TRUE( ConsumeOutputOpt( *unresolved_tx, 0, ghost_spender ) );
    // T: consume WITHOUT tracking — synthesized tombstones (verdict false by design).
    EXPECT_FALSE( ConsumeOutput( *tombstone_tx, 0, *ancient_spender ) );
    EXPECT_FALSE( ConsumeOutput( *tombstone_tx, 1, *ancient_spender ) );

    // Recent side last: A3 then S_R — the most recent arrival in the DAG.
    SeedTransaction( recent_candidate );
    ASSERT_TRUE( ConsumeOutput( *recent_candidate, 0, *recent_spender ) );
    SeedTransaction( recent_spender );
    ASSERT_TRUE( ConsumeOutput( *split_tx, 1, *recent_spender ) );

    // Construction (Light) + evaluation with window N = 2.
    PruningConfig config;
    config.finality_window = kWindow;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );
    const auto &result = evaluation.value();

    // Everything seeded under tx/ is reported — nothing silently dropped.
    ASSERT_EQ( result.candidates.size(), 11u );

    // Window sanity: S_R is the most recent put, so its spend height IS
    // maxHeight (window fails); S_E sits far below (window passes). A violated
    // invariant here is a loud failure, never a silently-wrong verdict.
    const auto s_e_priority = db_->GetKeyPriority( TransactionManager::GetTransactionPath( *ancient_spender ) );
    const auto s_r_priority = db_->GetKeyPriority( TransactionManager::GetTransactionPath( *recent_spender ) );
    ASSERT_TRUE( s_e_priority.has_value() );
    ASSERT_TRUE( s_r_priority.has_value() );
    const auto heads = db_->GetCRDTHeadList();
    ASSERT_TRUE( heads.has_value() );
    const uint64_t max_height = heads.value().second;
    ASSERT_LT( max_height - s_r_priority.value(), kWindow );
    ASSERT_GE( max_height - s_e_priority.value(), kWindow );

    // A1 — SC-1: the eligible set is EXACTLY {E} (set equality on hashes).
    std::vector<std::string> eligible_hashes;
    for ( const auto &candidate : result.candidates )
    {
        if ( candidate.eligible )
        {
            eligible_hashes.push_back( HashOfPath( candidate.tx_path ) );
        }
    }
    std::sort( eligible_hashes.begin(), eligible_hashes.end() );
    std::vector<std::string> expected_hashes{ eligible_tx->GetHash() };
    std::sort( expected_hashes.begin(), expected_hashes.end() );
    EXPECT_EQ( eligible_hashes, expected_hashes );

    const auto *eligible_verdict = FindVerdict( result, *eligible_tx );
    ASSERT_NE( eligible_verdict, nullptr );
    EXPECT_TRUE( eligible_verdict->eligible );
    EXPECT_TRUE( eligible_verdict->reasons.empty() );
    ASSERT_TRUE( eligible_verdict->spend_height.has_value() );
    EXPECT_EQ( eligible_verdict->spend_height.value(), s_e_priority.value() );
    EXPECT_TRUE( eligible_verdict->cid.has_value() );

    // A2 — the owner's counterexample: ANCIENT creation, RECENT spender.
    const auto *counterexample_verdict = FindVerdict( result, *counterexample );
    ASSERT_NE( counterexample_verdict, nullptr );
    EXPECT_FALSE( counterexample_verdict->eligible );
    EXPECT_TRUE( HasReason( *counterexample_verdict, PruneReason::SPEND_WITHIN_WINDOW ) );

    // A3 — recent creation, recent spend.
    const auto *recent_verdict = FindVerdict( result, *recent_candidate );
    ASSERT_NE( recent_verdict, nullptr );
    EXPECT_FALSE( recent_verdict->eligible );
    EXPECT_TRUE( HasReason( *recent_verdict, PruneReason::SPEND_WITHIN_WINDOW ) );

    // A4 — any unspent output.
    const auto *unspent_verdict = FindVerdict( result, *unspent_tx );
    ASSERT_NE( unspent_verdict, nullptr );
    EXPECT_FALSE( unspent_verdict->eligible );
    EXPECT_TRUE( HasReason( *unspent_verdict, PruneReason::UNSPENT_OUTPUT ) );

    // A5 — split-time spends: the MOST-RECENT spend binds (Pitfall 3).
    const auto *split_verdict = FindVerdict( result, *split_tx );
    ASSERT_NE( split_verdict, nullptr );
    EXPECT_FALSE( split_verdict->eligible );
    EXPECT_TRUE( HasReason( *split_verdict, PruneReason::SPEND_WITHIN_WINDOW ) );
    ASSERT_TRUE( split_verdict->spend_height.has_value() );
    EXPECT_EQ( split_verdict->spend_height.value(), s_r_priority.value() );

    // A6 — synthesized zero-amount tombstones are not spend evidence (D-C2).
    const auto *tombstone_verdict = FindVerdict( result, *tombstone_tx );
    ASSERT_NE( tombstone_verdict, nullptr );
    EXPECT_FALSE( tombstone_verdict->eligible );
    EXPECT_TRUE( HasReason( *tombstone_verdict, PruneReason::UNSPENT_OUTPUT ) );

    // Genuine spend with no recorded spender identity.
    const auto *nospender_verdict = FindVerdict( result, *nospender_tx );
    ASSERT_NE( nospender_verdict, nullptr );
    EXPECT_FALSE( nospender_verdict->eligible );
    EXPECT_TRUE( HasReason( *nospender_verdict, PruneReason::SPENDER_NOT_RECORDED ) );

    // Spender whose tx->CID entry does not exist: unresolvable, NEVER a
    // vacuous priority-0 window pass (T-17-0403).
    const auto *unresolved_verdict = FindVerdict( result, *unresolved_tx );
    ASSERT_NE( unresolved_verdict, nullptr );
    EXPECT_FALSE( unresolved_verdict->eligible );
    EXPECT_TRUE( HasReason( *unresolved_verdict, PruneReason::SPENDER_UNRESOLVED ) );

    // No UTXO parameters (registration) and empty produced-outputs (spenders):
    // both fail closed with NO_OUTPUTS.
    const auto *registration_verdict = FindVerdict( result, *registration );
    ASSERT_NE( registration_verdict, nullptr );
    EXPECT_FALSE( registration_verdict->eligible );
    EXPECT_TRUE( HasReason( *registration_verdict, PruneReason::NO_OUTPUTS ) );
    const auto *ancient_spender_verdict = FindVerdict( result, *ancient_spender );
    ASSERT_NE( ancient_spender_verdict, nullptr );
    EXPECT_FALSE( ancient_spender_verdict->eligible );
    EXPECT_TRUE( HasReason( *ancient_spender_verdict, PruneReason::NO_OUTPUTS ) );
    const auto *recent_spender_verdict = FindVerdict( result, *recent_spender );
    ASSERT_NE( recent_spender_verdict, nullptr );
    EXPECT_FALSE( recent_spender_verdict->eligible );
    EXPECT_TRUE( HasReason( *recent_spender_verdict, PruneReason::NO_OUTPUTS ) );

    // Count cross-check: exactly one eligible among the eleven candidates.
    EXPECT_EQ( result.EligibleCount(), 1u );
}

// ---------------------------------------------------------------------------
// Group B: SC-4 — fail-closed gates
// ---------------------------------------------------------------------------

/** B1/B2 — construction refuses every non-Light role (GATE-01). */
TEST_F( PruneEligibilityTest, FailClosedGates_ConstructionRefusesFullAndArchive )
{
    PruningConfig config;
    config.finality_window = 2;

    auto full = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Full, config );
    EXPECT_TRUE( full.has_error() );

    auto archive = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Archive, config );
    EXPECT_TRUE( archive.has_error() );
}

/** Evaluate without a finality window fails closed — nothing can become eligible. */
TEST_F( PruneEligibilityTest, FailClosedGates_EvaluateWithoutWindowFails )
{
    PruningConfig config; // enabled=false, no finality_window

    auto evaluator = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    EXPECT_TRUE( evaluation.has_error() );
}

/**
 * T-17-0405 — a malformed stored value under a tx/ key is reported
 * DESERIALIZE_FAILED and never crashes the evaluation; the scan continues,
 * so a well-formed eligible peer still evaluates eligible alongside it.
 */
TEST_F( PruneEligibilityTest, FailClosedGates_GarbageValueReportsDeserializeFailed )
{
    // A well-formed, fully-eligible peer: candidate first, spender second.
    auto eligible_tx = MakeTransfer( 1, 1 );
    SeedTransaction( eligible_tx );
    TrackOutputs( *eligible_tx );
    auto spender = MakeTransfer( 2, 0 );
    SeedTransaction( spender );
    ASSERT_TRUE( ConsumeOutput( *eligible_tx, 0, *spender ) );

    // Malformed value at a REAL tx path (real-topic Put — only the bytes are garbage).
    const std::string      garbage_hash = std::string( 64, 'b' );
    crdt::GlobalDB::Buffer garbage;
    garbage.put( std::vector<uint8_t>{ 0xde, 0xad, 0xbe, 0xef } );
    ASSERT_TRUE(
        db_->Put( { TransactionManager::GetTransactionPath( garbage_hash ) }, garbage, { kSeedTopic } ).has_value() );

    // One more (non-tx) put so the spender sits two heights below maxHeight.
    crdt::GlobalDB::Buffer driver;
    driver.put( "head-driver" );
    ASSERT_TRUE( db_->Put( { "/seed/prune/garbage-case" }, driver, { kSeedTopic } ).has_value() );

    PruningConfig config;
    config.finality_window = 2;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() ); // the garbage value did not crash the scan

    // Candidate, spender (empty outputs -> NO_OUTPUTS), and the garbage entry.
    ASSERT_EQ( evaluation.value().candidates.size(), 3u );
    const std::string garbage_path = TransactionManager::GetTransactionPath( garbage_hash );
    const auto        garbage_it   = std::find_if( evaluation.value().candidates.begin(),
                                                   evaluation.value().candidates.end(),
                                                   [&garbage_path]( const PruneCandidateResult &c )
                                                   { return c.tx_path == garbage_path; } );
    ASSERT_NE( garbage_it, evaluation.value().candidates.end() );
    EXPECT_FALSE( garbage_it->eligible );
    EXPECT_TRUE( HasReason( *garbage_it, PruneReason::DESERIALIZE_FAILED ) );

    const auto *eligible_verdict = FindVerdict( evaluation.value(), *eligible_tx );
    ASSERT_NE( eligible_verdict, nullptr );
    EXPECT_TRUE( eligible_verdict->eligible ); // the scan continued past the garbage
}

/**
 * B3 — the one allowlist home rejects every crafted out-of-prefix key
 * (T-17-0401): cert slot, registration path, unmonitored network, slashless
 * sibling, proof key — and accepts a real monitored-network tx key.
 */
TEST_F( PruneEligibilityTest, FailClosedGates_AllowlistRejectsCraftedKeys )
{
    PruningConfig config;
    config.finality_window = 2;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    const auto &allowlist = *evaluator.value();

    const std::string address = account_->GetAddress();
    const std::string tx_hash = std::string( 64, 'a' );
    const std::string base    = TransactionManager::GetBlockChainBase();

    EXPECT_FALSE( allowlist.IsAllowlistedTxKey( "/cert/" + address + ":41" ) );
    EXPECT_FALSE( allowlist.IsAllowlistedTxKey( base + "reg/" + address ) );
    EXPECT_FALSE( allowlist.IsAllowlistedTxKey( TransactionManager::GetBlockChainBase( 999 ) + "tx/" + tx_hash ) );
    EXPECT_FALSE( allowlist.IsAllowlistedTxKey( base + "txfoo" ) );
    EXPECT_FALSE( allowlist.IsAllowlistedTxKey( base + "proof/" + tx_hash ) );

    EXPECT_TRUE( allowlist.IsAllowlistedTxKey( base + "tx/" + tx_hash ) );
}

// ---------------------------------------------------------------------------
// Group C: PruningConfig parse/validate contract (GATE-06 policy half)
// ---------------------------------------------------------------------------

namespace
{
    PruningConfig ParseConfig( const char *json )
    {
        rapidjson::Document document;
        document.Parse( json );
        EXPECT_FALSE( document.HasParseError() );
        return PruningConfig::FromJSON( document );
    }
} // namespace

/** C1 — absent members (empty object) keep every default. */
TEST_F( PruneEligibilityTest, PruningConfig_AbsentObjectKeepsDefaults )
{
    const auto config = ParseConfig( "{}" );
    EXPECT_FALSE( config.enabled );
    EXPECT_FALSE( config.dry_run );
    EXPECT_FALSE( config.finality_window.has_value() );
    EXPECT_FALSE( config.disabled_reason.has_value() );
}

/** C2 — a fully-specified enabled request parses verbatim. */
TEST_F( PruneEligibilityTest, PruningConfig_EnabledWithWindowParses )
{
    const auto config = ParseConfig( R"( { "enabled": true, "finality_window": 100, "dry_run": true } )" );
    EXPECT_TRUE( config.enabled );
    EXPECT_TRUE( config.dry_run );
    ASSERT_TRUE( config.finality_window.has_value() );
    EXPECT_EQ( config.finality_window.value(), 100u );
    EXPECT_FALSE( config.disabled_reason.has_value() );
}

/** C3 — enabled WITHOUT finality_window is fail-closed to disabled with the fixed reason. */
TEST_F( PruneEligibilityTest, PruningConfig_EnabledWithoutWindowFailsClosed )
{
    const auto config = ParseConfig( R"( { "enabled": true } )" );
    EXPECT_FALSE( config.enabled );
    ASSERT_TRUE( config.disabled_reason.has_value() );
    EXPECT_NE( config.disabled_reason.value().find( "finality_window" ), std::string::npos );
}

/** C4 — ill-typed values keep per-key defaults (no partial trust). */
TEST_F( PruneEligibilityTest, PruningConfig_IllTypedValuesKeepDefaults )
{
    const auto config = ParseConfig( R"( { "enabled": "yes", "finality_window": "x", "dry_run": 5 } )" );
    EXPECT_FALSE( config.enabled );
    EXPECT_FALSE( config.dry_run );
    EXPECT_FALSE( config.finality_window.has_value() );
    EXPECT_FALSE( config.disabled_reason.has_value() );
}

/** C5 — enabled WITH a zero window fails closed exactly like a missing one (WR-03). */
TEST_F( PruneEligibilityTest, PruningConfig_EnabledWithZeroWindowFailsClosed )
{
    const auto config = ParseConfig( R"( { "enabled": true, "finality_window": 0 } )" );
    EXPECT_FALSE( config.enabled );
    EXPECT_FALSE( config.finality_window.has_value() ); // zero rejected at read time
    ASSERT_TRUE( config.disabled_reason.has_value() );
    EXPECT_NE( config.disabled_reason.value().find( "finality_window" ), std::string::npos );
}

/** C6 — a zero window on a DISABLED request is simply ignored, no fail-close (WR-03 scope). */
TEST_F( PruneEligibilityTest, PruningConfig_ZeroWindowWhileDisabledStaysUnset )
{
    const auto config = ParseConfig( R"( { "finality_window": 0 } )" );
    EXPECT_FALSE( config.enabled );
    EXPECT_FALSE( config.finality_window.has_value() );
    EXPECT_FALSE( config.disabled_reason.has_value() );
}

/** C7 — absent budget/settle members keep their defaults (100 erases / 60s). */
TEST_F( PruneEligibilityTest, PruningConfig_EraseBudgetAndSettleDefaultWhenAbsent )
{
    const auto empty = ParseConfig( "{}" );
    EXPECT_EQ( empty.max_erases_per_cycle, 100u );
    EXPECT_EQ( empty.settle_seconds, 60u );

    // A fully-specified enabled request with neither new key: defaults too.
    const auto enabled = ParseConfig( R"( { "enabled": true, "finality_window": 100 } )" );
    EXPECT_EQ( enabled.max_erases_per_cycle, 100u );
    EXPECT_EQ( enabled.settle_seconds, 60u );
}

/** C8 — explicit positive budget/settle values parse verbatim. */
TEST_F( PruneEligibilityTest, PruningConfig_EraseBudgetAndSettleExplicitValuesParse )
{
    const auto config = ParseConfig( R"( { "max_erases_per_cycle": 250, "settle_seconds": 45 } )" );
    EXPECT_EQ( config.max_erases_per_cycle, 250u );
    EXPECT_EQ( config.settle_seconds, 45u );
}

/**
 * C9 — zero budget and zero settle are DEGENERATE and rejected at read time:
 * a 0-K cycle can never drain (WR-03 rule) and a settle-less cycle can fire
 * during initial sync. The members keep their safe defaults.
 */
TEST_F( PruneEligibilityTest, PruningConfig_ZeroEraseBudgetAndSettleKeepDefaults )
{
    const auto config = ParseConfig( R"( { "max_erases_per_cycle": 0, "settle_seconds": 0 } )" );
    EXPECT_EQ( config.max_erases_per_cycle, 100u );
    EXPECT_EQ( config.settle_seconds, 60u );
}

/** C10 — ill-typed budget/settle members keep defaults (no partial trust). */
TEST_F( PruneEligibilityTest, PruningConfig_IllTypedBudgetAndSettleKeepDefaults )
{
    const auto config = ParseConfig( R"( { "max_erases_per_cycle": "many", "settle_seconds": true } )" );
    EXPECT_EQ( config.max_erases_per_cycle, 100u );
    EXPECT_EQ( config.settle_seconds, 60u );
}

/** C11 — absent compact_threshold_mb keeps the 16MB default (20-01). */
TEST_F( PruneEligibilityTest, PruningConfig_CompactThresholdDefaultWhenAbsent )
{
    const auto empty = ParseConfig( "{}" );
    EXPECT_EQ( empty.compact_threshold_mb, 16u );

    // A fully-specified enabled request without the key: default too.
    const auto enabled = ParseConfig( R"( { "enabled": true, "finality_window": 100 } )" );
    EXPECT_EQ( enabled.compact_threshold_mb, 16u );
}

/** C12 — an explicit positive compact_threshold_mb parses verbatim. */
TEST_F( PruneEligibilityTest, PruningConfig_CompactThresholdExplicitValueParses )
{
    const auto config = ParseConfig( R"( { "compact_threshold_mb": 8 } )" );
    EXPECT_EQ( config.compact_threshold_mb, 8u );
}

/**
 * C13 — a zero compact_threshold_mb is DEGENERATE and rejected at read
 * time: a 0-MB threshold would compact after every cycle — a write-amp
 * denial of service on self (V5). The member keeps its safe default.
 */
TEST_F( PruneEligibilityTest, PruningConfig_ZeroCompactThresholdKeepsDefault )
{
    const auto config = ParseConfig( R"( { "compact_threshold_mb": 0 } )" );
    EXPECT_EQ( config.compact_threshold_mb, 16u );
}

/** C14 — an ill-typed compact_threshold_mb keeps the default (no partial trust). */
TEST_F( PruneEligibilityTest, PruningConfig_IllTypedCompactThresholdKeepsDefault )
{
    const auto config = ParseConfig( R"( { "compact_threshold_mb": "tiny" } )" );
    EXPECT_EQ( config.compact_threshold_mb, 16u );
}

// ---------------------------------------------------------------------------
// Group D: SC-2 — the multi-element block gate (PRUNE-03, D-C3)
// ---------------------------------------------------------------------------

/**
 * A1 — two eligible transactions whose elements landed in ONE delta block
 * (batched commit). The block is erasable: every carried key is
 * allowlist-conformant and inside the eligible set. Block presence and exact
 * carried-key set are asserted BEFORE gating (a vacuous pass proves nothing —
 * Pitfall 11).
 */
TEST_F( PruneEligibilityTest, BlockGate_MultiElementBlockAllEligibleIsErasable )
{
    constexpr uint64_t kWindow = 2;

    // Ancient spender first, then driver heights, so its arrival height sits
    // beyond the window below the final head (D-02).
    auto ancient_spender = MakeTransfer( 1, 0 );
    SeedTransaction( ancient_spender );
    PutSeedValue( "/seed/prune/a1-driver-1", "d1" );

    auto tx1 = MakeTransfer( 2, 1 );
    auto tx2 = MakeTransfer( 3, 1 );
    TrackOutputs( *tx1 );
    TrackOutputs( *tx2 );
    const std::string block_cid = SeedTransactionsInOneBlock( { tx1, tx2 } );
    ASSERT_TRUE( ConsumeOutput( *tx1, 0, *ancient_spender ) );
    ASSERT_TRUE( ConsumeOutput( *tx2, 0, *ancient_spender ) );
    PutSeedValue( "/seed/prune/a1-driver-2", "d2" );

    // Block presence: the committed block carries EXACTLY the two elements.
    auto carried = db_->GetLocalDeltaKeys( block_cid );
    ASSERT_TRUE( carried.has_value() );
    ASSERT_FALSE( carried.value().empty() );
    std::vector<std::string> sorted_keys = carried.value();
    std::sort( sorted_keys.begin(), sorted_keys.end() );
    std::vector<std::string> expected_keys{ TransactionManager::GetTransactionPath( *tx1 ),
                                            TransactionManager::GetTransactionPath( *tx2 ) };
    std::sort( expected_keys.begin(), expected_keys.end() );
    EXPECT_EQ( sorted_keys, expected_keys );

    // Window sanity (D-02): the spender's priority is beyond the window.
    const auto s_priority = db_->GetKeyPriority( TransactionManager::GetTransactionPath( *ancient_spender ) );
    const auto heads      = db_->GetCRDTHeadList();
    ASSERT_TRUE( s_priority.has_value() );
    ASSERT_TRUE( heads.has_value() );
    ASSERT_GE( heads.value().second - s_priority.value(), kWindow );

    PruningConfig config;
    config.finality_window = kWindow;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );

    // Both block-mates are eligible (and the ancient spender reports NO_OUTPUTS).
    ASSERT_EQ( evaluation.value().candidates.size(), 3u );
    ASSERT_EQ( evaluation.value().EligibleCount(), 2u );
    EXPECT_TRUE( FindVerdict( evaluation.value(), *tx1 )->eligible );
    EXPECT_TRUE( FindVerdict( evaluation.value(), *tx2 )->eligible );

    // ONE block report (distinct CID deduplication), erasable, no reasons.
    ASSERT_EQ( evaluation.value().block_reports.size(), 1u );
    const auto &report = evaluation.value().block_reports.front();
    EXPECT_EQ( report.cid, block_cid );
    EXPECT_TRUE( report.erasable );
    EXPECT_TRUE( report.reasons.empty() );
}

/**
 * A2 — same block shape, but one carried element belongs to a NON-eligible
 * transaction (an unspent sibling): the block is NOT erasable and carries
 * NON_ELIGIBLE_SIBLING. The sibling tx itself stays a separate verdict.
 */
TEST_F( PruneEligibilityTest, BlockGate_MultiElementBlockWithLiveSiblingIsNotErasable )
{
    constexpr uint64_t kWindow = 2;

    auto ancient_spender = MakeTransfer( 1, 0 );
    SeedTransaction( ancient_spender );
    PutSeedValue( "/seed/prune/a2-driver-1", "d1" );

    auto eligible_tx     = MakeTransfer( 2, 1 );
    auto unspent_sibling = MakeTransfer( 3, 1 ); // outputs never consumed
    TrackOutputs( *eligible_tx );
    TrackOutputs( *unspent_sibling );
    const std::string block_cid = SeedTransactionsInOneBlock( { eligible_tx, unspent_sibling } );
    ASSERT_TRUE( ConsumeOutput( *eligible_tx, 0, *ancient_spender ) );
    PutSeedValue( "/seed/prune/a2-driver-2", "d2" );

    // Block presence: BOTH elements are in the block.
    auto carried = db_->GetLocalDeltaKeys( block_cid );
    ASSERT_TRUE( carried.has_value() );
    ASSERT_FALSE( carried.value().empty() );

    PruningConfig config;
    config.finality_window = kWindow;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );

    // The tx verdicts stay separate: eligible_tx eligible, sibling not.
    EXPECT_TRUE( FindVerdict( evaluation.value(), *eligible_tx )->eligible );
    const auto *sibling_verdict = FindVerdict( evaluation.value(), *unspent_sibling );
    ASSERT_NE( sibling_verdict, nullptr );
    EXPECT_FALSE( sibling_verdict->eligible );
    EXPECT_TRUE( HasReason( *sibling_verdict, PruneReason::UNSPENT_OUTPUT ) );

    // The BLOCK verdict: held by the sibling.
    ASSERT_EQ( evaluation.value().block_reports.size(), 1u );
    const auto &report = evaluation.value().block_reports.front();
    EXPECT_EQ( report.cid, block_cid );
    EXPECT_FALSE( report.erasable );
    EXPECT_TRUE( HasReason( report, PruneReason::NON_ELIGIBLE_SIBLING ) );
}

/**
 * A3 — a non-tx key committed into the same delta as an eligible element:
 * the block is NOT erasable with OUT_OF_PREFIX (T-17-0504 — nothing smuggles
 * past the per-key allowlist conformance inside the gate).
 */
TEST_F( PruneEligibilityTest, BlockGate_BlockWithOutOfPrefixKeyIsNotErasable )
{
    constexpr uint64_t kWindow = 2;

    auto ancient_spender = MakeTransfer( 1, 0 );
    SeedTransaction( ancient_spender );
    PutSeedValue( "/seed/prune/a3-driver-1", "d1" );

    auto eligible_tx = MakeTransfer( 2, 1 );
    TrackOutputs( *eligible_tx );
    const std::string block_cid = SeedBlockWithOutOfPrefixKey( eligible_tx, "/prune-test/out-of-prefix" );
    SeedTxCidEntry( eligible_tx->GetHash(), block_cid );
    ASSERT_TRUE( ConsumeOutput( *eligible_tx, 0, *ancient_spender ) );
    PutSeedValue( "/seed/prune/a3-driver-2", "d2" );

    // Block presence: the block carries the tx element AND the foreign key.
    auto carried = db_->GetLocalDeltaKeys( block_cid );
    ASSERT_TRUE( carried.has_value() );
    ASSERT_FALSE( carried.value().empty() );

    PruningConfig config;
    config.finality_window = kWindow;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );

    EXPECT_TRUE( FindVerdict( evaluation.value(), *eligible_tx )->eligible );

    ASSERT_EQ( evaluation.value().block_reports.size(), 1u );
    const auto &report = evaluation.value().block_reports.front();
    EXPECT_EQ( report.cid, block_cid );
    EXPECT_FALSE( report.erasable );
    EXPECT_TRUE( HasReason( report, PruneReason::OUT_OF_PREFIX ) );
}

/**
 * A4 — a block carrying a TOMBSTONE for a key not in the eligible set is NOT
 * erasable (Pitfall 4: an elements-only gate would wrongly pass this block —
 * the tombstone key is asserted present in the enumeration, and the
 * NON_ELIGIBLE_SIBLING reason can only come from it).
 */
TEST_F( PruneEligibilityTest, BlockGate_TombstoneKeyHoldsBlockNotErasable )
{
    constexpr uint64_t kWindow = 2;

    auto ancient_spender = MakeTransfer( 1, 0 );
    SeedTransaction( ancient_spender );
    PutSeedValue( "/seed/prune/a4-driver-1", "d1" );

    // Tombstone target: a tx key with garbage value (a would-be candidate that
    // can never be eligible) — committed BEFORE the batch so Remove can build
    // the tombstone delta from the existing element.
    const std::string garbage_hash = std::string( 64, 'g' );
    const std::string garbage_path = TransactionManager::GetTransactionPath( garbage_hash );
    PutSeedValue( garbage_path, std::string( "\xde\xad\xbe\xef" ) );

    auto eligible_tx = MakeTransfer( 2, 1 );
    TrackOutputs( *eligible_tx );
    const std::string block_cid = SeedBlockWithTombstone( eligible_tx, garbage_path );
    SeedTxCidEntry( eligible_tx->GetHash(), block_cid );
    ASSERT_TRUE( ConsumeOutput( *eligible_tx, 0, *ancient_spender ) );
    PutSeedValue( "/seed/prune/a4-driver-2", "d2" );

    // Block presence AND tombstone visibility: the enumeration carries the
    // eligible element AND the tombstone key (elements-only would miss it).
    auto carried = db_->GetLocalDeltaKeys( block_cid );
    ASSERT_TRUE( carried.has_value() );
    ASSERT_FALSE( carried.value().empty() );
    EXPECT_EQ( std::count( carried.value().begin(), carried.value().end(), garbage_path ), 1u );
    EXPECT_EQ( std::count( carried.value().begin(),
                           carried.value().end(),
                           TransactionManager::GetTransactionPath( *eligible_tx ) ),
               1u );

    PruningConfig config;
    config.finality_window = kWindow;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );

    EXPECT_TRUE( FindVerdict( evaluation.value(), *eligible_tx )->eligible );

    // The tombstone key is allowlisted but NOT in the eligible set — the only
    // source of NON_ELIGIBLE_SIBLING on this block (the element is eligible).
    ASSERT_EQ( evaluation.value().block_reports.size(), 1u );
    const auto &report = evaluation.value().block_reports.front();
    EXPECT_EQ( report.cid, block_cid );
    EXPECT_FALSE( report.erasable );
    EXPECT_TRUE( HasReason( report, PruneReason::NON_ELIGIBLE_SIBLING ) );
    EXPECT_FALSE( HasReason( report, PruneReason::OUT_OF_PREFIX ) );
}

/** A5 — an unknown CID fails closed: not erasable, DESERIALIZE_FAILED, no crash. */
TEST_F( PruneEligibilityTest, BlockGate_UnknownCidFailsClosed )
{
    PruningConfig config;
    config.finality_window = 2;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );

    PruneEvaluationResult empty_eligible;
    auto                  gate = evaluator.value()->EvaluateDeltaBlockErasable( "not-a-cid", empty_eligible );
    ASSERT_TRUE( gate.has_value() ); // in-band verdict, never a crash
    EXPECT_FALSE( gate.value().erasable );
    EXPECT_TRUE( HasReason( gate.value(), PruneReason::DESERIALIZE_FAILED ) );
}

// ---------------------------------------------------------------------------
// Group E: D-04 — the pruned-marker contract on the node-local raw surface
// ---------------------------------------------------------------------------

/** B1 — marker round-trip preserves tx_path, cid, and the height pair. */
TEST_F( PruneEligibilityTest, PruneMarker_RoundTripPreservesPathAndCid )
{
    EXPECT_EQ( PruneEligibility::BuildPruneMarkerKey( "abc" ), "/prune/tx/abc" );

    // A hash whose tx value was never seeded: marker present, value absent.
    const std::string                      hash = std::string( 64, 'a' );
    SGTransaction::PrunedTransactionRecord record;
    record.set_tx_path( TransactionManager::GetTransactionPath( hash ) );
    record.set_cid( "bafytestblockcid" );
    record.set_has_erased_at_height( true );
    record.set_erased_at_height( 42 );
    ASSERT_TRUE( PruneEligibility::WritePrunedEntryForTest( *db_, hash, record ).has_value() );

    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, hash );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    ASSERT_TRUE( lookup.value().second.has_value() );
    EXPECT_EQ( lookup.value().second->tx_path(), record.tx_path() );
    EXPECT_EQ( lookup.value().second->cid(), record.cid() );
    EXPECT_TRUE( lookup.value().second->has_erased_at_height() );
    EXPECT_EQ( lookup.value().second->erased_at_height(), 42u );
}

/** B2 — no marker and no value is NOT_FOUND (Phase 19 SURV-02's other half). */
TEST_F( PruneEligibilityTest, PruneMarker_NotFoundByDefault )
{
    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, std::string( 64, 'b' ) );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::NOT_FOUND );
    EXPECT_FALSE( lookup.value().second.has_value() );
}

/**
 * B3 — marker AND value both present is the invariant violation: the enum is
 * reported loudly and is never PRUNED (T-17-0503).
 */
TEST_F( PruneEligibilityTest, PruneMarker_MarkerAndValueIsInvariantViolation )
{
    auto present_tx = MakeTransfer( 1, 1 );
    SeedTransaction( present_tx ); // the tx value IS still present

    SGTransaction::PrunedTransactionRecord record;
    record.set_tx_path( TransactionManager::GetTransactionPath( *present_tx ) );
    record.set_cid( "stale-marker-cid" );
    ASSERT_TRUE( PruneEligibility::WritePrunedEntryForTest( *db_, present_tx->GetHash(), record ).has_value() );

    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, present_tx->GetHash() );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION );
    EXPECT_NE( lookup.value().first, PrunedLookup::PRUNED );
}

/**
 * WR-04 — a decoded marker whose tx_path does NOT end with the requested
 * tx_hash is an invariant-violation error, never a silent PRUNED (the
 * SPENDER_PRUNED_WINDOW_SATISFIED grant depends on this check).
 */
TEST_F( PruneEligibilityTest, PruneMarker_MismatchedPathIsError )
{
    const std::string marker_hash = std::string( 64, 'c' );
    const std::string other_hash  = std::string( 64, 'd' );

    // Well-formed record — but it names ANOTHER transaction's path.
    SGTransaction::PrunedTransactionRecord record;
    record.set_tx_path( TransactionManager::GetTransactionPath( other_hash ) );
    record.set_cid( "cross-path-cid" );
    ASSERT_TRUE( PruneEligibility::WritePrunedEntryForTest( *db_, marker_hash, record ).has_value() );

    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, marker_hash );
    ASSERT_TRUE( lookup.has_error() );
}

/**
 * WR-04 — an EMPTY marker payload decodes as a record with an empty tx_path;
 * the lookup must fail closed instead of prefix-scanning the whole store.
 */
TEST_F( PruneEligibilityTest, PruneMarker_EmptyPayloadIsError )
{
    const std::string                      hash = std::string( 64, 'e' );
    SGTransaction::PrunedTransactionRecord empty_record;
    ASSERT_TRUE( PruneEligibility::WritePrunedEntryForTest( *db_, hash, empty_record ).has_value() );

    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, hash );
    ASSERT_TRUE( lookup.has_error() );
}

/**
 * B4 — a candidate whose spender was ERASED (Phase-16 flow, test-sanctioned)
 * evaluates eligible with the explicit SPENDER_PRUNED_WINDOW_SATISFIED
 * evidence code; the control (erased spender, NO marker) stays
 * SPENDER_UNRESOLVED ineligible (D-02/B spec point 5).
 */
TEST_F( PruneEligibilityTest, PruneMarker_SpenderPrunedWindowSatisfied )
{
    constexpr uint64_t kWindow = 2;

    auto candidate = MakeTransfer( 1, 1 );
    auto spender   = MakeTransfer( 2, 0 );
    SeedTransaction( candidate );
    SeedTransaction( spender );
    TrackOutputs( *candidate );
    ASSERT_TRUE( ConsumeOutput( *candidate, 0, *spender ) );

    auto control         = MakeTransfer( 3, 1 );
    auto control_spender = MakeTransfer( 4, 0 );
    SeedTransaction( control );
    SeedTransaction( control_spender );
    TrackOutputs( *control );
    ASSERT_TRUE( ConsumeOutput( *control, 0, *control_spender ) );

    // A final topic put AFTER all seeds so no erased block is a topic head.
    PutSeedValue( "/seed/prune/b4-driver", "d" );

    // The pruned state, via the REAL Phase-16 erase flow (never hand-deletes).
    ASSERT_TRUE( tm_->EraseTransactionEntry( spender->GetHash() ).has_value() );
    ASSERT_TRUE( tm_->EraseTransactionEntry( control_spender->GetHash() ).has_value() );

    // Marker ONLY for the first spender: candidate's window is satisfied by
    // the marker; control's spender is simply gone.
    SGTransaction::PrunedTransactionRecord record;
    record.set_tx_path( TransactionManager::GetTransactionPath( *spender ) );
    record.set_cid( "erased-spender-cid" );
    ASSERT_TRUE( PruneEligibility::WritePrunedEntryForTest( *db_, spender->GetHash(), record ).has_value() );

    PruningConfig config;
    config.finality_window = kWindow;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );

    // The erased spenders are no longer candidates; both survivors are.
    ASSERT_EQ( evaluation.value().candidates.size(), 2u );

    const auto *candidate_verdict = FindVerdict( evaluation.value(), *candidate );
    ASSERT_NE( candidate_verdict, nullptr );
    EXPECT_TRUE( candidate_verdict->eligible );
    EXPECT_TRUE( HasReason( *candidate_verdict, PruneReason::SPENDER_PRUNED_WINDOW_SATISFIED ) );
    ASSERT_TRUE( candidate_verdict->spend_height.has_value() );
    EXPECT_EQ( candidate_verdict->spend_height.value(), 0u );

    const auto *control_verdict = FindVerdict( evaluation.value(), *control );
    ASSERT_NE( control_verdict, nullptr );
    EXPECT_FALSE( control_verdict->eligible );
    EXPECT_TRUE( HasReason( *control_verdict, PruneReason::SPENDER_UNRESOLVED ) );
}

// ---------------------------------------------------------------------------
// Group F: D-C1 — the production marker write joins the erase WriteBatch
// ---------------------------------------------------------------------------

/**
 * F1 — BuildPruneMarkerEntry produces byte-identical buffers to the
 * test-only writer's PutRaw pair (one key home, one record shape).
 */
TEST_F( PruneEligibilityTest, PruneMarker_BuildEntryBytesMatchTestWriter )
{
    const std::string hash = std::string( 64, 'f' );

    const auto marker_pair = PruneEligibility::BuildPruneMarkerEntry( hash,
                                                                      TransactionManager::GetTransactionPath( hash ) );
    EXPECT_EQ( marker_pair.first.toString(), "/prune/tx/" + hash );

    // The record the production builder embeds: the tx path named by the hash
    // (the WR-04 shape LookupPrunedEntry demands), no optional extras.
    SGTransaction::PrunedTransactionRecord record;
    record.set_tx_path( TransactionManager::GetTransactionPath( hash ) );
    ASSERT_TRUE( PruneEligibility::WritePrunedEntryForTest( *db_, hash, record ).has_value() );

    auto written = db_->GetRaw( marker_pair.first );
    ASSERT_TRUE( written.has_value() );
    EXPECT_EQ( written.value().toString(), marker_pair.second.toString() );

    // The staged pair decodes to a PRUNED verdict against a value that never
    // existed (path named by the hash, value absent).
    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, hash );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
}

/**
 * F2 — an erase invoked WITH the staged marker pair lands value, block, and
 * marker in ONE commit: value absent, block absent, marker PRUNED (the
 * value-XOR-marker invariant holds atomically).
 */
TEST_F( PruneEligibilityTest, PruneMarker_EraseWithStagedMarkerCommitsAtomically )
{
    auto              tx      = MakeTransfer( 1, 1 );
    const std::string tx_path = TransactionManager::GetTransactionPath( *tx );

    crdt::GlobalDB::Buffer serialized;
    serialized.put( tx->SerializeByteVector() );
    auto put_result = db_->Put( { tx_path }, serialized, { kSeedTopic } );
    ASSERT_TRUE( put_result.has_value() );
    const std::string block_cid = put_result.value().toString().value();

    crdt::GlobalDB::Buffer map_key;
    map_key.put( tx_path );
    crdt::GlobalDB::Buffer map_value;
    map_value.put( block_cid );
    ASSERT_TRUE( db_->GetDataStore()->put( map_key, map_value ).has_value() );

    // A later topic put so the seeded block is NOT the topic head (guard 1).
    PutSeedValue( "/seed/prune/f2-driver", "d" );

    // Pre-state: value present, block present, marker absent.
    ASSERT_TRUE( db_->GetRaw( map_key ).has_value() );
    auto carried = db_->GetLocalDeltaKeys( block_cid );
    ASSERT_TRUE( carried.has_value() );
    ASSERT_FALSE( carried.value().empty() );
    auto pre_lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
    ASSERT_TRUE( pre_lookup.has_value() );
    EXPECT_EQ( pre_lookup.value().first, PrunedLookup::NOT_FOUND );

    auto cid = CID::fromString( block_cid );
    ASSERT_TRUE( cid.has_value() );
    const auto marker_pair = PruneEligibility::BuildPruneMarkerEntry( tx->GetHash(), tx_path );

    auto erased = db_->EraseEntry( tx_path, cid.value(), marker_pair );
    ASSERT_TRUE( erased.has_value() );

    // One commit, both sides: value gone, block gone, marker present.
    auto raw_after = db_->GetRaw( map_key );
    ASSERT_TRUE( raw_after.has_error() );
    EXPECT_TRUE( db_->GetLocalDeltaKeys( block_cid ).has_error() );

    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    ASSERT_TRUE( lookup.value().second.has_value() );
    EXPECT_EQ( lookup.value().second->tx_path(), tx_path );
}

/**
 * F3 — a seam-interrupted batch (every pre-commit step) leaves BOTH sides in
 * the pre-erase state: the value is untouched AND no marker exists. Value
 * XOR marker is never observable mid-flight.
 */
TEST_F( PruneEligibilityTest, PruneMarker_InterruptedEraseLeavesValueAndMarkerUntouched )
{
    auto              tx      = MakeTransfer( 2, 1 );
    const std::string tx_path = TransactionManager::GetTransactionPath( *tx );

    crdt::GlobalDB::Buffer serialized;
    serialized.put( tx->SerializeByteVector() );
    auto put_result = db_->Put( { tx_path }, serialized, { kSeedTopic } );
    ASSERT_TRUE( put_result.has_value() );
    const std::string block_cid = put_result.value().toString().value();

    crdt::GlobalDB::Buffer map_key;
    map_key.put( tx_path );
    crdt::GlobalDB::Buffer map_value;
    map_value.put( block_cid );
    ASSERT_TRUE( db_->GetDataStore()->put( map_key, map_value ).has_value() );

    PutSeedValue( "/seed/prune/f3-driver", "d" );

    auto cid = CID::fromString( block_cid );
    ASSERT_TRUE( cid.has_value() );
    auto marker_pair = PruneEligibility::BuildPruneMarkerEntry( tx->GetHash(), tx_path );

    for ( const int step : { 1, 2, 3 } )
    {
        // Reset to the pre-erase state before each interrupted attempt.
        ASSERT_TRUE( db_->GetRaw( map_key ).has_value() );
        auto pre_lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( pre_lookup.has_value() );
        ASSERT_EQ( pre_lookup.value().first, PrunedLookup::NOT_FOUND );

        crdt::GlobalDB::SetEraseInterruptHookForTest( [step]( int fired ) { return fired == step; } );
        auto erased = db_->EraseEntry( tx_path, cid.value(), marker_pair );
        crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

        ASSERT_TRUE( erased.has_error() );

        // Both sides untouched: the value is still present and no marker
        // was written (the dying batch proved all-or-nothing).
        EXPECT_TRUE( db_->GetRaw( map_key ).has_value() );
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::NOT_FOUND );
    }
}

// ---------------------------------------------------------------------------
// Group G: stranding-edge evidence + tick-gate input + the pruned-erase driver
// ---------------------------------------------------------------------------

namespace
{
    /// Test-fabricated well-formed CID (never published, never sealed) — the
    /// absent-block shape (globaldb_erase_test makeUnseenCid recipe).
    sgns::CID MakeUnseenCid( const std::vector<uint8_t> &digest )
    {
        using libp2p::multi::HashType;
        using libp2p::multi::Multihash;
        return sgns::CID( sgns::CID::Version::V1,
                          sgns::CID::Multicodec::SHA2_256,
                          Multihash::create( HashType::sha256, digest ).value() );
    }
} // namespace

/**
 * G1 — the stranding edge closed at the gate: after one sibling's sanctioned
 * erase deleted the SHARED delta block (the exhausted-budget shape), the
 * surviving eligible sibling's block gate sees the ABSENT block and returns
 * an ERASABLE verdict carrying the explicit BLOCK_ALREADY_ERASED evidence —
 * never a fail-closed stranding.
 */
TEST_F( PruneEligibilityTest, BlockGate_AbsentBlockCompletesEligibleSibling )
{
    constexpr uint64_t kWindow = 2;

    auto ancient_spender = MakeTransfer( 1, 0 );
    SeedTransaction( ancient_spender );
    PutSeedValue( "/seed/prune/g1-driver-1", "d1" );

    auto tx1 = MakeTransfer( 2, 1 );
    auto tx2 = MakeTransfer( 3, 1 );
    TrackOutputs( *tx1 );
    TrackOutputs( *tx2 );
    const std::string shared_cid = SeedTransactionsInOneBlock( { tx1, tx2 } );
    ASSERT_FALSE( shared_cid.empty() );
    ASSERT_TRUE( ConsumeOutput( *tx1, 0, *ancient_spender ) );
    ASSERT_TRUE( ConsumeOutput( *tx2, 0, *ancient_spender ) );
    PutSeedValue( "/seed/prune/g1-driver-2", "d2" );

    PruningConfig config;
    config.finality_window = kWindow;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    {
        // Both siblings eligible before the erase (the A1 recipe).
        auto evaluation = evaluator.value()->Evaluate();
        ASSERT_TRUE( evaluation.has_value() );
        ASSERT_NE( FindVerdict( evaluation.value(), *tx1 ), nullptr );
        EXPECT_TRUE( FindVerdict( evaluation.value(), *tx1 )->eligible );
        ASSERT_NE( FindVerdict( evaluation.value(), *tx2 ), nullptr );
        EXPECT_TRUE( FindVerdict( evaluation.value(), *tx2 )->eligible );
    }

    // The budget-exhaustion shape: tx1's sanctioned erase deletes the SHARED
    // block; tx2 is left behind stranded.
    ASSERT_TRUE( tm_->EraseTransactionEntry( tx1->GetHash() ).has_value() );
    ASSERT_TRUE( db_->GetLocalDeltaKeys( shared_cid ).has_error() ); // block gone

    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );
    const auto *tx2_verdict = FindVerdict( evaluation.value(), *tx2 );
    ASSERT_NE( tx2_verdict, nullptr );
    EXPECT_TRUE( tx2_verdict->eligible );

    const auto *report = FindBlockReport( evaluation.value(), shared_cid );
    ASSERT_NE( report, nullptr );
    EXPECT_TRUE( report->erasable );
    EXPECT_TRUE( HasReason( *report, PruneReason::BLOCK_ALREADY_ERASED ) );
}

/**
 * G2 — the discrimination case: a block whose BYTES are present but
 * unparseable still fails closed with DESERIALIZE_FAILED — the
 * absent-block evidence never fires for a corrupt block (byte presence,
 * not parse success, decides absence).
 */
TEST_F( PruneEligibilityTest, BlockGate_CorruptBlockStillFailsClosed )
{
    auto ancient_spender = MakeTransfer( 4, 0 );
    SeedTransaction( ancient_spender );
    PutSeedValue( "/seed/prune/g2-driver-1", "d1" );

    auto tx = MakeTransfer( 5, 1 );
    TrackOutputs( *tx );
    const std::string block_cid = SeedTransactionsInOneBlock( { tx } );
    ASSERT_FALSE( block_cid.empty() );
    ASSERT_TRUE( ConsumeOutput( *tx, 0, *ancient_spender ) );
    PutSeedValue( "/seed/prune/g2-driver-2", "d2" );

    // Corrupt the block bytes in place: present-but-unparseable. The key is
    // exactly CID::toBytes() (the StageBlockRemoval / RocksdbDatastore
    // encode shape).
    auto cid = CID::fromString( block_cid );
    ASSERT_TRUE( cid.has_value() );
    auto key_bytes = cid.value().toBytes();
    ASSERT_TRUE( key_bytes.has_value() );
    crdt::GlobalDB::Buffer block_key( std::move( key_bytes.value() ) );
    crdt::GlobalDB::Buffer garbage;
    garbage.put( std::string( "corrupt-block-bytes-not-a-dag-node" ) );
    ASSERT_TRUE( db_->GetDataStore()->put( block_key, garbage ).has_value() );

    // The corruption is observable: the local parse path fails.
    ASSERT_TRUE( db_->GetLocalDeltaKeys( block_cid ).has_error() );

    PruningConfig config;
    config.finality_window = 2;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );

    const auto *report = FindBlockReport( evaluation.value(), block_cid );
    ASSERT_NE( report, nullptr );
    EXPECT_FALSE( report->erasable );
    EXPECT_TRUE( HasReason( *report, PruneReason::DESERIALIZE_FAILED ) );
    EXPECT_FALSE( HasReason( *report, PruneReason::BLOCK_ALREADY_ERASED ) );
}

/**
 * G3 — the evidence only COMPLETES an otherwise-eligible erase: an absent
 * block tied to a NON-eligible candidate stays not-erasable (fail closed).
 */
TEST_F( PruneEligibilityTest, BlockGate_AbsentBlockNonEligibleCandidateNotErasable )
{
    // A candidate that is NOT eligible: its only output was spent RECENTLY.
    auto candidate      = MakeTransfer( 6, 1 );
    auto recent_spender = MakeTransfer( 7, 0 );
    SeedTransaction( candidate );
    SeedTransaction( recent_spender );
    TrackOutputs( *candidate );
    ASSERT_TRUE( ConsumeOutput( *candidate, 0, *recent_spender ) );

    // A well-formed CID whose block never existed here — the absent shape —
    // fabricated into the map entry (the gate's input wiring).
    const std::string absent_cid = MakeUnseenCid( { 0x11, 0x22, 0x33 } ).toString().value();
    SeedTxCidEntry( candidate->GetHash(), absent_cid );
    PutSeedValue( "/seed/prune/g3-driver", "d" );

    PruningConfig config;
    config.finality_window = 2;
    auto evaluator         = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
    ASSERT_TRUE( evaluator.has_value() );
    auto evaluation = evaluator.value()->Evaluate();
    ASSERT_TRUE( evaluation.has_value() );

    const auto *verdict = FindVerdict( evaluation.value(), *candidate );
    ASSERT_NE( verdict, nullptr );
    EXPECT_FALSE( verdict->eligible );

    // Direct gate call: absent block tied only to the NON-eligible candidate.
    auto gate = evaluator.value()->EvaluateDeltaBlockErasable( absent_cid, evaluation.value() );
    ASSERT_TRUE( gate.has_value() );
    EXPECT_FALSE( gate.value().erasable );
    EXPECT_FALSE( HasReason( gate.value(), PruneReason::BLOCK_ALREADY_ERASED ) );
}

/** G4 — the tick-gate input: HasMissingTxHashes is const and false when the missing set is empty. */
TEST_F( PruneEligibilityTest, TickGate_HasMissingTxHashesFalseWhenEmpty )
{
    const TransactionManager &const_tm = *tm_;
    EXPECT_FALSE( const_tm.HasMissingTxHashes() );
}

/**
 * G5 — the production pruned-erase driver: resolves and erases exactly like
 * EraseTransactionEntry (one resolution home) AND stages the marker pair, so
 * the completed erase answers PRUNED through the tri-state lookup.
 */
TEST_F( PruneEligibilityTest, PrunedEraseDriver_ErasesAndMarksAtomically )
{
    auto ancient_spender = MakeTransfer( 8, 0 );
    SeedTransaction( ancient_spender );
    PutSeedValue( "/seed/prune/g5-driver-1", "d1" );

    auto tx = MakeTransfer( 9, 1 );
    TrackOutputs( *tx );
    SeedTransaction( tx );
    ASSERT_TRUE( ConsumeOutput( *tx, 0, *ancient_spender ) );
    PutSeedValue( "/seed/prune/g5-driver-2", "d2" );

    auto pre = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
    ASSERT_TRUE( pre.has_value() );
    EXPECT_EQ( pre.value().first, PrunedLookup::NOT_FOUND );

    ASSERT_TRUE( tm_->EraseTransactionEntryPruned( tx->GetHash() ).has_value() );

    // Value and map entry gone; the marker answers PRUNED.
    crdt::GlobalDB::Buffer map_key;
    map_key.put( TransactionManager::GetTransactionPath( *tx ) );
    EXPECT_TRUE( db_->GetRaw( map_key ).has_error() );

    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    ASSERT_TRUE( lookup.value().second.has_value() );
    EXPECT_EQ( lookup.value().second->tx_path(), TransactionManager::GetTransactionPath( *tx ) );
}

/**
 * WR-02 (driver-level): the pruned driver marks the RESOLVED network path.
 * An entry tracked under a NON-local monitored network (the map spans every
 * monitored network) is erased at ITS path, and the marker's record names
 * THAT path — never the local network's formatting of the hash (which names
 * a key that never existed and deafens the invariant detector).
 */
TEST_F( PruneEligibilityTest, PrunedEraseDriver_MarksTheResolvedCrossNetworkPath )
{
    auto tx = MakeTransfer( 10, 1 );

    const std::string local_path   = TransactionManager::GetTransactionPath( *tx );
    const std::string foreign_path = TransactionManager::GetTransactionPath( sgns::version::TEST_NET_ID,
                                                                             tx->GetHash() );
    ASSERT_NE( foreign_path, local_path );

    // Seed the value + tx->CID map entry ONLY under the foreign network's
    // path (the SeedTransaction shapes, foreign-path flavour).
    crdt::GlobalDB::Buffer serialized;
    serialized.put( tx->SerializeByteVector() );
    auto put_result = db_->Put( { foreign_path }, serialized, { kSeedTopic } );
    ASSERT_TRUE( put_result.has_value() );
    const std::string block_cid = put_result.value().toString().value();

    crdt::GlobalDB::Buffer key_buffer;
    key_buffer.put( foreign_path );
    crdt::GlobalDB::Buffer value_buffer;
    value_buffer.put( block_cid );
    ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );

    // A later topic put so the seeded block is NOT the topic head (guard 1).
    PutSeedValue( "/seed/prune/g6-driver", "d" );

    ASSERT_TRUE( tm_->EraseTransactionEntryPruned( tx->GetHash() ).has_value() );

    // The foreign-network value is gone and the marker names THE RESOLVED path.
    crdt::GlobalDB::Buffer probe;
    probe.put( foreign_path );
    EXPECT_TRUE( db_->GetRaw( probe ).has_error() );

    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    ASSERT_TRUE( lookup.value().second.has_value() );
    EXPECT_EQ( lookup.value().second->tx_path(), foreign_path );
}

/**
 * WR-02 (unit): the production builder carries the RESOLVED path verbatim —
 * the marker key home is unchanged by network, and a marker naming a
 * cross-network path whose value still EXISTS trips the invariant violation
 * through the real probe (a local-network-formatted record would probe a
 * vacuously absent key and wrongly answer PRUNED).
 */
TEST_F( PruneEligibilityTest, PruneMarker_CrossNetworkRecordCarriesResolvedPath )
{
    const std::string hash = std::string( 64, '2' );

    const std::string local_path   = TransactionManager::GetTransactionPath( hash );
    const std::string foreign_path = TransactionManager::GetTransactionPath( sgns::version::TEST_NET_ID, hash );
    ASSERT_NE( foreign_path, local_path );

    const auto marker_pair = PruneEligibility::BuildPruneMarkerEntry( hash, foreign_path );
    EXPECT_EQ( marker_pair.first.toString(), "/prune/tx/" + hash ); // key home unchanged by network

    // The value still EXISTS at the resolved path.
    crdt::GlobalDB::Buffer value;
    value.put( "survivor-value" );
    ASSERT_TRUE( db_->Put( { foreign_path }, value, { kSeedTopic } ).has_value() );

    SGTransaction::PrunedTransactionRecord record;
    record.set_tx_path( foreign_path );
    ASSERT_TRUE( PruneEligibility::WritePrunedEntryForTest( *db_, hash, record ).has_value() );

    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, hash );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION );
    EXPECT_NE( lookup.value().first, PrunedLookup::PRUNED );
}
