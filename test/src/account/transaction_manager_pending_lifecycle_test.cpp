/**
 * @file       transaction_manager_pending_lifecycle_test.cpp
 * @brief      CRDT-backed TransactionManager recovery integration tests.
 * @details    Covers send recovery, nonce reconciliation, previous-hash recovery,
 *             and transaction deletion without full GeniusNode startup.
 * @date       2026-06-16
 */

#include <gtest/gtest.h>

#include "account/TransactionManager.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "account/MintTransaction.hpp"
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

        static outcome::result<void> ChangeTransactionState( TransactionManager                       &manager,
                                                             const std::shared_ptr<GeniusTransaction> &transaction,
                                                             TransactionManager::TransactionStatus     status )
        {
            return manager.ChangeTransactionState( transaction, status );
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
            if ( !transaction->Put( sgns::crdt::HierarchicalKey( "/recovery/already-committed" ),
                                    std::move( value ) ) )
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
        void StoreCertificate( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            auto subject = sgns::ConsensusManager::CreateNonceSubject( account_->GetAddress(),
                                                                       transaction->GetNonce(),
                                                                       transaction->GetHash(),
                                                                       transaction->SerializeToEmbeddedTransaction(),
                                                                       std::nullopt,
                                                                       std::nullopt );
            ASSERT_TRUE( subject.has_value() );

            auto proposal = sgns::ConsensusManager::CreateProposal( subject.value(),
                                                                    account_->GetAddress(),
                                                                    registry_->GetRegistryCid(),
                                                                    registry_->GetRegistryEpoch(),
                                                                    [this]( std::vector<uint8_t> payload )
                                                                    { return account_->Sign( payload ); } );
            ASSERT_TRUE( proposal.has_value() );

            sgns::ConsensusManager::Vote vote;
            vote.set_proposal_id( proposal.value().proposal_id() );
            vote.set_voter_id( account_->GetAddress() );
            vote.set_approve( true );
            vote.set_timestamp( proposal.value().timestamp() );
            auto vote_bytes = sgns::VoteSigningBytes( vote );
            ASSERT_TRUE( vote_bytes.has_value() );
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

            sgns::crdt::GlobalDB::Buffer certificate_data;
            certificate_data.put( certificate.SerializeAsString() );
            ASSERT_TRUE( db_->Put( sgns::crdt::HierarchicalKey( "/cert/" + transaction->GetHash() ),
                                   certificate_data,
                                   { "CRDT.Datastore.TEST.Channel" } )
                             .has_value() );
            ASSERT_TRUE( blockchain_->CheckCertificate( transaction->GetHash() ) );
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
    sgns::TransactionManagerPendingLifecycleTestAccess::Enqueue(
        *manager_, failed_transaction, std::move( committed_transaction ) );
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
    manager_->AsyncWaitForTransactionOutgoing(
        transaction->GetHash(),
        std::chrono::milliseconds( 10 ),
        [&]( sgns::TransactionManager::TransactionCompletion result ) { completion = std::move( result ); } );

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

TEST_F( TransactionManagerPreviousHashTest, UsesPersistedConfirmedHeadWhenPreviousTransactionIsNotTracked )
{
    auto previous_transaction = MakeTransaction( 0 );
    StoreCertificate( previous_transaction );
    const auto persisted_hash = account_->GetLocalConfirmedTxHash( 0 );
    ASSERT_TRUE( persisted_hash.has_value() );
    ASSERT_EQ( persisted_hash.value(), previous_transaction->GetHash() );

    RecreateManager();
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
    account_->RollBackPeerConfirmedNonce( 0, account_->GetAddress() );
    account_->SetPeerConfirmedNonce( 0, account_->GetAddress() );
    StoreTransaction( previous_transaction );
    ASSERT_TRUE( account_->GetLocalConfirmedTxHash( 0 ).has_error() );

    RecreateManager();
    ASSERT_EQ( manager_->GetTransactionStatusByTxId( previous_transaction->GetHash() ),
               sgns::TransactionManager::TransactionStatus::INVALID );

    const auto transaction_id = manager_->MigrationFunds( 1, "crdt-fallback", kTokenId );
    ASSERT_TRUE( transaction_id.has_value() );
    const auto transaction = FindOutgoingTransaction( transaction_id.value() );
    ASSERT_TRUE( transaction );

    EXPECT_EQ( transaction->GetNonce(), 1U );
    EXPECT_EQ( transaction->GetPreviousHash(), previous_transaction->GetHash() );
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
        sgns::EscrowTransaction::New( std::move( escrow_params.value() ),
                                      1,
                                      account_->GetAddress(),
                                      0,
                                      std::move( escrow_dag ) ) );
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
