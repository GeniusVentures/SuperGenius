/**
 * @file       tx_pruner_test.cpp
 * @brief      SC-1 (first real erase), cycle idempotence, the sweep-as-cycle
 *             re-prune (D-02b), the stranding-edge budget behavior, the SC-4
 *             tick-gate refusals, the RECL-03/RECL-04 observability +
 *             status contracts (including the D-01 compose-lock
 *             non-interleaving proof), the SC-2 crash matrix — killing
 *             the cycle at EVERY interrupt point of BOTH seams and reopening
 *             the database on the same path converges to the same fixed
 *             point — and the SURV-01 RestartMatrix: a pruned node
 *             restarted on the same path boots through the REAL StartCore
 *             path to READY with ZERO pruned-CID fetch attempts, syncs a
 *             new tip, receives/spends, and chains the next outgoing tx
 *             correctly through the pruned frontier — all against the
 *             TxPruner SERVICE on real stores (GlobalDB + pubsub + temp
 *             dir, no GeniusNode).
 * @details    Every suite targets the service because GeniusNode is not
 *             unit-testable (the established testability split). Seeding
 *             always goes through the REAL publish path on the fixture's
 *             CRDT topic (never raw local puts — the Phase-16 lesson), and
 *             every erase assertion is three-part (CRDT value absent, raw
 *             tx->CID map entry absent, /prune/tx/<hash> marker PRUNED) with
 *             negative controls and byte-identity snapshots of everything
 *             the cycle must NOT touch (the /utxo/ prefix, non-tx records,
 *             the DAG heads, and the seal markers — GATE-03). Fixture
 *             discipline: BOTH interrupt seams (the cycle seam and
 *             GlobalDB's erase seam) are cleared FIRST in the fixture
 *             destructor so a hook armed by a failing case never leaks, and
 *             fresh DB paths back every case (double-run safe).
 *
 *             The RECL-03/RECL-04 honest-split contract: the erase-time
 *             reporting stays counts-only, PruneStatus.bytes_reclaimed is
 *             stamped ONLY at compaction success (nullopt under the default
 *             threshold — these fixtures never cross it), and the
 *             CompactionGate_ canary crosses a 1MB threshold end-to-end and
 *             asserts the measured field.
 * @date       2026-10-21
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/host/host.hpp>

#include "account/GeniusAccount.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ConsensusAuth.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"
#include "transaction/PruneEligibility.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TxPruner.hpp"

using namespace sgns;

namespace sgns
{
    /**
     * Reuse of the existing test seam declared in TransactionManager.hpp
     * (friend class TransactionManagerPendingLifecycleTestAccess): the
     * TxPruner tick gate requires State::READY, which no public API reaches
     * deterministically (StartCore's async nonce path is network-timed).
     * The pending-lifecycle test owns the same class name in its own binary;
     * this per-binary definition carries only the members this binary uses:
     * ChangeState (the tick gate), the two RebuildGuard drivers (the real
     * InitializeUTXOs boot path and the belt pass), and the RestartMatrix
     * send-path drivers (enqueue, the nonce-reserving DAG fill, and the
     * GATE-04 previous-hash ladder — all protected/private members).
     */
    class TransactionManagerPendingLifecycleTestAccess
    {
    public:
        static void ChangeState( TransactionManager &manager, TransactionManager::State state )
        {
            manager.ChangeState( state );
        }

        static void RunInitializeUTXOs( TransactionManager &manager )
        {
            manager.InitializeUTXOs();
        }

        static size_t RunCrosscheckRebuiltUtxos( TransactionManager &manager )
        {
            return manager.CrosscheckRebuiltUtxos();
        }

        static outcome::result<void> ChangeTransactionState( TransactionManager                       &manager,
                                                             const std::shared_ptr<GeniusTransaction> &transaction,
                                                             TransactionManager::TransactionStatus     status )
        {
            return manager.ChangeTransactionState( transaction, status );
        }

        /// Enqueues @p transaction as a one-tx outgoing batch staged on @p atomic_tx
        /// (the pending-lifecycle binary's Enqueue shape).
        static void EnqueueOutgoing( TransactionManager                       &manager,
                                     const std::shared_ptr<GeniusTransaction> &transaction,
                                     std::shared_ptr<crdt::AtomicTransaction>  atomic_tx )
        {
            TransactionManager::TransactionBatch batch;
            batch.emplace_back( transaction, std::nullopt );
            manager.EnqueueTransaction( { std::move( batch ), std::move( atomic_tx ) } );
        }

        /// The production outgoing-tx DAG fill: reserves the next nonce and
        /// resolves previous_hash through the GATE-04 ladder.
        static SGTransaction::DAGStruct RunFillDAGStruct( TransactionManager &manager )
        {
            return manager.FillDAGStruct();
        }

        /// One iteration of the manager's own tick loop. The fixture's io_ is
        /// a hand-driven context (the CRDTFixture contract), so on the REAL
        /// -boot leg the test thread drives the loop StartCore armed — the
        /// loop logic is the production one, only its scheduler is manual.
        static void RunTickOnce( TransactionManager &manager )
        {
            manager.TickOnce();
        }

        /// The GATE-04 ladder read (cert-slot rung is pruned-proof by design).
        static std::string OutgoingPreviousHash( const TransactionManager &manager, uint64_t nonce )
        {
            return manager.GetOutgoingPreviousHash( nonce );
        }
    };
} // namespace sgns

namespace
{
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// The fixture's pre-seeded CRDT topic: puts on it create real blocks,
    /// heads, and seals (an empty-topics Put creates no destinations).
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// Finality window N for every suite (the 17-04 recipes' value).
    constexpr uint64_t kWindow = 2;

    /// The account-generation tag the tests stamp onto cycles (D-C4).
    constexpr uint64_t kTestGeneration = 7;

    /// Settle window used wherever a cycle must run soon (waited out through
    /// the settled-cycle helper, never slept past blindly).
    constexpr uint64_t kSmallSettleSeconds = 1;

    /// Raw-surface snapshot: full key -> value bytes under one prefix.
    using RawSnapshot = std::map<std::string, std::string>;

    /// Binary-safe std::string copy of a buffer (Buffer::toString is a view).
    std::string BytesOf( const base::Buffer &buffer )
    {
        return std::string( buffer.toString() );
    }

    /// Simplified head map: topic -> set of CID strings.
    using HeadSnapshot = std::map<std::string, std::set<std::string>>;
} // namespace

/**
 * @brief Real-store fixture: GlobalDB + Blockchain + TransactionManager
 *        (NodeType::Light, no GeniusNode, no network sync) — the
 *        PruneEligibilityTest stack with its load-bearing teardown order,
 *        plus the TxPruner seeding/snapshot helpers.
 */
class TxPrunerTest : public ::test::CRDTFixture
{
public:
    TxPrunerTest() : ::test::CRDTFixture( "tx_pruner_test" )
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

