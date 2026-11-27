/**
 * @file       transaction_manager_pending_lifecycle_test.cpp
 * @brief      CRDT-backed TransactionManager recovery integration tests.
 * @details    Covers send recovery, nonce reconciliation, previous-hash recovery,
 *             and transaction deletion without full GeniusNode startup.
 *             D-02 adds pre-broadcast failure classification (READY-preserving)
 *             and the bounded SYNCING exit on the durable confirmed nonce.
 * @date       2026-06-16
 */

#include <gtest/gtest.h>

#include "transaction/TransactionManager.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "account/MintTransaction.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/TransferTransaction.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ConsensusAuth.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/atomic_transaction.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

#include <string>
#include <vector>

namespace sgns
{
    /**
     * @brief Test-only access for deterministic TransactionManager recovery failures.
     */
    class TransactionManagerPendingLifecycleTestAccess
    {
    public:
        static void ChangeState( TransactionManager &manager, TransactionManager::State state )
        {
            manager.ChangeState( state );
        }

        static void Enqueue( TransactionManager                      &manager,
                             std::shared_ptr<GeniusTransaction>       transaction,
                             std::shared_ptr<crdt::AtomicTransaction> crdt_transaction )
        {
            TransactionManager::TransactionBatch batch;
            batch.emplace_back( std::move( transaction ), std::nullopt );
            manager.EnqueueTransaction( { std::move( batch ), std::move( crdt_transaction ) } );
        }

        static void TickOnce( TransactionManager &manager )
        {
            manager.TickOnce();
        }

        static void RunPeriodicHeadSync( TransactionManager &manager, std::chrono::steady_clock::time_point now )
        {
            manager.RunPeriodicHeadSync( now );
        }

        static std::chrono::steady_clock::time_point PeriodicSyncLastAttempt( TransactionManager &manager )
        {
            return manager.last_periodic_sync_time_;
        }

        static void SetPeriodicSyncLastAttempt( TransactionManager                      &manager,
                                                std::chrono::steady_clock::time_point when )
        {
            manager.last_periodic_sync_time_ = when;
        }

        static outcome::result<std::unordered_set<std::string>> MonitoredTopics( TransactionManager &manager )
        {
            return manager.globaldb_m->GetMonitoredTopics();
        }

        static void RunInitializeUTXOs( TransactionManager &manager )
        {
            ASSERT_TRUE( manager.InitializeUTXOs().has_value() );
        }

        static void RunInitTransactions( TransactionManager &manager )
        {
            manager.InitTransactions();
        }

        static void SetMissingTxMaxAttempts( uint8_t max_attempts )
        {
            TransactionManager::SetMissingTxMaxAttemptsForTest( max_attempts );
        }

        static void SetSyncingExitTimeout( std::chrono::milliseconds timeout )
        {
            TransactionManager::SetSyncingExitTimeoutForTest( timeout );
        }

        static void RestoreSyncingExitTimeout()
        {
            TransactionManager::SetSyncingExitTimeoutForTest( TransactionManager::k_syncing_exit_timeout );
        }

        static bool SyncingWindowIsArmed( TransactionManager &manager )
        {
            return manager.syncing_since_.has_value();
        }

        static outcome::result<void> ChangeTransactionState( TransactionManager                       &manager,
                                                             const std::shared_ptr<GeniusTransaction> &transaction,
                                                             TransactionManager::TransactionStatus     status )
        {
            return manager.ChangeTransactionState( transaction, status );
        }

        static std::optional<TransactionManager::TrackedTx> GetTrackedTx( TransactionManager    &manager,
                                                                         const std::string     &tx_hash )
        {
            std::shared_lock lock( manager.tx_mutex_m );
            auto             it = manager.tx_processed_m.find( TransactionManager::GetTransactionPath( tx_hash ) );
            return it != manager.tx_processed_m.end() ? std::optional{ it->second } : std::nullopt;
        }

        static size_t ForgetTrackedTx( TransactionManager &manager, const std::string &tx_hash )
        {
            std::unique_lock lock( manager.tx_mutex_m );
            return manager.tx_processed_m.erase( TransactionManager::GetTransactionPath( tx_hash ) );
        }

        static std::string QueryOutgoingPreviousHashFromCRDT( const TransactionManager &manager, uint64_t nonce )
        {
            return manager.QueryOutgoingPreviousHashFromCRDT( nonce );
        }

        static void SetFailNextPutUTXOStore( sgns::UTXOManager &utxo_manager, bool fail )
        {
            utxo_manager.SetFailNextPutUTXOStoreForTest( fail );
        }

        static std::string ScopedChainId( const std::string &private_network_id )
        {
            return TransactionManager::ScopedChainId( private_network_id );
        }

        static bool SelectsGeniusValidator( TransactionManager                      &manager,
                                            const std::shared_ptr<GeniusTransaction> &tx )
        {
            const auto selection    = manager.SelectInputValidator( *tx );
            const auto *chosen      = &selection.validator;
            const bool  is_genius   = chosen == &manager.genius_input_validator_;
            const bool  is_registry = chosen == IInputValidator::Get( "supergenius" )
                                   || chosen == IInputValidator::Get( "supergenius_chain" );
            return is_genius || is_registry;
        }

        /// Drives the real proposal-subject ingress path (the same entry the
        /// consensus manager's HandleProposal uses) synchronously.
        static outcome::result<ConsensusManager::ValidationResult> HandleNonceConsensusSubject(
            TransactionManager              &manager,
            const ConsensusManager::Subject &subject )
        {
            return manager.consensus_m_->HandleNonceConsensusSubject( subject );
        }

        /// Drives the real certificate-delivery path (the same entry the
        /// consensus manager's ProcessCommittedCertificate invokes) synchronously.
        static outcome::result<ConsensusManager::Check> OnConsensusCertificate(
            TransactionManager                        &manager,
            const std::string                         &tx_hash,
            const ConsensusManager::Certificate       &certificate )
        {
            return manager.consensus_m_->OnConsensusCertificate( tx_hash, certificate );
        }
    };
} // namespace sgns

namespace
{
    class TransactionManagerRecoveryTest : public test::CRDTFixture
    {
    public:
        TransactionManagerRecoveryTest() : CRDTFixture( "transaction_manager_recovery_test" )
        {
        }

        void SetUp() override
        {
            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage>
                { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
            account_ = sgns::GeniusAccount::New( kTokenId, base_path / "account" );
            ASSERT_TRUE( account_ );
            ASSERT_TRUE( account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() ).has_value() );
            (void) account_->ConfigureDatabaseDependencies( db_ );

            blockchain_ = sgns::Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
            ASSERT_TRUE( blockchain_ );

            manager_ = sgns::TransactionManager::New( db_, io_, account_, blockchain_ );
            ASSERT_TRUE( manager_ );
            manager_->RegisterTopicNames();

            account_->SetPeerConfirmedNonce( 0, account_->GetAddress() );
            sgns::TransactionManagerPendingLifecycleTestAccess::ChangeState( *manager_,
                                                                             sgns::TransactionManager::State::READY );
        }

        void TearDown() override
        {
            if ( manager_ )
            {
                manager_->Stop();
            }
        }

    protected:
        std::shared_ptr<sgns::MintTransaction> MakeTransaction()
        {
            return MakeTransaction( account_->ReserveNextNonce() );
        }

