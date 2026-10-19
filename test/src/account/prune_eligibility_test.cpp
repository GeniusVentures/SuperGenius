/**
 * @file       prune_eligibility_test.cpp
 * @brief      SC-1 (exact eligible set) and SC-4 (fail-closed gates) proofs for
 *             the PRUNE-01 dry-run eligibility evaluator.
 * @details    Group A seeds the three-way window discriminator plus every spend
 *             discriminator through the REAL publish path on the fixture's CRDT
 *             topic (never raw local puts — Phase-16 lesson) and asserts the
 *             eligible set is EXACTLY the expected transaction, including the
 *             owner's counterexample: an ancient-creation tx spent recently is
 *             NEVER eligible (the window reads spend recency, never creation
 *             age, D-02). Group B proves construction refuses Full/Archive and
 *             the allowlist rejects five crafted out-of-prefix keys. Group C
 *             pins the PruningConfig parse/validate contract (GATE-06).
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