    ~TxPrunerTest() override
    {
        // Seam discipline (the globaldb_erase_test destructor order): a hook
        // armed by a failing case must never leak further — clear BOTH
        // interrupt seams before any teardown touches the stores.
        sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
        crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

        /*
         * Teardown invariant (asio), copied verbatim from
         * PruneEligibilityTest/CertificateFallbackTest: stop the blockchain
         * FIRST so the ConsensusManager round-timer thread joins before any
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

        // The nonce responder (RestartMatrix legs only) is independent of the
        // fixture's db stack: stop its messenger worker thread BEFORE its
        // pubsub host (the responder has no db/graphsync chain of its own).
        if ( nonce_responder_account_ )
        {
            nonce_responder_account_->StopMessenger();
            nonce_responder_account_.reset();
        }
        if ( nonce_responder_pubsub_ )
        {
            nonce_responder_pubsub_->Stop();
            nonce_responder_pubsub_.reset();
        }
    }

    std::shared_ptr<GeniusAccount>      account_;
    std::shared_ptr<Blockchain>         blockchain_;
    std::shared_ptr<TransactionManager> tm_;

    //
    // SURV-01 nonce responder (RestartMatrix legs): CheckNonce's trusting
    // -local path needs a peer that ANSWERS the nonce request (with or
    // without a nonce) — a strictly peerless Light node fails closed on the
    // fetch error. The responder is a second real pubsub host carrying an
    // ephemeral account's messenger; it never joins a CRDT topic, so it can
    // move neither the fetch counter nor any CRDT state.
    //
    std::shared_ptr<ipfs_pubsub::GossipPubSub> nonce_responder_pubsub_;
    std::shared_ptr<GeniusAccount>             nonce_responder_account_;

    //
    // Seeding helpers — every transaction goes through the REAL topic Put so
    // its CRDT value, delta CID, head, and per-key priority all exist, then
    // gets a raw tx->CID map entry in the exact StoreTransactionCID shapes
    // (the PruneEligibilityTest recipes verbatim).
    //

    /// Transfer with @p output_count outputs owned by this account (empty for spenders).
    /// @p dest_pad_bytes appends that many low-entropy bytes to every
    /// destination address — sizes the serialized VALUE for the compaction
    /// threshold without extra outputs (the CompactionGate canary).
    std::shared_ptr<TransferTransaction> MakeTransfer( uint64_t nonce, size_t output_count, size_t dest_pad_bytes = 0 )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );

        const std::string destination = dest_pad_bytes > 0 ? account_->GetAddress() + std::string( dest_pad_bytes, 'x' )
                                                           : account_->GetAddress();
        std::vector<OutputDestInfo> destinations;
        destinations.reserve( output_count );
        for ( size_t i = 0; i < output_count; ++i )
        {
            destinations.push_back( { 100 + i, destination, kTestTokenId } );
        }
        return std::make_shared<TransferTransaction>(
            TransferTransaction::New( {}, std::move( destinations ), std::move( dag ) ) );
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
    /// AtomicTransaction on the seed topic — the shared-block recipe) and
    /// records each tx->CID map entry against that block's CID.
    void SeedTransactionsInOneBlock( const std::vector<std::shared_ptr<GeniusTransaction>> &transactions )
    {
        auto batch = db_->BeginTransaction();
        ASSERT_NE( batch, nullptr );
        if ( batch == nullptr )
        {
            return;
        }
        for ( const auto &transaction : transactions )
        {
            crdt::GlobalDB::Buffer serialized;
            serialized.put( transaction->SerializeByteVector() );
            ASSERT_TRUE(
                batch->Put( { TransactionManager::GetTransactionPath( *transaction ) }, serialized ).has_value() );
        }
        auto commit = batch->Commit( { kSeedTopic } );
        ASSERT_TRUE( commit.has_value() );
        if ( !commit.has_value() )
        {
            return;
        }
        const std::string block_cid = commit.value().toString().value();

        for ( const auto &transaction : transactions )
        {
            crdt::GlobalDB::Buffer key_buffer;
            key_buffer.put( TransactionManager::GetTransactionPath( *transaction ) );
            crdt::GlobalDB::Buffer value_buffer;
            value_buffer.put( block_cid );
            ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
        }
    }

    /// Plain topic Put of a raw string at @p key (driver heights, non-tx
    /// data, tombstone targets) — real publish path, never a local-only put.
    void PutSeedValue( const std::string &key, const std::string &value )
    {
        crdt::GlobalDB::Buffer buffer;
        buffer.put( value );
        ASSERT_TRUE( db_->Put( { key }, buffer, { kSeedTopic } ).has_value() );
    }

    /// PutSeedValue that also reports the delta CID string (seal snapshots).
    std::string PutSeedValueReturningCid( const std::string &key, const std::string &value )
    {
        crdt::GlobalDB::Buffer buffer;
        buffer.put( value );
        auto put = db_->Put( { key }, buffer, { kSeedTopic } );
        EXPECT_TRUE( put.has_value() );
        return put.has_value() ? put.value().toString().value() : "";
    }

    /// Records the tx->CID map entry (StoreTransactionCID shapes) for @p hash
    /// against @p cid — the resurrection re-wiring after a re-publish.
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

    /// Spends one output by @p spender; returns ConsumeUTXOs's verdict.
    /// @p owner_address overrides the consuming owner (a padded destination
    /// — ConsumeUTXOs matches the STORED owner, so a padded output must be
    /// consumed under its own address).
    bool ConsumeOutput( const GeniusTransaction &transaction,
                        uint32_t                 output_idx,
                        const GeniusTransaction &spender,
                        const std::string       &owner_address = "" )
    {
        auto txid = base::Hash256::fromReadableString( transaction.GetHash() );
        EXPECT_FALSE( txid.has_error() );
        auto consumed = account_->GetUTXOManager().ConsumeUTXOs(
            { { txid.value(), output_idx, {} } },
            owner_address.empty() ? account_->GetAddress() : owner_address,
            UTXOManager::UTXOType::UTXO_NORMAL,
            SpenderHash( spender ) );
        EXPECT_TRUE( consumed.has_value() );
        return consumed.has_value() && consumed.value();
    }

    static base::Hash256 SpenderHash( const GeniusTransaction &transaction )
    {
        return base::Hash256::fromReadableString( transaction.GetHash() ).value();
    }

    //
    // RebuildGuard helpers (GATE-05's checkpoint-rebuild half, D-01): the
    // certificate recipe is the pending-lifecycle binary's verbatim shape, the
    // mismatch arm seeds the RELOAD shape 19-01 proved is the real-world wipe
    // path (disk-resident CONSUMED evidence reloaded through LoadUTXOs), and
    // the raw-record helpers give the Pitfall-2 disk-level proofs.
    //

    /// Validator registry backing SeedCertificate; lazily initialized so only
    /// RebuildGuard tests carry registry records in their fresh per-test DB.
    std::shared_ptr<ValidatorRegistry> registry_;

    /// Seeds a valid consensus certificate for @p transaction at its slot (the
    /// transaction_manager_pending_lifecycle_test recipe verbatim). The nonce
    /// subject's account is the transaction's OWN source address, so foreign
    /// -source deliveries (the RestartMatrix receive legs) certify through the
    /// same recipe as self-sourced ones.
    void SeedCertificate( const std::shared_ptr<GeniusTransaction> &transaction )
    {
        if ( registry_ == nullptr )
        {
            registry_ = blockchain_->GetValidatorRegistry();
            ASSERT_TRUE( registry_ != nullptr );
            ASSERT_TRUE( registry_
                             ->StoreGenesisRegistry( { account_->GetAddress() },
                                                     [this]( std::vector<uint8_t> payload )
                                                     { return account_->Sign( payload ); } )
                             .has_value() );
        }

        auto subject = ConsensusManager::CreateNonceSubject( transaction->GetSrcAddress(),
                                                             transaction->GetNonce(),
                                                             transaction->GetHash(),
                                                             transaction->SerializeToEmbeddedTransaction(),
                                                             std::nullopt,
                                                             std::nullopt );
        ASSERT_TRUE( subject.has_value() );

        auto proposal = ConsensusManager::CreateProposal( subject.value(),
                                                          account_->GetAddress(),
                                                          registry_->GetRegistryCid(),
                                                          registry_->GetRegistryEpoch(),
                                                          [this]( std::vector<uint8_t> payload )
                                                          { return account_->Sign( payload ); } );
        ASSERT_TRUE( proposal.has_value() );

        ConsensusManager::Vote vote;
        vote.set_proposal_id( proposal.value().proposal_id() );
        vote.set_voter_id( account_->GetAddress() );
        vote.set_approve( true );
        vote.set_timestamp( proposal.value().timestamp() );
        auto vote_bytes = VoteSigningBytes( vote );
        ASSERT_TRUE( vote_bytes.has_value() );
        const auto vote_signature = account_->Sign( std::move( vote_bytes.value() ) );
        vote.set_signature( vote_signature.data(), vote_signature.size() );

        ConsensusManager::Certificate certificate;
        certificate.set_proposal_id( proposal.value().proposal_id() );
        certificate.set_registry_cid( proposal.value().registry_cid() );
        certificate.set_registry_epoch( proposal.value().registry_epoch() );
        certificate.set_total_weight( 1 );
        certificate.set_approved_weight( 1 );
        certificate.set_timestamp( vote.timestamp() );
        *certificate.add_votes()        = vote;
        *certificate.mutable_proposal() = proposal.value();

        crdt::GlobalDB::Buffer certificate_data;
        certificate_data.put( certificate.SerializeAsString() );
        ASSERT_TRUE(
            db_->Put( crdt::HierarchicalKey( "/cert/" + transaction->GetSlotID() ), certificate_data, { kSeedTopic } )
                .has_value() );
        ASSERT_TRUE( blockchain_->CheckCertificateForSlot( transaction->GetSlotID() ) );
    }

    /// Signed transfer spending @p source's output @p output_idx (nonce @p
    /// nonce, one change output back to this account).
    std::shared_ptr<TransferTransaction> MakeSpender( const GeniusTransaction &source,
                                                      uint32_t                 output_idx,
                                                      uint64_t                 nonce )
    {
        auto source_txid = base::Hash256::fromReadableString( source.GetHash() );
        EXPECT_FALSE( source_txid.has_error() );

        InputUTXOInfo input;
        input.txid_hash_  = source_txid.value();
        input.output_idx_ = output_idx;
        input.signature_  = account_->Sign( input.SerializeForSigning() );

        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );

        auto spender = std::make_shared<TransferTransaction>(
            TransferTransaction::New( { std::move( input ) },
                                      { OutputDestInfo{ 100, account_->GetAddress(), kTestTokenId } },
                                      std::move( dag ) ) );
        spender->MakeSignature( *account_ );
        return spender;
    }

    /// The deterministic checkpoint-mismatch arm: a certified producer (one
    /// READY output), then a certified spender consuming that output (change
    /// READY). Everything applies through the REAL stack (certificate delivery
    /// -> consensus handler -> effects); the handler's approval-time
    /// checkpoint leaves the latest checkpoint rooted over the post-spend
    /// READY set (the change output). The mismatch itself is armed per-case by
    /// ArmMismatchByLosingChangeOutput (the plan-sanctioned divergent-checkpoint
    /// induction).
    struct RebuildArm
    {
        std::shared_ptr<TransferTransaction> producer;
        std::shared_ptr<TransferTransaction> spender;
        base::Hash256                        spent_outpoint;  ///< (producer hash, output 0).
        base::Hash256                        change_outpoint; ///< (spender hash, output 0).
    };

    void SeedMismatchArm( RebuildArm &arm )
    {
        auto &utxo_mgr = account_->GetUTXOManager();

        arm.producer = MakeTransfer( 1, 1 );
        ASSERT_NE( arm.producer, nullptr );
        SeedTransaction( arm.producer );
        SeedCertificate( arm.producer );

        auto producer_txid = base::Hash256::fromReadableString( arm.producer->GetHash() );
        ASSERT_FALSE( producer_txid.has_error() );
        arm.spent_outpoint = producer_txid.value();

        // The certificate delivery applies the producer asynchronously through
        // the real stack — wait for its output (bounded, never a blind sleep).
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto state = utxo_mgr.GetOutPointState( arm.spent_outpoint, 0 );
                return state.has_value() && state.value() == UTXOManager::UTXOState::UTXO_READY;
            },
            std::chrono::seconds( 5 ),
            "certified producer output becomes READY",
            nullptr );

        arm.spender = MakeSpender( *arm.producer, 0, 2 );
        SeedTransaction( arm.spender );
        SeedCertificate( arm.spender );

        auto spender_txid = base::Hash256::fromReadableString( arm.spender->GetHash() );
        ASSERT_FALSE( spender_txid.has_error() );
        arm.change_outpoint = spender_txid.value();

        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto spent   = utxo_mgr.GetOutPointState( arm.spent_outpoint, 0 );
                auto change  = utxo_mgr.GetOutPointState( arm.change_outpoint, 0 );
                auto spender = utxo_mgr.GetOutPointSpender( arm.spent_outpoint, 0 );
                return spent.has_value() && spent.value() == UTXOManager::UTXOState::UTXO_CONSUMED &&
                       change.has_value() && change.value() == UTXOManager::UTXOState::UTXO_READY &&
                       spender.has_value() && spender.value() == SpenderHash( *arm.spender );
            },
            std::chrono::seconds( 5 ),
            "certified spend applies (input CONSUMED by the spender, change READY)",
            nullptr );
    }

    /// Raw /utxo/ record (key + decoded) for one outpoint — the Pitfall-2
    /// disk-level proof helper.
    std::optional<std::pair<std::string, SGTransaction::UTXOEntryRecord>> FindUtxoRecord( const base::Hash256 &txid,
                                                                                          uint32_t output_idx )
    {
        for ( const auto &[key, value] : SnapshotPrefix( "/utxo" ) )
        {
            SGTransaction::UTXOEntryRecord record;
            if ( !record.ParseFromArray( value.data(), static_cast<int>( value.size() ) ) )
            {
                continue;
            }
            auto recorded_txid = base::Hash256::fromSpan(
                gsl::span( reinterpret_cast<uint8_t *>( const_cast<char *>( record.utxo().hash().data() ) ),
                           record.utxo().hash().size() ) );
            if ( recorded_txid.has_error() || recorded_txid.value() != txid ||
                 record.utxo().output_idx() != static_cast<int>( output_idx ) )
            {
                continue;
            }
            return std::make_pair( key, record );
        }
        return std::nullopt;
    }

    /// Overwrites ONE durable /utxo/ record, marking the outpoint CONSUMED by
    /// @p spender — the "one corrupted record" divergence class (T-19-0401)
    /// the rebuild crosscheck exists for. Amount untouched (genuine class).
    void TamperUtxoRecordToConsumed( const std::string &key, const base::Hash256 &spender )
    {
        const auto snapshot     = SnapshotPrefix( "/utxo" );
        const auto record_bytes = snapshot.find( key );
        ASSERT_NE( record_bytes, snapshot.end() );

        SGTransaction::UTXOEntryRecord record;
        ASSERT_TRUE(
            record.ParseFromArray( record_bytes->second.data(), static_cast<int>( record_bytes->second.size() ) ) );
        record.set_state( SGTransaction::UTXO_ENTRY_CONSUMED );
        record.set_has_spent_by_txid( true );
        record.set_spent_by_txid( spender.data(), spender.size() );

        crdt::GlobalDB::Buffer key_buffer;
        key_buffer.put( key );
        crdt::GlobalDB::Buffer value_buffer;
        value_buffer.put( record.SerializeAsString() );
        ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
    }

    /// Arms the checkpoint mismatch by deleting the spender's change record
    /// (the corrupted/lost-record class): the durable READY set loses the
    /// change output while the input's CONSUMED evidence and the latest
    /// checkpoint (rooted over the change) survive — a deterministic local
    /// root != checkpoint root for the next InitializeUTXOs, AND the exact
    /// precondition of the own-replay hazard (input spend evidence preserved,
    /// the spender's outputs absent).
    void ArmMismatchByLosingChangeOutput( const RebuildArm &arm )
    {
        ASSERT_TRUE(
            account_->GetUTXOManager().DeleteUTXO( arm.change_outpoint, 0, account_->GetAddress() ).has_value() );
    }

    /// Rebuilds ONLY the TransactionManager over the live db/account: fresh
    /// tracked map, durable /utxo/ + checkpoint + cert records untouched, and
    /// NO db restart. (A db restart re-delivers the cert topic, whose
    /// approval-time checkpoint re-converges the latest-checkpoint pointer to
    /// the post-spend local state — deterministically destroying the mismatch
    /// arm. The REAL-boot db-restart leg is 19-06's matrix.) The fresh
    /// manager's InitializeUTXOs reloads everything from disk — the exact
    /// index-resident reload shape 19-01 proved is the real-world wipe path.
    void RebuildTransactionManagerOnly()
    {
        tm_.reset();
        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );
        tm_                                = TransactionManager::New( db_,
                                       io_,
                                       account_,
                                       blockchain_,
                                       NodeType::Light,
                                       0,
                                       kTimestampTolerance,
                                       kMutabilityWindow );
        ASSERT_NE( tm_, nullptr );
    }

    /// Extracts the tx hash from a /bc-<net>/tx/<hash> candidate path.
    static std::string HashOfPath( const std::string &tx_path )
    {
        const std::string prefix = TransactionManager::GetBlockChainBase() + "tx/";
        return tx_path.substr( prefix.size() );
    }

    //
    // Pruner helpers.
    //

    /// Constructs a Light TxPruner with the suite's window and dry_run=false.
    /// @p compact_threshold_mb defaults to the PruningConfig default so no
    /// existing call site changes (the CompactionGate canary passes 1).
    std::shared_ptr<sgns::TxPruner> MakePruner( uint64_t k_budget,
                                                uint64_t settle_seconds,
                                                uint64_t compact_threshold_mb = PruningConfig{}.compact_threshold_mb )
    {
        PruningConfig config;
        config.enabled              = true;
        config.dry_run              = false;
        config.finality_window      = kWindow;
        config.max_erases_per_cycle = k_budget;
        config.settle_seconds       = settle_seconds;
        config.compact_threshold_mb = compact_threshold_mb;
        auto pruner                 = sgns::TxPruner::New( NodeType::Light, config );
        EXPECT_TRUE( pruner.has_value() );
        return pruner.has_value() ? pruner.value() : nullptr;
    }

    /// Drives the manager into the tick gate's READY state (the friend seam
    /// noted above; the gate is the only state consumer on this path).
    void ForceManagerReady()
    {
        TransactionManagerPendingLifecycleTestAccess::ChangeState( *tm_, TransactionManager::State::READY );
        ASSERT_EQ( tm_->GetState(), TransactionManager::State::READY );
    }

    /**
     * Runs cycles until one is NOT gate-skipped, delivering its stats.
     * FRESH-INSTANCE CONTRACT: the discriminator is the watermark turning
     * set (skipped ticks leave the status untouched), so @p pruner must not
     * have completed a cycle yet. The bounded wait polls RunCycle — a skip
     * is cheap and side-effect free.
     */
    void RunSettledCycle( sgns::TxPruner &pruner, PruneCycleStats &out_stats )
    {
        ForceManagerReady();
        std::chrono::milliseconds waited( 0 );
        bool                      cycle_failed = false;
        bool                      ran          = false;
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto result = pruner.RunCycle( *db_, *tm_, account_->GetUTXOManager(), kTestGeneration );
                if ( result.has_error() )
                {
                    cycle_failed = true;
                    return true; // exit the bounded wait; EXPECT below fails loudly
                }
                out_stats = result.value();
                ran       = pruner.GetPruneStatus().watermark.has_value();
                return ran;
            },
            std::chrono::milliseconds( kSmallSettleSeconds * 1000 + 8000 ),
            "tick gate settles and one prune cycle runs to completion",
            &waited );
        EXPECT_FALSE( cycle_failed );
        EXPECT_TRUE( ran );
    }

    /// One plain cycle (the gate is already settled on @p pruner).
    outcome::result<PruneCycleStats> RunCycle( sgns::TxPruner &pruner, uint64_t generation = kTestGeneration )
    {
        return pruner.RunCycle( *db_, *tm_, account_->GetUTXOManager(), generation );
    }

    //
    // Crash-matrix helpers (SC-2): the simulated kill tears the whole stack
    // down in the fixture's own destruction order and rebuilds it on the SAME
    // database path — the Phase-16 restart discipline (restartNodePending
    // Start), so every fixed-point assertion reads what survived on disk.
    //

    /**
     * Runs cycles until one RUNS (examined > 0) with an interruption seam
     * already armed — the RunSettledCycle discriminator (watermark turning
     * set) cannot be used here because seam-interrupted cycles deliberately
     * leave the status untouched. A gate-skipped tick is side-effect free,
     * so polling RunCycle is safe with either seam armed.
     */
    PruneCycleStats RunUntilCycleRuns( sgns::TxPruner &pruner )
    {
        ForceManagerReady();
        PruneCycleStats           stats;
        bool                      cycle_failed = false;
        bool                      ran          = false;
        std::chrono::milliseconds waited( 0 );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto result = pruner.RunCycle( *db_, *tm_, account_->GetUTXOManager(), kTestGeneration );
                if ( result.has_error() )
                {
                    cycle_failed = true;
                    return true; // exit the bounded wait; EXPECT below fails loudly
                }
                stats = result.value();
                ran   = stats.examined > 0;
                return ran;
            },
            std::chrono::milliseconds( kSmallSettleSeconds * 1000 + 8000 ),
            "tick gate settles and the interrupted prune cycle runs",
            &waited );
        EXPECT_FALSE( cycle_failed );
        EXPECT_TRUE( ran );
        return stats;
    }

    /**
     * Tears the whole stack down (the fixture destructor's own order: both
     * seams already cleared, blockchain first, db before pubsub) and
     * reconstructs the GlobalDB + account + blockchain + manager stack on the
     * SAME database path — the crash/restart of the SC-2 matrix. The pubsub
     * host and the io pool survive (the Phase-16 makeNode wiring, rebuilt
     * graphsync network and scheduler over the live host); the manager is
     * re-driven to READY for the post-restart cycles (the seam path).
     *
     * The @p real_boot path (SURV-01) instead wires the reopened account for
     * the REAL boot: the account messenger (CheckNonce needs a peer) and the
     * durable nonce store (ConfigureDatabaseDependencies — the confirmed
     * nonces survive the restart on the same path), then tm_->StartCore()
     * with a bounded READY wait — ForceManagerReady is NEVER taken on this
     * leg (Pitfall 5: the seam bypasses InitializeUTXOs).
     */
    void ReopenStackOnSamePath( bool real_boot = false )
    {
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        tm_.reset();
        blockchain_.reset();
        account_.reset();
        if ( db_ )
        {
            db_->ShutdownNow();
        }
        db_.reset();

        auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( pubs_->GetAsioContext() ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
        auto graphsync_network = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(),
                                                                                              scheduler );
        auto generator         = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
        auto reopened          = crdt::GlobalDB::New( io_,
                                                      db_path_,
                                                      pubs_,
                                                      crdt::CrdtOptions::DefaultOptions(),
                                                      graphsync_network,
                                                      scheduler,
                                                      generator );
        ASSERT_TRUE( reopened.has_value() );
        db_ = reopened.value();
        db_->AddListenTopic( kSeedTopic );
        db_->AddBroadcastTopic( kSeedTopic );
        db_->Start();

        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        ASSERT_NE( account_, nullptr );
        ASSERT_TRUE( account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() ).has_value() );

        if ( real_boot )
        {
            // The real-boot wiring order (GeniusNode's): messenger first, then
            // the database dependencies — the durable nonce store reloads the
            // confirmed nonces from the SAME path the pre-restart node wrote.
            ASSERT_TRUE( account_->InitMessenger( pubs_ ) );
            ASSERT_TRUE( account_->ConfigureDatabaseDependencies( db_ ) );
        }

        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        ASSERT_NE( blockchain_, nullptr );
        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );
        tm_                                = TransactionManager::New( db_,
                                                                      io_,
                                                                      account_,
                                                                      blockchain_,
                                                                      NodeType::Light,
                                                                      0,
                                                                      kTimestampTolerance,
                                                                      kMutabilityWindow );
        ASSERT_NE( tm_, nullptr );
        if ( real_boot )
        {
            // SURV-01: the REAL boot — no ForceManagerReady anywhere on this
            // leg. StartCore -> INITIALIZING -> InitializeUTXOs (the 19-04
            // marker consult must keep the missing set empty for locally
            // pruned creating refs) -> InitTransactions -> CheckNonce, whose
            // trusting-local path passes once the nonce responder answers.
            // The fixture's io_ is hand-driven (the CRDTFixture contract), so
            // the wait drives the armed tick loop itself.
            tm_->StartCore();
            ASSERT_WAIT_FOR_CONDITION(
                [this]
                {
                    TransactionManagerPendingLifecycleTestAccess::RunTickOnce( *tm_ );
                    return tm_->GetState() == TransactionManager::State::READY;
                },
                std::chrono::seconds( 30 ),
                "real StartCore boot reaches READY on the restarted pruned node",
                nullptr );
        }
        else
        {
            ForceManagerReady();
        }
    }

    //
    // RestartMatrix helpers (SURV-01): the nonce responder, the certified
    // pruned-frontier arm, foreign-source receive deliveries, and the
    // outgoing-chain builder.
    //

    /// Spins the nonce responder: a second real pubsub host (loopback
    /// advertised — the EraseNodeCollection makeNode wiring) carrying an
    /// ephemeral account's AccountMessenger, dialed ONE direction to the
    /// fixture host (the multi_node one-dial lesson). The responder subscribes
    /// only to the messenger topics — never a CRDT topic — so no CRDT or
    /// graphsync machinery exists on it.
    void StartNonceResponder()
    {
        if ( nonce_responder_account_ != nullptr )
        {
            return;
        }
        const std::string responder_base = ( base_path / "nonce-responder" ).string();
        boost::filesystem::create_directories( responder_base );

        crdt::KeyPairFileStorage key_store( responder_base + "/key" );
        auto                     key_pair = key_store.GetKeyPair();
        ASSERT_TRUE( key_pair.has_value() );
        if ( key_pair.has_error() )
        {
            return;
        }
        nonce_responder_pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>( key_pair.value() );
        const auto start_error  = nonce_responder_pubsub_->Start( 0, {}, "0.0.0.0", { "127.0.0.1" } ).get();
        ASSERT_FALSE( start_error ) << "could not start the nonce responder host: " << start_error.message();
        if ( start_error )
        {
            return;
        }

        nonce_responder_account_ = GeniusAccount::NewEphemeral( kTestTokenId );
        ASSERT_NE( nonce_responder_account_, nullptr );
        ASSERT_TRUE( nonce_responder_account_->InitMessenger( nonce_responder_pubsub_ ) );

        // One-direction dial (two dials inside one gossip heartbeat collide on
        // the shared source tuple), then wait for a connected host pair.
        nonce_responder_pubsub_->AddPeers( { pubs_->GetInterfaceAddress() } );
        auto                      responder_host = nonce_responder_pubsub_->GetHost();
        auto                      fixture_info   = pubs_->GetHost()->getPeerInfo();
        std::chrono::milliseconds waited( 0 );
        ASSERT_WAIT_FOR_CONDITION(
            [&] { return responder_host->connectedness( fixture_info ) == libp2p::Host::Connectedness::CONNECTED; },
            std::chrono::seconds( 10 ),
            "nonce responder dials the fixture host",
            &waited );
    }

    /// Measures (A4) the exact precondition CheckNonce's trusting-local path
    /// needs: FetchNetworkNonce succeeds WITH an empty optional — a peer
    /// answered the nonce request reporting it has no nonce for us. Probed
    /// BEFORE StartCore so the boot's first CheckNonce passes deterministically.
    void ProbeNonceResponderAnswersWithoutNonce()
    {
        ASSERT_NE( account_, nullptr );
        std::chrono::milliseconds waited( 0 );
        ASSERT_WAIT_FOR_CONDITION(
            [this]
            {
                auto network_nonce = account_->FetchNetworkNonce( std::chrono::milliseconds( 2000 ) );
                return network_nonce.has_value() && !network_nonce.value().has_value();
            },
            std::chrono::seconds( 15 ),
            "nonce responder answers without a nonce (the trusting-local CheckNonce precondition)",
            &waited );
    }

    /// The SURV-01 pruned-frontier arm: a certified three-tx chain (nonces
    /// 1..3) whose first two links become prunable — the producer's output is
    /// consumed by the middle spender, whose change is consumed by the
    /// frontier spender. After a settled prune cycle the PRUNED FRONTIER is
    /// nonces 1 and 2 (markers present, values/blocks gone) while the
    /// frontier tx (nonce 3, its change the live balance) survives with its
    /// never-pruned certificate at slot <addr>:3 — the GATE-04 rung the next
    /// outgoing tx must chain onto.
    struct RestartArm
    {
        std::shared_ptr<TransferTransaction> producer; ///< nonce 1 (prunable link)
        std::shared_ptr<TransferTransaction> middle;   ///< nonce 2 (prunable link)
        std::shared_ptr<TransferTransaction> frontier; ///< nonce 3 (live frontier)
        base::Hash256                        producer_outpoint;
        base::Hash256                        middle_outpoint;
        base::Hash256                        frontier_outpoint;
    };

    /// Seeds + certifies the RestartArm through the REAL stack (certificate
    /// delivery applies every effect); driver puts between links keep the
    /// prunable depths beyond the finality window and retire headships.
    void SeedRestartArm( RestartArm &arm )
    {
        auto &utxo_mgr = account_->GetUTXOManager();

        arm.producer = MakeTransfer( 1, 1 );
        ASSERT_NE( arm.producer, nullptr );
        SeedTransaction( arm.producer );
        SeedCertificate( arm.producer );
        auto producer_txid = base::Hash256::fromReadableString( arm.producer->GetHash() );
        ASSERT_FALSE( producer_txid.has_error() );
        arm.producer_outpoint = producer_txid.value();
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto state = utxo_mgr.GetOutPointState( arm.producer_outpoint, 0 );
                return state.has_value() && state.value() == UTXOManager::UTXOState::UTXO_READY;
            },
            std::chrono::seconds( 5 ),
            "certified producer output becomes READY",
            nullptr );
        PutSeedValue( "/seed/txpruner/rm-driver-1", "d1" );

        arm.middle = MakeSpender( *arm.producer, 0, 2 );
        ASSERT_NE( arm.middle, nullptr );
        SeedTransaction( arm.middle );
        SeedCertificate( arm.middle );
        auto middle_txid = base::Hash256::fromReadableString( arm.middle->GetHash() );
        ASSERT_FALSE( middle_txid.has_error() );
        arm.middle_outpoint = middle_txid.value();
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto spent  = utxo_mgr.GetOutPointState( arm.producer_outpoint, 0 );
                auto change = utxo_mgr.GetOutPointState( arm.middle_outpoint, 0 );
                return spent.has_value() && spent.value() == UTXOManager::UTXOState::UTXO_CONSUMED &&
                       change.has_value() && change.value() == UTXOManager::UTXOState::UTXO_READY;
            },
            std::chrono::seconds( 5 ),
            "certified middle spend applies (producer consumed, middle change READY)",
            nullptr );
        PutSeedValue( "/seed/txpruner/rm-driver-2", "d2" );

        arm.frontier = MakeSpender( *arm.middle, 0, 3 );
        ASSERT_NE( arm.frontier, nullptr );
        SeedTransaction( arm.frontier );
        SeedCertificate( arm.frontier );
        auto frontier_txid = base::Hash256::fromReadableString( arm.frontier->GetHash() );
        ASSERT_FALSE( frontier_txid.has_error() );
        arm.frontier_outpoint = frontier_txid.value();
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto spent  = utxo_mgr.GetOutPointState( arm.middle_outpoint, 0 );
                auto change = utxo_mgr.GetOutPointState( arm.frontier_outpoint, 0 );
                return spent.has_value() && spent.value() == UTXOManager::UTXOState::UTXO_CONSUMED &&
                       change.has_value() && change.value() == UTXOManager::UTXOState::UTXO_READY;
            },
            std::chrono::seconds( 5 ),
            "certified frontier spend applies (middle consumed, frontier change READY)",
            nullptr );
        PutSeedValue( "/seed/txpruner/rm-driver-3", "d3" );
    }

    /// Foreign-source transfer signed by @p source_account (the responder's
    /// ephemeral account — CheckTransactionAuthorization verifies the tx
    /// signature against the source address, so an unsigned foreign tx is
    /// filtered before processing). @p spend is the optional input outpoint
    /// (a still-unspent output of a prior foreign delivery); every output
    /// list shape pays this account once, so UTXO effects are observable.
    std::shared_ptr<TransferTransaction> MakeForeignTransfer(
        GeniusAccount                                           &source_account,
        uint64_t                                                 nonce,
        const std::optional<std::pair<base::Hash256, uint32_t>> &spend )
    {
        std::vector<InputUTXOInfo> inputs;
        if ( spend.has_value() )
        {
            InputUTXOInfo input;
            input.txid_hash_  = spend->first;
            input.output_idx_ = spend->second;
            inputs.push_back( std::move( input ) );
        }

        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( source_account.GetAddress() );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );

        auto transfer = std::make_shared<TransferTransaction>(
            TransferTransaction::New( std::move( inputs ),
                                      { OutputDestInfo{ 100, source_account.GetAddress(), kTestTokenId },
                                        OutputDestInfo{ 100, account_->GetAddress(), kTestTokenId } },
                                      std::move( dag ) ) );
        transfer->MakeSignature( source_account );
        return transfer;
    }

    /// The SURV-01 matrix context: the pruned-frontier arm plus the
    /// pre-restart snapshots the post-restart state is compared against.
    struct RestartMatrixContext
    {
        RestartArm arm;

        std::set<std::string> utxo_keys_before; ///< /utxo/ record key set.
        RawSnapshot           seals_before;     ///< tx-delta seals (GATE-03).
        HeadSnapshot          heads_before;     ///< topic -> head CIDs.
        uint64_t              max_height_before = 0;

        std::string producer_cid; ///< delta CID (erased with the value).
        std::string middle_cid;   ///< delta CID (erased with the value).
        std::string frontier_cid; ///< delta CID (survives — live frontier).
    };

    /// Raw /utxo/ record key set — resurrection (a new key) and evidence loss
    /// (a missing key) are both visible without byte-comparing values (the
    /// legitimate redelivery replay rewrites consumed records as tombstones,
    /// so values are NOT byte-stable across a restart — 19-04 key-decision 3).
    std::set<std::string> UtxoRecordKeys()
    {
        std::set<std::string> keys;
        for ( const auto &[key, value] : SnapshotPrefix( "/utxo/" ) )
        {
            keys.insert( key );
        }
        return keys;
    }

    /**
     * The shared SURV-01 prologue (the matrix's first three beats): wire the
     * durable nonce chain, seed + certify the pruned-frontier arm, prune the
     * two consumed links through a settled cycle, snapshot the survival
     * surfaces, mesh the nonce responder, and reopen the stack on the SAME
     * path through the REAL StartCore boot (zero-fetch-bracketed).
     */
    void SeedPruneAndReopenRealBoot( RestartMatrixContext &context )
    {
        // The durable nonce chain: the messenger first (GeniusNode's order),
        // then the nonce store — every certified confirmation below persists
        // onto the same path the reopened node reloads.
        ASSERT_TRUE( account_->InitMessenger( pubs_ ) );
        ASSERT_TRUE( account_->ConfigureDatabaseDependencies( db_ ) );

        SeedRestartArm( context.arm );
        ASSERT_FALSE( HasFatalFailure() );

        auto durable_nonce = account_->GetLocalConfirmedNonce();
        ASSERT_TRUE( durable_nonce.has_value() );
        ASSERT_EQ( durable_nonce.value(), 3u );

        // Delta CIDs BEFORE the erase removes the map entries.
        context.producer_cid = SnapshotMapEntry( TransactionManager::GetTransactionPath( *context.arm.producer ) );
        context.middle_cid   = SnapshotMapEntry( TransactionManager::GetTransactionPath( *context.arm.middle ) );
        context.frontier_cid = SnapshotMapEntry( TransactionManager::GetTransactionPath( *context.arm.frontier ) );
        ASSERT_NE( context.producer_cid, "absent" );
        ASSERT_NE( context.middle_cid, "absent" );
        ASSERT_NE( context.frontier_cid, "absent" );

        // The prune: a settled cycle erases the two consumed links (nonces 1-2).
        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );
        PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );
        ASSERT_EQ( stats.erased, 2u );
        for ( const auto *pruned : { context.arm.producer.get(), context.arm.middle.get() } )
        {
            auto lookup = PruneEligibility::LookupPrunedEntry( *db_, pruned->GetHash() );
            ASSERT_TRUE( lookup.has_value() );
            ASSERT_EQ( lookup.value().first, PrunedLookup::PRUNED );
        }
        ASSERT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *context.arm.frontier ) } ).has_value() );

        // Survival snapshots across the restart.
        context.utxo_keys_before = UtxoRecordKeys();
        context.seals_before     = SnapshotSeals( { context.producer_cid, context.middle_cid, context.frontier_cid } );
        context.heads_before     = SnapshotHeads( context.max_height_before );
        ASSERT_FALSE( context.utxo_keys_before.empty() );

        // The nonce responder must answer BEFORE the boot: probe (the A4
        // measurement) proves the trusting-local precondition holds, so the
        // first CheckNonce inside StartCore passes deterministically.
        StartNonceResponder();
        ASSERT_FALSE( HasFatalFailure() );
        ProbeNonceResponderAnswersWithoutNonce();
        ASSERT_FALSE( HasFatalFailure() );

        // The fixture-held registry pins pre-reopen db handles (the 19-04
        // RocksDB-lock lesson): release it BEFORE reopening the same path.
        registry_.reset();

        // ZERO-FETCH BRACKET: the entire restart window — teardown, reopen,
        // real StartCore boot, bounded READY wait — must fetch nothing.
        crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
        ReopenStackOnSamePath( /*real_boot=*/true );
        ASSERT_FALSE( HasFatalFailure() );
    }

    /// The outgoing-chain builder: spends @p source's output @p output_idx
    /// under a PRE-BUILT dag (RunFillDAGStruct reserved the nonce and resolved
    /// previous_hash through the ladder — the values under test).
    std::shared_ptr<TransferTransaction> MakeSpenderWithDag( const GeniusTransaction &source,
                                                             uint32_t                 output_idx,
                                                             SGTransaction::DAGStruct dag )
    {
        auto source_txid = base::Hash256::fromReadableString( source.GetHash() );
        EXPECT_FALSE( source_txid.has_error() );

        InputUTXOInfo input;
        input.txid_hash_  = source_txid.value();
        input.output_idx_ = output_idx;
        input.signature_  = account_->Sign( input.SerializeForSigning() );

        auto spender = std::make_shared<TransferTransaction>(
            TransferTransaction::New( { std::move( input ) },
                                      { OutputDestInfo{ 100, account_->GetAddress(), kTestTokenId } },
                                      std::move( dag ) ) );
        spender->MakeSignature( *account_ );
        return spender;
    }

    //
    // Snapshot helpers — byte-identity proofs of everything a cycle must
    // NOT touch (the unspent UTXO set, non-tx records, heads, seals).
    //

    /// Raw key -> value bytes of every entry under @p prefix.
    RawSnapshot SnapshotPrefix( const std::string &prefix )
    {
        RawSnapshot snapshot;
        auto        entries = db_->QueryRaw( crdt::GlobalDB::Buffer{}.put( prefix ) );
        if ( entries.has_error() )
        {
            return snapshot;
        }
        for ( const auto &[key, value] : entries.value() )
        {
            snapshot[BytesOf( key )] = BytesOf( value );
        }
        return snapshot;
    }

    /// Raw /crdt/-namespace entries (the durable element-key families) whose
    /// key contains @p needle — the CRDT-layer footprint of one element. The
    /// transient /crdt/work/ broadcaster-queue entries are EXCLUDED: they are
    /// pending-publish plumbing drained asynchronously by the pubsub
    /// broadcaster, not record bytes — their presence depends on drain
    /// timing, not on any prune action.
    RawSnapshot SnapshotCrdtEntriesContaining( const std::string &needle )
    {
        RawSnapshot snapshot;
        auto        entries = db_->QueryRaw( crdt::GlobalDB::Buffer{}.put( std::string( "/crdt/" ) ) );
        if ( entries.has_error() )
        {
            return snapshot;
        }
        static const std::string kTransientWorkFamily = "/crdt/work/";
        for ( const auto &[key, value] : entries.value() )
        {
            const std::string key_str = BytesOf( key );
            if ( key_str.rfind( kTransientWorkFamily, 0 ) == 0 )
            {
                continue;
            }
            if ( key_str.find( needle ) != std::string::npos )
            {
                snapshot[key_str] = BytesOf( value );
            }
        }
        return snapshot;
    }

    /// The whole topic -> heads map (guard-1's scan surface).
    HeadSnapshot SnapshotHeads( uint64_t &max_height )
    {
        HeadSnapshot snapshot;
        auto         heads = db_->GetCRDTHeadList();
        EXPECT_TRUE( heads.has_value() );
        if ( heads.has_error() )
        {
            return snapshot;
        }
        for ( const auto &[topic, topic_heads] : heads.value().first )
        {
            for ( const auto &cid : topic_heads )
            {
                snapshot[topic].insert( cid.toString().value() );
            }
        }
        max_height = heads.value().second;
        return snapshot;
    }

    /// Seal-marker bytes per CID (the raw <cid>sealed key; "absent" when
    /// gone) — GATE-03's never-erased surface.
    RawSnapshot SnapshotSeals( const std::vector<std::string> &cids )
    {
        RawSnapshot snapshot;
        for ( const auto &cid : cids )
        {
            auto seal     = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( cid + "sealed" ) );
            snapshot[cid] = seal.has_value() ? BytesOf( seal.value() ) : std::string( "absent" );
        }
        return snapshot;
    }

    /// Raw block-bytes hex per CID ("absent" when gone) — the block level.
    RawSnapshot SnapshotBlocks( const std::vector<std::string> &cids )
    {
        RawSnapshot snapshot;
        for ( const auto &cid : cids )
        {
            auto parsed = CID::fromString( cid );
            EXPECT_TRUE( parsed.has_value() );
            if ( parsed.has_error() )
            {
                snapshot[cid] = "unparseable";
                continue;
            }
            auto bytes = parsed.value().toBytes();
            EXPECT_TRUE( bytes.has_value() );
            auto block = db_->GetRaw(
                crdt::GlobalDB::Buffer( bytes.has_value() ? std::move( bytes.value() ) : std::vector<uint8_t>{} ) );
            snapshot[cid] = block.has_value() ? BytesOf( block.value() ) : std::string( "absent" );
        }
        return snapshot;
    }

    /// The raw tx->CID map entry bytes at @p tx_path ("absent" when gone).
    std::string SnapshotMapEntry( const std::string &tx_path )
    {
        auto entry = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( tx_path ) );
        return entry.has_value() ? BytesOf( entry.value() ) : std::string( "absent" );
    }
};