        SGTransaction::DAGStruct MakeDAG( uint64_t nonce, std::string previous_hash = {} ) const
        {
            SGTransaction::DAGStruct dag;
            dag.set_nonce( nonce );
            dag.set_previous_hash( std::move( previous_hash ) );
            dag.set_source_addr( account_->GetAddress() );
            dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() )
                                   .count() );
            return dag;
        }

        std::shared_ptr<sgns::MintTransaction> MakeTransaction( uint64_t nonce )
        {
            auto transaction = std::make_shared<sgns::MintTransaction>(
                sgns::MintTransaction::New( 1,
                                            std::string( sgns::GeniusTransaction::GENIUS_CHAIN_ID ),
                                            kTokenId,
                                            MakeDAG( nonce ) ) );
            transaction->MakeSignature( *account_ );
            return transaction;
        }

        void RecreateManager()
        {
            manager_->Stop();
            manager_.reset();
            manager_ = sgns::TransactionManager::New( db_, io_, account_, blockchain_ );
            ASSERT_TRUE( manager_ );
            manager_->RegisterTopicNames();
            sgns::TransactionManagerPendingLifecycleTestAccess::ChangeState( *manager_,
                                                                             sgns::TransactionManager::State::READY );
        }

        std::shared_ptr<sgns::GeniusTransaction> FindOutgoingTransaction( const std::string &hash ) const
        {
            for ( auto bytes : manager_->GetOutTransactions() )
            {
                auto transaction = sgns::TransactionManager::DeSerializeTransaction(
                    sgns::base::Buffer( std::move( bytes ) ) );
                if ( transaction.has_value() && transaction.value()->GetHash() == hash )
                {
                    return transaction.value();
                }
            }
            return nullptr;
        }

        std::shared_ptr<sgns::crdt::AtomicTransaction> MakeCommittedTransaction()
        {
            auto                         transaction = db_->BeginTransaction();
            sgns::crdt::GlobalDB::Buffer value;
            value.put( "committed" );
            if ( !transaction->Put( sgns::crdt::HierarchicalKey( "/recovery/already-committed" ), std::move( value ) ) )
            {
                ADD_FAILURE() << "Failed to populate the committed CRDT transaction";
                return nullptr;
            }
            if ( !transaction->Commit( { "CRDT.Datastore.TEST.Channel" } ) )
            {
                ADD_FAILURE() << "Failed to commit the CRDT transaction";
                return nullptr;
            }
            return transaction;
        }

        static inline const sgns::TokenID kTokenId = sgns::TokenID::FromBytes( { 0x00 } );

        std::shared_ptr<sgns::GeniusAccount>      account_;
        std::shared_ptr<sgns::Blockchain>         blockchain_;
        std::shared_ptr<sgns::TransactionManager> manager_;
    };

    class TransactionManagerPreviousHashTest : public TransactionManagerRecoveryTest
    {
    protected:
        void SetUp() override
        {
            TransactionManagerRecoveryTest::SetUp();
            if ( HasFatalFailure() )
            {
                return;
            }
            registry_ = blockchain_->GetValidatorRegistry();
            ASSERT_TRUE( registry_ );
            ASSERT_TRUE( registry_
                             ->StoreGenesisRegistry( { account_->GetAddress() },
                                                     [this]( std::vector<uint8_t> payload )
                                                     { return account_->Sign( payload ); } )
                             .has_value() );
        }

        sgns::ConsensusManager::Certificate MakeCertificate(
            sgns::GeniusAccount                            &signer,
            const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            auto subject = sgns::ConsensusManager::CreateNonceSubject( transaction->GetSrcAddress(),
                                                                       transaction->GetNonce(),
                                                                       transaction->GetHash(),
                                                                       transaction->SerializeToEmbeddedTransaction(),
                                                                       std::nullopt,
                                                                       std::nullopt );
            EXPECT_TRUE( subject.has_value() );

            auto proposal = sgns::ConsensusManager::CreateProposal( subject.value(),
                                                                    signer.GetAddress(),
                                                                    registry_->GetRegistryCid(),
                                                                    registry_->GetRegistryEpoch(),
                                                                    [&signer]( std::vector<uint8_t> payload )
                                                                    { return signer.Sign( payload ); } );
            EXPECT_TRUE( proposal.has_value() );

            sgns::ConsensusManager::Vote vote;
            vote.set_proposal_id( proposal.value().proposal_id() );
            vote.set_voter_id( signer.GetAddress() );
            vote.set_approve( true );
            vote.set_timestamp( proposal.value().timestamp() );
            auto vote_bytes = sgns::VoteSigningBytes( vote );
            EXPECT_TRUE( vote_bytes.has_value() );
            const auto vote_signature = account_->Sign( std::move( vote_bytes.value() ) );
            vote.set_signature( vote_signature.data(), vote_signature.size() );

            sgns::ConsensusManager::Certificate certificate;
            certificate.set_proposal_id( proposal.value().proposal_id() );
            certificate.set_registry_cid( proposal.value().registry_cid() );
            certificate.set_registry_epoch( proposal.value().registry_epoch() );
            certificate.set_total_weight( 1 );
            certificate.set_approved_weight( 1 );
            certificate.set_timestamp( vote.timestamp() );
            *certificate.add_votes()        = vote;
            *certificate.mutable_proposal() = proposal.value();
            return certificate;
        }

        /// Builds a delegated escrow payout: any signer may spend output idx 0 of
        /// an escrow-lock outpoint (UTXOManager::VerifyParameters delegated branch),
        /// which is exactly the authorization shape of the 2026-11-19 staging
        /// double-spend incident. The payout chains onto the SIGNER's own nonce
        /// chain (previous_hash), never onto the escrow hold.
        std::shared_ptr<sgns::TransferTransaction> MakePayout( sgns::GeniusAccount         &signer,
                                                               const sgns::base::Hash256  &outpoint_txid,
                                                               uint64_t                    outpoint_idx,
                                                               uint64_t                    nonce,
                                                               const std::string          &previous_hash,
                                                               const std::string          &destination )
        {
            sgns::InputUTXOInfo input;
            input.txid_hash_  = outpoint_txid;
            input.output_idx_ = outpoint_idx;
            input.signature_  = signer.Sign( input.SerializeForSigning() );
            auto dag          = MakeDAG( nonce, previous_hash );
            dag.set_source_addr( signer.GetAddress() );
            auto payout       = std::make_shared<sgns::TransferTransaction>( sgns::TransferTransaction::New(
                std::vector{ std::move( input ) },
                { sgns::OutputDestInfo{ 1, destination, kTokenId } },
                std::move( dag ) ) );
            payout->MakeSignature( signer );
            return payout;
        }

        void StoreCertificate( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            const auto certificate = MakeCertificate( *account_, transaction );

            sgns::crdt::GlobalDB::Buffer certificate_data;
            certificate_data.put( certificate.SerializeAsString() );
            // The canonical slot record is the sole certificate authority: store
            // and check at the transaction's slot, never at its hash.
            ASSERT_TRUE( db_->Put( sgns::crdt::HierarchicalKey( "/cert/" + transaction->GetSlotID() ),
                                   certificate_data,
                                   { "CRDT.Datastore.TEST.Channel" } )
                             .has_value() );
            ASSERT_TRUE( blockchain_->CheckCertificateForSlot( transaction->GetSlotID() ) );
        }

        void StoreTransaction( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            sgns::crdt::GlobalDB::Buffer transaction_data;
            transaction_data.put( transaction->SerializeByteVector() );
            ASSERT_TRUE(
                db_->Put( sgns::crdt::HierarchicalKey( sgns::TransactionManager::GetTransactionPath( *transaction ) ),
                          transaction_data,
                          { account_->GetAddress() } )
                    .has_value() );
        }

        void ProcessStoredTransaction( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            sgns::test::assertWaitForCondition(
                [&]()
                {
                    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
                    return manager_->GetTransactionStatusByTxId( transaction->GetHash() ) ==
                           sgns::TransactionManager::TransactionStatus::CONFIRMED;
                },
                std::chrono::seconds( 5 ),
                "stored transaction was not processed" );
            ASSERT_EQ( manager_->GetTransactionStatusByTxId( transaction->GetHash() ),
                       sgns::TransactionManager::TransactionStatus::CONFIRMED );
        }

        void DeleteStoredTransaction( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            ASSERT_TRUE( db_->Remove( sgns::crdt::HierarchicalKey(
                                          sgns::TransactionManager::GetTransactionPath( *transaction ) ),
                                      { account_->GetAddress() } )
                             .has_value() );
            sgns::test::assertWaitForCondition(
                [&]()
                {
                    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
                    return manager_->GetTransactionStatusByTxId( transaction->GetHash() ) ==
                           sgns::TransactionManager::TransactionStatus::INVALID;
                },
                std::chrono::seconds( 5 ),
                "deleted transaction was not processed" );
            ASSERT_EQ( manager_->GetTransactionStatusByTxId( transaction->GetHash() ),
                       sgns::TransactionManager::TransactionStatus::INVALID );
        }

        std::shared_ptr<sgns::ValidatorRegistry> registry_;
    };

    class TransactionDeletionRecoveryTest : public TransactionManagerPreviousHashTest
    {
    };
} // namespace

TEST_F( TransactionManagerRecoveryTest, NonRetryableFailureDoesNotStrandFollowingTransaction )
{
    auto       failed_transaction = MakeTransaction();
    const auto failed_nonce       = failed_transaction->GetNonce();

    // Reusing a committed CRDT transaction makes Put() fail deterministically and
    // exercises the non-retryable send recovery path without altering production code.
    auto committed_transaction = MakeCommittedTransaction();
    ASSERT_TRUE( committed_transaction );
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_,
                                                                 failed_transaction,
                                                                 std::move( committed_transaction ) );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );

    ASSERT_EQ( manager_->GetState(), sgns::TransactionManager::State::SYNCING );
    ASSERT_EQ( manager_->GetTransactionStatusByTxId( failed_transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::FAILED );

    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
    ASSERT_EQ( manager_->GetState(), sgns::TransactionManager::State::READY );

    auto following_transaction = MakeTransaction();
    ASSERT_EQ( following_transaction->GetNonce(), failed_nonce );

    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_,
                                                                 following_transaction,
                                                                 db_->BeginTransaction() );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );

    EXPECT_EQ( manager_->GetTransactionStatusByTxId( following_transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::SENDING );
}

TEST_F( TransactionManagerRecoveryTest, LocalNonceAheadChecksTrackedTransactions )
{
    auto transaction = MakeTransaction();
    ASSERT_EQ( transaction->GetNonce(), 1U );

    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_, transaction, db_->BeginTransaction() );
    ASSERT_EQ( manager_->GetTransactionStatusByTxId( transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::CREATED );

    sgns::TransactionManagerPendingLifecycleTestAccess::ChangeState( *manager_,
                                                                     sgns::TransactionManager::State::SYNCING );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );

    EXPECT_EQ( manager_->GetState(), sgns::TransactionManager::State::SYNCING );
    EXPECT_EQ( manager_->GetTransactionStatusByTxId( transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::CONFIRMED );
}

TEST_F( TransactionManagerRecoveryTest, AsyncOutgoingWaitCompletesOnTerminalState )
{
    auto transaction = MakeTransaction();
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_, transaction, db_->BeginTransaction() );

    std::optional<sgns::TransactionManager::TransactionCompletion> completion;
    manager_->AsyncWaitForTransactionOutgoing( transaction->GetHash(),
                                               std::chrono::seconds( 5 ),
                                               [&]( sgns::TransactionManager::TransactionCompletion result )
                                               { completion = std::move( result ); } );

    ASSERT_TRUE( sgns::TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState(
                     *manager_,
                     transaction,
                     sgns::TransactionManager::TransactionStatus::FAILED )
                     .has_value() );

    sgns::test::assertWaitForCondition(
        [&]
        {
            io_->restart();
            io_->poll();
            return completion.has_value();
        },
        std::chrono::seconds( 1 ),
        "asynchronous transaction completion was not delivered" );

    ASSERT_TRUE( completion.has_value() );
    EXPECT_EQ( completion->transaction_id, transaction->GetHash() );
    EXPECT_EQ( completion->status, sgns::TransactionManager::TransactionStatus::FAILED );
    EXPECT_FALSE( completion->error );
}

TEST_F( TransactionManagerRecoveryTest, StopCancelsPendingOutgoingWait )
{
    auto transaction = MakeTransaction();
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_, transaction, db_->BeginTransaction() );

    std::optional<sgns::TransactionManager::TransactionCompletion> completion;
    manager_->AsyncWaitForTransactionOutgoing( transaction->GetHash(),
                                               std::chrono::seconds( 30 ),
                                               [&]( sgns::TransactionManager::TransactionCompletion result )
                                               { completion = std::move( result ); } );

    manager_->Stop();

    ASSERT_TRUE( completion.has_value() );
    EXPECT_EQ( completion->transaction_id, transaction->GetHash() );
    EXPECT_EQ( completion->status, sgns::TransactionManager::TransactionStatus::INVALID );
    EXPECT_EQ( completion->error, boost::asio::error::operation_aborted );
}

TEST_F( TransactionManagerRecoveryTest, AsyncOutgoingWaitTimesOutWithoutPollingThread )
{
    auto transaction = MakeTransaction();
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_, transaction, db_->BeginTransaction() );

    std::optional<sgns::TransactionManager::TransactionCompletion> completion;
    manager_->AsyncWaitForTransactionOutgoing( transaction->GetHash(),
                                               std::chrono::milliseconds( 10 ),
                                               [&]( sgns::TransactionManager::TransactionCompletion result )
                                               { completion = std::move( result ); } );

    sgns::test::assertWaitForCondition(
        [&]
        {
            io_->restart();
            io_->poll();
            return completion.has_value();
        },
        std::chrono::seconds( 1 ),
        "asynchronous transaction timeout was not delivered" );

    ASSERT_TRUE( completion.has_value() );
    EXPECT_EQ( completion->transaction_id, transaction->GetHash() );
    EXPECT_EQ( completion->status, sgns::TransactionManager::TransactionStatus::CREATED );
    EXPECT_EQ( completion->error, boost::system::errc::make_error_code( boost::system::errc::timed_out ) );
}

TEST_F( TransactionManagerRecoveryTest, PeriodicSyncFailureBacksOffUntilNextInterval )
{
    // The fixture pubsub has no peers, so the head request fails at the
    // request-topic peer gate. A failed attempt must still advance the
    // schedule, or the 300 ms TickOnce loop re-fires it ~3x/s.
    const auto topics = sgns::TransactionManagerPendingLifecycleTestAccess::MonitoredTopics( *manager_ );
    ASSERT_TRUE( topics.has_value() );
    ASSERT_FALSE( topics.value().empty() );

    const auto t0 = std::chrono::steady_clock::now() - std::chrono::seconds( 60 );
    sgns::TransactionManagerPendingLifecycleTestAccess::SetPeriodicSyncLastAttempt( *manager_, t0 );

    sgns::TransactionManagerPendingLifecycleTestAccess::RunPeriodicHeadSync( *manager_, t0 );
    // Attempt recorded despite the request failure.
    EXPECT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::PeriodicSyncLastAttempt( *manager_ ), t0 );

    // 5 s later, inside the 30 s initial interval: not due again yet.
    sgns::TransactionManagerPendingLifecycleTestAccess::RunPeriodicHeadSync( *manager_,
                                                                             t0 + std::chrono::seconds( 5 ) );
    EXPECT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::PeriodicSyncLastAttempt( *manager_ ), t0 );

    // Past the interval: due again, the attempt timestamp advances.
    const auto t1 = t0 + std::chrono::seconds( 31 );
    sgns::TransactionManagerPendingLifecycleTestAccess::RunPeriodicHeadSync( *manager_, t1 );
    EXPECT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::PeriodicSyncLastAttempt( *manager_ ), t1 );
}

TEST_F( TransactionManagerPreviousHashTest, UsesPersistedConfirmedHeadWhenPreviousTransactionIsNotTracked )
{
    auto previous_transaction = MakeTransaction( 0 );
    StoreCertificate( previous_transaction );
    // The certificate's slot record alone no longer proves a chain link from the
    // bare head hash (the by-hash recovery path is removed; no legacy records
    // exist): the durable transaction plus its slot certificate do.
    StoreTransaction( previous_transaction );
    const auto persisted_hash = account_->GetLocalConfirmedTxHash( 0 );
    ASSERT_TRUE( persisted_hash.has_value() );
    ASSERT_EQ( persisted_hash.value(), previous_transaction->GetHash() );

    RecreateManager();
    ASSERT_EQ( manager_->GetTransactionStatusByTxId( previous_transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::CONFIRMED );
    // Startup now reconstructs tracking. Forget only that transient entry so
    // this test still exercises the durable head without deleting its evidence.
    ASSERT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::ForgetTrackedTx( *manager_,
                                                                                    previous_transaction->GetHash() ),
               1U );
    ASSERT_EQ( manager_->GetTransactionStatusByTxId( previous_transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::INVALID );

    const auto transaction_id = manager_->MigrationFunds( 1, "persisted-head", kTokenId );
    ASSERT_TRUE( transaction_id.has_value() );
    const auto transaction = FindOutgoingTransaction( transaction_id.value() );
    ASSERT_TRUE( transaction );

    EXPECT_EQ( transaction->GetNonce(), 1U );
    EXPECT_EQ( transaction->GetPreviousHash(), previous_transaction->GetHash() );
}

TEST_F( TransactionManagerPreviousHashTest, FallsBackToCrdtWhenConfirmedHeadHistoryIsMissing )
{
    auto previous_transaction = MakeTransaction( 0 );
    StoreCertificate( previous_transaction );
    StoreTransaction( previous_transaction );

    RecreateManager();
    ASSERT_EQ( manager_->GetTransactionStatusByTxId( previous_transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::CONFIRMED );
    // Restore the missing-history scenario after startup has rebuilt both the
    // transient tracking entry and the account's confirmed-head history.
    ASSERT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::ForgetTrackedTx( *manager_,
                                                                                    previous_transaction->GetHash() ),
               1U );
    account_->RollBackPeerConfirmedNonce( 0, account_->GetAddress() );
    account_->SetPeerConfirmedNonce( 0, account_->GetAddress() );
    ASSERT_TRUE( account_->GetLocalConfirmedTxHash( 0 ).has_error() );
    ASSERT_EQ( manager_->GetTransactionStatusByTxId( previous_transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::INVALID );
    // The certificate-only predecessor path can also satisfy MigrationFunds;
    // verify the CRDT scan itself while its fallback preconditions hold.
    ASSERT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::QueryOutgoingPreviousHashFromCRDT( *manager_, 1 ),
               previous_transaction->GetHash() );

    const auto transaction_id = manager_->MigrationFunds( 1, "crdt-fallback", kTokenId );
    ASSERT_TRUE( transaction_id.has_value() );
    const auto transaction = FindOutgoingTransaction( transaction_id.value() );
    ASSERT_TRUE( transaction );

    EXPECT_EQ( transaction->GetNonce(), 1U );
    EXPECT_EQ( transaction->GetPreviousHash(), previous_transaction->GetHash() );
}

// Regression (CI run 35021511258, MissedCrdtHeadIsRecoveredAfterReconnect): a
// nonce-slot competitor arriving while the loser is terminally rejected must not
// resurrect the loser to VERIFYING — the resurrected entry then qualifies as the
// next outgoing transaction's previous hash, chaining it onto a predecessor whose
// certificate will never exist.
TEST_F( TransactionManagerPreviousHashTest, NonceConflictKeepsTerminallyRejectedTransactionFailed )
{
    auto failed_transaction = MakeTransaction( 1 );
    // Reusing a committed CRDT transaction makes Put() fail deterministically.
    auto committed_transaction = MakeCommittedTransaction();
    ASSERT_TRUE( committed_transaction );
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_,
                                                                 failed_transaction,
                                                                 std::move( committed_transaction ) );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
    ASSERT_EQ( manager_->GetTransactionStatusByTxId( failed_transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::FAILED );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
    ASSERT_EQ( manager_->GetState(), sgns::TransactionManager::State::READY );

    // Same-nonce competitor stored without a certificate: processing it must not
    // disturb the loser's terminal FAILED status.
    auto competitor = MakeTransaction( 1 );
    ASSERT_NE( competitor->GetHash(), failed_transaction->GetHash() );
    StoreTransaction( competitor );
    sgns::test::assertWaitForCondition(
        [&]()
        {
            sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
            return manager_->GetTransactionStatusByTxId( competitor->GetHash() ) ==
                   sgns::TransactionManager::TransactionStatus::VERIFYING;
        },
        std::chrono::seconds( 5 ),
        "stored competitor was not processed" );

    const auto tracked_failed =
        sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx( *manager_,
                                                                          failed_transaction->GetHash() );
    ASSERT_TRUE( tracked_failed.has_value() );
    EXPECT_EQ( tracked_failed->status, sgns::TransactionManager::TransactionStatus::FAILED );
}

// Regression (same run): with two tracked same-nonce candidates, the chain head
// for the next transaction must be the CONFIRMED one — never a non-terminal
// competitor — regardless of tracked-map iteration order.
TEST_F( TransactionManagerPreviousHashTest, TrackedPreviousHashPrefersConfirmedCandidate )
{
    auto confirmed_transaction = MakeTransaction( 1 );
    StoreCertificate( confirmed_transaction );
    StoreTransaction( confirmed_transaction );
    ProcessStoredTransaction( confirmed_transaction );

    // Simulate the mid-race state: a doomed same-nonce competitor tracked as
    // non-terminal alongside the confirmed head.
    auto resurrected_competitor = MakeTransaction( 1 );
    ASSERT_NE( resurrected_competitor->GetHash(), confirmed_transaction->GetHash() );
    (void) sgns::TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState(
        *manager_, resurrected_competitor, sgns::TransactionManager::TransactionStatus::VERIFYING );
    const auto tracked_competitor =
        sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx( *manager_,
                                                                          resurrected_competitor->GetHash() );
    ASSERT_TRUE( tracked_competitor.has_value() );
    ASSERT_EQ( tracked_competitor->status, sgns::TransactionManager::TransactionStatus::VERIFYING );

    const auto transaction_id = manager_->MigrationFunds( 1, "tracked-prefers-confirmed", kTokenId );
    ASSERT_TRUE( transaction_id.has_value() );
    const auto transaction = FindOutgoingTransaction( transaction_id.value() );
    ASSERT_TRUE( transaction );

    EXPECT_EQ( transaction->GetNonce(), 2U );
    EXPECT_EQ( transaction->GetPreviousHash(), confirmed_transaction->GetHash() );
}

TEST_F( TransactionDeletionRecoveryTest, TransferAndEscrowDeletionRestoresConsumedInputs )
{
    auto previous_transaction = MakeTransaction( 0 );
    StoreCertificate( previous_transaction );
    StoreTransaction( previous_transaction );
    ProcessStoredTransaction( previous_transaction );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance(), 1U );

    const auto dag = MakeDAG( account_->ReserveNextNonce(), previous_transaction->GetHash() );

    const auto mint_outpoint = sgns::base::Hash256::fromReadableString( previous_transaction->GetHash() );
    ASSERT_TRUE( mint_outpoint.has_value() );

    auto transfer_params = account_->GetUTXOManager().CreateTxParameter( 1, account_->GetAddress(), kTokenId );
    ASSERT_TRUE( transfer_params.has_value() );
    auto [transfer_inputs, transfer_outputs] = std::move( transfer_params.value() );
    auto transfer                            = std::make_shared<sgns::TransferTransaction>(
        sgns::TransferTransaction::New( std::move( transfer_inputs ), std::move( transfer_outputs ), dag ) );
    transfer->MakeSignature( *account_ );
    StoreCertificate( transfer );
    StoreTransaction( transfer );
    ProcessStoredTransaction( transfer );

    const auto transfer_outpoint = sgns::base::Hash256::fromReadableString( transfer->GetHash() );
    ASSERT_TRUE( transfer_outpoint.has_value() );
    EXPECT_FALSE( account_->GetUTXOManager().GetUnconsumedUTXO( mint_outpoint.value(), 0 ).has_value() );
    EXPECT_TRUE( account_->GetUTXOManager().GetUnconsumedUTXO( transfer_outpoint.value(), 0 ).has_value() );

    DeleteStoredTransaction( transfer );

    EXPECT_TRUE( account_->GetUTXOManager().GetUnconsumedUTXO( mint_outpoint.value(), 0 ).has_value() );
    EXPECT_FALSE( account_->GetUTXOManager().GetUnconsumedUTXO( transfer_outpoint.value(), 0 ).has_value() );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), 1U );

    const std::string escrow_lock   = "0x" + std::string( 64, '1' );
    auto              escrow_params = account_->GetUTXOManager().CreateTxParameter( 1, escrow_lock, kTokenId );
    ASSERT_TRUE( escrow_params.has_value() );
    auto escrow_dag = MakeDAG( account_->ReserveNextNonce(), previous_transaction->GetHash() );
    escrow_dag.set_uncle_hash( escrow_lock );
    auto escrow = std::make_shared<sgns::EscrowTransaction>(
        sgns::EscrowTransaction::New( std::move( escrow_params.value() ), 1, std::move( escrow_dag ) ) );
    escrow->MakeSignature( *account_ );
    StoreCertificate( escrow );
    StoreTransaction( escrow );
    ProcessStoredTransaction( escrow );

    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), 0U );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( escrow_lock ), 1U );

    DeleteStoredTransaction( escrow );

    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), 1U );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( escrow_lock ), 0U );

    DeleteStoredTransaction( previous_transaction );

    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), 0U );
}