// ---------------------------------------------------------------------------
// RealPrune: SC-1 — the first real erase on the service, everything else
// byte-identical
// ---------------------------------------------------------------------------

/**
 * The phase's core service proof. Two spent-beyond-window transactions erase
 * through ONE cycle; per erased tx the three-part set holds (CRDT value
 * absent, raw map entry absent, marker PRUNED) plus the block bytes absent;
 * the /utxo/ prefix (the unspent set AND the consumed spend evidence), the
 * cert/seed non-tx records, the DAG heads, and every seal marker stay
 * byte-identical; the unspent sibling is the untouched negative control.
 */
TEST_F( TxPrunerTest, RealPrune_ErasesEligibleAndLeavesEverythingElseByteIdentical )
{
    // Ancient spender first, then a driver, so its arrival height sits far
    // below the final head (the 17-04 seeding recipe).
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );
    auto eligible_b = MakeTransfer( 3, 1 );
    auto unspent    = MakeTransfer( 4, 2 ); // the untouched sibling

    SeedTransaction( spender );
    const std::string driver_cid_1 = PutSeedValueReturningCid( "/seed/txpruner/real-driver-1", "d1" );
    SeedTransaction( eligible_a );
    SeedTransaction( eligible_b );
    SeedTransaction( unspent );

    TrackOutputs( *eligible_a );
    TrackOutputs( *eligible_b );
    TrackOutputs( *unspent ); // deliberately never consumed
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );

    // Non-tx data: a cert-shaped record and plain seed keys.
    const std::string cert_key    = "/cert/" + account_->GetAddress() + ":41";
    const std::string cert_cid    = PutSeedValueReturningCid( cert_key, "certificate-record" );
    const std::string seed_cid    = PutSeedValueReturningCid( "/seed/txpruner/real-data", "untouched" );
    const std::string driver_cid2 = PutSeedValueReturningCid( "/seed/txpruner/real-driver-2", "d2" );

    // Block CIDs from the map entries (captured before any erase removes them).
    const std::string cid_a = SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_a ) );
    const std::string cid_b = SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_b ) );
    const std::string cid_u = SnapshotMapEntry( TransactionManager::GetTransactionPath( *unspent ) );
    const std::string cid_s = SnapshotMapEntry( TransactionManager::GetTransactionPath( *spender ) );
    ASSERT_NE( cid_a, "absent" );
    ASSERT_NE( cid_b, "absent" );

    // Snapshots BEFORE: everything the cycle must not touch.
    const RawSnapshot  utxo_before  = SnapshotPrefix( "/utxo/" );
    const RawSnapshot  cert_before  = SnapshotCrdtEntriesContaining( cert_key );
    const RawSnapshot  seed_before  = SnapshotCrdtEntriesContaining( "/seed/txpruner/real-data" );
    uint64_t           max_height   = 0;
    const HeadSnapshot heads_before = SnapshotHeads( max_height );
    const RawSnapshot  seals_before = SnapshotSeals(
        { cid_a, cid_b, cid_u, cid_s, cert_cid, seed_cid, driver_cid_1, driver_cid2 } );
    const RawSnapshot blocks_before = SnapshotBlocks( { cid_u, cid_s } );
    const std::string sibling_value = BytesOf(
        db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() );
    const RawSnapshot sibling_crdt = SnapshotCrdtEntriesContaining(
        TransactionManager::GetTransactionPath( *unspent ) );
    ASSERT_FALSE( utxo_before.empty() ); // tracked outputs (spent + unspent) are on disk

    // Pre-state: both eligibles present, no markers yet.
    for ( const auto &tx : { eligible_a.get(), eligible_b.get() } )
    {
        ASSERT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *tx ) } ).has_value() );
        auto pre = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( pre.has_value() );
        ASSERT_EQ( pre.value().first, PrunedLookup::NOT_FOUND );
    }

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );

    // Counts match the fixture: 4 candidates (spender, A, B, U), 2 eligible,
    // 2 erased, nothing held/skipped/errored.
    EXPECT_EQ( stats.examined, 4u );
    EXPECT_EQ( stats.eligible, 2u );
    EXPECT_EQ( stats.erased, 2u );
    EXPECT_EQ( stats.held_blocks, 0u );
    EXPECT_EQ( stats.skipped, 0u );
    EXPECT_EQ( stats.errors, 0u );
    EXPECT_EQ( stats.generation, kTestGeneration );

    for ( const auto &tx : { eligible_a.get(), eligible_b.get() } )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *tx );

        // Three-part assertion: CRDT value gone (all four families), raw map
        // entry gone, marker PRUNED naming this tx's path.
        EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
        EXPECT_TRUE( SnapshotCrdtEntriesContaining( tx_path ).empty() );
        EXPECT_EQ( SnapshotMapEntry( tx_path ), "absent" );
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
        ASSERT_TRUE( lookup.value().second.has_value() );
        EXPECT_EQ( lookup.value().second->tx_path(), tx_path );
    }

    // The block bytes of both erased deltas are gone.
    const RawSnapshot erased_blocks = SnapshotBlocks( { cid_a, cid_b } );
    EXPECT_EQ( erased_blocks.at( cid_a ), "absent" );
    EXPECT_EQ( erased_blocks.at( cid_b ), "absent" );

    // Byte-identity of every untouched surface.
    EXPECT_EQ( SnapshotPrefix( "/utxo/" ), utxo_before ) << "the unspent UTXO set changed";
    EXPECT_EQ( SnapshotCrdtEntriesContaining( cert_key ), cert_before ) << "cert record changed";
    EXPECT_EQ( SnapshotCrdtEntriesContaining( "/seed/txpruner/real-data" ), seed_before ) << "non-tx data changed";
    uint64_t           max_height_after = 0;
    const HeadSnapshot heads_after      = SnapshotHeads( max_height_after );
    EXPECT_EQ( heads_after, heads_before ) << "the DAG heads changed";
    EXPECT_EQ( max_height_after, max_height );
    EXPECT_EQ( SnapshotSeals( { cid_a, cid_b, cid_u, cid_s, cert_cid, seed_cid, driver_cid_1, driver_cid2 } ),
               seals_before )
        << "a seal marker changed (erased deltas' seals must survive — GATE-03)";

    // Negative control: the unspent sibling is byte-identical on every level.
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), sibling_value );
    EXPECT_EQ( SnapshotMapEntry( TransactionManager::GetTransactionPath( *unspent ) ), cid_u );
    EXPECT_EQ( SnapshotBlocks( { cid_u, cid_s } ), blocks_before );
    EXPECT_EQ( SnapshotCrdtEntriesContaining( TransactionManager::GetTransactionPath( *unspent ) ), sibling_crdt );
    auto sibling_lookup = PruneEligibility::LookupPrunedEntry( *db_, unspent->GetHash() );
    ASSERT_TRUE( sibling_lookup.has_value() );
    EXPECT_EQ( sibling_lookup.value().first, PrunedLookup::NOT_FOUND );

    // The watermark is the maxHeight the completed cycle observed.
    const auto status = pruner->GetPruneStatus();
    ASSERT_TRUE( status.watermark.has_value() );
    EXPECT_EQ( status.watermark.value(), max_height );
}