TEST_F( TransactionManagerRecoveryTest, FundsAPIsFailClosedAfterStop )
{
    /**
     * Stop() detaches the manager from GlobalDB, Blockchain and the account
     * WITHOUT moving state_m out of READY — the funds APIs must check
     * stopped_ separately from the state. A late burn event on the EthWatch
     * thread (or an RPC call racing shutdown) that passed the READY-only
     * guard would enqueue a mint no live manager ever sends while returning
     * success to the relayer.
     */
    ASSERT_EQ( manager_->GetState(), sgns::TransactionManager::State::READY );

    manager_->Stop();

    EXPECT_TRUE( manager_->TransferFunds( 1, account_->GetAddress(), kTokenId ).has_error() );
    EXPECT_TRUE( manager_
                     ->MintFunds( 1,
                                  std::string( 64, '1' ),
                                  "public",
                                  kTokenId,
                                  account_->GetAddress() )
                     .has_error() );
    EXPECT_TRUE(
        manager_->MigrationFunds( 1, "0.2.0", kTokenId, account_->GetAddress() ).has_error() );
}

TEST_F( TransactionManagerRecoveryTest, PutConvergentImmutableFailsClosedAfterGlobalDBShutdown )
{
    /**
     * ShutdownNow() moves the CRDT datastore handle out of GlobalDB: every
     * accessor must go through ActiveCRDTDataStore() and fail closed.
     * PutConvergentImmutable dereferenced the raw member — the exact class
     * of null-deref that segfaulted migration_sync_test on aarch64 — so a
     * certificate write racing node shutdown crashed instead of failing.
     */
    manager_->Stop();
    db_->ShutdownNow();

    sgns::crdt::HierarchicalKey key( "immutable/after-shutdown" );
    sgns::crdt::GlobalDB::Buffer value;
    value.put( "certificate-bytes" );
    const auto put = db_->PutConvergentImmutable( key, value, {} );
    ASSERT_TRUE( put.has_error() );
    EXPECT_EQ( put.error(), std::errc::operation_canceled );
}

TEST_F( TransactionManagerRecoveryTest, ConcurrentDuplicateBurnMintsExactlyOnce )
{
    /**
     * Duplicate-burn TOCTOU: two concurrent MintFunds calls for the same burn
     * event (relayer redelivery racing an RPC mint) could both pass the
     * reserved/consumed/marker checks before either reserved, and ReserveUTXOs
     * was silent when the same id already held the reservation — so both mints
     * were created and both applied effects, minting one verified burn twice.
     * The atomic TryReserveOutpoint claim must admit exactly one caller
     * regardless of interleaving.
     */
    ASSERT_EQ( manager_->GetState(), sgns::TransactionManager::State::READY );

    const std::string burn_hash = std::string( 64, '9' );
    std::atomic<int>  successes{ 0 };
    std::atomic<bool> go{ false };

    auto worker = [&]
    {
        while ( !go.load( std::memory_order_acquire ) )
        {
            std::this_thread::yield();
        }
        auto mint = manager_->MintFunds( 1000, burn_hash, "public", kTokenId, "" );
        if ( mint.has_value() )
        {
            successes.fetch_add( 1 );
        }
    };

    std::thread first( worker );
    std::thread second( worker );
    go.store( true, std::memory_order_release );
    first.join();
    second.join();

    EXPECT_EQ( successes.load(), 1 );
}

TEST_F( TransactionManagerRecoveryTest, NonMintConfirmAppliesEffectsBeforeConfirmRecord )
{
    /**
     * The non-mint CONFIRMED path wrote the CONFIRMED tracking record BEFORE
     * ParseTransaction, so a parse failure stranded CONFIRMED-without-effects
     * and redelivery short-circuited on the existing entry — permanently
     * unrecoverable. Effects must apply first; a failed parse stays retryable
     * (VERIFYING, effects_applied=false) and a retry completes the confirm.
     */
    auto transaction = MakeTransaction();
    ASSERT_TRUE( transaction );

    // First confirm attempt fails inside ParseTransaction (output store fails).
    sgns::TransactionManagerPendingLifecycleTestAccess::SetFailNextPutUTXOStore(
        account_->GetUTXOManager(), true );
    auto first = sgns::TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState(
        *manager_, transaction, sgns::TransactionManager::TransactionStatus::CONFIRMED );
    ASSERT_TRUE( first.has_error() );

    auto tracked = sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx( *manager_,
                                                                                     transaction->GetHash() );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_EQ( tracked->status, sgns::TransactionManager::TransactionStatus::VERIFYING );
    EXPECT_FALSE( tracked->effects_applied );

    // Retry with the store healthy: effects apply, then the CONFIRMED record.
    auto second = sgns::TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState(
        *manager_, transaction, sgns::TransactionManager::TransactionStatus::CONFIRMED );
    ASSERT_TRUE( second.has_value() );

    tracked = sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx( *manager_, transaction->GetHash() );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_EQ( tracked->status, sgns::TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_TRUE( tracked->effects_applied );
}

namespace
{
    /// Validator whose witness verdict is controllable, registered for a
    /// dedicated chain id so the pending mapping can be exercised without
    /// building full witness proofs.
    class ControllableWitnessValidator final : public sgns::IInputValidator
    {
    public:
        sgns::IInputValidator::WitnessVerdict verdict_ = sgns::IInputValidator::WitnessVerdict::kValid;

        bool ValidateUTXOParameters( const sgns::UTXOTxParameters &,
                                     const std::string &,
                                     const sgns::UTXOManager & ) const override
        {
            return true;
        }

        sgns::IInputValidator::WitnessVerdict ValidateWitness(
            const sgns::ConsensusSubject &,
            const sgns::GeniusTransaction &,
            const sgns::UTXOTxParameters &,
            const sgns::Blockchain & ) const override
        {
            return verdict_;
        }

        bool RequiresConsensusUTXOData() const override
        {
            return false;
        }
    };

    /// Validator whose parameter verdict is controllable, registered for a
    /// dedicated chain id so the type-rules leg can be reached deterministically:
    /// the witness leg always approves (kValid, no consensus UTXO data required)
    /// and ONLY the UTXO-parameter verdict can fail the subject.
    class ControllableParametersValidator final : public sgns::IInputValidator
    {
    public:
        bool parameters_valid_ = true;

        bool ValidateUTXOParameters( const sgns::UTXOTxParameters &,
                                     const std::string &,
                                     const sgns::UTXOManager & ) const override
        {
            return parameters_valid_;
        }

        sgns::IInputValidator::WitnessVerdict ValidateWitness(
            const sgns::ConsensusSubject &,
            const sgns::GeniusTransaction &,
            const sgns::UTXOTxParameters &,
            const sgns::Blockchain & ) const override
        {
            return sgns::IInputValidator::WitnessVerdict::kValid;
        }

        bool RequiresConsensusUTXOData() const override
        {
            return false;
        }
    };

    /// Swaps a probe validator in for a production chain id and restores the
    /// original registration on scope exit (the registry is insert-only, and
    /// the production genius validator registers itself at static init).
    class ScopedValidatorSwap
    {
    public:
        ScopedValidatorSwap( const char *chain_id, sgns::IInputValidator *probe ) :
            chain_id_( chain_id ), probe_( probe )
        {
            original_ = sgns::IInputValidator::Get( chain_id_ );
            if ( original_ != nullptr )
            {
                sgns::IInputValidator::UnregisterIf( chain_id_, original_ );
            }
            registered_ = sgns::IInputValidator::Register( chain_id_, probe_ );
            EXPECT_TRUE( registered_ ) << "probe validator registration failed for " << chain_id_;
        }

        ~ScopedValidatorSwap()
        {
            if ( registered_ )
            {
                sgns::IInputValidator::UnregisterIf( chain_id_, probe_ );
            }
            if ( original_ != nullptr )
            {
                EXPECT_TRUE( sgns::IInputValidator::Register( chain_id_, original_ ) );
            }
        }

        ScopedValidatorSwap( const ScopedValidatorSwap & )            = delete;
        ScopedValidatorSwap &operator=( const ScopedValidatorSwap & ) = delete;

    private:
        const char                    *chain_id_   = nullptr;
        sgns::IInputValidator         *probe_      = nullptr;
        sgns::IInputValidator::ValidatorPtr original_ = nullptr;
        bool                           registered_ = false;
    };
} // namespace

TEST_F( TransactionManagerRecoveryTest, UnsyncedProducerWitnessIsPendingNotInvalid )
{
    /**
     * Witness validation conflated "producer not synced yet" with "invalid":
     * cross-delta arrival order is unordered, so certificate-first delivery
     * turned a transient gap into a hard validation failure. kNotSynced must
     * map to PENDING (retryable), with INVALID still rejected.
     */
    static ControllableWitnessValidator validator;
    validator.verdict_ = sgns::IInputValidator::WitnessVerdict::kNotSynced;
    ASSERT_TRUE( sgns::IInputValidator::Register( "witness-pending-chain", &validator ) );

    // The tx must carry UTXO parameters to reach the validator and route to
    // the controllable validator's chain: a MintV2 with a burn input whose
    // producer record is absent locally — exactly the unsynced shape.
    const auto producer_hash = sgns::base::Hash256::fromReadableString( std::string( 64, '7' ) ).value();
    auto transaction = std::make_shared<sgns::MintTransactionV2>( sgns::MintTransactionV2::New(
        1,
        "witness-pending-chain",
        kTokenId,
        MakeDAG( account_->ReserveNextNonce() ),
        { { producer_hash, 0, {} } },
        account_->GetAddress() ) );
    transaction->MakeSignature( *account_ );
    ASSERT_TRUE( transaction );

    auto commitment = manager_->BuildUTXOTransitionCommitment( *transaction );
    ASSERT_TRUE( commitment.has_value() );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( account_->GetAddress(),
                                                               transaction->GetNonce(),
                                                               transaction->GetHash(),
                                                               transaction->SerializeToEmbeddedTransaction(),
                                                               commitment,
                                                               std::nullopt );
    ASSERT_TRUE( subject.has_value() );

    {
        const auto result = manager_->ValidateWitnessForConsensus( subject.value(), *transaction );
        EXPECT_EQ( result, sgns::TransactionManager::WitnessValidationResult::PENDING );
    }
    {
        validator.verdict_ = sgns::IInputValidator::WitnessVerdict::kInvalid;
        const auto result = manager_->ValidateWitnessForConsensus( subject.value(), *transaction );
        EXPECT_EQ( result, sgns::TransactionManager::WitnessValidationResult::INVALID );
    }

    sgns::IInputValidator::UnregisterIf( "witness-pending-chain", &validator );
}

TEST_F( TransactionManagerPreviousHashTest, UnknownInputTypeRulesPendingNotRejected )
{
    /**
     * Cross-delta arrival order is unordered: an escrow payout proposal can
     * arrive while the certificate of the escrow hold it spends has not been
     * applied locally, so its input outpoints are absent from the in-memory
     * UTXO index (the producer record may even have synced — only the local
     * UTXO application lags). The type-rules leg treated that "not synced yet"
     * state as invalid and hard-rejected (live staging incident 2026-11-19:
     * payout rejected with "Type rules failed", then certified by the network
     * anyway and accepted on certificate arrival 69 s later). It must return
     * Pending instead — the same retry policy the witness leg already applies
     * to kNotSynced — so consensus retries once the hold's certificate lands.
     *
     * The probe validator approves the witness leg (so the flow reaches the
     * type-rules leg) and fails the UTXO-parameter leg (so with the gate in
     * place, ONLY the missing-input gate can produce Pending — a Reject here
     * would mean the gate is absent, a parameter-false Approve would mean the
     * gate short-circuits wrongly).
     */
    static ControllableParametersValidator validator;
    validator.parameters_valid_ = false;
    ASSERT_TRUE( sgns::IInputValidator::Register( "type-rules-pending-chain", &validator ) );

    // Replay protection (checked before type rules) needs a resolvable,
    // certified predecessor for the reserved nonce, so chain onto a stored one.
    auto previous_transaction = MakeTransaction( 0 );
    StoreCertificate( previous_transaction );
    StoreTransaction( previous_transaction );

    // A spend whose input outpoint is unknown to the local UTXO index —
    // exactly the early-payout shape.
    const auto unknown_producer = sgns::base::Hash256::fromReadableString( std::string( 64, '9' ) ).value();
    auto transaction = std::make_shared<sgns::MintTransactionV2>( sgns::MintTransactionV2::New(
        1,
        "type-rules-pending-chain",
        kTokenId,
        MakeDAG( account_->ReserveNextNonce(), previous_transaction->GetHash() ),
        { { unknown_producer, 0, {} } },
        account_->GetAddress() ) );
    transaction->MakeSignature( *account_ );
    ASSERT_TRUE( transaction );

    auto commitment = manager_->BuildUTXOTransitionCommitment( *transaction );
    ASSERT_TRUE( commitment.has_value() );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( account_->GetAddress(),
                                                               transaction->GetNonce(),
                                                               transaction->GetHash(),
                                                               transaction->SerializeToEmbeddedTransaction(),
                                                               commitment,
                                                               std::nullopt );
    ASSERT_TRUE( subject.has_value() );

    const auto result = sgns::TransactionManagerPendingLifecycleTestAccess::HandleNonceConsensusSubject(
        *manager_,
        subject.value() );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value().check, sgns::ConsensusManager::Check::Pending );

    // The subject must stay retryable, not be marked locally FAILED.
    const auto tracked = sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx( *manager_,
                                                                                           transaction->GetHash() );
    ASSERT_TRUE( tracked.has_value() );
    EXPECT_NE( tracked->status, sgns::TransactionManager::TransactionStatus::FAILED );

    sgns::IInputValidator::UnregisterIf( "type-rules-pending-chain", &validator );
}

TEST_F( TransactionDeletionRecoveryTest, ConflictingSpendOfConsumedInputIsRejected )
{
    using Status = sgns::TransactionManager::TransactionStatus;
    auto         apply = [this]( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
    {
        return sgns::TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState( *manager_,
                                                                                           transaction,
                                                                                           Status::CONFIRMED );
    };

    auto mint = MakeTransaction( 0 );
    ASSERT_FALSE( apply( mint ).has_error() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance(), 1U );

    const auto mint_outpoint = sgns::base::Hash256::fromReadableString( mint->GetHash() );
    ASSERT_TRUE( mint_outpoint.has_value() );

    const std::string dest_a = "0x" + std::string( 64, 'a' );
    const std::string dest_b = "0x" + std::string( 64, 'b' );

    auto make_transfer = [&]( const std::string &destination )
    {
        sgns::InputUTXOInfo input;
        input.txid_hash_  = mint_outpoint.value();
        input.output_idx_ = 0;
        input.signature_  = account_->Sign( input.SerializeForSigning() );
        auto dag          = MakeDAG( account_->ReserveNextNonce(), mint->GetHash() );
        auto transfer     = std::make_shared<sgns::TransferTransaction>(
            sgns::TransferTransaction::New( std::vector{ std::move( input ) },
                                            { sgns::OutputDestInfo{ 1, destination, kTokenId } },
                                            std::move( dag ) ) );
        transfer->MakeSignature( *account_ );
        return transfer;
    };

    auto first = make_transfer( dest_a );
    ASSERT_FALSE( apply( first ).has_error() );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( dest_a ), 1U );

    // A conflicting transfer of the same input must be rejected without crediting its destination
    auto second = make_transfer( dest_b );
    EXPECT_TRUE( apply( second ).has_error() );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( dest_b ), 0U );

    // Startup-style replay of the applied transfer finds its outputs already present and stays
    // idempotent instead of being mistaken for a double spend
    RecreateManager();
    ASSERT_FALSE( apply( first ).has_error() );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( dest_a ), 1U );
}