// ---------------------------------------------------------------------------
// Idempotent: a second cycle after a full drain is a no-op
// ---------------------------------------------------------------------------

/**
 * After the backlog drains (cycles until erased==0), one more cycle examines
 * the surviving candidates, erases nothing, and mutates nothing: markers,
 * /utxo/, heads, and the still-absent erased entries are all unchanged.
 */
TEST_F( TxPrunerTest, Idempotent_CycleAfterFullDrainErasesAndMutatesNothing )
{
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );
    auto eligible_b = MakeTransfer( 3, 1 );
    auto unspent    = MakeTransfer( 4, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/idem-driver-1", "d1" );
    SeedTransaction( eligible_a );
    SeedTransaction( eligible_b );
    SeedTransaction( unspent );

    TrackOutputs( *eligible_a );
    TrackOutputs( *eligible_b );
    TrackOutputs( *unspent );
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/idem-driver-2", "d2" );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    // Drain fully: the settled first cycle erases both; repeat until quiet.
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    ASSERT_EQ( stats.erased, 2u );
    for ( int i = 0; i < 3; ++i )
    {
        auto again = RunCycle( *pruner );
        ASSERT_TRUE( again.has_value() );
        EXPECT_EQ( again.value().erased, 0u );
    }

    // Snapshot the drained state.
    const RawSnapshot  markers_before = SnapshotPrefix( "/prune/tx/" );
    const RawSnapshot  utxo_before    = SnapshotPrefix( "/utxo/" );
    uint64_t           max_height     = 0;
    const HeadSnapshot heads_before   = SnapshotHeads( max_height );
    const std::string  map_a          = SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_a ) );
    const std::string  map_b          = SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_b ) );
    const std::string  sibling_value  = BytesOf(
        db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() );
    ASSERT_EQ( markers_before.size(), 2u ); // exactly one marker per erased tx
    ASSERT_EQ( map_a, "absent" );
    ASSERT_EQ( map_b, "absent" );

    // The post-drain no-op cycle: examined>0, erased==0, nothing changes.
    auto result = RunCycle( *pruner );
    ASSERT_TRUE( result.has_value() );
    EXPECT_GT( result.value().examined, 0u ); // spender + unspent sibling remain
    EXPECT_EQ( result.value().eligible, 0u );
    EXPECT_EQ( result.value().erased, 0u );
    EXPECT_EQ( result.value().held_blocks, 0u );
    EXPECT_EQ( result.value().skipped, 0u );
    EXPECT_EQ( result.value().errors, 0u );

    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ), markers_before );
    EXPECT_EQ( SnapshotPrefix( "/utxo/" ), utxo_before );
    uint64_t           max_height_after = 0;
    const HeadSnapshot heads_after      = SnapshotHeads( max_height_after );
    EXPECT_EQ( heads_after, heads_before );
    EXPECT_EQ( max_height_after, max_height );
    EXPECT_EQ( SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_a ) ), "absent" );
    EXPECT_EQ( SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_b ) ), "absent" );
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), sibling_value );

    // The markers still answer PRUNED (value absent + marker present).
    for ( const auto &tx : { eligible_a.get(), eligible_b.get() } )
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }
}

// ---------------------------------------------------------------------------
// SweepReprunes: the cycle IS the sweep (D-02b, PRUNE-05's second half)
// ---------------------------------------------------------------------------

/**
 * A resurrected entry (an add-wins re-merge simulation: the same element
 * re-published through the REAL topic path after a full erase) is visible as
 * the marker-plus-value resurrected state, and the NEXT cycle re-erases it —
 * the evaluation is fresh every cycle (no cached eligible sets), and the
 * marker is idempotent (exactly one marker entry, no duplicate harm).
 */
TEST_F( TxPrunerTest, SweepReprunes_ResurrectedEntryIsReErasedNextCycle )
{
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );
    auto unspent  = MakeTransfer( 3, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/sweep-driver-1", "d1" );
    SeedTransaction( eligible );
    SeedTransaction( unspent );

    TrackOutputs( *eligible );
    TrackOutputs( *unspent );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/sweep-driver-2", "d2" );

    const std::string tx_path = TransactionManager::GetTransactionPath( *eligible );
    const std::string cid_1   = SnapshotMapEntry( tx_path );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    // First cycle: the full erase.
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    ASSERT_EQ( stats.erased, 1u );
    ASSERT_TRUE( db_->Get( { tx_path } ).has_error() );
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        ASSERT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }

    // The add-wins re-merge simulation: the same element again through the
    // REAL publish path (a delayed concurrent merge landing after the
    // erase), plus the map entry re-wired to the new delta.
    crdt::GlobalDB::Buffer serialized;
    serialized.put( eligible->SerializeByteVector() );
    auto republish = db_->Put( { tx_path }, serialized, { kSeedTopic } );
    ASSERT_TRUE( republish.has_value() );
    const std::string cid_2 = republish.value().toString().value();
    SeedTxCidEntry( eligible->GetHash(), cid_2 );
    PutSeedValue( "/seed/txpruner/sweep-driver-3", "d3" ); // retire cid_2 from headship

    // The resurrected state is VISIBLE: value present again while the old
    // marker survived — the marker-plus-value shape, never a silent pass.
    ASSERT_TRUE( db_->Get( { tx_path } ).has_value() );
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION );
    }

    // The sweep: ONE later cycle re-erases the resurrected entry (the
    // evaluation enumerated it again — no cached eligible set could have).
    auto sweep = RunCycle( *pruner );
    ASSERT_TRUE( sweep.has_value() );
    EXPECT_EQ( sweep.value().examined, 3u ); // spender + resurrected + unspent
    EXPECT_EQ( sweep.value().eligible, 1u );
    EXPECT_EQ( sweep.value().erased, 1u );
    EXPECT_EQ( sweep.value().held_blocks, 0u );
    EXPECT_EQ( sweep.value().skipped, 0u );
    EXPECT_EQ( sweep.value().errors, 0u );

    EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
    EXPECT_EQ( SnapshotMapEntry( tx_path ), "absent" );
    EXPECT_EQ( SnapshotBlocks( { cid_1, cid_2 } ).at( cid_2 ), "absent" );
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }

    // No duplicate-marker harm: exactly ONE marker entry for this hash.
    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ).size(), 1u );
}

// ---------------------------------------------------------------------------
// SharedBlockBudget: the stranding edge (D-04 group-or-defer)
// ---------------------------------------------------------------------------

/**
 * Two eligible transactions sharing ONE delta block: with K=1 the group
 * DEFERS WHOLE (erased==0, held_blocks==1, both txs intact — a partial take
 * would strand a sibling behind the exhausted budget); with K=2 (a second
 * pruner instance — K is captured at construction) both erase together: the
 * shared block is gone once and both markers are present.
 */
TEST_F( TxPrunerTest, SharedBlockBudget_K1DefersWholeThenK2TakesWhole )
{
    auto spender = MakeTransfer( 1, 0 );
    auto tx_a    = MakeTransfer( 2, 1 );
    auto tx_b    = MakeTransfer( 3, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/shared-driver-1", "d1" );

    TrackOutputs( *tx_a );
    TrackOutputs( *tx_b );
    SeedTransactionsInOneBlock( { tx_a, tx_b } );
    ASSERT_TRUE( ConsumeOutput( *tx_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *tx_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/shared-driver-2", "d2" );

    const std::string path_a     = TransactionManager::GetTransactionPath( *tx_a );
    const std::string path_b     = TransactionManager::GetTransactionPath( *tx_b );
    const std::string shared_cid = SnapshotMapEntry( path_a );
    ASSERT_EQ( shared_cid, SnapshotMapEntry( path_b ) ); // ONE shared delta

    // K = 1: the 2-tx group does not fit — deferred WHOLE.
    auto pruner_k1 = MakePruner( 1, kSmallSettleSeconds );
    ASSERT_NE( pruner_k1, nullptr );
    PruneCycleStats stats;
    RunSettledCycle( *pruner_k1, stats );
    EXPECT_EQ( stats.examined, 3u );
    EXPECT_EQ( stats.eligible, 2u );
    EXPECT_EQ( stats.erased, 0u );
    EXPECT_EQ( stats.held_blocks, 1u );
    EXPECT_TRUE( db_->Get( { path_a } ).has_value() );
    EXPECT_TRUE( db_->Get( { path_b } ).has_value() );
    EXPECT_NE( SnapshotBlocks( { shared_cid } ).at( shared_cid ), "absent" );
    {
        auto lookup_a = PruneEligibility::LookupPrunedEntry( *db_, tx_a->GetHash() );
        ASSERT_TRUE( lookup_a.has_value() );
        EXPECT_EQ( lookup_a.value().first, PrunedLookup::NOT_FOUND );
        auto lookup_b = PruneEligibility::LookupPrunedEntry( *db_, tx_b->GetHash() );
        ASSERT_TRUE( lookup_b.has_value() );
        EXPECT_EQ( lookup_b.value().first, PrunedLookup::NOT_FOUND );
    }

    // K = 2 (fresh instance): the group fits — both erase TOGETHER.
    auto pruner_k2 = MakePruner( 2, kSmallSettleSeconds );
    ASSERT_NE( pruner_k2, nullptr );
    PruneCycleStats stats_k2;
    RunSettledCycle( *pruner_k2, stats_k2 );
    EXPECT_EQ( stats_k2.eligible, 2u );
    EXPECT_EQ( stats_k2.erased, 2u );
    EXPECT_EQ( stats_k2.held_blocks, 0u );
    EXPECT_EQ( stats_k2.errors, 0u );

    EXPECT_TRUE( db_->Get( { path_a } ).has_error() );
    EXPECT_TRUE( db_->Get( { path_b } ).has_error() );
    EXPECT_EQ( SnapshotMapEntry( path_a ), "absent" );
    EXPECT_EQ( SnapshotMapEntry( path_b ), "absent" );
    EXPECT_EQ( SnapshotBlocks( { shared_cid } ).at( shared_cid ), "absent" ); // gone ONCE
    for ( const auto &tx : { tx_a.get(), tx_b.get() } )
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }
}

/**
 * The missing-block completion arm: sibling A is erased directly through the
 * sanctioned Phase-16 driver (deleting the SHARED block), leaving B
 * stranded; a FRESH evaluation reports the BLOCK_ALREADY_ERASED evidence on
 * the absent block's report, and the next cycle completes B through that
 * evidence — nothing strands.
 */
TEST_F( TxPrunerTest, SharedBlockBudget_MissingBlockSiblingCompletesViaEvidence )
{
    auto spender = MakeTransfer( 1, 0 );
    auto tx_a    = MakeTransfer( 2, 1 );
    auto tx_b    = MakeTransfer( 3, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/missing-driver-1", "d1" );

    TrackOutputs( *tx_a );
    TrackOutputs( *tx_b );
    SeedTransactionsInOneBlock( { tx_a, tx_b } );
    ASSERT_TRUE( ConsumeOutput( *tx_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *tx_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/missing-driver-2", "d2" );

    const std::string path_b     = TransactionManager::GetTransactionPath( *tx_b );
    const std::string shared_cid = SnapshotMapEntry( TransactionManager::GetTransactionPath( *tx_a ) );

    // The budget-exhaustion shape: A's sanctioned erase deletes the SHARED
    // block and strands B (the 18-03 G1 recipe, driven on this fixture).
    ASSERT_TRUE( tm_->EraseTransactionEntry( tx_a->GetHash() ).has_value() );
    ASSERT_EQ( SnapshotBlocks( { shared_cid } ).at( shared_cid ), "absent" );
    ASSERT_TRUE( db_->Get( { path_b } ).has_value() ); // B stranded, value intact

    // The explicit evidence in a FRESH evaluation's block_reports (the
    // checker-adjusted assertion: the evidence lives in the evaluator result).
    {
        PruningConfig config;
        config.finality_window = kWindow;
        auto evaluator = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
        ASSERT_TRUE( evaluator.has_value() );
        auto evaluation = evaluator.value()->Evaluate();
        ASSERT_TRUE( evaluation.has_value() );
        const BlockGateResult *report = nullptr;
        for ( const auto &candidate_report : evaluation.value().block_reports )
        {
            if ( candidate_report.cid == shared_cid )
            {
                report = &candidate_report;
            }
        }
        ASSERT_NE( report, nullptr );
        EXPECT_TRUE( report->erasable );
        const auto evidence_found = std::find( report->reasons.begin(),
                                               report->reasons.end(),
                                               PruneReason::BLOCK_ALREADY_ERASED );
        EXPECT_NE( evidence_found, report->reasons.end() );
    }

    // The cycle completes B through the evidence: value + map entry + marker.
    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    EXPECT_EQ( stats.erased, 1u );
    EXPECT_EQ( stats.held_blocks, 0u );
    EXPECT_EQ( stats.errors, 0u );

    EXPECT_TRUE( db_->Get( { path_b } ).has_error() );
    EXPECT_EQ( SnapshotMapEntry( path_b ), "absent" );
    auto lookup_b = PruneEligibility::LookupPrunedEntry( *db_, tx_b->GetHash() );
    ASSERT_TRUE( lookup_b.has_value() );
    EXPECT_EQ( lookup_b.value().first, PrunedLookup::PRUNED );

    // The plain driver wrote NO marker for A — the pruned marker is the
    // cycle's alone (the two drivers stay distinguishable).
    auto lookup_a = PruneEligibility::LookupPrunedEntry( *db_, tx_a->GetHash() );
    ASSERT_TRUE( lookup_a.has_value() );
    EXPECT_EQ( lookup_a.value().first, PrunedLookup::NOT_FOUND );
}

// ---------------------------------------------------------------------------
// TickGate: SC-4 service-half refusals
// ---------------------------------------------------------------------------

/** Non-Light roles are refused at construction — the GATE-01 mirror. */
TEST_F( TxPrunerTest, TickGate_NonLightRolesRefusedAtConstruction )
{
    PruningConfig config;
    config.enabled         = true;
    config.finality_window = kWindow;

    auto full = sgns::TxPruner::New( NodeType::Full, config );
    ASSERT_TRUE( full.has_error() );
    EXPECT_EQ( full.error(), std::errc::operation_not_permitted );
    EXPECT_TRUE( sgns::TxPruner::IsPermanentStop( full.error() ) );

    auto archive = sgns::TxPruner::New( NodeType::Archive, config );
    ASSERT_TRUE( archive.has_error() );
    EXPECT_EQ( archive.error(), std::errc::operation_not_permitted );

    auto light = sgns::TxPruner::New( NodeType::Light, config );
    EXPECT_TRUE( light.has_value() );
}

/**
 * A TransactionManager outside State::READY gate-skips the cycle: zeroed
 * stats, status untouched, nothing erased — and the same pruner DOES run
 * once the manager reaches READY (the skip is the gate, not a vacuous pass).
 */
TEST_F( TxPrunerTest, TickGate_NotReadySkipsThenRunsWhenReady )
{
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/gate-driver-1", "d1" );
    SeedTransaction( eligible );
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/gate-driver-2", "d2" );

    ASSERT_NE( tm_->GetState(), TransactionManager::State::READY ); // still CREATING

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    auto skipped = RunCycle( *pruner );
    ASSERT_TRUE( skipped.has_value() );
    EXPECT_EQ( skipped.value().examined, 0u );
    EXPECT_EQ( skipped.value().eligible, 0u );
    EXPECT_EQ( skipped.value().erased, 0u );
    EXPECT_EQ( skipped.value().held_blocks, 0u );
    EXPECT_EQ( skipped.value().skipped, 0u );
    EXPECT_EQ( skipped.value().errors, 0u );
    EXPECT_FALSE( pruner->GetPruneStatus().watermark.has_value() ); // status untouched
    EXPECT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *eligible ) } ).has_value() );

    // Non-vacuous proof: READY reached, the settled cycle erases.
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    EXPECT_EQ( stats.erased, 1u );
    EXPECT_TRUE( pruner->GetPruneStatus().watermark.has_value() );
}

/**
 * The settle window: with a large settle the gate holds READY yet skips
 * twice (clock start, then not elapsed) without touching anything; a second
 * pruner with the small settle then proves the skip was the settle gate —
 * its cycle runs and erases.
 */
TEST_F( TxPrunerTest, TickGate_SettleWindowNotElapsedSkipsThenAllowsAfterSettle )
{
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/settle-driver-1", "d1" );
    SeedTransaction( eligible );
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/settle-driver-2", "d2" );

    auto pruner_large_settle = MakePruner( 10, 3600 ); // settle far beyond the test
    ASSERT_NE( pruner_large_settle, nullptr );
    ForceManagerReady();

    for ( int i = 0; i < 2; ++i )
    {
        auto skipped = RunCycle( *pruner_large_settle );
        ASSERT_TRUE( skipped.has_value() );
        EXPECT_EQ( skipped.value().examined, 0u );
        EXPECT_EQ( skipped.value().erased, 0u );
    }
    EXPECT_FALSE( pruner_large_settle->GetPruneStatus().watermark.has_value() );
    EXPECT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *eligible ) } ).has_value() );

    // The same fixture under a small settle runs: the earlier skips were the
    // settle window, nothing else.
    auto pruner_small_settle = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner_small_settle, nullptr );
    PruneCycleStats stats;
    RunSettledCycle( *pruner_small_settle, stats );
    EXPECT_EQ( stats.erased, 1u );
}

/**
 * The missing-set conjunct at the accessor level: an empty fixture reports
 * HasMissingTxHashes()==false, so the gate's third input holds in every
 * cycle above. Driving a NON-empty missing set has no public seam (it fills
 * only through the sync scan inside StartCore) — not faked here.
 */
TEST_F( TxPrunerTest, TickGate_EmptyMissingSetAtAccessorLevel )
{
    const TransactionManager &const_tm = *tm_;
    EXPECT_FALSE( const_tm.HasMissingTxHashes() );
}

// ---------------------------------------------------------------------------
// Observability: RECL-03 — per-cycle fields match the fixture exactly
// ---------------------------------------------------------------------------

/**
 * The per-cycle observability contract: every PruneCycleStats counter equals
 * the fixture's known actions, the generation tag travels onto the stats and
 * the queryable status, the watermark equals the maxHeight the completed
 * cycle observed, and status.last_cycle carries the same counters. The
 * honest-split half (D-02): under the DEFAULT threshold this fixture never
 * compacts — bytes_reclaimed stays nullopt and compacted stays false, so no
 * fabricated reclamation number can exist at erase time.
 */
TEST_F( TxPrunerTest, Observability_StatsAndStatusMatchFixtureActions )
{
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );
    auto eligible_b = MakeTransfer( 3, 1 );
    auto unspent    = MakeTransfer( 4, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/obs-driver-1", "d1" );
    SeedTransaction( eligible_a );
    SeedTransaction( eligible_b );
    SeedTransaction( unspent );

    TrackOutputs( *eligible_a );
    TrackOutputs( *eligible_b );
    TrackOutputs( *unspent );
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/obs-driver-2", "d2" );

    uint64_t max_height = 0;
    SnapshotHeads( max_height );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );

    // Counters == the fixture's actions, nothing approximate.
    EXPECT_EQ( stats.examined, 4u );
    EXPECT_EQ( stats.eligible, 2u );
    EXPECT_EQ( stats.erased, 2u );
    EXPECT_EQ( stats.held_blocks, 0u );
    EXPECT_EQ( stats.skipped, 0u );
    EXPECT_EQ( stats.errors, 0u );
    EXPECT_EQ( stats.generation, kTestGeneration );
    EXPECT_FALSE( stats.generation_superseded ); // caller-owned: ships false (D-C4)
    EXPECT_FALSE( stats.compacted );             // default threshold: this fixture never compacts
    EXPECT_EQ( stats.compaction_duration_ms, 0u );

    const auto status = pruner->GetPruneStatus();
    ASSERT_TRUE( status.watermark.has_value() );
    EXPECT_EQ( status.watermark.value(), max_height ); // the observed maxHeight
    EXPECT_EQ( status.last_cycle.examined, stats.examined );
    EXPECT_EQ( status.last_cycle.eligible, stats.eligible );
    EXPECT_EQ( status.last_cycle.erased, stats.erased );
    EXPECT_EQ( status.last_cycle.held_blocks, stats.held_blocks );
    EXPECT_EQ( status.last_cycle.skipped, stats.skipped );
    EXPECT_EQ( status.last_cycle.errors, stats.errors );
    EXPECT_EQ( status.last_cycle.generation, stats.generation );
    EXPECT_EQ( status.last_cycle.duration_ms, stats.duration_ms );
    EXPECT_FALSE( status.bytes_reclaimed.has_value() ); // honest nullopt: no compaction ran

    // The generation tag travels on every invocation's stats (a
    // generation-superseded caller would see ITS tag here).
    auto noop = RunCycle( *pruner, kTestGeneration + 2 );
    ASSERT_TRUE( noop.has_value() );
    EXPECT_EQ( noop.value().generation, kTestGeneration + 2 );
    EXPECT_EQ( pruner->GetPruneStatus().last_cycle.generation, kTestGeneration + 2 );
}

// ---------------------------------------------------------------------------
// Compaction gate: D-01 — the threshold-crossed canary (ONE positive case;
// the conjunct matrix is 20-04's scope)
// ---------------------------------------------------------------------------

/**
 * The end-to-end mechanism proof: two spent-beyond-window transactions whose
 * evaluation-carried sizes (~1MB values each) cross a 1MB threshold inside
 * ONE settled cycle trigger the post-cycle compaction through the real
 * GlobalDB surface — the returned stats carry compacted=true with a recorded
 * duration, the queryable status stamps the MEASURED bytes_reclaimed (never
 * at erase time), and the erased values are genuinely gone (not vacuous).
 */
TEST_F( TxPrunerTest, CompactionGate_ThresholdCrossedCycleCompactsOnceAndStampsReclaimedBytes )
{
    constexpr size_t kCanaryValueBytes = 1u << 20; // ~1MB low-entropy value each

    // ConsumeUTXOs matches the STORED owner, so the padded outputs are
    // consumed under their own (padded) address.
    const std::string padded_owner = account_->GetAddress() + std::string( kCanaryValueBytes, 'x' );

    auto spender = MakeTransfer( 1, 0 );
    auto big_a   = MakeTransfer( 2, 1, kCanaryValueBytes );
    auto big_b   = MakeTransfer( 3, 1, kCanaryValueBytes );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/compact-driver-1", "d1" );
    SeedTransaction( big_a );
    SeedTransaction( big_b );
    PutSeedValue( "/seed/txpruner/compact-driver-2", "d2" );

    TrackOutputs( *big_a );
    TrackOutputs( *big_b );
    ASSERT_TRUE( ConsumeOutput( *big_a, 0, *spender, padded_owner ) );
    ASSERT_TRUE( ConsumeOutput( *big_b, 0, *spender, padded_owner ) );

    // Pre-state: both values present, nothing reclaimed yet (honest nullopt).
    for ( const auto *tx : { big_a.get(), big_b.get() } )
    {
        ASSERT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *tx ) } ).has_value() );
    }

    // Threshold 1MB: the two ~1MB erased values cross it inside this cycle.
    auto pruner = MakePruner( 10, kSmallSettleSeconds, 1 );
    ASSERT_NE( pruner, nullptr );
    EXPECT_FALSE( pruner->GetPruneStatus().bytes_reclaimed.has_value() );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );

    EXPECT_EQ( stats.erased, 2u );
    EXPECT_TRUE( stats.compacted );
    EXPECT_GT( stats.compaction_duration_ms, 0u );

    const auto status = pruner->GetPruneStatus();
    ASSERT_TRUE( status.bytes_reclaimed.has_value() );
    EXPECT_GT( status.bytes_reclaimed.value(), 0u );

    // Non-vacuous: the erased values are gone (marker PRUNED, value absent).
    for ( const auto *tx : { big_a.get(), big_b.get() } )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *tx );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }
}

// ---------------------------------------------------------------------------
// PruneStatus: RECL-04 — the watermark contract
// ---------------------------------------------------------------------------

/**
 * The watermark is the maxHeight of the last cycle that ran to completion
 * without hard error: an INTERRUPTED cycle (seam-held before its first
 * erase) leaves both the watermark and last_cycle untouched, and the next
 * completed cycle advances the watermark monotonically to the current head.
 */
TEST_F( TxPrunerTest, PruneStatus_InterruptedCycleLeavesWatermarkUntouched )
{
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/wm-driver-1", "d1" );
    SeedTransaction( eligible_a );
    TrackOutputs( *eligible_a );
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/wm-driver-2", "d2" );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats first;
    RunSettledCycle( *pruner, first );
    ASSERT_EQ( first.erased, 1u );
    const auto status_first = pruner->GetPruneStatus();
    ASSERT_TRUE( status_first.watermark.has_value() );
    const uint64_t watermark_first = status_first.watermark.value();

    // New heights and NEW eligible work past the first watermark.
    PutSeedValue( "/seed/txpruner/wm-driver-3", "d3" );
    auto eligible_b = MakeTransfer( 3, 1 );
    SeedTransaction( eligible_b );
    TrackOutputs( *eligible_b );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/wm-driver-4", "d4" );
    uint64_t max_height_now = 0;
    SnapshotHeads( max_height_now );
    ASSERT_GT( max_height_now, watermark_first );

    // Interrupt the next cycle BEFORE its first erase: status untouched.
    sgns::TxPruner::SetCycleInterruptHookForTest( []( int step )
                                                  { return step == sgns::TxPruner::kInterruptBeforeErase; } );
    auto interrupted = RunCycle( *pruner );
    sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
    ASSERT_TRUE( interrupted.has_value() );
    EXPECT_GT( interrupted.value().examined, 0u ); // it evaluated — and still did not advance
    EXPECT_EQ( interrupted.value().erased, 0u );

    const auto status_interrupted = pruner->GetPruneStatus();
    ASSERT_TRUE( status_interrupted.watermark.has_value() );
    EXPECT_EQ( status_interrupted.watermark.value(), watermark_first ); // NOT advanced
    EXPECT_EQ( status_interrupted.last_cycle.erased, first.erased );    // last_cycle still cycle 1's

    // The next completed cycle advances monotonically to the current head.
    auto completed = RunCycle( *pruner );
    ASSERT_TRUE( completed.has_value() );
    EXPECT_EQ( completed.value().erased, 1u );
    const auto status_done = pruner->GetPruneStatus();
    ASSERT_TRUE( status_done.watermark.has_value() );
    EXPECT_EQ( status_done.watermark.value(), max_height_now );
    EXPECT_GE( status_done.watermark.value(), watermark_first );
}