TEST_F( TransactionDeletionRecoveryTest, ConflictingEscrowPayoutsAreArbitratedAtVoteTime )
{
    /**
     * Staging incident 2026-11-19 (escrow hold aaa7ca2d): two processors both
     * built a payout spending the same escrow outpoint. The slot key of a
     * transaction subject is src:nonce, so the two payouts sat in DIFFERENT
     * slots and slot arbitration never compared them; the vote-time conflict
     * gate only looked at already-CONFIRMED transactions, and neither payout
     * had been applied when votes were cast — so BOTH reached quorum and both
     * certificates were written. The loser's certificate is undeliverable
     * forever ("Rejected double-spending transfer") and poisoned every
     * subsequent certification on the network.
     *
     * Vote time must arbitrate conflicting in-flight spends deterministically
     * (Blockchain::BestHash, the same tie-break slot arbitration uses): every
     * validator that sees both proposals votes Approve only for the
     * lower-hash one, so only one certificate can ever form.
     */
    using Status = sgns::TransactionManager::TransactionStatus;
    auto apply   = [this]( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
    {
        return sgns::TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState( *manager_,
                                                                                           transaction,
                                                                                           Status::CONFIRMED );
    };

    // The production supergenius_chain validator requires chain-synced UTXO
    // witness data this in-process fixture does not carry. The probe approves
    // the witness and parameter legs so the test isolates the outpoint
    // arbitration gate (the same pattern UnknownInputTypeRulesPendingNotRejected
    // uses for its probe).
    static ControllableParametersValidator validator;
    validator.parameters_valid_ = true;
    ScopedValidatorSwap swap( "supergenius_chain", &validator );

    auto mint = MakeTransaction( 0 );
    ASSERT_FALSE( apply( mint ).has_error() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance(), 1U );

    // Escrow hold locks the mint output under a lock address.
    const std::string escrow_lock = "0x" + std::string( 64, '1' );
    auto              escrow_params = account_->GetUTXOManager().CreateTxParameter( 1, escrow_lock, kTokenId );
    ASSERT_TRUE( escrow_params.has_value() );
    auto escrow_dag = MakeDAG( account_->ReserveNextNonce(), mint->GetHash() );
    escrow_dag.set_uncle_hash( escrow_lock );
    auto escrow = std::make_shared<sgns::EscrowTransaction>(
        sgns::EscrowTransaction::New( std::move( escrow_params.value() ), 1, std::move( escrow_dag ) ) );
    escrow->MakeSignature( *account_ );
    StoreCertificate( escrow );
    StoreTransaction( escrow );
    ASSERT_FALSE( apply( escrow ).has_error() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance( escrow_lock ), 1U );

    const auto escrow_outpoint = sgns::base::Hash256::fromReadableString( escrow->GetHash() );
    ASSERT_TRUE( escrow_outpoint.has_value() );

    const std::string dest_a = "0x" + std::string( 64, 'a' );
    const std::string dest_b = "0x" + std::string( 64, 'b' );
    // Two different processors fire payouts for the same escrow outpoint, each
    // on its own nonce chain (slot keys src:nonce never collide) — the exact
    // staging race.
    auto processor_b = sgns::GeniusAccount::New( kTokenId, base_path / "processor_b" );
    ASSERT_TRUE( processor_b );
    auto payout_a = MakePayout( *account_,
                                escrow_outpoint.value(),
                                0,
                                account_->ReserveNextNonce(),
                                escrow->GetHash(),
                                dest_a );
    auto payout_b = MakePayout( *processor_b, escrow_outpoint.value(), 0, 0, "", dest_b );

    auto drive = []( sgns::TransactionManager                        &manager,
                     const std::shared_ptr<sgns::TransferTransaction> &payout )
    {
        auto commitment = manager.BuildUTXOTransitionCommitment( *payout );
        EXPECT_TRUE( commitment.has_value() );
        auto subject = sgns::ConsensusManager::CreateNonceSubject( payout->GetSrcAddress(),
                                                                   payout->GetNonce(),
                                                                   payout->GetHash(),
                                                                   payout->SerializeToEmbeddedTransaction(),
                                                                   commitment,
                                                                   std::nullopt );
        EXPECT_TRUE( subject.has_value() );
        return sgns::TransactionManagerPendingLifecycleTestAccess::HandleNonceConsensusSubject(
            manager,
            subject.value() );
    };

    auto result_a = drive( *manager_, payout_a );
    ASSERT_TRUE( result_a.has_value() );
    // First arrival sees no conflict yet: its Approve is provisional and the
    // decision is the final tracked state once both payouts went through the
    // gate (arrival order must not decide the outcome — BestHash does).
    EXPECT_EQ( result_a.value().check, sgns::ConsensusManager::Check::Approve );
    auto result_b = drive( *manager_, payout_b );
    ASSERT_TRUE( result_b.has_value() );

    const auto winner = sgns::Blockchain::BestHash( payout_a->GetHash(), payout_b->GetHash() );
    EXPECT_EQ( result_b.value().check,
               winner == payout_b->GetHash() ? sgns::ConsensusManager::Check::Approve :
                                               sgns::ConsensusManager::Check::Reject )
        << "the second arrival must be approved exactly when BestHash ranks it first";

    auto tracked_a = sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx( *manager_,
                                                                                       payout_a->GetHash() );
    auto tracked_b = sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx( *manager_,
                                                                                       payout_b->GetHash() );
    ASSERT_TRUE( tracked_a.has_value() );
    ASSERT_TRUE( tracked_b.has_value() );
    // Exactly one payout survives, and it is the deterministic winner: the
    // loser must never stay votable, otherwise both certificates can form.
    EXPECT_EQ( winner == payout_a->GetHash() ? tracked_a->status : tracked_b->status, Status::VERIFYING )
        << "BestHash winner payout must remain votable";
    EXPECT_EQ( winner == payout_a->GetHash() ? tracked_b->status : tracked_a->status, Status::FAILED )
        << "conflicting payout must fail at vote time instead of reaching quorum too";
}

TEST_F( TransactionDeletionRecoveryTest, PayoutSlotKeyIsTheSpentOutpointWhoeverSigns )
{
    /**
     * F13 (stress2-20261120 B1): the payout of an escrow is a plain
     * TransferTransaction, so its slot key was src:nonce of whoever signed it.
     * Two processors' payouts of one escrow outpoint therefore sat in
     * DIFFERENT slots, slot arbitration never compared them, and different
     * voter subsets certified different payouts (gate job 1, 2026-11-20
     * 07:13 UTC — chain split). MintTransactionV2 already keys its slot on
     * content including its first input txid; a spend must exclude its
     * competitors through the slot ID the same way: the slot of a transfer
     * that spends UTXO outputs is the first spent outpoint (txid:index),
     * whoever signs it, on whatever nonce chain.
     */
    auto outpoint =
        sgns::base::Hash256::fromReadableString( "5f4d0d8b1e2c3a4b5f4d0d8b1e2c3a4b5f4d0d8b1e2c3a4b5f4d0d8b1e2c3a4b" );
    ASSERT_TRUE( outpoint.has_value() );
    auto other_outpoint =
        sgns::base::Hash256::fromReadableString( "6e5d0d8b1e2c3a4b6e5d0d8b1e2c3a4b6e5d0d8b1e2c3a4b6e5d0d8b1e2c3a4b" );
    ASSERT_TRUE( other_outpoint.has_value() );

    const std::string dest_a = "0x" + std::string( 64, 'a' );
    const std::string dest_b = "0x" + std::string( 64, 'b' );
    // Two different processors fire payouts for the same escrow outpoint, each
    // on its own nonce chain — the exact gate-job-1 race.
    auto processor_b = sgns::GeniusAccount::New( kTokenId, base_path / "slot_processor_b" );
    ASSERT_TRUE( processor_b );
    auto payout_a = MakePayout( *account_, outpoint.value(), 0, account_->ReserveNextNonce(), "prev-a", dest_a );
    auto payout_b = MakePayout( *processor_b, outpoint.value(), 0, 0, "", dest_b );

    ASSERT_NE( payout_a->GetSrcAddress(), payout_b->GetSrcAddress() );
    EXPECT_EQ( payout_a->GetSlotID(), payout_b->GetSlotID() )
        << "payouts spending the same outpoint must share one arbitration slot whoever signs them";
    EXPECT_NE( payout_a->GetSlotID().find( outpoint.value().toReadableString() ), std::string::npos )
        << "the shared slot key must be derived from the spent outpoint";
    EXPECT_NE( payout_a->GetSlotID().find( ":0" ), std::string::npos )
        << "the shared slot key must include the spent output index";

    // A different outpoint is a different competition: its slot must not merge.
    auto payout_c = MakePayout( *account_, other_outpoint.value(), 0, account_->ReserveNextNonce(), "prev-c", dest_b );
    EXPECT_NE( payout_a->GetSlotID(), payout_c->GetSlotID() )
        << "spends of different outpoints must not be forced into one slot";

    // Nonce competition within one account keeps its legacy slot when nothing
    // is spent (a tx without UTXO inputs falls back to src:nonce).
    auto dag   = MakeDAG( account_->ReserveNextNonce(), "prev-d" );
    auto plain = std::make_shared<sgns::TransferTransaction>(
        sgns::TransferTransaction::New( {}, { sgns::OutputDestInfo{ 1, dest_a, kTokenId } }, std::move( dag ) ) );
    plain->MakeSignature( *account_ );
    EXPECT_EQ( plain->GetSlotID(), plain->GetSrcAddress() + ":" + std::to_string( plain->GetNonce() ) )
        << "an inputless transfer keeps the legacy src:nonce slot key";
}

TEST_F( TransactionDeletionRecoveryTest, ChainAcrossUpgradeResolvesLegacySlotCertificate )
{
    /**
     * F18 (stress2-20261120 B1 post-F13): a certificate is stored at the slot
     * key of the binary era that sealed it. Pre-upgrade nodes keyed a spend's
     * certificate at src:nonce; F13 keys it on the spent outpoint. A follower
     * that chains onto a pre-upgrade spend must still resolve the
     * predecessor's certificate — both derivations are bound to the
     * certificate's own subject, so the lookup accepts either key. FN
     * privnet 2026-11-20 23:44 UTC: every escrow payout pended forever with
     * "Missing previous certificate" (hashes 7141fe9c/6cd577d1) and both
     * processors retried the same payout bytes past their 30 s timeouts.
     */
    using Status = sgns::TransactionManager::TransactionStatus;
    auto apply   = [this]( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
    {
        return sgns::TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState( *manager_,
                                                                                           transaction,
                                                                                           Status::CONFIRMED );
    };

    static ControllableParametersValidator validator;
    validator.parameters_valid_ = true;
    ScopedValidatorSwap swap( "supergenius_chain", &validator );

    auto mint_a = MakeTransaction( 0 );
    ASSERT_FALSE( apply( mint_a ).has_error() );
    auto mint_b = std::make_shared<sgns::MintTransaction>(
        sgns::MintTransaction::New( 1,
                                    std::string( sgns::GeniusTransaction::GENIUS_CHAIN_ID ),
                                    kTokenId,
                                    MakeDAG( 1, mint_a->GetHash() ) ) );
    mint_b->MakeSignature( *account_ );
    ASSERT_FALSE( apply( mint_b ).has_error() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance(), 2U );

    auto make_escrow = [this, &apply]( const std::string &lock, const std::string &previous_hash )
    {
        auto params = account_->GetUTXOManager().CreateTxParameter( 1, lock, kTokenId );
        EXPECT_TRUE( params.has_value() );
        auto dag = MakeDAG( account_->ReserveNextNonce(), previous_hash );
        dag.set_uncle_hash( lock );
        auto escrow = std::make_shared<sgns::EscrowTransaction>(
            sgns::EscrowTransaction::New( std::move( params.value() ), 1, std::move( dag ) ) );
        escrow->MakeSignature( *account_ );
        StoreCertificate( escrow );
        StoreTransaction( escrow );
        EXPECT_FALSE( apply( escrow ).has_error() );
        return escrow;
    };

    const std::string lock_a = "0x" + std::string( 64, '1' );
    const std::string lock_b = "0x" + std::string( 64, '2' );
    auto escrow_a = make_escrow( lock_a, mint_b->GetHash() );
    auto escrow_b = make_escrow( lock_b, escrow_a->GetHash() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance( lock_a ), 1U );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance( lock_b ), 1U );

    const auto escrow_a_outpoint = sgns::base::Hash256::fromReadableString( escrow_a->GetHash() );
    ASSERT_TRUE( escrow_a_outpoint.has_value() );
    const auto escrow_b_outpoint = sgns::base::Hash256::fromReadableString( escrow_b->GetHash() );
    ASSERT_TRUE( escrow_b_outpoint.has_value() );

    const std::string dest_a = "0x" + std::string( 64, 'a' );
    const std::string dest_b = "0x" + std::string( 64, 'b' );

    // The pre-upgrade payout: input-bearing, certified and stored under the
    // legacy src:nonce slot key — exactly what a pre-F13 binary wrote for it.
    auto payout = MakePayout( *account_, escrow_a_outpoint.value(), 0, account_->ReserveNextNonce(),
                              escrow_b->GetHash(), dest_a );
    const std::string legacy_slot = payout->GetSrcAddress() + ":" + std::to_string( payout->GetNonce() );
    ASSERT_NE( legacy_slot, payout->GetSlotID() )
        << "an input-bearing payout must carry the outpoint slot key under F13";

    const auto certificate = MakeCertificate( *account_, payout );
    sgns::crdt::GlobalDB::Buffer certificate_data;
    certificate_data.put( certificate.SerializeAsString() );
    ASSERT_TRUE( db_->Put( sgns::crdt::HierarchicalKey( "/cert/" + legacy_slot ),
                           certificate_data,
                           { "CRDT.Datastore.TEST.Channel" } )
                     .has_value() );
    ASSERT_TRUE( blockchain_->CheckCertificateForSlot( legacy_slot ) )
        << "a certificate stored at its sealing-era slot key must stay readable after the upgrade";

    StoreTransaction( payout );

    // The payout itself validates fine: its own predecessor (the escrow hold)
    // was certified at a slot key both eras agree on.
    auto payout_commitment = manager_->BuildUTXOTransitionCommitment( *payout );
    ASSERT_TRUE( payout_commitment.has_value() );
    auto payout_subject = sgns::ConsensusManager::CreateNonceSubject( payout->GetSrcAddress(),
                                                                      payout->GetNonce(),
                                                                      payout->GetHash(),
                                                                      payout->SerializeToEmbeddedTransaction(),
                                                                      payout_commitment,
                                                                      std::nullopt );
    ASSERT_TRUE( payout_subject.has_value() );
    const auto payout_result = sgns::TransactionManagerPendingLifecycleTestAccess::HandleNonceConsensusSubject(
        *manager_,
        payout_subject.value() );
    ASSERT_TRUE( payout_result.has_value() );
    ASSERT_EQ( payout_result.value().check, sgns::ConsensusManager::Check::Approve );
    const auto tracked_payout = sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx(
        *manager_,
        payout->GetHash() );
    ASSERT_TRUE( tracked_payout.has_value() )
        << "the intermediate payout must be tracked so the follower's nonce window accepts it";

    // The follower chains onto the payout on the account's nonce chain but
    // spends a fresh outpoint (the in-flight payout already owns the first
    // one). Its replay protection must find the predecessor's certificate at
    // the legacy slot, because the F13 outpoint slot holds nothing.
    auto follower = MakePayout( *account_, escrow_b_outpoint.value(), 0, account_->ReserveNextNonce(),
                                payout->GetHash(), dest_b );

    auto commitment = manager_->BuildUTXOTransitionCommitment( *follower );
    ASSERT_TRUE( commitment.has_value() );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( follower->GetSrcAddress(),
                                                               follower->GetNonce(),
                                                               follower->GetHash(),
                                                               follower->SerializeToEmbeddedTransaction(),
                                                               commitment,
                                                               std::nullopt );
    ASSERT_TRUE( subject.has_value() );
    const auto result = sgns::TransactionManagerPendingLifecycleTestAccess::HandleNonceConsensusSubject(
        *manager_,
        subject.value() );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value().check, sgns::ConsensusManager::Check::Approve )
        << "a chain whose predecessor was certified pre-upgrade must still validate";
}