/**
 * A CONCURRENT GetPruneStatus() during a running cycle returns coherent
 * snapshots without deadlock: a reader thread spins on the status while the
 * cycle is seam-held mid-flight, releases it only after many successful
 * reads, and both sides complete.
 */
TEST_F( TxPrunerTest, PruneStatus_ConcurrentReadDuringCycleIsCoherent )
{
    auto spender = MakeTransfer( 1, 0 );
    auto unspent = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    SeedTransaction( unspent );
    PutSeedValue( "/seed/txpruner/conc-driver-1", "d1" );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    // Prime: a completed watermark-carrying cycle with no eligible work.
    PruneCycleStats prime;
    RunSettledCycle( *pruner, prime );
    ASSERT_EQ( prime.erased, 0u );
    ASSERT_TRUE( pruner->GetPruneStatus().watermark.has_value() );

    // New eligible work so the raced cycle has an erase to perform.
    auto eligible = MakeTransfer( 3, 1 );
    SeedTransaction( eligible );
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/conc-driver-2", "d2" );

    std::atomic<bool>   hook_entered{ false };
    std::atomic<bool>   release_cycle{ false };
    std::atomic<bool>   stop_reader{ false };
    std::atomic<size_t> reads{ 0 };
    std::atomic<bool>   watermark_seen{ false };

    // Reader: spin on GetPruneStatus the whole time; after enough concurrent
    // reads INSIDE the held window, release the cycle thread.
    std::thread reader(
        [&]
        {
            while ( !stop_reader.load() )
            {
                const auto snapshot = pruner->GetPruneStatus();
                ++reads;
                if ( snapshot.watermark.has_value() )
                {
                    watermark_seen.store( true );
                }
                if ( hook_entered.load() && reads.load() > 200 )
                {
                    release_cycle.store( true );
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            release_cycle.store( true ); // never leave the cycle held on exit
        } );

    sgns::TxPruner::SetCycleInterruptHookForTest(
        [&]( int step )
        {
            if ( step == sgns::TxPruner::kInterruptAfterEvaluation )
            {
                hook_entered.store( true );
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
                while ( !release_cycle.load() && std::chrono::steady_clock::now() < deadline )
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                }
                return false; // continue the cycle (the hold was the point)
            }
            return false;
        } );

    auto raced = RunCycle( *pruner ); // returns only once the reader released it
    sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
    stop_reader.store( true );
    reader.join();

    ASSERT_TRUE( raced.has_value() );
    EXPECT_EQ( raced.value().erased, 1u );
    EXPECT_GT( reads.load(), 200u );      // the concurrent reads really happened
    EXPECT_TRUE( watermark_seen.load() ); // every snapshot was coherent (a full copy)
}

// ---------------------------------------------------------------------------
// ComposeLock: the D-01 non-interleaving proof
// ---------------------------------------------------------------------------

/**
 * The critical-section capstone. The cycle is held INSIDE the erase compose
 * (GlobalDB's own seam step 1: guards passed, CRDT staged, commit pending —
 * the compose lock HELD) while a concurrent PutElems re-add races on the
 * SAME key: the merge provably cannot interleave (it blocks on the
    * owner-vended lock until the erase commits), and the final state is a
 * legal serialization — the marker exists (the erase committed atomically)
 * and the re-added value is the add-wins resurrected shape. The next cycle
 * re-erases it (the sweep closes the race). NEVER a torn mix: value absent
 * without a marker is unreachable.
 */
TEST_F( TxPrunerTest, ComposeLock_ConcurrentMergeSerializesNeverTorn )
{
    auto spender = MakeTransfer( 1, 0 );
    auto unspent = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    SeedTransaction( unspent );
    PutSeedValue( "/seed/txpruner/cl-driver-1", "d1" );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    // Prime the settle clock with a completed no-work cycle (fresh-instance
    // contract of RunSettledCycle), so the raced cycle runs immediately.
    PruneCycleStats prime;
    RunSettledCycle( *pruner, prime );
    ASSERT_EQ( prime.erased, 0u );

    // The erase target: eligible, sealed, retired from headship.
    auto eligible = MakeTransfer( 3, 1 );
    SeedTransaction( eligible );
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/cl-driver-2", "d2" );

    const std::string tx_path = TransactionManager::GetTransactionPath( *eligible );
    const std::string cid_1   = SnapshotMapEntry( tx_path );

    // Hold the cycle thread inside the compose (guards passed, staged, NOT
    // committed) via GlobalDB's erase seam; release only from this thread.
    std::promise<void> entered_promise;
    std::future<void>  entered_future = entered_promise.get_future();
    std::promise<void> release_promise;
    std::future<void>  release_future = release_promise.get_future();
    crdt::GlobalDB::SetEraseInterruptHookForTest(
        [&]( int step )
        {
            if ( step == 1 )
            {
                entered_promise.set_value();
                release_future.wait_for( std::chrono::seconds( 5 ) );
            }
            return false; // continue: the HOLD is the deterministic window
        } );

    std::optional<outcome::result<PruneCycleStats>> cycle_result;
    std::thread                                     cycle_thread( [&] { cycle_result = RunCycle( *pruner ); } );

    ASSERT_TRUE( entered_future.wait_for( std::chrono::seconds( 5 ) ) == std::future_status::ready )
        << "the cycle never reached the erase compose";

    // The racer: a concurrent merge re-adding the SAME element through the
    // real publish path (PutElems build+commit under the same CrdtSet mutex).
    std::atomic<bool>          racer_done{ false };
    std::optional<std::string> racer_cid;
    crdt::GlobalDB::Buffer     serialized;
    serialized.put( eligible->SerializeByteVector() );
    std::thread racer(
        [&]
        {
            auto put = db_->Put( { tx_path }, serialized, { kSeedTopic } );
            if ( put.has_value() )
            {
                racer_cid = put.value().toString().value();
            }
            racer_done.store( true );
        } );

    // The non-interleaving proof: while the compose lock is held, the merge
    // cannot complete (bounded observation — it is blocked on the lock).
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( 300 );
        while ( std::chrono::steady_clock::now() < deadline && !racer_done.load() )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        EXPECT_FALSE( racer_done.load() ) << "the merge interleaved with the held erase compose";
    }

    // Release: the erase commits first, the merge serializes after it.
    release_promise.set_value();
    {
        std::chrono::milliseconds waited( 0 );
        ASSERT_WAIT_FOR_CONDITION( [&] { return racer_done.load(); },
                                   std::chrono::milliseconds( 5000 ),
                                   "the merge completes after the compose lock frees",
                                   &waited );
    }
    racer.join();
    cycle_thread.join();
    crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

    ASSERT_TRUE( cycle_result.has_value() && cycle_result.value().has_value() );
    EXPECT_EQ( cycle_result.value().value().erased, 1u );
    ASSERT_TRUE( racer_cid.has_value() );

    // The legal serialization (this ordering: erase committed, THEN the
    // merge's add-wins re-add): the marker EXISTS (the erase never commits
    // partially) and the value is present again — never a torn mix.
    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_NE( lookup.value().first, PrunedLookup::NOT_FOUND );     // the marker committed
    EXPECT_TRUE( db_->Get( { tx_path } ).has_value() );             // the merge won the value
    EXPECT_EQ( SnapshotBlocks( { cid_1 } ).at( cid_1 ), "absent" ); // the erased block stays gone

    // The sweep closes the race: re-wire the map entry to the merged delta,
    // retire it from headship, and the next cycle re-erases.
    SeedTxCidEntry( eligible->GetHash(), racer_cid.value() );
    PutSeedValue( "/seed/txpruner/cl-driver-3", "d3" );
    auto sweep = RunCycle( *pruner );
    ASSERT_TRUE( sweep.has_value() );
    EXPECT_EQ( sweep.value().erased, 1u );
    EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
    auto final_lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( final_lookup.has_value() );
    EXPECT_EQ( final_lookup.value().first, PrunedLookup::PRUNED );
}

/**
 * The guards are evaluated INSIDE the critical section at erase time: an
 * otherwise-eligible candidate whose delta block is the LIVE topic head is
 * refused by the compose's head guard — the evaluation and the block gate
 * accepted it, the erase refused — leaving value AND marker both in the
 * pre-erase state (never torn), counted as a per-tx error on a cycle that
 * still completes. Once the headship retires, the next cycle erases cleanly.
 */
TEST_F( TxPrunerTest, ComposeLock_HeadGuardRefusesAtEraseTimeNeverTorn )
{
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/hg-driver-1", "d1" );
    SeedTransaction( eligible ); // its delta IS the topic's live head
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    // NO later put: the eligible tx's block stays the head.

    const std::string tx_path = TransactionManager::GetTransactionPath( *eligible );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );

    // The evaluation accepted it (eligible), the compose refused it (guard):
    // a per-tx error, nothing erased, nothing written.
    EXPECT_EQ( stats.examined, 2u );
    EXPECT_EQ( stats.eligible, 1u );
    EXPECT_EQ( stats.erased, 0u );
    EXPECT_EQ( stats.errors, 1u );
    EXPECT_EQ( stats.held_blocks, 0u );
    EXPECT_EQ( stats.skipped, 0u );

    // Never torn: the value is untouched AND no marker exists.
    EXPECT_TRUE( db_->Get( { tx_path } ).has_value() );
    EXPECT_NE( SnapshotMapEntry( tx_path ), "absent" );
    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::NOT_FOUND );

    // The cycle still ran to completion (per-tx errors never block): the
    // watermark advanced despite the refusal.
    const auto status = pruner->GetPruneStatus();
    ASSERT_TRUE( status.watermark.has_value() );

    // Retire the headship: the next cycle erases cleanly — the refusal was
    // the live guard, not a permanent fault.
    PutSeedValue( "/seed/txpruner/hg-driver-2", "d2" );
    auto retry = RunCycle( *pruner );
    ASSERT_TRUE( retry.has_value() );
    EXPECT_EQ( retry.value().erased, 1u );
    EXPECT_EQ( retry.value().errors, 0u );
    EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
    auto pruned = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( pruned.has_value() );
    EXPECT_EQ( pruned.value().first, PrunedLookup::PRUNED );
}

// ---------------------------------------------------------------------------
// CrashFixedPoint: SC-2 — kill the cycle at ANY interrupt point of BOTH
// seams, reopen the database, converge to the SAME fixed point
// ---------------------------------------------------------------------------

namespace
{
    /// One kill point of the crash matrix: which seam, which step, and which
    /// visit of that step aborts (the GlobalDB seam fires three step visits
    /// per erase compose; the cycle seam's BeforeErase visit repeats per tx).
    struct CrashKillPoint
    {
        const char *name;
        /// true: the TxPruner cycle seam; false: GlobalDB's compose seam.
        bool use_cycle_seam;
        /// The seam's step constant (GlobalDB steps are literal 1/2/3).
        int step;
        /// 1-based visit of @p step that aborts.
        int invocation;
        /// Expected: did the killed run still complete its cycle (per-tx
        /// erase failures and AfterStatus interruptions do)?
        bool cycle_completes;
    };

    /**
     * The full matrix: every cycle-level interruption position and every
     * GlobalDB compose step. The GlobalDB cases kill the SECOND erase's
     * compose (the first erase's three step visits pass) — true partial
     * progress: one tx fully committed, the next tx's batch killed at the
     * requested step, the cycle itself continuing past the per-tx error
     * (the sanctioned log-and-continue scan contract).
     */
    const std::vector<CrashKillPoint> kCrashKillPoints = {
        { "CycleAfterEvaluation", true, sgns::TxPruner::kInterruptAfterEvaluation, 1, false },
        { "CycleBeforeFirstErase", true, sgns::TxPruner::kInterruptBeforeErase, 1, false },
        { "CycleBetweenErases", true, sgns::TxPruner::kInterruptBeforeErase, 2, false },
        { "CycleBeforeStatus", true, sgns::TxPruner::kInterruptBeforeStatus, 1, false },
        { "CycleAfterStatus", true, sgns::TxPruner::kInterruptAfterStatus, 1, true },
        { "ComposeStep1MidProgress", false, 1, 2, true },
        { "ComposeStep2MidProgress", false, 2, 2, true },
        { "ComposeStep3MidProgress", false, 3, 2, true },
    };

    std::string CrashKillName( const ::testing::TestParamInfo<CrashKillPoint> &info )
    {
        return info.param.name;
    }
} // namespace

/**
 * SC-2, the crash-consistency matrix. For every interrupt point of BOTH
 * seams: seed a mixed fixture (two one-tx blocks and one shared two-tx
 * block), kill the first cycle at that point, REOPEN the whole stack on the
 * same database path, and assert (a) the immediate fixed point — every
 * transaction is in the COMPLETE pre-erase state (value + map entry, no
 * marker) or the COMPLETE post-erase state (all gone, marker PRUNED), never
 * a torn mix: never value-without-map-entry, never an orphan map entry,
 * never marker-plus-value; a same-block sibling MAY legally strand (value
 * present, its shared block already gone) — the BLOCK_ALREADY_ERASED
 * evidence completes it; (b) cycles re-run on the reopened node converge to
 * exactly the uninterrupted run's end state (all eligibles PRUNED, controls
 * untouched, heads/seals/UTXO byte-identical); and (c) one more cycle on the
 * fully-drained node is a no-op.
 *
 * TRUE-PROCESS-KILL NOTE (research open-question 2 default, plan-sanctioned
 * stretch): the seam-simulated matrix above IS the SC-2 gate. A fork-based
 * true-kill case (child opens the fixture DB, _exit(3)s mid-compose inside
 * the seam, parent reopens) is deliberately ABSENT: the fixture's pubsub
 * host and asio pool carry live threads whose locks do not survive fork()
 * (research assumption A3 — the child would deadlock on inherited mutexes
 * before reaching the compose), and GlobalDB has no pubsub-less
 * construction path. Documented here so the absence is never silent.
 */
class TxPrunerCrashFixedPointTest : public TxPrunerTest, public ::testing::WithParamInterface<CrashKillPoint>
{
};

TEST_P( TxPrunerCrashFixedPointTest, KilledCycleReopensToFixedPointAndConverges )
{
    const CrashKillPoint kill = GetParam();

    //
    // Seed the mixed fixture: an ancient spender, TWO eligible one-tx-block
    // transactions, TWO eligibles sharing ONE multi-tx block, and an unspent
    // control sibling — every erase-order permutation the cycle can take.
    //
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );
    auto eligible_b = MakeTransfer( 3, 1 );
    auto eligible_c = MakeTransfer( 4, 1 );
    auto unspent    = MakeTransfer( 5, 2 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/crash-driver-1", "d1" );
    SeedTransaction( eligible_a );
    TrackOutputs( *eligible_a );
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    SeedTransactionsInOneBlock( { eligible_b, eligible_c } );
    TrackOutputs( *eligible_b );
    TrackOutputs( *eligible_c );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_c, 0, *spender ) );
    SeedTransaction( unspent );
    TrackOutputs( *unspent ); // deliberately never consumed
    PutSeedValue( "/seed/txpruner/crash-driver-2", "d2" );

    const std::string path_a = TransactionManager::GetTransactionPath( *eligible_a );
    const std::string path_b = TransactionManager::GetTransactionPath( *eligible_b );
    const std::string path_c = TransactionManager::GetTransactionPath( *eligible_c );

    // Block CIDs and the pre-kill byte-identity snapshots.
    const std::string cid_a      = SnapshotMapEntry( path_a );
    const std::string shared_cid = SnapshotMapEntry( path_b );
    const std::string cid_u      = SnapshotMapEntry( TransactionManager::GetTransactionPath( *unspent ) );
    const std::string cid_s      = SnapshotMapEntry( TransactionManager::GetTransactionPath( *spender ) );
    ASSERT_NE( cid_a, "absent" );
    ASSERT_EQ( shared_cid, SnapshotMapEntry( path_c ) ); // ONE shared delta
    ASSERT_EQ( SnapshotPrefix( "/prune/tx/" ).size(), 0u );

    const RawSnapshot  utxo_before   = SnapshotPrefix( "/utxo/" );
    uint64_t           max_height    = 0;
    const HeadSnapshot heads_before  = SnapshotHeads( max_height );
    const RawSnapshot  seals_before  = SnapshotSeals( { cid_a, shared_cid, cid_u, cid_s } );
    const RawSnapshot  blocks_before = SnapshotBlocks( { cid_u, cid_s } );
    const std::string  unspent_value = BytesOf(
        db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() );
    const std::string spender_value = BytesOf(
        db_->Get( { TransactionManager::GetTransactionPath( *spender ) } ).value() );

    //
    // The kill: arm the requested seam, run cycles until one runs, disarm.
    // Every captured local lives at FUNCTION scope — the hooks fire inside
    // RunUntilCycleRuns below, after this arming block's branches close, so
    // block-scope captures would dangle.
    //
    std::atomic<bool> killed{ false };
    const int         step          = kill.step;
    const int         invocation    = kill.invocation;
    const int         fire_at_visit = 3 + kill.step; // GlobalDB seam only
    int               visits        = 0;
    if ( kill.use_cycle_seam )
    {
        sgns::TxPruner::SetCycleInterruptHookForTest(
            [&]( int visited )
            {
                if ( visited != step )
                {
                    return false;
                }
                if ( ++visits < invocation )
                {
                    return false; // let earlier visits pass (partial progress)
                }
                killed.store( true );
                return true;
            } );
    }
    else
    {
        // GlobalDB's seam visits steps 1/2/3 per erase compose: visits 1-3
        // are the FIRST erase (allowed through — true partial progress),
        // visit 3+step is the SECOND erase's @p step (the kill).
        crdt::GlobalDB::SetEraseInterruptHookForTest(
            [&]( int visited_step )
            {
                ++visits;
                if ( visits == fire_at_visit && visited_step == step )
                {
                    killed.store( true );
                    return true;
                }
                return false;
            } );
    }

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );
    const PruneCycleStats kill_stats = RunUntilCycleRuns( *pruner );

    sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
    crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

    // The kill really landed at the requested point (never vacuous).
    ASSERT_TRUE( killed.load() ) << "the interrupt point was never reached: " << kill.name;
    // The status contract held across the kill: a completed run recorded the
    // watermark, an interrupted run left the status untouched.
    EXPECT_EQ( pruner->GetPruneStatus().watermark.has_value(), kill.cycle_completes );

    //
    // The REOPEN: tear the stack down and rebuild it on the same DB path —
    // every assertion below reads what actually survived on disk.
    //
    ASSERT_NO_FATAL_FAILURE( ReopenStackOnSamePath() );

    // (a) The immediate fixed point on the REOPENED node: for every
    // eligible tx either the complete pre-erase state or the complete
    // post-erase state — never a torn mix.
    size_t erased_count = 0;
    for ( const auto *tx : { eligible_a.get(), eligible_b.get(), eligible_c.get() } )
    {
        const std::string tx_path       = TransactionManager::GetTransactionPath( *tx );
        const bool        value_present = db_->Get( { tx_path } ).has_value();
        const bool        map_present   = SnapshotMapEntry( tx_path ) != "absent";
        auto              lookup        = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );

        // Value and map entry live and die together (one batch).
        EXPECT_EQ( value_present, map_present ) << kill.name << ": torn value/map mix at " << tx_path;
        // Never marker-plus-value (the resurrection shape is a loudly
        // reported invariant violation, not a legal crash state).
        EXPECT_NE( lookup.value().first, PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION )
            << kill.name << ": marker AND value both survived at " << tx_path;
        if ( value_present )
        {
            EXPECT_EQ( lookup.value().first, PrunedLookup::NOT_FOUND ) << kill.name << ": marker without erase";
        }
        else
        {
            EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED ) << kill.name << ": erase without marker";
            ++erased_count;
        }
    }
    // The one-tx-block tx: its block lives and dies with its own value.
    EXPECT_EQ( SnapshotBlocks( { cid_a } ).at( cid_a ) == "absent", db_->Get( { path_a } ).has_error() )
        << kill.name << ": own-block bytes/value diverged";
    // The shared block: a crash BETWEEN two same-block erases may legally
    // leave the surviving sibling's value present with the block already
    // gone (the BLOCK_ALREADY_ERASED-completable state); but BOTH siblings
    // erased MUST mean the block is gone.
    if ( db_->Get( { path_b } ).has_error() && db_->Get( { path_c } ).has_error() )
    {
        EXPECT_EQ( SnapshotBlocks( { shared_cid } ).at( shared_cid ), "absent" )
            << kill.name << ": both shared siblings erased but the block survived";
    }
    // The controls are untouched by any crash position.
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), unspent_value );
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *spender ) } ).value() ), spender_value );
    EXPECT_EQ( SnapshotBlocks( { cid_u, cid_s } ), blocks_before );
    EXPECT_EQ( SnapshotPrefix( "/utxo/" ), utxo_before );
    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ).size(), erased_count );

    //
    // (b) Convergence: fresh pruner on the reopened node, cycles until the
    // drain — the end state equals the UNINTERRUPTED run's end state
    // (18-06's RealPrune pinned that state: every eligible fully erased +
    // marked, controls byte-identical, heads/seals/UTXO untouched).
    //
    auto converger = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( converger, nullptr );
    PruneCycleStats conv_stats;
    RunSettledCycle( *converger, conv_stats );
    for ( int i = 0; i < 3 && conv_stats.erased > 0; ++i )
    {
        auto more = RunCycle( *converger );
        ASSERT_TRUE( more.has_value() );
        conv_stats = more.value();
    }

    for ( const auto *tx : { eligible_a.get(), eligible_b.get(), eligible_c.get() } )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *tx );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_error() ) << kill.name << ": value survived convergence";
        EXPECT_TRUE( SnapshotCrdtEntriesContaining( tx_path ).empty() ) << kill.name << ": CRDT footprint survived";
        EXPECT_EQ( SnapshotMapEntry( tx_path ), "absent" ) << kill.name << ": map entry survived convergence";
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }
    const RawSnapshot converged_blocks = SnapshotBlocks( { cid_a, shared_cid, cid_u, cid_s } );
    EXPECT_EQ( converged_blocks.at( cid_a ), "absent" );
    EXPECT_EQ( converged_blocks.at( shared_cid ), "absent" ); // gone ONCE
    EXPECT_EQ( converged_blocks.at( cid_u ), blocks_before.at( cid_u ) );
    EXPECT_EQ( converged_blocks.at( cid_s ), blocks_before.at( cid_s ) );
    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ).size(), 3u ); // exactly one marker per eligible
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), unspent_value );
    EXPECT_EQ( SnapshotMapEntry( TransactionManager::GetTransactionPath( *unspent ) ), cid_u );
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *spender ) } ).value() ), spender_value );
    EXPECT_EQ( SnapshotPrefix( "/utxo/" ), utxo_before );
    uint64_t           max_height_after = 0;
    const HeadSnapshot heads_after      = SnapshotHeads( max_height_after );
    EXPECT_EQ( heads_after, heads_before );
    EXPECT_EQ( max_height_after, max_height );
    EXPECT_EQ( SnapshotSeals( { cid_a, shared_cid, cid_u, cid_s } ), seals_before )
        << "a seal marker changed (GATE-03)";

    //
    // (c) The clean-node no-op: one more cycle on the drained reopened node
    // examines the survivors and erases nothing.
    //
    const RawSnapshot markers_converged = SnapshotPrefix( "/prune/tx/" );
    auto              noop              = RunCycle( *converger );
    ASSERT_TRUE( noop.has_value() );
    EXPECT_GT( noop.value().examined, 0u ); // spender + unspent sibling remain
    EXPECT_EQ( noop.value().eligible, 0u );
    EXPECT_EQ( noop.value().erased, 0u );
    EXPECT_EQ( noop.value().held_blocks, 0u );
    EXPECT_EQ( noop.value().errors, 0u );
    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ), markers_converged );
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), unspent_value );
}

INSTANTIATE_TEST_SUITE_P( CrashFixedPointMatrix,
                          TxPrunerCrashFixedPointTest,
                          ::testing::ValuesIn( kCrashKillPoints ),
                          CrashKillName );

//
// ---------------------------------------------------------------------------
// RebuildGuard family — GATE-05's checkpoint-rebuild half (D-01): a checkpoint
// mismatch on a pruned node must clear-and-rebuild without resurrecting spent
// outputs (preserve-clear + PutUTXO duplicate check by construction, belt
// proven via the tampered-record induction), without falsely rejecting the
// legitimate spender's own rebuild replay (the derived hazard), and without
// burning recovery requests on locally-pruned creating txs (marker consult).
// Boot runs the REAL InitializeUTXOs on the reopened stack (the reload shape
// 19-01 proved is the real-world wipe path); the StartCore/network leg is
// 19-06's restart matrix and is deliberately not duplicated here.
// ---------------------------------------------------------------------------
//

/// Case 1 (no resurrection): retained producer + pruned spender. The preserved
/// CONSUMED evidence must block re-creation of the spent outpoint and survive
/// the clear ON DISK — the Pitfall-2 anti-vacuous proof (a SetUTXOs-shaped
/// clear would have wiped the prefix and the rebuild would re-create the
/// outpoint READY).
TEST_F( TxPrunerTest, RebuildGuardNoResurrectionKeepsPreservedConsumeEvidence )
{
    RebuildArm arm;
    SeedMismatchArm( arm );
    ASSERT_FALSE( HasFatalFailure() );
    ASSERT_NO_FATAL_FAILURE( ArmMismatchByLosingChangeOutput( arm ) );

    // The pruned-spender scenario: the spender's history is physically gone
    // (marker present) while the producer stays retained.
    ASSERT_TRUE( tm_->EraseTransactionEntryPruned( arm.spender->GetHash() ).has_value() );

    RebuildTransactionManagerOnly();
    ASSERT_FALSE( HasFatalFailure() );

    TransactionManagerPendingLifecycleTestAccess::RunInitializeUTXOs( *tm_ );

    auto      &utxo_mgr    = account_->GetUTXOManager();
    const auto spent_state = utxo_mgr.GetOutPointState( arm.spent_outpoint, 0 );
    ASSERT_TRUE( spent_state.has_value() );
    EXPECT_NE( spent_state.value(), UTXOManager::UTXOState::UTXO_READY ) << "spent outpoint resurrected READY";
    EXPECT_EQ( spent_state.value(), UTXOManager::UTXOState::UTXO_CONSUMED );
    const auto spender = utxo_mgr.GetOutPointSpender( arm.spent_outpoint, 0 );
    ASSERT_TRUE( spender.has_value() );
    EXPECT_EQ( spender.value().toReadableString(), arm.spender->GetHash() );

    // Disk-level proof: the CONSUMED record with its spent_by_txid survived
    // the clear on the raw /utxo/ prefix.
    const auto record = FindUtxoRecord( arm.spent_outpoint, 0 );
    ASSERT_TRUE( record.has_value() );
    EXPECT_EQ( record->second.state(), SGTransaction::UTXO_ENTRY_CONSUMED );
    ASSERT_TRUE( record->second.has_spent_by_txid() );
    EXPECT_EQ( record->second.spent_by_txid(),
               std::string( reinterpret_cast<const char *>( SpenderHash( *arm.spender ).data() ),
                            SpenderHash( *arm.spender ).size() ) );

    // The producer replay processed fine — nothing went missing, nothing to
    // re-request (zero doomed recovery requests).
    EXPECT_FALSE( tm_->HasMissingTxHashes() );

    // The pruned spender's change output did not come back either: the clear
    // dropped it (it was READY) and only its erased spender could re-create it.
    const auto change_state = utxo_mgr.GetOutPointState( arm.change_outpoint, 0 );
    EXPECT_FALSE( change_state.has_value() ) << "pruned spender's change output resurrected";
}