TEST_F( TransactionDeletionRecoveryTest, LoserPayoutCertificateIsQuarantinedNotRetriedForever )
{
    /**
     * Backstop for certificates formed before the vote-time gate (exactly the
     * staging 2026-11-19 state: both payouts certified, winner applied, loser's
     * certificate permanently undeliverable). The handler must classify the
     * permanent apply failure (double-spend invalid_argument) as a final
     * Reject — which the certificate work journal retires (MarkDone) and
     * uses to wake dependent pending proposals — instead of returning an
     * error, which ProcessCommittedCertificate retries with exponential
     * backoff forever ("handler failed N times, backing off 60000ms").
     */
    using Status = sgns::TransactionManager::TransactionStatus;
    auto apply   = [this]( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
    {
        return sgns::TransactionManagerPendingLifecycleTestAccess::ChangeTransactionState( *manager_,
                                                                                           transaction,
                                                                                           Status::CONFIRMED );
    };

    // Same probe as ConflictingEscrowPayoutsAreArbitratedAtVoteTime: the
    // certificate-delivery path under test runs after quorum, where the
    // witness leg no longer applies.
    static ControllableParametersValidator validator;
    validator.parameters_valid_ = true;
    ScopedValidatorSwap swap( "supergenius_chain", &validator );

    auto mint = MakeTransaction( 0 );
    ASSERT_FALSE( apply( mint ).has_error() );

    const std::string escrow_lock   = "0x" + std::string( 64, '1' );
    auto              escrow_params = account_->GetUTXOManager().CreateTxParameter( 1, escrow_lock, kTokenId );
    ASSERT_TRUE( escrow_params.has_value() );
    auto escrow_dag = MakeDAG( account_->ReserveNextNonce(), mint->GetHash() );
    escrow_dag.set_uncle_hash( escrow_lock );
    auto escrow = std::make_shared<sgns::EscrowTransaction>(
        sgns::EscrowTransaction::New( std::move( escrow_params.value() ), 1, std::move( escrow_dag ) ) );
    escrow->MakeSignature( *account_ );
    StoreCertificate( escrow );
    StoreTransaction( escrow );
    ASSERT_FALSE( apply( escrow ).has_error() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance( escrow_lock ), 1U );

    const auto escrow_outpoint = sgns::base::Hash256::fromReadableString( escrow->GetHash() );
    ASSERT_TRUE( escrow_outpoint.has_value() );

    const std::string dest_a = "0x" + std::string( 64, 'a' );
    const std::string dest_b = "0x" + std::string( 64, 'b' );
    const std::string dest_c = "0x" + std::string( 64, 'c' );
    auto processor_b = sgns::GeniusAccount::New( kTokenId, base_path / "processor_b" );
    ASSERT_TRUE( processor_b );
    auto payout_a = MakePayout( *account_,
                                escrow_outpoint.value(),
                                0,
                                account_->ReserveNextNonce(),
                                escrow->GetHash(),
                                dest_a );
    auto payout_b = MakePayout( *processor_b, escrow_outpoint.value(), 0, 0, "", dest_b );

    // The loser's proposal was votable while neither payout had been applied —
    // the pre-halt staging behavior that let both certificates form.
    auto commitment_b = manager_->BuildUTXOTransitionCommitment( *payout_b );
    ASSERT_TRUE( commitment_b.has_value() );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( payout_b->GetSrcAddress(),
                                                               payout_b->GetNonce(),
                                                               payout_b->GetHash(),
                                                               payout_b->SerializeToEmbeddedTransaction(),
                                                               commitment_b,
                                                               std::nullopt );
    ASSERT_TRUE( subject.has_value() );
    auto vote_result = sgns::TransactionManagerPendingLifecycleTestAccess::HandleNonceConsensusSubject(
        *manager_,
        subject.value() );
    ASSERT_TRUE( vote_result.has_value() );
    ASSERT_EQ( vote_result.value().check, sgns::ConsensusManager::Check::Approve );

    // The winner's certificate is delivered and applied first: the escrow
    // outpoint is consumed and only dest_a is credited.
    ASSERT_FALSE( apply( payout_a ).has_error() );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance( dest_a ), 1U );
    EXPECT_FALSE( account_->GetUTXOManager().GetUnconsumedUTXO( escrow_outpoint.value(), 0 ).has_value() );

    // The loser's certificate arrives afterwards. It can never be applied; the
    // handler must report a final Reject, not a retryable error.
    const auto certificate_b = MakeCertificate( *processor_b, payout_b );
    auto cert_result = sgns::TransactionManagerPendingLifecycleTestAccess::OnConsensusCertificate(
        *manager_,
        payout_b->GetHash(),
        certificate_b );
    ASSERT_TRUE( cert_result.has_value() )
        << "a permanently rejected certificate must not be returned as a retryable error";
    EXPECT_EQ( cert_result.value(), sgns::ConsensusManager::Check::Reject );

    const auto tracked_b = sgns::TransactionManagerPendingLifecycleTestAccess::GetTrackedTx(
        *manager_,
        payout_b->GetHash() );
    ASSERT_TRUE( tracked_b.has_value() );
    EXPECT_EQ( tracked_b->status, Status::FAILED );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( dest_b ), 0U );

    // The pipeline must move on: an independent transaction still applies and
    // credits its destination after the poison certificate was quarantined.
    const auto payout_a_outpoint = sgns::base::Hash256::fromReadableString( payout_a->GetHash() );
    ASSERT_TRUE( payout_a_outpoint.has_value() );
    auto followup = MakePayout( *account_,
                                payout_a_outpoint.value(),
                                0,
                                account_->ReserveNextNonce(),
                                payout_a->GetHash(),
                                dest_c );
    ASSERT_FALSE( apply( followup ).has_error() );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance( dest_c ), 1U );
}

TEST_F( TransactionManagerPreviousHashTest, EscrowChainIdDefaultAndScopedOverride )
{
    const std::string private_id = "0xabcd0123abcd0123abcd0123abcd0123abcd0123abcd0123abcd0123abcd0123";

    // ScopedChainId goldens: public is byte-identical to TransactionManager::GENIUS_CHAIN_ID,
    // scoped appends the identity under the genius branch.
    EXPECT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::ScopedChainId( "" ), "supergenius" );
    EXPECT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::ScopedChainId( private_id ),
               "supergenius/" + private_id );

    // Fund the account (mint ceremony) so escrow construction has a spendable UTXO —
    // the same prerequisite TransferAndEscrowDeletionRestoresConsumedInputs uses.
    auto previous_transaction = MakeTransaction( 0 );
    StoreCertificate( previous_transaction );
    StoreTransaction( previous_transaction );
    ProcessStoredTransaction( previous_transaction );
    ASSERT_EQ( account_->GetUTXOManager().GetBalance(), 1U );

    // Escrow construction precedent (TransferAndEscrowDeletionRestoresConsumedInputs).
    const std::string escrow_lock   = "0x" + std::string( 64, '1' );
    auto              escrow_params = account_->GetUTXOManager().CreateTxParameter( 1, escrow_lock, kTokenId );
    ASSERT_TRUE( escrow_params.has_value() );
    auto escrow_dag = MakeDAG( account_->ReserveNextNonce(), previous_transaction->GetHash() );
    escrow_dag.set_uncle_hash( escrow_lock );
    auto escrow = std::make_shared<sgns::EscrowTransaction>(
        sgns::EscrowTransaction::New( std::move( escrow_params.value() ), 1, std::move( escrow_dag ) ) );
    escrow->MakeSignature( *account_ );

    // Default: byte-identical to the GeniusTransaction chain id.
    EXPECT_EQ( escrow->GetChainId(), std::string( sgns::GeniusTransaction::GENIUS_CHAIN_ID ) );
    EXPECT_EQ( escrow->GetChainId(), "supergenius_chain" );

    // Validator routing before any override: the genius validator serves the default chain id.
    EXPECT_TRUE( sgns::TransactionManagerPendingLifecycleTestAccess::SelectsGeniusValidator( *manager_, escrow ) );

    // Scoped override (what a scoped HoldEscrow applies): chain id carries the network scope.
    escrow->SetChainIdOverride(
        sgns::TransactionManagerPendingLifecycleTestAccess::ScopedChainId( private_id ) );
    EXPECT_EQ( escrow->GetChainId(), "supergenius/" + private_id );

    // A scoped genius chain id must still route to the genius input validator, not the
    // public-chain fallback (misrouting would send private escrow through public validation).
    EXPECT_TRUE( sgns::TransactionManagerPendingLifecycleTestAccess::SelectsGeniusValidator( *manager_, escrow ) );

    // Counterfactual: a foreign chain id falls through to the public-chain validator —
    // proving the assertions above discriminate genius routing from public routing.
    escrow->SetChainIdOverride( "0xforeignchain" );
    EXPECT_FALSE( sgns::TransactionManagerPendingLifecycleTestAccess::SelectsGeniusValidator( *manager_, escrow ) );

    // Public-node equivalence: the PUBLIC ScopedChainId value equals the TransactionManager
    // genius chain id, so an empty-scope HoldEscrow keeps routing unchanged.
    EXPECT_EQ( sgns::TransactionManagerPendingLifecycleTestAccess::ScopedChainId( "" ), "supergenius" );
}