/// Case 2 (own replay): a spender whose transaction SURVIVES but whose inputs'
/// CONSUMED evidence is preserved through the clear. The rebuild replays it
/// without the double-spend reject and re-creates its change output; a FOREIGN
/// certified spender of the same input still rejects (T-19-0402).
TEST_F( TxPrunerTest, RebuildGuardOwnReplayReappliesSpenderChangeWithoutDoubleSpendReject )
{
    RebuildArm arm;
    SeedMismatchArm( arm );
    ASSERT_FALSE( HasFatalFailure() );
    ASSERT_NO_FATAL_FAILURE( ArmMismatchByLosingChangeOutput( arm ) );

    // The foreign conflicting spender (different nonce): driven straight
    // through the CONFIRMED parse below — the exact production code path the
    // rebuild would take for it — after the rebuild settled, so the reject it
    // must hit is order-independent.
    auto conflicting = MakeSpender( *arm.producer, 0, 3 );

    RebuildTransactionManagerOnly();
    ASSERT_FALSE( HasFatalFailure() );

    TransactionManagerPendingLifecycleTestAccess::RunInitializeUTXOs( *tm_ );

    auto &utxo_mgr = account_->GetUTXOManager();

    // The legitimate spender's change output is READY after the rebuild —
    // impossible under the double-spend reject (the reject precedes
    // PutProducedUTXOs, and the CONFIRMED status below is the same code path
    // as the "Rejected double-spending transfer" ERROR: the status proves the
    // reject never fired).
    const auto change_state = utxo_mgr.GetOutPointState( arm.change_outpoint, 0 );
    ASSERT_TRUE( change_state.has_value() );
    EXPECT_EQ( change_state.value(), UTXOManager::UTXOState::UTXO_READY );
    EXPECT_EQ( tm_->GetTransactionStatusByTxId( arm.spender->GetHash() ),
               TransactionManager::TransactionStatus::CONFIRMED );

    // The consumed input was not resurrected by the producer's replay.
    const auto spent_state = utxo_mgr.GetOutPointState( arm.spent_outpoint, 0 );
    ASSERT_TRUE( spent_state.has_value() );
    EXPECT_EQ( spent_state.value(), UTXOManager::UTXOState::UTXO_CONSUMED );

    // The foreign spender still rejects: its input's spender-of-record is the
    // legitimate spender, so the own-replay discriminator must NOT accept it
    // (T-19-0402 — accepting a foreign spender would be the actual bypass).
    const auto conflicting_result = TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState(
        *tm_,
        conflicting,
        TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_TRUE( conflicting_result.has_error() );
    const auto conflicting_state = utxo_mgr.GetOutPointState(
        base::Hash256::fromReadableString( conflicting->GetHash() ).value(),
        0 );
    EXPECT_FALSE( conflicting_state.has_value() ) << "foreign double spend credited its output";
}

/// Case 3 (the MANDATORY belt induction): the CONSUMED entry removed post-clear
/// so PutUTXO re-creates the outpoint READY, while a durable CONSUMED record
/// with the spender binding survives — the crosscheck must DETECT and REVERT
/// (no exercised-by-construction downgrade).
TEST_F( TxPrunerTest, RebuildGuardCrosscheckBeltRevertsResurrectedOutpoint )
{
    RebuildArm arm;
    SeedMismatchArm( arm );
    ASSERT_FALSE( HasFatalFailure() );
    ASSERT_TRUE( tm_->EraseTransactionEntryPruned( arm.spender->GetHash() ).has_value() );

    auto       &utxo_mgr = account_->GetUTXOManager();
    const auto &owner    = account_->GetAddress();

    // Re-index through a reload first: the live consume above leaves the
    // CONSUMED entry orphaned from the address index (the 19-01 finding), and
    // the removal below must see the index-resident shape.
    ASSERT_TRUE( utxo_mgr.LoadUTXOs( db_->GetDataStore() ).has_value() );

    // Arm: the post-clear CONSUMED entry is removed and the producer replay's
    // PutUTXO re-creates the outpoint READY (the resurrection the belt exists
    // for — exactly what the by-construction duplicate check would have
    // blocked had the entry survived).
    ASSERT_TRUE( utxo_mgr.DeleteUTXO( arm.spent_outpoint, 0, owner ).has_value() );
    const auto outputs = arm.producer->GetProducedUTXOs();
    ASSERT_TRUE( outputs.has_value() && !outputs->empty() );
    const auto resurrected = utxo_mgr.PutUTXO( outputs->front(), outputs->front().GetOwnerAddress() );
    ASSERT_TRUE( resurrected.has_value() && resurrected.value() );
    const auto pre_state = utxo_mgr.GetOutPointState( arm.spent_outpoint, 0 );
    ASSERT_TRUE( pre_state.has_value() );
    ASSERT_EQ( pre_state.value(), UTXOManager::UTXOState::UTXO_READY );

    // Divergence: the durable /utxo/ record still carries the CONSUMED
    // evidence with the spender binding (the one-corrupted-record class,
    // T-19-0401) — the in-memory READY entry can never name its spender.
    const auto record = FindUtxoRecord( arm.spent_outpoint, 0 );
    ASSERT_TRUE( record.has_value() );
    ASSERT_NO_FATAL_FAILURE( TamperUtxoRecordToConsumed( record->first, SpenderHash( *arm.spender ) ) );

    const size_t reverted = TransactionManagerPendingLifecycleTestAccess::RunCrosscheckRebuiltUtxos( *tm_ );
    EXPECT_EQ( reverted, 1u );

    const auto post_state = utxo_mgr.GetOutPointState( arm.spent_outpoint, 0 );
    ASSERT_TRUE( post_state.has_value() );
    EXPECT_EQ( post_state.value(), UTXOManager::UTXOState::UTXO_CONSUMED );
    const auto spender_after = utxo_mgr.GetOutPointSpender( arm.spent_outpoint, 0 );
    ASSERT_TRUE( spender_after.has_value() );
    EXPECT_EQ( spender_after.value().toReadableString(), arm.spender->GetHash() );
}

/// Case 4a (marker consult, pruned arm): a local READY UTXO whose creating tx
/// was pruned locally (marker present, value absent) is NOT relevant missing
/// history — no recovery request, no missing-set entry, boot proceeds.
TEST_F( TxPrunerTest, RebuildGuardMarkerConsultSkipsLocallyPrunedCreatingTx )
{
    auto pruned_producer = MakeTransfer( 1, 1 );
    SeedTransaction( pruned_producer );
    // Move the topic head past the producer's delta (a topic head never
    // erases — GATE-03), then track its output and prune the history.
    PutSeedValue( "rebuildguard/marker-consult/head", "advance" );
    TrackOutputs( *pruned_producer );
    ASSERT_TRUE( tm_->EraseTransactionEntryPruned( pruned_producer->GetHash() ).has_value() );

    // No checkpoint: the local-UTXO walk (the insert site under test) runs.
    TransactionManagerPendingLifecycleTestAccess::RunInitializeUTXOs( *tm_ );

    EXPECT_FALSE( tm_->HasMissingTxHashes() );
}

/// Case 4b (marker consult, negative control): an UNMARKED missing creating tx
/// keeps the 18-landed bounded recovery path exactly as-is (the insert fires).
TEST_F( TxPrunerTest, RebuildGuardMarkerConsultKeepsBoundedRecoveryForUnmarkedCreatingTx )
{
    auto ghost_producer = MakeTransfer( 1, 1 ); // never seeded: no value, no marker
    TrackOutputs( *ghost_producer );

    TransactionManagerPendingLifecycleTestAccess::RunInitializeUTXOs( *tm_ );

    EXPECT_TRUE( tm_->HasMissingTxHashes() );
}

// ---------------------------------------------------------------------------
// RestartMatrix: SC-1/SURV-01 — the full restart matrix on a pruned node
// ---------------------------------------------------------------------------

/**
 * The REAL-boot leg. A pruned node (frontier nonces 1-2 erased with markers,
 * the certified nonce-3 frontier alive) is restarted on the SAME database
 * path and booted through the REAL TransactionManager::StartCore — never
 * ForceManagerReady (Pitfall 5) — reaching READY within a bounded wait while
 * making ZERO pruned-CID fetch attempts across the ENTIRE restart window:
 * the 19-03 retry guard keeps the ladder still, the 19-04 marker consult
 * keeps the missing set empty for the locally-pruned creating refs, and the
 * spend evidence + live balance + seals + heads all survive the restart.
 *
 * READY strategy: (a) nonce responder. The plan's primary (b) — a pre-seeded
 * local confirmed nonce — cannot fire on this fixture: CheckNonce (read in
 * full) only passes a Light node when FetchNetworkNonce SUCCEEDS (with
 * "no nonce info" or a network nonce <= local), and with no answering peer
 * the fetch errors and fails closed regardless of any local pre-seed. The
 * responder answers "no nonce for that address" — the trusting-local path.
 */
TEST_F( TxPrunerTest, RestartMatrix_RealBootPrunedNodeReachesReadyWithZeroPrunedCidFetches )
{
    RestartMatrixContext context;
    SeedPruneAndReopenRealBoot( context );
    ASSERT_FALSE( HasFatalFailure() );

    // The real boot's own gates.
    EXPECT_EQ( tm_->GetState(), TransactionManager::State::READY );
    EXPECT_FALSE( tm_->HasMissingTxHashes() ); // pruned creating refs never entered the set
    EXPECT_EQ( crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0u );
    // The per-instance ladder observable (dedup-safe, the 19-07 lesson): no
    // failed-root retry entry exists on the reopened datastore.
    EXPECT_EQ( db_->GetCRDTDataStore()->GetFailedRootRetryCountForTest(), 0u );

    // The pruned frontier is intact and never resurrected.
    EXPECT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *context.arm.producer ) } ).has_error() );
    EXPECT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *context.arm.middle ) } ).has_error() );
    EXPECT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *context.arm.frontier ) } ).has_value() );
    const RawSnapshot erased_blocks = SnapshotBlocks( { context.producer_cid, context.middle_cid } );
    EXPECT_EQ( erased_blocks.at( context.producer_cid ), "absent" );
    EXPECT_EQ( erased_blocks.at( context.middle_cid ), "absent" );
    EXPECT_NE( SnapshotBlocks( { context.frontier_cid } ).at( context.frontier_cid ), "absent" );

    // Spend evidence + live balance survived the restart.
    auto      &utxo_mgr       = account_->GetUTXOManager();
    const auto producer_state = utxo_mgr.GetOutPointState( context.arm.producer_outpoint, 0 );
    ASSERT_TRUE( producer_state.has_value() );
    EXPECT_EQ( producer_state.value(), UTXOManager::UTXOState::UTXO_CONSUMED );
    const auto middle_state = utxo_mgr.GetOutPointState( context.arm.middle_outpoint, 0 );
    ASSERT_TRUE( middle_state.has_value() );
    EXPECT_EQ( middle_state.value(), UTXOManager::UTXOState::UTXO_CONSUMED );
    const auto frontier_state = utxo_mgr.GetOutPointState( context.arm.frontier_outpoint, 0 );
    ASSERT_TRUE( frontier_state.has_value() );
    EXPECT_EQ( frontier_state.value(), UTXOManager::UTXOState::UTXO_READY );

    // The /utxo/ record key set is identical (no resurrection, no loss);
    // seals are byte-identical; the DAG heads are unchanged.
    EXPECT_EQ( UtxoRecordKeys(), context.utxo_keys_before );
    EXPECT_EQ( SnapshotSeals( { context.producer_cid, context.middle_cid, context.frontier_cid } ),
               context.seals_before );
    uint64_t           max_height_after = 0;
    const HeadSnapshot heads_after      = SnapshotHeads( max_height_after );
    EXPECT_EQ( heads_after, context.heads_before );

    // The durable nonce chain survived the restart (the reloaded local
    // confirmed nonce is the frontier's certified nonce).
    auto local_nonce = account_->GetLocalConfirmedNonce();
    ASSERT_TRUE( local_nonce.has_value() );
    EXPECT_EQ( local_nonce.value(), 3u );
}

/**
 * The seam companion (cheap regression breadth): the SAME zero-fetch
 * invariant across the db-restart window on the ForceManagerReady seam path.
 * The seam bypasses InitializeUTXOs/InitTransactions entirely (Pitfall 5 —
 * which is exactly why it can never be the only leg), so this case proves
 * the RESTART machinery itself fetches nothing; the boot-truth proof lives
 * in the real-boot leg above.
 */
TEST_F( TxPrunerTest, RestartMatrix_SeamBootZeroFetchCompanion )
{
    RestartArm arm;
    SeedRestartArm( arm );
    ASSERT_FALSE( HasFatalFailure() );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    ASSERT_EQ( stats.erased, 2u );

    registry_.reset(); // release pre-reopen db handles (the 19-04 lock lesson)

    crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    ReopenStackOnSamePath(); // seam path: ForceManagerReady
    ASSERT_FALSE( HasFatalFailure() );

    EXPECT_EQ( tm_->GetState(), TransactionManager::State::READY );
    EXPECT_EQ( crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0u );
    EXPECT_FALSE( tm_->HasMissingTxHashes() );

    // The pruned frontier is still pruned on the seam path too.
    for ( const auto *pruned : { arm.producer.get(), arm.middle.get() } )
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, pruned->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }
}

/**
 * The full SC-1/SURV-01 matrix, in the sentence's own order: prune ->
 * restart (REAL StartCore) -> READY with zero pruned-CID fetches -> new-tip
 * sync -> receive/spend -> the next outgoing tx chains correctly through
 * the pruned frontier. Instantiated TWICE (MatrixDoubleRun): the whole
 * family runs twice consecutively, each run on its own fresh DB path (a
 * fresh fixture per instantiation — the double-run stability gate).
 */
class TxPrunerRestartMatrixTest : public TxPrunerTest, public ::testing::WithParamInterface<uint32_t>
{
};

TEST_P( TxPrunerRestartMatrixTest, FullMatrixPruneRestartReadyTipReceiveOutgoing )
{
    RestartMatrixContext context;
    SeedPruneAndReopenRealBoot( context );
    ASSERT_FALSE( HasFatalFailure() );

    // Beats 1-3 (prune -> restart -> READY): the zero-fetch boot gates.
    EXPECT_EQ( tm_->GetState(), TransactionManager::State::READY );
    EXPECT_FALSE( tm_->HasMissingTxHashes() );
    EXPECT_EQ( crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0u );
    EXPECT_EQ( db_->GetCRDTDataStore()->GetFailedRootRetryCountForTest(), 0u );

    //
    // Beat 4a — TipSync: a new tip arrives on the fixture's real topic path
    // (a foreign-source transfer certified at its slot) and the restarted
    // pruned node processes it: tracked map + UTXO effects, tip advanced,
    // still zero fetches.
    //
    crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    auto incoming_tip = MakeForeignTransfer( *nonce_responder_account_, 1, std::nullopt );
    ASSERT_NE( incoming_tip, nullptr );
    SeedTransaction( incoming_tip );
    SeedCertificate( incoming_tip );
    auto tip_txid = base::Hash256::fromReadableString( incoming_tip->GetHash() );
    ASSERT_FALSE( tip_txid.has_error() );
    {
        auto &utxo_mgr = account_->GetUTXOManager();
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                TransactionManagerPendingLifecycleTestAccess::RunTickOnce( *tm_ );
                auto tracked = tm_->GetTransactionStatusByTxId( incoming_tip->GetHash() );
                if ( tracked == TransactionManager::TransactionStatus::INVALID )
                {
                    return false;
                }
                auto paid = utxo_mgr.GetOutPointState( tip_txid.value(), 1 ); // output 1 pays this account
                return paid.has_value() && paid.value() == UTXOManager::UTXOState::UTXO_READY;
            },
            std::chrono::seconds( 10 ),
            "new-tip delivery is processed (tracked map + UTXO effects)",
            nullptr );
    }
    {
        uint64_t max_height_tip = 0;
        SnapshotHeads( max_height_tip );
        EXPECT_GT( max_height_tip, context.max_height_before ) << "the tip advanced past the pre-restart height";
    }
    EXPECT_EQ( crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0u );

    //
    // Beat 4b — ReceiveSpend: a second foreign delivery SPENDS the still
    // -unspent output 0 of the tip (the responder's own output) and pays this
    // account again: the real ConsumeUTXOs branch + a new READY output.
    //
    crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    auto incoming_spend = MakeForeignTransfer( *nonce_responder_account_, 2, std::make_pair( tip_txid.value(), 0u ) );
    ASSERT_NE( incoming_spend, nullptr );
    SeedTransaction( incoming_spend );
    SeedCertificate( incoming_spend );
    auto spend_txid = base::Hash256::fromReadableString( incoming_spend->GetHash() );
    ASSERT_FALSE( spend_txid.has_error() );
    {
        auto &utxo_mgr = account_->GetUTXOManager();
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                TransactionManagerPendingLifecycleTestAccess::RunTickOnce( *tm_ );
                auto spent = utxo_mgr.GetOutPointState( tip_txid.value(), 0 );
                auto paid  = utxo_mgr.GetOutPointState( spend_txid.value(), 1 );
                return spent.has_value() && spent.value() == UTXOManager::UTXOState::UTXO_CONSUMED &&
                       paid.has_value() && paid.value() == UTXOManager::UTXOState::UTXO_READY;
            },
            std::chrono::seconds( 10 ),
            "incoming spend of a still-unspent output applies its UTXO effects",
            nullptr );
        const auto spender_of_record = utxo_mgr.GetOutPointSpender( tip_txid.value(), 0 );
        ASSERT_TRUE( spender_of_record.has_value() );
        EXPECT_EQ( spender_of_record.value().toReadableString(), incoming_spend->GetHash() );
    }
    EXPECT_EQ( crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0u );

    //
    // Beat 5 — OutgoingChain: the next outgoing transaction carries the
    // durable-chain nonce and chains onto the GATE-04 resolution of the
    // pruned frontier (asserted BY COMPARISON with the ladder, never by
    // re-deriving the rule).
    //
    crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    constexpr uint64_t kExpectedNextNonce = 4; // durable chain: certified nonces 1..3
    auto               local_nonce        = account_->GetLocalConfirmedNonce();
    ASSERT_TRUE( local_nonce.has_value() );
    ASSERT_EQ( local_nonce.value(), 3u );

    const std::string ladder_hash = TransactionManagerPendingLifecycleTestAccess::OutgoingPreviousHash(
        *tm_,
        kExpectedNextNonce );
    ASSERT_FALSE( ladder_hash.empty() ) << "the ladder failed to resolve the pruned frontier";
    EXPECT_EQ( ladder_hash, context.arm.frontier->GetHash() ) << "the chain resolves onto the certified frontier";
    EXPECT_NE( ladder_hash, context.arm.producer->GetHash() ) << "resolved through a PRUNED tx";
    EXPECT_NE( ladder_hash, context.arm.middle->GetHash() ) << "resolved through a PRUNED tx";

    // Belt: the cert-slot rung's own data — the never-pruned certificate at
    // slot <addr>:3 embeds exactly the hash the ladder answered with.
    {
        auto slot_cert = blockchain_->GetCertificateBySlot( account_->GetAddress() + ":3" );
        ASSERT_FALSE( slot_cert.has_error() );
        auto slot_subject = ConsensusManager::DecodeNonceSubject( slot_cert.value().proposal().subject() );
        ASSERT_FALSE( slot_subject.has_error() );
        ASSERT_EQ( slot_subject.value().nonce(), 3u );
        auto slot_embedded = TransactionManager::DeSerializeEmbeddedTransaction( slot_subject.value().transaction() );
        ASSERT_TRUE( slot_embedded.has_value() && slot_embedded.value() );
        EXPECT_EQ( slot_embedded.value()->GetHash(), ladder_hash );
    }

    // Build through the production DAG fill (reserves the nonce + resolves
    // previous_hash), enqueue through the production queue, and drive the
    // send: the self-running tick loop (proven live — it carried the boot)
    // processes it without any pruned-CID fetch.
    auto dag = TransactionManagerPendingLifecycleTestAccess::RunFillDAGStruct( *tm_ );
    EXPECT_EQ( dag.nonce(), kExpectedNextNonce );
    EXPECT_EQ( dag.previous_hash(), ladder_hash );

    auto outgoing = MakeSpenderWithDag( *context.arm.frontier, 0, dag );
    ASSERT_NE( outgoing, nullptr );
    TransactionManagerPendingLifecycleTestAccess::EnqueueOutgoing( *tm_, outgoing, db_->BeginTransaction() );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            TransactionManagerPendingLifecycleTestAccess::RunTickOnce( *tm_ );
            auto status = tm_->GetTransactionStatusByTxId( outgoing->GetHash() );
            return status != TransactionManager::TransactionStatus::CREATED &&
                   status != TransactionManager::TransactionStatus::INVALID;
        },
        std::chrono::seconds( 15 ),
        "post-restart outgoing send is driven past CREATED",
        nullptr );
    // The send published durably and the manager is still READY (the 19-02
    // READY-preserving behavior — a send-path failure must never wedge it).
    EXPECT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *outgoing ) } ).has_value() );
    EXPECT_EQ( tm_->GetState(), TransactionManager::State::READY );
    EXPECT_EQ( crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0u );
}

INSTANTIATE_TEST_SUITE_P( RestartMatrixDoubleRun,
                          TxPrunerRestartMatrixTest,
                          ::testing::Range<uint32_t>( 0, 2 ),
                          []( const ::testing::TestParamInfo<uint32_t> &info )
                          { return "Round" + std::to_string( info.param ); } );