// GATE-05 live incident (2026-10-20/22): a READY UTXO whose creating
// transaction never existed (a fabricated burn reference leaked before the
// 18-05 fix) used to wedge the TransactionManager in INITIALIZING forever —
// InitTransactions unconditionally returned below a TODO comment, never
// requesting the missing tx and never transitioning. Bounded recovery must
// reach READY and drop the unresolvable hash so neither boot nor the prune
// tick (HasMissingTxHashes gate) stays wedged.
TEST_F( TransactionManagerRecoveryTest, MissingCreatingTxDoesNotWedgeBoot )
{
    sgns::TransactionManagerPendingLifecycleTestAccess::SetMissingTxMaxAttempts( 0 );

    const auto fake_hash_result = sgns::base::Hash256::fromReadableString( std::string( 64, 'a' ) );
    ASSERT_TRUE( fake_hash_result.has_value() );
    const auto fake_tx_hash = fake_hash_result.value();

    ASSERT_TRUE( account_->GetUTXOManager()
                     .PutUTXO( sgns::GeniusUTXO( fake_tx_hash, 0, 30000, kTokenId, account_->GetAddress() ) )
                     .has_value() );

    sgns::TransactionManagerPendingLifecycleTestAccess::ChangeState( *manager_,
                                                                     sgns::TransactionManager::State::INITIALIZING );
    sgns::TransactionManagerPendingLifecycleTestAccess::RunInitializeUTXOs( *manager_ );
    EXPECT_TRUE( manager_->HasMissingTxHashes() );

    // This fixture has no messenger, so CheckNonce cannot pass here — the
    // READY transition is the fixture's own SetUp idiom (direct ChangeState).
    // The GATE-05 property under test is the wedge fix itself: the recovery
    // path below the former dead return now runs and DROPS the unresolvable
    // hash, so neither boot nor the HasMissingTxHashes-gated prune tick can
    // stay wedged. (Live READY acceptance: the operator's previously-wedged
    // staging node boots with this fix — 18-HUMAN-UAT item 1.)
    sgns::TransactionManagerPendingLifecycleTestAccess::RunInitTransactions( *manager_ );

    EXPECT_FALSE( manager_->HasMissingTxHashes() );

    sgns::TransactionManagerPendingLifecycleTestAccess::SetMissingTxMaxAttempts( 12 );
}

namespace
{
    /// Restores the process-wide SYNCING-exit window when a test scope ends,
    /// including fatal-assertion paths (the knob is static, like the
    /// missing-tx attempt cap).
    class SyncingExitWindowGuard
    {
    public:
        explicit SyncingExitWindowGuard( std::chrono::milliseconds timeout )
        {
            sgns::TransactionManagerPendingLifecycleTestAccess::SetSyncingExitTimeout( timeout );
        }

        ~SyncingExitWindowGuard()
        {
            sgns::TransactionManagerPendingLifecycleTestAccess::RestoreSyncingExitTimeout();
        }
    };
} // namespace

// D-02 half 1 (folded todo failed-send-wedges-tm-in-syncing): a send that
// fails BEFORE broadcast — here an oversized payload tripping the preflight
// size gate before any CRDT Put/Commit — must leave the manager READY, never
// SYNCING, with the tx FAILED and its reserved nonce released, so the next
// send reuses the nonce cleanly. (The witness gate needs a consensus mock
// this fixture does not carry; the size gate is the deterministic driver.)
TEST_F( TransactionManagerRecoveryTest, PreBroadcastFailureKeepsManagerReadyWithNonceRelease )
{
    const auto reserved_nonce = account_->ReserveNextNonce();
    ASSERT_EQ( reserved_nonce, 1U );

    auto oversized_dag = MakeDAG( reserved_nonce );
    oversized_dag.set_uncle_hash( std::string( 70 * 1024, 'x' ) );
    auto oversized = std::make_shared<sgns::MintTransaction>(
        sgns::MintTransaction::New( 1,
                                    std::string( sgns::GeniusTransaction::GENIUS_CHAIN_ID ),
                                    kTokenId,
                                    std::move( oversized_dag ) ) );
    oversized->MakeSignature( *account_ );
    ASSERT_TRUE( oversized->SerializeToEmbeddedTransaction().ByteSizeLong() > 64 * 1024 );

    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_, oversized, db_->BeginTransaction() );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );

    EXPECT_EQ( manager_->GetTransactionStatusByTxId( oversized->GetHash() ),
               sgns::TransactionManager::TransactionStatus::FAILED );
    // The incident's wedge: this tick used to land in SYNCING (one tick is the
    // discriminator — SYNCING would only resolve on a LATER sync pass).
    EXPECT_EQ( manager_->GetState(), sgns::TransactionManager::State::READY );

    // The FAILED transition released the reserved nonce: the next reservation
    // hands out the same nonce again.
    EXPECT_EQ( account_->ReserveNextNonce(), reserved_nonce );

    // A subsequent valid send reuses the nonce and proceeds normally.
    auto retry = MakeTransaction( reserved_nonce );
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_, retry, db_->BeginTransaction() );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
    EXPECT_EQ( manager_->GetTransactionStatusByTxId( retry->GetHash() ),
               sgns::TransactionManager::TransactionStatus::SENDING );
    EXPECT_EQ( manager_->GetState(), sgns::TransactionManager::State::READY );
}

// D-02 half 1 negative control: a failure that is NOT a typed pre-broadcast
// error (a CRDT Put failure on an already-committed transaction — send
// territory where something may have been staged) still takes the SYNCING
// recovery path exactly as before.
TEST_F( TransactionManagerRecoveryTest, PreBroadcastNegativeControlNonTypedFailureStillEntersSyncing )
{
    auto transaction = MakeTransaction();

    auto committed_transaction = MakeCommittedTransaction();
    ASSERT_TRUE( committed_transaction );
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_,
                                                                 transaction,
                                                                 std::move( committed_transaction ) );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );

    EXPECT_EQ( manager_->GetState(), sgns::TransactionManager::State::SYNCING );
    EXPECT_EQ( manager_->GetTransactionStatusByTxId( transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::FAILED );
}

// D-02 half 2: a SYNCING state with no confirming nonce advance within the
// (ForTest-shrunk) window falls back to the last DURABLE confirmed nonce,
// WARNs (window + anchor + READY return are in the log line), and returns to
// READY. Held reservations survive the fallback untouched — the next reserved
// nonce is strictly greater (the collision proof: releasing or resetting
// pending nonces here would hand a held nonce to the next sender).
TEST_F( TransactionManagerRecoveryTest, SyncingExitFallsBackToDurableNonceAfterWindow )
{
    SyncingExitWindowGuard window_guard( std::chrono::milliseconds( 100 ) );

    // Enter SYNCING through the real send-failure path (non-typed error) —
    // this is the machinery that arms syncing_since_ at the D-02 wiring site.
    auto transaction           = MakeTransaction();
    auto committed_transaction = MakeCommittedTransaction();
    ASSERT_TRUE( committed_transaction );
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_,
                                                                 transaction,
                                                                 std::move( committed_transaction ) );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
    ASSERT_EQ( manager_->GetState(), sgns::TransactionManager::State::SYNCING );
    ASSERT_TRUE( sgns::TransactionManagerPendingLifecycleTestAccess::SyncingWindowIsArmed( *manager_ ) );

    // Hold a reservation across the fallback fire: proposed stays above
    // confirmed+1 with no confirming advance, so the sync can never resolve
    // normally (CheckTransactionValidity only flips tracked status, never the
    // confirmed nonce).
    const auto held_nonce = account_->ReserveNextNonce();
    ASSERT_GT( held_nonce, 0U );

    sgns::test::assertWaitForCondition(
        [&]
        {
            sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
            return manager_->GetState() == sgns::TransactionManager::State::READY;
        },
        std::chrono::seconds( 5 ),
        "bounded SYNCING exit did not return to READY" );
    EXPECT_FALSE( sgns::TransactionManagerPendingLifecycleTestAccess::SyncingWindowIsArmed( *manager_ ) );

    // Durable anchor unchanged: the fallback read it, never advanced it.
    ASSERT_TRUE( account_->GetLocalConfirmedNonce().has_value() );
    EXPECT_EQ( account_->GetLocalConfirmedNonce().value(), 0U );

    // Collision proof: the reservation held across the fallback fire kept the
    // nonce out of circulation — the next reservation is strictly greater.
    EXPECT_GT( account_->ReserveNextNonce(), held_nonce );
}

// D-02 half 2 negative control: the normal SYNCING-to-READY resolution
// (proposed == confirmed+1) is unchanged and does not wait out the window —
// with nothing held, the first sync pass after entry exits immediately, and
// the exit clears the window arm so no stale timestamp leaks onward.
TEST_F( TransactionManagerRecoveryTest, SyncingExitNormalSyncPathDoesNotDependOnWindow )
{
    SyncingExitWindowGuard window_guard( std::chrono::seconds( 60 ) );

    auto transaction           = MakeTransaction();
    auto committed_transaction = MakeCommittedTransaction();
    ASSERT_TRUE( committed_transaction );
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue( *manager_,
                                                                 transaction,
                                                                 std::move( committed_transaction ) );
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
    ASSERT_EQ( manager_->GetState(), sgns::TransactionManager::State::SYNCING );
    ASSERT_TRUE( sgns::TransactionManagerPendingLifecycleTestAccess::SyncingWindowIsArmed( *manager_ ) );

    // No reservation is held: the normal resolution must fire on this pass —
    // the 60s window never gates it.
    sgns::TransactionManagerPendingLifecycleTestAccess::TickOnce( *manager_ );
    EXPECT_EQ( manager_->GetState(), sgns::TransactionManager::State::READY );
    EXPECT_FALSE( sgns::TransactionManagerPendingLifecycleTestAccess::SyncingWindowIsArmed( *manager_ ) );
}
