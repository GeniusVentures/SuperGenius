/**
 * @file       transaction_manager_pending_lifecycle_test.cpp
 * @brief      CRDT-backed TransactionManager recovery integration tests.
 * @details    Covers send recovery, nonce reconciliation, previous-hash recovery,
 *             transaction deletion, and noncanonical MintFunds identity rejection
 *             without full GeniusNode startup.
 * @date       2026-06-16
 */

#include <gtest/gtest.h>

#include "account/EscrowTransaction.hpp"
#include "account/GeniusAccount.hpp"
#include "account/GeniusInputValidator.hpp"
#include "account/MintTransaction.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOMerkle.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ConsensusAuth.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/atomic_transaction.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "storage/database_error.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace sgns
{
    /**
     * @brief Test-only access for deterministic TransactionManager recovery failures.
     */
    class TransactionManagerPendingLifecycleTestAccess
    {
    public:
        using UTXOFaultStage = UTXOManager::FaultStage;
        using MintFaultStage = TransactionManager::MintFaultStage;

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

        static void SetReady( TransactionManager &manager )
        {
            manager.state_m = TransactionManager::State::READY;
        }

        static outcome::result<std::string> MintFunds( TransactionManager &manager,
                                                       uint64_t            amount,
                                                       std::string         transaction_hash,
                                                       std::string         chain_id,
                                                       uint32_t            receipt_log_index,
                                                       TokenID             token_id,
                                                       std::string         destination )
        {
            return manager.MintFunds( amount,
                                      std::move( transaction_hash ),
                                      std::move( chain_id ),
                                      receipt_log_index,
                                      std::move( token_id ),
                                      std::move( destination ) );
        }

        static size_t QueueSize( TransactionManager &manager )
        {
            std::lock_guard lock( manager.mutex_m );
            return manager.tx_queue_m.size();
        }

        static outcome::result<TransactionManager::BridgeBurnState> GetBridgeBurnState(
            const TransactionManager &manager,
            const std::string        &chain_id,
            const std::string        &transaction_hash,
            uint32_t                  receipt_log_index )
        {
            return manager.GetBridgeBurnState(
                chain_id, transaction_hash, receipt_log_index );
        }

        static void SetBridgeApplicationReader(
            TransactionManager &manager,
            UTXOManager::BridgeApplicationReader reader )
        {
            manager.account_m->GetUTXOManager().bridge_application_reader_ =
                std::move( reader );
        }

        static void ResetBridgeApplicationReader( TransactionManager &manager )
        {
            manager.account_m->GetUTXOManager().ResetBridgeApplicationReader();
        }

        static void SetUTXOFault(
            TransactionManager &manager,
            UTXOManager::FaultCallback callback )
        {
            manager.account_m->GetUTXOManager().fault_callback_ = std::move( callback );
        }

        static void ResetUTXOFault( TransactionManager &manager )
        {
            manager.account_m->GetUTXOManager().ResetFaultCallback();
        }

        static void SetMintFault(
            TransactionManager &manager,
            TransactionManager::MintFaultCallback callback )
        {
            manager.mint_fault_callback_ = std::move( callback );
        }

        static void ResetMintFault( TransactionManager &manager )
        {
            manager.ResetMintFaultCallback();
        }

        static outcome::result<void> Confirm(
            TransactionManager &manager,
            const std::shared_ptr<GeniusTransaction> &tx )
        {
            return manager.ChangeTransactionState(
                tx, TransactionManager::TransactionStatus::CONFIRMED );
        }

        static std::optional<TransactionManager::TransactionStatus> Status(
            const TransactionManager &manager,
            const GeniusTransaction &tx )
        {
            std::shared_lock lock( manager.tx_mutex_m );
            auto it = manager.tx_processed_m.find(
                TransactionManager::GetTransactionPath( tx ) );
            return it == manager.tx_processed_m.end()
                     ? std::nullopt
                     : std::optional<TransactionManager::TransactionStatus>( it->second.status );
        }

        static uint64_t ConfirmMetric( const TransactionManager &manager )
        {
            return manager.metrics_tracking_confirm_.load();
        }

        static std::optional<TransactionManager::AccountUTXOState> AccountState(
            const TransactionManager &manager,
            const std::string &address )
        {
            std::shared_lock lock( manager.account_utxo_state_mutex_ );
            auto it = manager.account_utxo_state_.find( address );
            return it == manager.account_utxo_state_.end()
                     ? std::nullopt
                     : std::optional<TransactionManager::AccountUTXOState>( it->second );
        }

        static outcome::result<std::optional<UTXOManager::BridgeApplication>> Application(
            const TransactionManager &manager,
            const std::string &chain,
            const base::Hash256 &burn,
            uint32_t index )
        {
            return manager.account_m->GetUTXOManager().GetBridgeApplication(
                chain, burn, index );
        }

        static std::shared_ptr<MintTransactionV2> LastQueuedMint( TransactionManager &manager )
        {
            std::lock_guard lock( manager.mutex_m );
            if ( manager.tx_queue_m.empty() || manager.tx_queue_m.back().first.empty() )
            {
                return nullptr;
            }
            return std::dynamic_pointer_cast<MintTransactionV2>(
                manager.tx_queue_m.back().first.back().first );
        }

        static ConsensusManager::ValidationResult EvaluateReplayProtection(
            const TransactionManager &manager,
            const GeniusTransaction  &transaction )
        {
            return manager.EvaluateTransactionReplayProtection( transaction ).validation;
        }

        static outcome::result<std::optional<ConsensusStateStore::BurnOutpoint>> DescribeResource(
            const TransactionManager &manager,
            const ConsensusManager::Subject &subject,
            const std::string &slot_id )
        {
            return manager.DescribeConsensusResource( subject, slot_id );
        }

        static std::shared_ptr<ConsensusManager> Consensus(
            const std::shared_ptr<Blockchain> &blockchain )
        {
            return blockchain->consensus_manager_;
        }

        static ConsensusManager::CertificateApplicationHandler ApplicationHandler(
            const std::shared_ptr<ConsensusManager> &consensus )
        {
            auto type_hash = ConsensusManager::ComputeSubjectTypeHash( NONCE_SUBJECT_TYPE );
            if ( !type_hash ) return {};
            std::shared_lock lock( consensus->certificate_handlers_mutex_ );
            auto it = consensus->certificate_application_handlers_.find( type_hash.value() );
            return it == consensus->certificate_application_handlers_.end() ?
                       ConsensusManager::CertificateApplicationHandler{} : it->second;
        }

        static std::shared_ptr<ConsensusStateStore> StateStore(
            const std::shared_ptr<ConsensusManager> &consensus )
        {
            return consensus->state_store_;
        }

        static void ObserveFinalizedHandle(
            std::function<void(
                const ConsensusManager::FinalizedReservationApplicationHandle * )> observer )
        {
            TransactionManager::finalized_handle_observer_ = std::move( observer );
        }

        static void ResetFinalizedHandleObserver()
        {
            TransactionManager::finalized_handle_observer_ = {};
        }

        static outcome::result<ConsensusManager::ApplicationDisposition> OnCertificateWithoutHandle(
            TransactionManager &manager,
            const std::string &tx_hash,
            const ConsensusManager::Certificate &certificate )
        {
            return manager.OnConsensusCertificate( tx_hash, certificate, std::nullopt );
        }

        static void RemoveApplicationHandler( const std::shared_ptr<ConsensusManager> &consensus )
        {
            auto type_hash = ConsensusManager::ComputeSubjectTypeHash( NONCE_SUBJECT_TYPE );
            if ( !type_hash ) return;
            std::unique_lock lock( consensus->certificate_handlers_mutex_ );
            consensus->certificate_application_handlers_.erase( type_hash.value() );
        }

        static ConsensusManager::FinalizeResult DriveFinalizedApplication(
            const std::shared_ptr<ConsensusManager> &consensus,
            const ConsensusManager::Certificate &certificate,
            const ConsensusStateStore::BurnOutpoint &outpoint )
        {
            auto slot = ConsensusManager::GetSlotKey( certificate.proposal().subject() );
            auto winner = ConsensusManager::GetSubjectHash( certificate.proposal().subject() );
            if ( !slot || !winner ) return ConsensusManager::FinalizeResult::StorageFailure;
            ConsensusManager::CertificateNormalization normalized;
            normalized.check = ConsensusManager::Check::Approve;
            normalized.certificate = certificate;
            normalized.deterministic_bytes = certificate.SerializeAsString();
            const auto digest_bytes = crypto::sha2_256(
                normalized.deterministic_bytes.data(), normalized.deterministic_bytes.size() );
            const auto digest = base::hex_lower(
                gsl::span<const uint8_t>( digest_bytes.data(), digest_bytes.size() ) );
            auto finalized = consensus->state_store_->FinalizeBurnReservation(
                slot.value(), outpoint, digest, certificate.proposal_id(), winner.value(), 1U );
            if ( !finalized )
            {
                return ConsensusManager::FinalizeResult::StorageFailure;
            }
            ConsensusStateStore::ProcessRecord process;
            process.set_schema_version( 2 );
            process.set_state( ConsensusStateStore::ProcessRecord::PENDING );
            process.set_slot_id( slot.value() );
            process.set_certificate_digest( digest );
            process.set_proposal_id( certificate.proposal_id() );
            process.set_winner_id( winner.value() );
            process.set_updated_at_ms( 1U );
            auto pending = consensus->state_store_->PutPendingProcess( process );
            if ( !pending )
            {
                return ConsensusManager::FinalizeResult::StorageFailure;
            }
            return consensus->ProcessFinalizedCertificate(
                normalized, slot.value(), winner.value() );
        }

        static ConsensusManager::FinalizeResult Finalize(
            const std::shared_ptr<ConsensusManager> &consensus,
            const ConsensusManager::Certificate &certificate )
        {
            return consensus->FinalizeSlot(
                certificate, ConsensusManager::DeliverySource::Recovery );
        }

        static outcome::result<void> RemovePersistedBridgeApplication(
            TransactionManager &manager,
            const std::string &chain,
            const base::Hash256 &burn,
            uint32_t index )
        {
            auto &utxo = manager.account_m->GetUTXOManager();
            std::unique_lock persistence_lock( utxo.persistence_mutex_ );
            std::unique_lock state_lock( utxo.utxos_mutex_ );
            if ( !utxo.db_ ) return outcome::failure( storage::DatabaseError::UNITIALIZED );
            base::Buffer key;
            key.put( UTXOManager::MakeBridgeApplicationKey( chain, burn, index ) );
            auto removed = utxo.db_->remove( key );
            if ( removed.has_error() ) return outcome::failure( removed.error() );
            return outcome::success();
        }

        static std::vector<std::pair<std::string, std::string>> RawUTXORecords(
            const TransactionManager &manager )
        {
            auto &utxo = manager.account_m->GetUTXOManager();
            std::unique_lock persistence_lock( utxo.persistence_mutex_ );
            std::shared_lock state_lock( utxo.utxos_mutex_ );
            std::vector<std::pair<std::string, std::string>> records;
            if ( !utxo.db_ ) return records;
            base::Buffer prefix;
            prefix.put( UTXOManager::DB_PREFIX );
            auto raw = utxo.db_->query( prefix );
            if ( !raw ) return records;
            for ( const auto &[key, value] : raw.value() )
                records.emplace_back( key.toString(), value.toString() );
            std::sort( records.begin(), records.end() );
            return records;
        }

        static bool RestoreCommittedApplicationCrashBoundary(
            const std::shared_ptr<ConsensusManager> &consensus,
            const std::shared_ptr<crdt::GlobalDB> &db,
            const std::string &slot )
        {
            auto current = consensus->state_store_->GetProcess( slot );
            if ( !current || !current.value() ) return false;
            auto pending = current.value().value();
            pending.set_state( ConsensusStateStore::ProcessRecord::PENDING );
            pending.set_lease_until_ms( 0 );
            pending.set_updated_at_ms( pending.updated_at_ms() + 1 );
            base::Buffer key;
            base::Buffer value;
            key.put( ConsensusStateStore::ProcessKey( slot ) );
            value.put( pending.SerializeAsString() );
            if ( db->GetDataStore()->put( key, value ).has_error() ) return false;
            const auto work_key = std::string( ConsensusManager::CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot;
            consensus->certificate_work_journal_->MarkSeen( work_key );
            consensus->certificate_work_journal_->MarkStalled( work_key, std::chrono::milliseconds( 0 ) );
            return true;
        }

        static bool HasCertificateWork( const std::shared_ptr<ConsensusManager> &consensus,
                                        const std::string &slot )
        {
            return consensus->certificate_work_journal_->GetEntry(
                       std::string( ConsensusManager::CERTIFICATE_SLOT_BASE_PATH_KEY ) + slot ).has_value();
        }

        static void RecoverCertificateWork( const std::shared_ptr<ConsensusManager> &consensus )
        {
            consensus->RecoverPendingCertificateWork();
            consensus->RecoverRestoredCertificateWork();
        }

        static void ReconcileBurnReservations( const std::shared_ptr<ConsensusManager> &consensus )
        {
            consensus->ReconcileBurnReservations();
        }

        static bool AddCertificateWaiter( const std::shared_ptr<ConsensusManager> &consensus,
                                          const ConsensusManager::Proposal &proposal,
                                          const std::string &winner )
        {
            return consensus->AddPendingProposal(
                proposal, "composed-recovery-waiter",
                ConsensusManager::ValidationResult::Pending(
                    { ConsensusManager::PendingDependencyKey::Certificate( winner ) } ) );
        }

        static std::size_t CertificateWaiterCount(
            const std::shared_ptr<ConsensusManager> &consensus,
            const std::string &winner )
        {
            std::lock_guard lock( consensus->proposals_mutex_ );
            auto it = consensus->pending_by_dependency_.find(
                ConsensusManager::PendingDependencyKey::Certificate( winner ) );
            return it == consensus->pending_by_dependency_.end() ? 0U : it->second.size();
        }

    };

    class ConsensusManagerTestAccess
    {
    public:
        static void SetCertificateReader(
            const std::shared_ptr<ConsensusManager> &manager,
            std::function<outcome::result<crdt::GlobalDB::Buffer>(
                const crdt::HierarchicalKey & )> reader )
        {
            manager->certificate_record_reader_ = std::move( reader );
        }
    };
} // namespace sgns

using namespace sgns;

namespace
{
    class TransactionManagerRecoveryTest : public ::test::CRDTFixture
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

    /**
     * @brief CRDT-backed fixture dedicated to noncanonical MintFunds identity rejection.
     * @details Avoids GeniusNode network startup and constructs GeniusAccount, Blockchain,
     *          and TransactionManager directly using the certificate fallback fixture pattern.
     */
    class TransactionManagerPendingLifecycleTest : public ::test::CRDTFixture
    {
    public:
        TransactionManagerPendingLifecycleTest()
            : CRDTFixture( "transaction_manager_pending_lifecycle_test" )
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                {
                    return std::make_shared<MemorySecureStorage>( identifier );
                } );
            account_ = GeniusAccount::NewFromPrivateKey(
                kTokenId,
                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                base_path / "account",
                false );
            EXPECT_NE( account_, nullptr );
            if ( !account_ )
            {
                return;
            }

            const auto *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
            if ( test_info && std::string_view( test_info->name() ) ==
                                  "CertifiedMintPersistedArtifactCorruptionRecoversToConsumedSafetyAndNeverRetries" )
            {
                secondary_account_ = GeniusAccount::NewFromPrivateKey(
                    kTokenId,
                    "cafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe",
                    base_path / "secondary-validator", false );
                EXPECT_NE( secondary_account_, nullptr );
            }

            auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
            EXPECT_TRUE( load_result.has_value() );

            blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
            EXPECT_NE( blockchain_, nullptr );
            if ( !blockchain_ )
            {
                return;
            }
            registry_ = blockchain_->GetValidatorRegistry();
            EXPECT_NE( registry_, nullptr );
            if ( !registry_ )
            {
                return;
            }
            std::vector<std::string> genesis_validators{ account_->GetAddress() };
            if ( secondary_account_ ) genesis_validators.push_back( secondary_account_->GetAddress() );
            auto genesis_result = registry_->StoreGenesisRegistry(
                genesis_validators,
                [this]( std::vector<uint8_t> payload )
                {
                    return account_->Sign( std::move( payload ) );
                } );
            EXPECT_TRUE( genesis_result.has_value() );
            for ( int attempt = 0;
                  attempt < 100 &&
                  ( registry_->LoadCurrentRegistry().has_error() ||
                    registry_->GetRegistryCid().empty() );
                  ++attempt )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            }
            EXPECT_FALSE( registry_->GetRegistryCid().empty() );
            consensus_ =
                TransactionManagerPendingLifecycleTestAccess::Consensus( blockchain_ );
            EXPECT_NE( consensus_, nullptr );

            manager_ = TransactionManager::New(
                db_,
                io_,
                account_,
                blockchain_,
                false,
                0,
                std::chrono::milliseconds( 300000 ),
                std::chrono::milliseconds( 600000 ) );
            EXPECT_NE( manager_, nullptr );
            if ( manager_ )
            {
                TransactionManagerPendingLifecycleTestAccess::SetReady( *manager_ );
            }
        }

    protected:
        static const TokenID kTokenId;
        static constexpr uint32_t kReceiptIndex = 19;
        static constexpr uint64_t kAmount = 1000;

        bool ExecutedKeyAbsent( const std::string &chain_id,
                                const std::string &transaction_hash ) const
        {
            if ( transaction_hash.size() != 64 ||
                 !std::all_of(
                     transaction_hash.begin(),
                     transaction_hash.end(),
                     []( unsigned char c )
                     {
                         return ( c >= '0' && c <= '9' ) ||
                                ( c >= 'a' && c <= 'f' );
                     } ) )
            {
                return true;
            }
            auto hash = base::Hash256::fromReadableString( transaction_hash );
            if ( hash.has_error() )
            {
                return true;
            }
            crdt::GlobalDB::Buffer key;
            key.put( UTXOManager::MakeBridgeApplicationKey(
                chain_id, hash.value(), kReceiptIndex ) );
            return db_->GetDataStore()->get( key ).has_error();
        }

        std::shared_ptr<MintTransactionV2> PrepareMint( char burn_digit )
        {
            const std::string burn_hash( 64, burn_digit );
            auto result = TransactionManagerPendingLifecycleTestAccess::MintFunds(
                *manager_,
                kAmount,
                burn_hash,
                "11155111",
                kReceiptIndex,
                kTokenId,
                account_->GetAddress() );
            EXPECT_TRUE( result.has_value() );
            return TransactionManagerPendingLifecycleTestAccess::LastQueuedMint( *manager_ );
        }

        void RebuildProductionObjects()
        {
            manager_.reset();
            consensus_.reset();
            registry_.reset();
            blockchain_.reset();
            if ( account_ )
            {
                account_->GetUTXOManager().ReleaseStorage();
            }
            account_.reset();

            ++restart_count_;
            account_ = GeniusAccount::NewFromPrivateKey(
                kTokenId,
                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                base_path / ( "account-restart-" + std::to_string( restart_count_ ) ),
                false );
            ASSERT_NE( account_, nullptr );
            ASSERT_TRUE( account_->GetUTXOManager()
                             .LoadUTXOs( db_->GetDataStore() )
                             .has_value() );
            blockchain_ = Blockchain::New(
                db_, account_, pubs_, []( outcome::result<void> ) {} );
            ASSERT_NE( blockchain_, nullptr );
            registry_ = blockchain_->GetValidatorRegistry();
            ASSERT_NE( registry_, nullptr );
            consensus_ =
                TransactionManagerPendingLifecycleTestAccess::Consensus( blockchain_ );
            ASSERT_NE( consensus_, nullptr );
            manager_ = TransactionManager::New(
                db_,
                io_,
                account_,
                blockchain_,
                false,
                0,
                std::chrono::milliseconds( 300000 ),
                std::chrono::milliseconds( 600000 ) );
            ASSERT_NE( manager_, nullptr );
            TransactionManagerPendingLifecycleTestAccess::SetReady( *manager_ );
        }

        void ExpectMintFundsInvalid( const std::string &chain_id,
                                     const std::string &transaction_hash )
        {
            ASSERT_NE( manager_, nullptr );
            const auto queue_before =
                TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ );
            const auto result = TransactionManagerPendingLifecycleTestAccess::MintFunds(
                *manager_,
                1000,
                transaction_hash,
                chain_id,
                kReceiptIndex,
                kTokenId,
                account_->GetAddress() );

            ASSERT_TRUE( result.has_error() );
            EXPECT_EQ( result.error(), std::make_error_code( std::errc::invalid_argument ) );
            EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ ),
                       queue_before );
            EXPECT_TRUE( ExecutedKeyAbsent( chain_id, transaction_hash ) );

            const bool parseable_hash =
                transaction_hash.size() == 64 &&
                std::all_of(
                    transaction_hash.begin(),
                    transaction_hash.end(),
                    []( unsigned char c )
                    {
                        return std::isxdigit( c ) != 0;
                    } );
            if ( parseable_hash )
            {
                auto parsed_hash = base::Hash256::fromReadableString( transaction_hash );
                ASSERT_TRUE( parsed_hash.has_value() );
                EXPECT_FALSE( account_->GetUTXOManager()
                                  .GetOutPointState( parsed_hash.value(), kReceiptIndex )
                                  .has_value() );
            }
        }

        static std::string RawBytes( const base::Hash256 &hash )
        {
            return std::string(
                reinterpret_cast<const char *>( hash.data() ), hash.size() );
        }

        static std::vector<uint8_t> SerializeOutpoint(
            const base::Hash256 &hash,
            uint32_t             output_index )
        {
            std::vector<uint8_t> payload( hash.begin(), hash.end() );
            utxo_merkle::AppendUInt32BE( payload, output_index );
            return payload;
        }

        static std::vector<uint8_t> SerializeOutput(
            const base::Hash256 &hash,
            uint32_t             output_index,
            const std::string   &owner,
            const TokenID       &token_id,
            uint64_t             amount )
        {
            std::vector<uint8_t> payload( hash.begin(), hash.end() );
            utxo_merkle::AppendUInt32BE( payload, output_index );
            utxo_merkle::AppendUInt32BE(
                payload, static_cast<uint32_t>( owner.size() ) );
            payload.insert( payload.end(), owner.begin(), owner.end() );
            const auto &token_bytes = token_id.bytes();
            payload.insert(
                payload.end(), token_bytes.begin(), token_bytes.end() );
            utxo_merkle::AppendUInt64BE( payload, amount );
            return payload;
        }

        outcome::result<ConsensusManager::Certificate> MakeCertificate(
            const std::string                             &transaction_hash,
            uint64_t                                       nonce,
            const std::optional<UTXOTransitionCommitment> &commitment =
                std::nullopt )
        {
            BOOST_OUTCOME_TRY(
                auto subject,
                ConsensusManager::CreateNonceSubject(
                    account_->GetAddress(),
                    nonce,
                    transaction_hash,
                    EmbeddedTransaction{},
                    commitment,
                    std::nullopt ) );
            BOOST_OUTCOME_TRY(
                auto proposal,
                ConsensusManager::CreateProposal(
                    subject,
                    account_->GetAddress(),
                    registry_->GetRegistryCid(),
                    registry_->GetRegistryEpoch(),
                    [this]( std::vector<uint8_t> payload )
                    {
                        return account_->Sign( std::move( payload ) );
                    } ) );
            BOOST_OUTCOME_TRY(
                auto vote,
                consensus_->CreateVote(
                    proposal.proposal_id(),
                    account_->GetAddress(),
                    true,
                    [this]( std::vector<uint8_t> payload )
                    {
                        return account_->Sign( std::move( payload ) );
                    } ) );
            return consensus_->CreateCertificate( proposal, { vote } );
        }

        outcome::result<ConsensusManager::Proposal> MakePendingProposal(
            const std::string &transaction_hash,
            uint64_t nonce )
        {
            BOOST_OUTCOME_TRY(
                auto subject,
                ConsensusManager::CreateNonceSubject(
                    account_->GetAddress(), nonce, transaction_hash,
                    EmbeddedTransaction{}, std::nullopt, std::nullopt ) );
            return ConsensusManager::CreateProposal(
                subject, account_->GetAddress(), registry_->GetRegistryCid(),
                registry_->GetRegistryEpoch(),
                [this]( std::vector<uint8_t> payload )
                { return account_->Sign( std::move( payload ) ); } );
        }

        outcome::result<ConsensusManager::Certificate> MakeCertificateWithEmbedded(
            const std::string &transaction_hash,
            uint64_t nonce,
            EmbeddedTransaction embedded )
        {
            BOOST_OUTCOME_TRY(
                auto subject,
                ConsensusManager::CreateNonceSubject(
                    account_->GetAddress(), nonce, transaction_hash, std::move( embedded ),
                    std::nullopt, std::nullopt ) );
            BOOST_OUTCOME_TRY(
                auto proposal,
                ConsensusManager::CreateProposal(
                    subject, account_->GetAddress(), registry_->GetRegistryCid(),
                    registry_->GetRegistryEpoch(),
                    [this]( std::vector<uint8_t> payload )
                    { return account_->Sign( std::move( payload ) ); } ) );
            ConsensusManager::Certificate certificate;
            *certificate.mutable_proposal() = proposal;
            certificate.set_proposal_id( proposal.proposal_id() );
            return certificate;
        }

        outcome::result<ConsensusManager::Certificate> MakeCertifiedMintCertificate(
            const std::shared_ptr<MintTransactionV2> &mint )
        {
            if ( !mint ) return outcome::failure( std::errc::invalid_argument );
            consensus_->SetSlotHashPopulator( []( ConsensusVote &vote )
            {
                vote.set_slot_0_hash( std::string( 32, '\x01' ) );
                vote.set_slot_1_hash( std::string( 32, '\x02' ) );
                vote.set_slot_2_hash( std::string( 32, '\x03' ) );
            } );
            BOOST_OUTCOME_TRY(
                auto subject,
                ConsensusManager::CreateNonceSubject(
                    account_->GetAddress(), mint->GetNonce(), mint->GetHash(),
                    mint->SerializeToEmbeddedTransaction( mint->dag_st ),
                    std::nullopt, std::nullopt ) );
            BOOST_OUTCOME_TRY(
                auto proposal,
                ConsensusManager::CreateProposal(
                    subject, account_->GetAddress(), registry_->GetRegistryCid(),
                    registry_->GetRegistryEpoch(),
                    [this]( std::vector<uint8_t> payload )
                    { return account_->Sign( std::move( payload ) ); } ) );
            BOOST_OUTCOME_TRY(
                auto vote,
                consensus_->CreateVote(
                    proposal.proposal_id(), account_->GetAddress(), true,
                    [this]( std::vector<uint8_t> payload )
                    { return account_->Sign( std::move( payload ) ); } ) );
            std::vector<ConsensusVote> votes{ vote };
            if ( secondary_account_ )
            {
                BOOST_OUTCOME_TRY(
                    auto secondary_vote,
                    consensus_->CreateVote(
                        proposal.proposal_id(), secondary_account_->GetAddress(), true,
                        [this]( std::vector<uint8_t> payload )
                        { return secondary_account_->Sign( std::move( payload ) ); } ) );
                votes.push_back( std::move( secondary_vote ) );
            }
            return consensus_->CreateCertificate( proposal, votes );
        }

        void RebuildConsensusWithoutTransactionManager()
        {
            manager_.reset();
            if ( consensus_ ) consensus_->Close();
            consensus_.reset();
            registry_.reset();
            blockchain_.reset();
            if ( account_ ) account_->GetUTXOManager().ReleaseStorage();
            account_.reset();

            ++restart_count_;
            account_ = GeniusAccount::NewFromPrivateKey(
                kTokenId,
                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                base_path / ( "account-composed-restart-" + std::to_string( restart_count_ ) ),
                false );
            ASSERT_NE( account_, nullptr );
            ASSERT_TRUE( account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() ) );
            blockchain_ = Blockchain::New(
                db_, account_, pubs_, []( outcome::result<void> ) {} );
            ASSERT_NE( blockchain_, nullptr );
            registry_ = blockchain_->GetValidatorRegistry();
            ASSERT_NE( registry_, nullptr );
            consensus_ = TransactionManagerPendingLifecycleTestAccess::Consensus( blockchain_ );
            ASSERT_NE( consensus_, nullptr );
        }

        void CreateProductionTransactionManager()
        {
            manager_ = TransactionManager::New(
                db_, io_, account_, blockchain_, false, 0,
                std::chrono::milliseconds( 300000 ),
                std::chrono::milliseconds( 600000 ) );
            ASSERT_NE( manager_, nullptr );
            TransactionManagerPendingLifecycleTestAccess::SetReady( *manager_ );
        }


        std::shared_ptr<TransferTransaction> MakeReplayTransaction(
            const std::string &previous_hash,
            uint64_t           nonce ) const
        {
            SGTransaction::DAGStruct dag;
            dag.set_source_addr( account_->GetAddress() );
            dag.set_previous_hash( previous_hash );
            dag.set_nonce( nonce );
            dag.set_timestamp(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() );
            return std::make_shared<TransferTransaction>(
                TransferTransaction::New( {}, {}, std::move( dag ) ) );
        }

        void UseRealCertificateReader()
        {
            ConsensusManagerTestAccess::SetCertificateReader(
                consensus_,
                [db = db_]( const crdt::HierarchicalKey &key )
                {
                    return db->Get( key );
                } );
        }

        void FailCertificateReads( storage::DatabaseError error )
        {
            ConsensusManagerTestAccess::SetCertificateReader(
                consensus_,
                [error]( const crdt::HierarchicalKey & )
                    -> outcome::result<crdt::GlobalDB::Buffer>
                {
                    return outcome::failure( error );
                } );
        }

        struct WitnessCase
        {
            std::shared_ptr<TransferTransaction> transaction;
            UTXOTxParameters                     parameters;
            ConsensusManager::Subject            subject;
            UTXOTransitionCommitment             producer_commitment;
        };

        outcome::result<WitnessCase> MakeWitnessCase(
            const std::string &producer_hash_text )
        {
            BOOST_OUTCOME_TRY(
                auto producer_hash,
                base::Hash256::fromReadableString( producer_hash_text ) );

            InputUTXOInfo input;
            input.txid_hash_  = producer_hash;
            input.output_idx_ = 0;
            input.signature_  = account_->Sign( input.SerializeForSigning() );
            OutputDestInfo output{
                kAmount, account_->GetAddress(), kTokenId
            };

            SGTransaction::DAGStruct dag;
            dag.set_source_addr( account_->GetAddress() );
            dag.set_previous_hash( producer_hash_text );
            dag.set_nonce( 8 );
            dag.set_timestamp(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() );
            auto transaction = std::make_shared<TransferTransaction>(
                TransferTransaction::New( { input }, { output }, std::move( dag ) ) );
            BOOST_OUTCOME_TRY(
                auto transaction_hash,
                base::Hash256::fromReadableString( transaction->GetHash() ) );

            const auto producer_leaf = SerializeOutput(
                producer_hash, 0, account_->GetAddress(), kTokenId, kAmount );
            UTXOTransitionCommitment producer_commitment;
            producer_commitment.set_produced_outputs_root(
                RawBytes( utxo_merkle::HashLeaf( producer_leaf ) ) );

            UTXOTransitionCommitment commitment;
            auto *consumed = commitment.add_consumed_outpoints();
            consumed->set_tx_id_hash( RawBytes( producer_hash ) );
            consumed->set_output_index( 0 );
            commitment.set_consumed_outpoints_root(
                RawBytes( utxo_merkle::ComputeMerkleRootFromPayloads(
                    { SerializeOutpoint( producer_hash, 0 ) } ) ) );
            auto *produced = commitment.add_produced_outputs();
            produced->set_tx_id_hash( RawBytes( transaction_hash ) );
            produced->set_output_index( 0 );
            produced->set_owner_address( account_->GetAddress() );
            produced->set_token_id(
                kTokenId.bytes().data(), kTokenId.bytes().size() );
            produced->set_amount( kAmount );
            commitment.set_produced_outputs_root(
                RawBytes( utxo_merkle::ComputeMerkleRootFromPayloads(
                    { SerializeOutput(
                        transaction_hash,
                        0,
                        account_->GetAddress(),
                        kTokenId,
                        kAmount ) } ) ) );

            UTXOWitness witness;
            auto       *proof = witness.add_consumed_inputs();
            proof->set_tx_id_hash( RawBytes( producer_hash ) );
            proof->set_output_index( 0 );
            proof->set_leaf_payload(
                producer_leaf.data(), producer_leaf.size() );

            BOOST_OUTCOME_TRY(
                auto subject,
                ConsensusManager::CreateNonceSubject(
                    account_->GetAddress(),
                    transaction->GetNonce(),
                    transaction->GetHash(),
                    transaction->SerializeToEmbeddedTransaction(),
                    commitment,
                    witness ) );
            return WitnessCase{
                transaction,
                UTXOTxParameters{ { input }, { output } },
                subject,
                producer_commitment
            };
        }

        std::shared_ptr<GeniusAccount>      account_;
        std::shared_ptr<GeniusAccount>      secondary_account_;
        std::shared_ptr<Blockchain>         blockchain_;
        std::shared_ptr<TransactionManager> manager_;
        std::shared_ptr<ValidatorRegistry>  registry_;
        std::shared_ptr<ConsensusManager>   consensus_;
        size_t                              restart_count_{ 0 };
    };

    const TokenID TransactionManagerPendingLifecycleTest::kTokenId =
        TokenID::FromBytes( { 0x00 } );
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

TEST_F( TransactionManagerPendingLifecycleTest, ApplicationHandleWeakOwnerExpiresSafely )
{
    ASSERT_TRUE( consensus_ );
    auto handler = TransactionManagerPendingLifecycleTestAccess::ApplicationHandler( consensus_ );
    ASSERT_TRUE( handler );
    auto store = TransactionManagerPendingLifecycleTestAccess::StateStore( consensus_ );
    ASSERT_TRUE( store );
    ConsensusStateStore::FinalizedReservationIdentity identity{
        std::string( 64, '1' ), { "11155111", std::string( 64, '2' ), kReceiptIndex },
        std::string( 64, '3' ), std::string( 64, '4' ), std::string( 64, '5' ),
        std::string( 64, '6' ) };
    manager_.reset();

    auto expired = handler(
        std::string( 64, '6' ), ConsensusCertificate{},
        ConsensusManager::FinalizedReservationApplicationHandle{ store, std::move( identity ) } );
    ASSERT_TRUE( expired.has_error() );
    EXPECT_EQ( expired.error(), std::make_error_code( std::errc::owner_dead ) );
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

TEST_F( TransactionManagerRecoveryTest, RecreatesManagerUsingSameBlockchain )
{
    ASSERT_NO_FATAL_FAILURE( RecreateManager() );
    ASSERT_TRUE( manager_ );
}

TEST_F( TransactionManagerPreviousHashTest, UsesPersistedConfirmedHeadWhenPreviousTransactionIsNotTracked )
{
    auto previous_transaction = MakeTransaction( 0 );
    StoreCertificate( previous_transaction );
    const auto persisted_hash = account_->GetLocalConfirmedTxHash( 0 );
    ASSERT_TRUE( persisted_hash.has_value() );
    ASSERT_EQ( persisted_hash.value(), previous_transaction->GetHash() );

    ASSERT_NO_FATAL_FAILURE( RecreateManager() );
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

    ASSERT_NO_FATAL_FAILURE( RecreateManager() );
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

TEST( TransactionManagerPendingLifecycleContractTest, CertificateLookupErrorsSeparatePendingFromCorruption )
{
    using Error = sgns::ConsensusManager::CertificateStoreError;

    const auto not_found = make_error_code( Error::NotFound );
    const auto integrity = make_error_code( Error::IntegrityError );

    EXPECT_NE( not_found, integrity );
    EXPECT_EQ( not_found.message(), "Certificate record not found" );
    EXPECT_EQ( integrity.message(), "Certificate store integrity error" );
}

TEST_F( TransactionManagerPendingLifecycleTest, MintFundsRejectsNoncanonicalChainBeforeMutation )
{
    const std::string valid_hash( 64, 'a' );
    for ( const auto &chain_id : { "", "public", "01", "+1", "1 " } )
    {
        ExpectMintFundsInvalid( chain_id, valid_hash );
    }
}

TEST_F( TransactionManagerPendingLifecycleTest, MintFundsRejectsNoncanonicalBurnHashBeforeMutation )
{
    for ( const auto &transaction_hash : {
              std::string{},
              std::string( 64, '0' ),
              "0x" + std::string( 64, 'a' ),
              std::string( 64, 'A' ),
              std::string( 63, 'a' ),
              std::string( 64, 'g' ),
          } )
    {
        ExpectMintFundsInvalid( "11155111", transaction_hash );
    }
}

TEST_F( TransactionManagerPendingLifecycleTest, MintFundsCanonicalIdentityCreatesIndexedMint )
{
    ASSERT_NE( manager_, nullptr );
    const std::string burn_hash( 64, 'a' );
    const auto queue_before =
        TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ );

    auto result = TransactionManagerPendingLifecycleTestAccess::MintFunds(
        *manager_,
        1000,
        burn_hash,
        "11155111",
        kReceiptIndex,
        kTokenId,
        account_->GetAddress() );

    ASSERT_TRUE( result.has_value() );
    EXPECT_FALSE( result.value().empty() );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ ),
               queue_before + 1 );

    auto mint = TransactionManagerPendingLifecycleTestAccess::LastQueuedMint( *manager_ );
    ASSERT_NE( mint, nullptr );
    const auto inputs = mint->GetUTXOParameters().first;
    ASSERT_EQ( inputs.size(), 1U );
    EXPECT_EQ( inputs.front().output_idx_, kReceiptIndex );
    EXPECT_EQ( inputs.front().txid_hash_.toReadableString(), burn_hash );
    EXPECT_EQ( account_->GetUTXOManager().GetOutPointState( inputs.front().txid_hash_, kReceiptIndex ),
               UTXOManager::UTXOState::UTXO_READY );

    auto embedded = mint->SerializeToEmbeddedTransaction( mint->dag_st );
    auto subject = ConsensusManager::CreateNonceSubject(
        account_->GetAddress(), mint->GetNonce(), mint->GetHash(), embedded, std::nullopt, std::nullopt );
    ASSERT_TRUE( subject );
    auto slot = mint->GetSlotID();
    ASSERT_TRUE( slot );
    auto descriptor = TransactionManagerPendingLifecycleTestAccess::DescribeResource(
        *manager_, subject.value(), slot.value() );
    ASSERT_TRUE( descriptor && descriptor.value() );
    EXPECT_EQ( descriptor.value()->source_chain, "11155111" );
    EXPECT_EQ( descriptor.value()->burn_hash, burn_hash );
    EXPECT_EQ( descriptor.value()->receipt_log_index, kReceiptIndex );
}

TEST_F( TransactionManagerPendingLifecycleTest, SharedStoreApplicationHandleReachesMintApplication )
{
    auto mint = PrepareMint( '7' );
    ASSERT_TRUE( mint );
    auto handler = TransactionManagerPendingLifecycleTestAccess::ApplicationHandler( consensus_ );
    ASSERT_TRUE( handler );
    auto expected_store = TransactionManagerPendingLifecycleTestAccess::StateStore( consensus_ );
    auto expected_slot = mint->GetSlotID();
    ASSERT_TRUE( expected_store && expected_slot );
    const auto input = mint->GetUTXOParameters().first.front();
    ConsensusStateStore::BurnOutpoint outpoint{
        mint->GetChainId(), input.txid_hash_.toReadableString(), input.output_idx_ };
    auto created = expected_store->CreateOrJoinBurnReservation(
        expected_slot.value(), outpoint, std::numeric_limits<uint64_t>::max(), 1U );
    ASSERT_TRUE( created );
    auto finalized = expected_store->FinalizeBurnReservation(
        expected_slot.value(), outpoint, std::string( 64, '2' ),
        std::string( 64, '3' ), mint->GetHash(), 2U );
    ASSERT_TRUE( finalized );
    ConsensusStateStore::FinalizedReservationIdentity identity{
        expected_slot.value(),
        outpoint, finalized.value().generation(), finalized.value().certificate_digest(),
        finalized.value().proposal_id(), finalized.value().winner_id() };
    std::atomic<uint64_t> observed{ 0 };
    TransactionManagerPendingLifecycleTestAccess::ObserveFinalizedHandle(
        [&]( const ConsensusManager::FinalizedReservationApplicationHandle *handle )
        {
            ASSERT_NE( handle, nullptr );
            EXPECT_EQ( handle->store.get(), expected_store.get() );
            EXPECT_EQ( handle->identity.slot_id, expected_slot.value() );
            ++observed;
        } );
    auto applied = handler(
        mint->GetHash(), ConsensusCertificate{},
        ConsensusManager::FinalizedReservationApplicationHandle{ expected_store, std::move( identity ) } );
    TransactionManagerPendingLifecycleTestAccess::ResetFinalizedHandleObserver();
    ASSERT_TRUE( applied );
    EXPECT_EQ( applied.value(), ConsensusManager::ApplicationDisposition::Applied );
    EXPECT_EQ( observed.load(), 1U );
    auto consumed = expected_store->GetBurnReservation( expected_slot.value() );
    ASSERT_TRUE( consumed && consumed.value() );
    EXPECT_EQ( consumed.value()->state(),
               ConsensusStateStore::BurnReservationRecord::CONSUMED );
    auto application = TransactionManagerPendingLifecycleTestAccess::Application(
        *manager_, mint->GetChainId(), input.txid_hash_, input.output_idx_ );
    ASSERT_TRUE( application && application.value() );
    EXPECT_TRUE( account_->GetUTXOManager().IsOutPointConsumed(
        input.txid_hash_, input.output_idx_ ) );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        ConsumedArtifactContradictionMapsToIrreconcilableThroughProductionHandler )
{
    auto mint = PrepareMint( '6' );
    ASSERT_TRUE( mint );
    auto handler = TransactionManagerPendingLifecycleTestAccess::ApplicationHandler( consensus_ );
    auto store = TransactionManagerPendingLifecycleTestAccess::StateStore( consensus_ );
    auto slot = mint->GetSlotID();
    ASSERT_TRUE( handler && store && slot );
    const auto input = mint->GetUTXOParameters().first.front();
    ConsensusStateStore::BurnOutpoint outpoint{
        mint->GetChainId(), input.txid_hash_.toReadableString(), input.output_idx_ };
    auto finalized = store->FinalizeBurnReservation(
        slot.value(), outpoint, std::string( 64, '2' ),
        std::string( 64, '3' ), mint->GetHash(), 2U );
    ASSERT_TRUE( finalized );
    ConsensusStateStore::FinalizedReservationIdentity identity{
        slot.value(), outpoint, finalized.value().generation(),
        finalized.value().certificate_digest(), finalized.value().proposal_id(),
        finalized.value().winner_id() };

    auto applied = handler(
        mint->GetHash(), ConsensusCertificate{},
        ConsensusManager::FinalizedReservationApplicationHandle{ store, identity } );
    ASSERT_TRUE( applied );
    EXPECT_EQ( applied.value(), ConsensusManager::ApplicationDisposition::Applied );
    auto consumed = store->GetBurnReservation( slot.value() );
    ASSERT_TRUE( consumed && consumed.value() );
    ASSERT_EQ( consumed.value()->state(),
               ConsensusStateStore::BurnReservationRecord::CONSUMED );

    TransactionManagerPendingLifecycleTestAccess::SetBridgeApplicationReader(
        *manager_,
        []( const std::shared_ptr<storage::rocksdb> &,
            const crdt::GlobalDB::Buffer & ) -> outcome::result<crdt::GlobalDB::Buffer>
        { return outcome::failure( storage::DatabaseError::NOT_FOUND ); } );
    auto contradiction = handler(
        mint->GetHash(), ConsensusCertificate{},
        ConsensusManager::FinalizedReservationApplicationHandle{ store, identity } );
    TransactionManagerPendingLifecycleTestAccess::ResetBridgeApplicationReader( *manager_ );
    ASSERT_TRUE( contradiction );
    EXPECT_EQ( contradiction.value(),
               ConsensusManager::ApplicationDisposition::Irreconcilable );
    auto still_consumed = store->GetBurnReservation( slot.value() );
    ASSERT_TRUE( still_consumed && still_consumed.value() );
    EXPECT_EQ( still_consumed.value()->state(),
               ConsensusStateStore::BurnReservationRecord::CONSUMED );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        CertifiedMintPersistedArtifactCorruptionRecoversToConsumedSafetyAndNeverRetries )
{
    auto mint = PrepareMint( 'a' );
    ASSERT_TRUE( mint );
    auto certificate = MakeCertifiedMintCertificate( mint );
    ASSERT_TRUE( certificate );
    const auto input = mint->GetUTXOParameters().first.front();
    const auto outpoint = ConsensusStateStore::BurnOutpoint{
        mint->GetChainId(), input.txid_hash_.toReadableString(), input.output_idx_ };
    auto slot = mint->GetSlotID();
    ASSERT_TRUE( slot );
    const auto winner = mint->GetHash();
    std::atomic<uint64_t> initial_handle_calls{ 0 };
    std::atomic<uint64_t> recovery_handle_calls{ 0 };
    std::atomic<uint64_t> cleanup_calls{ 0 };
    TransactionManagerPendingLifecycleTestAccess::ObserveFinalizedHandle(
        [&]( const ConsensusManager::FinalizedReservationApplicationHandle *handle )
        {
            ASSERT_NE( handle, nullptr );
            ++initial_handle_calls;
        } );
    ASSERT_TRUE( consensus_->RegisterProposalCleanupHandler(
        NONCE_SUBJECT_TYPE, [&]( const std::string & ) { ++cleanup_calls; } ) );

    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::Finalize(
                   consensus_, certificate.value() ),
               ConsensusManager::FinalizeResult::Applied );
    EXPECT_EQ( initial_handle_calls.load(), 1U );
    EXPECT_EQ( cleanup_calls.load(), 1U );
    auto store = TransactionManagerPendingLifecycleTestAccess::StateStore( consensus_ );
    ASSERT_TRUE( store );
    auto consumed = store->GetBurnReservation( slot.value() );
    ASSERT_TRUE( consumed && consumed.value() );
    ASSERT_EQ( consumed.value()->state(),
               ConsensusStateStore::BurnReservationRecord::CONSUMED );
    const auto consumed_identity = consumed.value().value();
    auto reciprocal = store->GetBurnReservation( outpoint );
    ASSERT_TRUE( reciprocal && reciprocal.value() );
    EXPECT_EQ( reciprocal.value()->SerializeAsString(),
               consumed_identity.SerializeAsString() );
    auto application = TransactionManagerPendingLifecycleTestAccess::Application(
        *manager_, outpoint.source_chain, input.txid_hash_, input.output_idx_ );
    ASSERT_TRUE( application && application.value() );
    const auto application_bytes = application.value()->canonical_bytes;
    ASSERT_FALSE( application_bytes.empty() );
    auto winner_hash = base::Hash256::fromReadableString( winner );
    ASSERT_TRUE( winner_hash );
    ASSERT_TRUE( account_->GetUTXOManager().GetUnconsumedUTXO(
        winner_hash.value(), 0 ) );
    ASSERT_TRUE( account_->GetUTXOManager().IsOutPointConsumed(
        input.txid_hash_, input.output_idx_ ) );
    const auto utxo_bytes_after_apply =
        TransactionManagerPendingLifecycleTestAccess::RawUTXORecords( *manager_ );
    ASSERT_GE( utxo_bytes_after_apply.size(), 2U );
    auto complete = store->GetProcess( slot.value() );
    ASSERT_TRUE( complete && complete.value() );
    ASSERT_EQ( complete.value()->state(),
               ConsensusStateStore::ProcessRecord::COMPLETE );

    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::RemovePersistedBridgeApplication(
        *manager_, outpoint.source_chain, input.txid_hash_, input.output_idx_ ) );
    auto removed = TransactionManagerPendingLifecycleTestAccess::Application(
        *manager_, outpoint.source_chain, input.txid_hash_, input.output_idx_ );
    ASSERT_TRUE( removed && !removed.value() );
    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::RestoreCommittedApplicationCrashBoundary(
        consensus_, db_, slot.value() ) );
    EXPECT_TRUE( TransactionManagerPendingLifecycleTestAccess::HasCertificateWork(
        consensus_, slot.value() ) );
    TransactionManagerPendingLifecycleTestAccess::ObserveFinalizedHandle(
        [&]( const ConsensusManager::FinalizedReservationApplicationHandle *handle )
        {
            ASSERT_NE( handle, nullptr );
            ++recovery_handle_calls;
        } );

    RebuildConsensusWithoutTransactionManager();
    auto waiter = MakePendingProposal( std::string( 64, 'e' ), 101 );
    ASSERT_TRUE( waiter );
    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::AddCertificateWaiter(
        consensus_, waiter.value(), winner ) );
    std::atomic<uint64_t> recovery_cleanup_calls{ 0 };
    ASSERT_TRUE( consensus_->RegisterProposalCleanupHandler(
        NONCE_SUBJECT_TYPE, [&]( const std::string & ) { ++recovery_cleanup_calls; } ) );
    CreateProductionTransactionManager();

    EXPECT_EQ( recovery_handle_calls.load(), 1U );
    EXPECT_EQ( recovery_cleanup_calls.load(), 0U );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::CertificateWaiterCount(
                   consensus_, winner ), 1U );
    store = TransactionManagerPendingLifecycleTestAccess::StateStore( consensus_ );
    auto terminal = store->GetBurnReservation( slot.value() );
    ASSERT_TRUE( terminal && terminal.value() );
    ASSERT_EQ( terminal.value()->state(),
               ConsensusStateStore::BurnReservationRecord::CONSUMED_SAFETY_ERROR );
    const auto terminal_identity = terminal.value().value();
    EXPECT_EQ( terminal_identity.slot_id(), consumed_identity.slot_id() );
    EXPECT_EQ( terminal_identity.source_chain(), consumed_identity.source_chain() );
    EXPECT_EQ( terminal_identity.burn_hash(), consumed_identity.burn_hash() );
    EXPECT_EQ( terminal_identity.receipt_log_index(), consumed_identity.receipt_log_index() );
    EXPECT_EQ( terminal_identity.generation(), consumed_identity.generation() );
    EXPECT_EQ( terminal_identity.certificate_digest(), consumed_identity.certificate_digest() );
    EXPECT_EQ( terminal_identity.proposal_id(), consumed_identity.proposal_id() );
    EXPECT_EQ( terminal_identity.winner_id(), consumed_identity.winner_id() );
    reciprocal = store->GetBurnReservation( outpoint );
    ASSERT_TRUE( reciprocal && reciprocal.value() );
    EXPECT_EQ( reciprocal.value()->SerializeAsString(),
               terminal_identity.SerializeAsString() );
    auto pending = store->GetProcess( slot.value() );
    ASSERT_TRUE( pending && pending.value() );
    EXPECT_NE( pending.value()->state(), ConsensusStateStore::ProcessRecord::COMPLETE );
    EXPECT_FALSE( TransactionManagerPendingLifecycleTestAccess::HasCertificateWork(
        consensus_, slot.value() ) );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::RawUTXORecords( *manager_ ),
               utxo_bytes_after_apply );

    RebuildConsensusWithoutTransactionManager();
    auto second_waiter = MakePendingProposal( std::string( 64, 'f' ), 102 );
    ASSERT_TRUE( second_waiter );
    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::AddCertificateWaiter(
        consensus_, second_waiter.value(), winner ) );
    std::atomic<uint64_t> later_cleanup_calls{ 0 };
    ASSERT_TRUE( consensus_->RegisterProposalCleanupHandler(
        NONCE_SUBJECT_TYPE, [&]( const std::string & ) { ++later_cleanup_calls; } ) );
    CreateProductionTransactionManager();
    TransactionManagerPendingLifecycleTestAccess::RecoverCertificateWork( consensus_ );
    TransactionManagerPendingLifecycleTestAccess::ReconcileBurnReservations( consensus_ );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::Finalize(
                   consensus_, certificate.value() ),
               ConsensusManager::FinalizeResult::AlreadyFinalized );

    EXPECT_EQ( recovery_handle_calls.load(), 1U );
    EXPECT_EQ( later_cleanup_calls.load(), 0U );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::CertificateWaiterCount(
                   consensus_, winner ), 1U );
    store = TransactionManagerPendingLifecycleTestAccess::StateStore( consensus_ );
    auto after_restart = store->GetBurnReservation( slot.value() );
    ASSERT_TRUE( after_restart && after_restart.value() );
    EXPECT_EQ( after_restart.value()->SerializeAsString(),
               terminal_identity.SerializeAsString() );
    reciprocal = store->GetBurnReservation( outpoint );
    ASSERT_TRUE( reciprocal && reciprocal.value() );
    EXPECT_EQ( reciprocal.value()->SerializeAsString(),
               terminal_identity.SerializeAsString() );
    removed = TransactionManagerPendingLifecycleTestAccess::Application(
        *manager_, outpoint.source_chain, input.txid_hash_, input.output_idx_ );
    ASSERT_TRUE( removed && !removed.value() );
    EXPECT_TRUE( account_->GetUTXOManager().GetUnconsumedUTXO(
        winner_hash.value(), 0 ) );
    EXPECT_TRUE( account_->GetUTXOManager().IsOutPointConsumed(
        input.txid_hash_, input.output_idx_ ) );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::RawUTXORecords( *manager_ ),
               utxo_bytes_after_apply );
    pending = store->GetProcess( slot.value() );
    ASSERT_TRUE( pending && pending.value() );
    EXPECT_NE( pending.value()->state(), ConsensusStateStore::ProcessRecord::COMPLETE );
    EXPECT_TRUE( store->DeleteReservedBurnReservation(
        slot.value(), terminal_identity.generation() ).has_error() );
    EXPECT_TRUE( store->CreateOrJoinBurnReservation(
        slot.value(), outpoint, std::numeric_limits<uint64_t>::max(), 1U ).has_error() );
    EXPECT_TRUE( store->FinalizeBurnReservation(
        slot.value(), outpoint, terminal_identity.certificate_digest(),
        terminal_identity.proposal_id(), std::string( 64, '9' ), 2U ).has_error() );
    EXPECT_EQ( application_bytes.size(), application.value()->canonical_bytes.size() );
    TransactionManagerPendingLifecycleTestAccess::ResetFinalizedHandleObserver();
}

TEST_F( TransactionManagerPendingLifecycleTest, FinalizedHandleMalformedEmbeddedMintIsIrreconcilable )
{
    auto mint = PrepareMint( '8' );
    ASSERT_TRUE( mint );
    auto authoritative = MakeCertificateWithEmbedded(
        mint->GetHash(), mint->GetNonce(), mint->SerializeToEmbeddedTransaction( mint->dag_st ) );
    ASSERT_TRUE( authoritative );
    const auto input = mint->GetUTXOParameters().first.front();
    ConsensusStateStore::BurnOutpoint outpoint{
        mint->GetChainId(), input.txid_hash_.toReadableString(), input.output_idx_ };
    auto production_handler =
        TransactionManagerPendingLifecycleTestAccess::ApplicationHandler( consensus_ );
    ASSERT_TRUE( production_handler );

    EmbeddedTransaction malformed_embedded;
    auto *malformed_mint = malformed_embedded.mutable_mint_v2();
    malformed_mint->set_chain_id( "11155111" );
    auto *malformed_input = malformed_mint->mutable_utxo_params()->add_inputs();
    malformed_input->set_tx_id_hash( std::string( 64, 'x' ) );
    malformed_input->set_output_index( kReceiptIndex );
    auto malformed_subject = ConsensusManager::CreateNonceSubject(
        account_->GetAddress(), 91, std::string( 64, 'd' ), malformed_embedded,
        std::nullopt, std::nullopt );
    ASSERT_TRUE( malformed_subject );
    ConsensusManager::Certificate malformed_certificate;
    *malformed_certificate.mutable_proposal()->mutable_subject() = malformed_subject.value();

    TransactionManagerPendingLifecycleTestAccess::RemoveApplicationHandler( consensus_ );
    ASSERT_TRUE( consensus_->RegisterCertificateApplicationHandler(
        NONCE_SUBJECT_TYPE,
        [production_handler, malformed_certificate](
            const std::string &,
            const ConsensusManager::Certificate &,
            ConsensusManager::FinalizedReservationApplicationHandle handle ) mutable
            -> outcome::result<ConsensusManager::ApplicationDisposition>
        {
            return production_handler(
                std::string( 64, 'd' ), malformed_certificate, std::move( handle ) );
        } ) );
    std::atomic<uint64_t> cleanup_count{ 0 };
    ASSERT_TRUE( consensus_->RegisterProposalCleanupHandler(
        NONCE_SUBJECT_TYPE, [&]( const std::string & ) { ++cleanup_count; } ) );

    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::DriveFinalizedApplication(
                   consensus_, authoritative.value(), outpoint ),
               ConsensusManager::FinalizeResult::AlreadyFinalized );
    EXPECT_EQ( cleanup_count.load(), 0U );
    auto slot = mint->GetSlotID();
    ASSERT_TRUE( slot );
    auto protected_burn = TransactionManagerPendingLifecycleTestAccess::StateStore( consensus_ )
                              ->GetBurnReservation( slot.value() );
    ASSERT_TRUE( protected_burn && protected_burn.value() );
    EXPECT_EQ( protected_burn.value()->state(),
               ConsensusStateStore::BurnReservationRecord::SAFETY_ERROR );
}

TEST_F( TransactionManagerPendingLifecycleTest, FinalizedHandleHashMismatchIsIrreconcilable )
{
    auto mint = PrepareMint( '9' );
    ASSERT_TRUE( mint );
    auto authoritative = MakeCertificateWithEmbedded(
        mint->GetHash(), mint->GetNonce(), mint->SerializeToEmbeddedTransaction( mint->dag_st ) );
    ASSERT_TRUE( authoritative );
    const auto input = mint->GetUTXOParameters().first.front();
    ConsensusStateStore::BurnOutpoint outpoint{
        mint->GetChainId(), input.txid_hash_.toReadableString(), input.output_idx_ };
    auto production_handler =
        TransactionManagerPendingLifecycleTestAccess::ApplicationHandler( consensus_ );
    ASSERT_TRUE( production_handler );
    const std::string mismatched_hash( 64, 'e' );
    auto mismatched_subject = ConsensusManager::CreateNonceSubject(
        account_->GetAddress(), mint->GetNonce(), mismatched_hash,
        mint->SerializeToEmbeddedTransaction( mint->dag_st ), std::nullopt, std::nullopt );
    ASSERT_TRUE( mismatched_subject );
    ConsensusManager::Certificate mismatched_certificate;
    *mismatched_certificate.mutable_proposal()->mutable_subject() = mismatched_subject.value();
    TransactionManagerPendingLifecycleTestAccess::RemoveApplicationHandler( consensus_ );
    ASSERT_TRUE( consensus_->RegisterCertificateApplicationHandler(
        NONCE_SUBJECT_TYPE,
        [production_handler, mismatched_certificate, mismatched_hash](
            const std::string &,
            const ConsensusManager::Certificate &,
            ConsensusManager::FinalizedReservationApplicationHandle handle ) mutable
            -> outcome::result<ConsensusManager::ApplicationDisposition>
        {
            return production_handler(
                mismatched_hash, mismatched_certificate, std::move( handle ) );
        } ) );
    std::atomic<uint64_t> cleanup_count{ 0 };
    ASSERT_TRUE( consensus_->RegisterProposalCleanupHandler(
        NONCE_SUBJECT_TYPE, [&]( const std::string & ) { ++cleanup_count; } ) );

    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::DriveFinalizedApplication(
                   consensus_, authoritative.value(), outpoint ),
               ConsensusManager::FinalizeResult::AlreadyFinalized );
    EXPECT_EQ( cleanup_count.load(), 0U );
    auto slot = mint->GetSlotID();
    ASSERT_TRUE( slot );
    auto protected_burn = TransactionManagerPendingLifecycleTestAccess::StateStore( consensus_ )
                              ->GetBurnReservation( slot.value() );
    ASSERT_TRUE( protected_burn && protected_burn.value() );
    EXPECT_EQ( protected_burn.value()->state(),
               ConsensusStateStore::BurnReservationRecord::SAFETY_ERROR );
}

TEST_F( TransactionManagerPendingLifecycleTest, LegacyFallbackWithoutFinalizedHandleRetainsCompatibility )
{
    const std::string legacy_hash( 64, 'f' );
    auto certificate = MakeCertificate( legacy_hash, 92 );
    ASSERT_TRUE( certificate );
    auto disposition = TransactionManagerPendingLifecycleTestAccess::OnCertificateWithoutHandle(
        *manager_, legacy_hash, certificate.value() );
    ASSERT_TRUE( disposition );
    EXPECT_EQ( disposition.value(), ConsensusManager::ApplicationDisposition::Applied );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        MintFundsExecutedReadErrorsFailClosedBeforeMutation )
{
    ASSERT_NE( manager_, nullptr );
    for ( const auto error :
          { storage::DatabaseError::CORRUPTION, storage::DatabaseError::IO_ERROR } )
    {
        const std::string burn_hash(
            64, error == storage::DatabaseError::CORRUPTION ? 'b' : 'c' );
        const auto parsed_hash = base::Hash256::fromReadableString( burn_hash );
        ASSERT_TRUE( parsed_hash.has_value() );
        const auto queue_before =
            TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ );

        TransactionManagerPendingLifecycleTestAccess::SetBridgeApplicationReader(
            *manager_,
            [error]( const std::shared_ptr<storage::rocksdb> &,
                     const crdt::GlobalDB::Buffer & )
                -> outcome::result<crdt::GlobalDB::Buffer>
            {
                return outcome::failure( error );
            } );
        auto result = TransactionManagerPendingLifecycleTestAccess::MintFunds(
            *manager_,
            kAmount,
            burn_hash,
            "11155111",
            kReceiptIndex,
            kTokenId,
            account_->GetAddress() );
        TransactionManagerPendingLifecycleTestAccess::ResetBridgeApplicationReader(
            *manager_ );

        ASSERT_TRUE( result.has_error() );
        EXPECT_EQ( result.error(), error );
        EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ ),
                   queue_before );
        EXPECT_FALSE( account_->GetUTXOManager()
                          .GetOutPointState( parsed_hash.value(), kReceiptIndex )
                          .has_value() );
        EXPECT_TRUE( ExecutedKeyAbsent( "11155111", burn_hash ) );
    }
}

TEST_F( TransactionManagerPendingLifecycleTest,
        MintFundsExecutedReadNotFoundAllowsIndexedMint )
{
    ASSERT_NE( manager_, nullptr );
    const std::string burn_hash( 64, 'd' );
    const auto queue_before =
        TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ );
    TransactionManagerPendingLifecycleTestAccess::SetBridgeApplicationReader(
        *manager_,
        []( const std::shared_ptr<storage::rocksdb> &,
            const crdt::GlobalDB::Buffer & )
            -> outcome::result<crdt::GlobalDB::Buffer>
        {
            return outcome::failure( storage::DatabaseError::NOT_FOUND );
        } );
    auto result = TransactionManagerPendingLifecycleTestAccess::MintFunds(
        *manager_,
        kAmount,
        burn_hash,
        "11155111",
        kReceiptIndex,
        kTokenId,
        account_->GetAddress() );
    TransactionManagerPendingLifecycleTestAccess::ResetBridgeApplicationReader( *manager_ );

    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ ),
               queue_before + 1 );
    auto mint = TransactionManagerPendingLifecycleTestAccess::LastQueuedMint( *manager_ );
    ASSERT_NE( mint, nullptr );
    ASSERT_EQ( mint->GetUTXOParameters().first.size(), 1U );
    EXPECT_EQ( mint->GetUTXOParameters().first.front().output_idx_, kReceiptIndex );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        BridgeBurnStateDistinguishesReservedConsumedAndPersisted )
{
    ASSERT_NE( manager_, nullptr );
    auto &utxo_manager = account_->GetUTXOManager();

    const std::string reserved_hash_text( 64, 'e' );
    auto reserved_hash = base::Hash256::fromReadableString( reserved_hash_text );
    ASSERT_TRUE( reserved_hash.has_value() );
    GeniusUTXO reserved_utxo(
        reserved_hash.value(), kReceiptIndex, kAmount, kTokenId, account_->GetAddress() );
    ASSERT_TRUE( utxo_manager
                     .PutUTXO( reserved_utxo,
                               account_->GetAddress(),
                               UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );
    auto reserved_inputs = account_->CreateInputsFromUTXOs( { reserved_utxo } );
    utxo_manager.ReserveUTXOs(
        reserved_inputs, reserved_hash_text, UTXOManager::UTXOType::UTXO_BRIDGE );
    auto reserved = TransactionManagerPendingLifecycleTestAccess::GetBridgeBurnState(
        *manager_, "11155111", reserved_hash_text, kReceiptIndex );
    ASSERT_TRUE( reserved.has_value() );
    EXPECT_EQ( reserved.value(), TransactionManager::BridgeBurnState::Reserved );

    const std::string consumed_hash_text( 64, 'f' );
    auto consumed_hash = base::Hash256::fromReadableString( consumed_hash_text );
    ASSERT_TRUE( consumed_hash.has_value() );
    GeniusUTXO consumed_utxo(
        consumed_hash.value(), kReceiptIndex, kAmount, kTokenId, account_->GetAddress() );
    ASSERT_TRUE( utxo_manager
                     .PutUTXO( consumed_utxo,
                               account_->GetAddress(),
                               UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );
    auto consumed_inputs = account_->CreateInputsFromUTXOs( { consumed_utxo } );
    ASSERT_TRUE( utxo_manager
                     .ConsumeUTXOs( consumed_inputs,
                                    account_->GetAddress(),
                                    UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );
    auto consumed = TransactionManagerPendingLifecycleTestAccess::GetBridgeBurnState(
        *manager_, "11155111", consumed_hash_text, kReceiptIndex );
    ASSERT_TRUE( consumed.has_error() );
    EXPECT_EQ( consumed.error(), std::make_error_code( std::errc::state_not_recoverable ) );

    const std::string persisted_hash( 64, '1' );
    crdt::GlobalDB::Buffer key;
    key.put( "/bridge/executed/11155111:" + persisted_hash + ":" +
             std::to_string( kReceiptIndex ) );
    crdt::GlobalDB::Buffer value;
    value.put( "executed" );
    ASSERT_TRUE( db_->GetDataStore()->put( key, value ).has_value() );
    auto persisted = TransactionManagerPendingLifecycleTestAccess::GetBridgeBurnState(
        *manager_, "11155111", persisted_hash, kReceiptIndex );
    ASSERT_TRUE( persisted.has_value() );
    EXPECT_EQ( persisted.value(), TransactionManager::BridgeBurnState::Available );

    const std::string durable_hash_text( 64, '2' );
    auto durable_hash = base::Hash256::fromReadableString( durable_hash_text );
    ASSERT_TRUE( durable_hash );
    SGTransaction::DAGStruct dag;
    dag.set_source_addr( account_->GetAddress() );
    dag.set_uncle_hash( durable_hash_text );
    auto durable_mint = MintTransactionV2::New(
        kAmount, "11155111", kTokenId, dag,
        { InputUTXOInfo{ durable_hash.value(), kReceiptIndex, {} } }, account_->GetAddress() );
    auto durable_slot = durable_mint.GetSlotID();
    ASSERT_TRUE( durable_slot );
    ConsensusStateStore reservation_store( db_->GetDataStore() );
    ASSERT_TRUE( reservation_store.CreateOrJoinBurnReservation(
        durable_slot.value(), { "11155111", durable_hash_text, kReceiptIndex },
        std::numeric_limits<uint64_t>::max(), 1U ) );
    auto durable = TransactionManagerPendingLifecycleTestAccess::GetBridgeBurnState(
        *manager_, "11155111", durable_hash_text, kReceiptIndex );
    ASSERT_TRUE( durable );
    EXPECT_EQ( durable.value(), TransactionManager::BridgeBurnState::Reserved );

    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::QueueSize( *manager_ ),
               0U );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        MintConfirmationProducedEffectFailureIsRetryable )
{
    using Stage = TransactionManagerPendingLifecycleTestAccess::UTXOFaultStage;
    auto mint = PrepareMint( '2' );
    ASSERT_NE( mint, nullptr );
    auto winner = base::Hash256::fromReadableString( mint->GetHash() );
    ASSERT_TRUE( winner.has_value() );
    const auto input = mint->GetUTXOParameters().first.front();

    TransactionManagerPendingLifecycleTestAccess::SetUTXOFault(
        *manager_,
        []( Stage stage ) -> outcome::result<void>
        {
            if ( stage == Stage::ProducedOutputStage )
            {
                return outcome::failure( storage::DatabaseError::IO_ERROR );
            }
            return outcome::success();
        } );
    auto failed = TransactionManagerPendingLifecycleTestAccess::Confirm( *manager_, mint );
    TransactionManagerPendingLifecycleTestAccess::ResetUTXOFault( *manager_ );
    ASSERT_TRUE( failed.has_error() );
    EXPECT_EQ( failed.error(), storage::DatabaseError::IO_ERROR );
    auto status = TransactionManagerPendingLifecycleTestAccess::Status( *manager_, *mint );
    EXPECT_TRUE( !status || *status != TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_FALSE( account_->GetUTXOManager().GetUnconsumedUTXO( winner.value(), 0 ) );
    EXPECT_NE( account_->GetUTXOManager().GetOutPointState(
                   input.txid_hash_, input.output_idx_ ),
               UTXOManager::UTXOState::UTXO_CONSUMED );
    EXPECT_TRUE( ExecutedKeyAbsent( "11155111", input.txid_hash_.toReadableString() ) );

    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::Confirm(
                     *manager_, mint ).has_value() );
    EXPECT_TRUE( account_->GetUTXOManager().GetUnconsumedUTXO( winner.value(), 0 ) );
    EXPECT_TRUE( account_->GetUTXOManager().IsOutPointConsumed(
        input.txid_hash_, input.output_idx_ ) );
    EXPECT_FALSE( ExecutedKeyAbsent( "11155111", input.txid_hash_.toReadableString() ) );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        MintConfirmationBridgeInputFailureLeavesNoPartialEffects )
{
    using Stage = TransactionManagerPendingLifecycleTestAccess::UTXOFaultStage;
    auto mint = PrepareMint( '3' );
    ASSERT_NE( mint, nullptr );
    auto winner = base::Hash256::fromReadableString( mint->GetHash() );
    ASSERT_TRUE( winner.has_value() );
    const auto input = mint->GetUTXOParameters().first.front();

    TransactionManagerPendingLifecycleTestAccess::SetUTXOFault(
        *manager_,
        []( Stage stage ) -> outcome::result<void>
        {
            if ( stage == Stage::BridgeInputStage )
            {
                return outcome::failure( storage::DatabaseError::IO_ERROR );
            }
            return outcome::success();
        } );
    auto failed = TransactionManagerPendingLifecycleTestAccess::Confirm( *manager_, mint );
    TransactionManagerPendingLifecycleTestAccess::ResetUTXOFault( *manager_ );
    ASSERT_TRUE( failed.has_error() );
    EXPECT_FALSE( account_->GetUTXOManager().GetUnconsumedUTXO( winner.value(), 0 ) );
    EXPECT_NE( account_->GetUTXOManager().GetOutPointState(
                   input.txid_hash_, input.output_idx_ ),
               UTXOManager::UTXOState::UTXO_CONSUMED );
    EXPECT_TRUE( ExecutedKeyAbsent( "11155111", input.txid_hash_.toReadableString() ) );

    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::Confirm(
                     *manager_, mint ).has_value() );
    EXPECT_TRUE( account_->GetUTXOManager().GetUnconsumedUTXO( winner.value(), 0 ) );
    EXPECT_TRUE( account_->GetUTXOManager().IsOutPointConsumed(
        input.txid_hash_, input.output_idx_ ) );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        DuplicateCertificateReplayDoesNotDoubleApplyMintEffects )
{
    auto mint = PrepareMint( '4' );
    ASSERT_NE( mint, nullptr );
    const auto input = mint->GetUTXOParameters().first.front();
    auto burn = input.txid_hash_;
    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::Confirm(
                     *manager_, mint ).has_value() );
    const auto balance = account_->GetUTXOManager().GetBalance();
    const auto root = account_->GetUTXOManager().ComputeUTXOMerkleRoot();
    const auto metric =
        TransactionManagerPendingLifecycleTestAccess::ConfirmMetric( *manager_ );
    const auto state =
        TransactionManagerPendingLifecycleTestAccess::AccountState(
            *manager_, account_->GetAddress() );
    auto application =
        TransactionManagerPendingLifecycleTestAccess::Application(
            *manager_, "11155111", burn, input.output_idx_ );
    ASSERT_TRUE( application.has_value() && application.value().has_value() );
    const auto bytes = application.value()->canonical_bytes;

    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::Confirm(
                     *manager_, mint ).has_value() );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), balance );
    EXPECT_EQ( account_->GetUTXOManager().ComputeUTXOMerkleRoot(), root );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::ConfirmMetric( *manager_ ),
               metric );
    const auto state_after =
        TransactionManagerPendingLifecycleTestAccess::AccountState(
            *manager_, account_->GetAddress() );
    ASSERT_EQ( state.has_value(), state_after.has_value() );
    if ( state && state_after )
    {
        EXPECT_EQ( state->version, state_after->version );
        EXPECT_EQ( state->root, state_after->root );
    }
    auto application_after =
        TransactionManagerPendingLifecycleTestAccess::Application(
            *manager_, "11155111", burn, input.output_idx_ );
    ASSERT_TRUE( application_after.has_value() &&
                 application_after.value().has_value() );
    EXPECT_EQ( application_after.value()->canonical_bytes, bytes );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        RestartRecoversCommittedAndInterruptedMintApplication )
{
    using UTXOStage = TransactionManagerPendingLifecycleTestAccess::UTXOFaultStage;
    using MintStage = TransactionManagerPendingLifecycleTestAccess::MintFaultStage;

    auto precommit_mint = PrepareMint( '5' );

    ASSERT_NE( precommit_mint, nullptr );
    const auto precommit_input = precommit_mint->GetUTXOParameters().first.front();
    TransactionManagerPendingLifecycleTestAccess::SetUTXOFault(
        *manager_,
        []( UTXOStage stage ) -> outcome::result<void>
        {
            if ( stage == UTXOStage::AtomicMintBeforeBatchCommit )
            {
                return outcome::failure( std::errc::operation_canceled );
            }
            return outcome::success();
        } );
    auto precommit_failure =
        TransactionManagerPendingLifecycleTestAccess::Confirm(
            *manager_, precommit_mint );
    TransactionManagerPendingLifecycleTestAccess::ResetUTXOFault( *manager_ );
    ASSERT_TRUE( precommit_failure.has_error() );
    EXPECT_TRUE( ExecutedKeyAbsent(
        "11155111", precommit_input.txid_hash_.toReadableString() ) );
    RebuildProductionObjects();
    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::Confirm(
                     *manager_, precommit_mint ).has_value() );
    EXPECT_TRUE( account_->GetUTXOManager().IsOutPointConsumed(
        precommit_input.txid_hash_, precommit_input.output_idx_ ) );

    auto mint = PrepareMint( '6' );
    ASSERT_NE( mint, nullptr );
    const auto input = mint->GetUTXOParameters().first.front();
    TransactionManagerPendingLifecycleTestAccess::SetMintFault(
        *manager_,
        []( MintStage ) -> outcome::result<void>
        {
            return outcome::failure( std::errc::operation_canceled );
        } );
    auto interrupted =
        TransactionManagerPendingLifecycleTestAccess::Confirm( *manager_, mint );
    TransactionManagerPendingLifecycleTestAccess::ResetMintFault( *manager_ );
    ASSERT_TRUE( interrupted.has_error() );
    auto status = TransactionManagerPendingLifecycleTestAccess::Status( *manager_, *mint );
    EXPECT_TRUE( !status || *status != TransactionManager::TransactionStatus::CONFIRMED );
    auto application =
        TransactionManagerPendingLifecycleTestAccess::Application(
            *manager_, "11155111", input.txid_hash_, input.output_idx_ );
    ASSERT_TRUE( application.has_value() && application.value().has_value() );

    RebuildProductionObjects();
    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::Confirm(
                     *manager_, mint ).has_value() );
    status = TransactionManagerPendingLifecycleTestAccess::Status( *manager_, *mint );
    ASSERT_TRUE( status.has_value() );
    EXPECT_EQ( *status, TransactionManager::TransactionStatus::CONFIRMED );
    const auto metric =
        TransactionManagerPendingLifecycleTestAccess::ConfirmMetric( *manager_ );
    const auto balance = account_->GetUTXOManager().GetBalance();
    const auto root = account_->GetUTXOManager().ComputeUTXOMerkleRoot();
    RebuildProductionObjects();
    ASSERT_TRUE( TransactionManagerPendingLifecycleTestAccess::Confirm(
                     *manager_, mint ).has_value() );
    EXPECT_EQ( TransactionManagerPendingLifecycleTestAccess::ConfirmMetric( *manager_ ), 1U );
    EXPECT_EQ( account_->GetUTXOManager().GetBalance(), balance );
    EXPECT_EQ( account_->GetUTXOManager().ComputeUTXOMerkleRoot(), root );
    EXPECT_GE( metric, 1U );
}

TEST_F( TransactionManagerPendingLifecycleTest,
        PreviousNonceCertificateLookupPreservesConsumerSemantics )
{
    ASSERT_NE( manager_, nullptr );
    ASSERT_NE( consensus_, nullptr );
    const std::string winner_hash( 64, 'a' );
    auto              certificate = MakeCertificate( winner_hash, 7 );
    ASSERT_TRUE( certificate.has_value() );
    ASSERT_TRUE( consensus_->SubmitCertificate( certificate.value() ).has_value() );

    auto winning_transaction = MakeReplayTransaction( winner_hash, 8 );
    ASSERT_NE( winning_transaction, nullptr );
    auto approved =
        TransactionManagerPendingLifecycleTestAccess::EvaluateReplayProtection(
            *manager_, *winning_transaction );
    EXPECT_EQ( approved.check, ConsensusManager::Check::Approve );
    EXPECT_TRUE( approved.dependencies.empty() );

    const std::string absent_hash( 64, 'b' );
    auto absent_transaction = MakeReplayTransaction( absent_hash, 8 );
    auto pending =
        TransactionManagerPendingLifecycleTestAccess::EvaluateReplayProtection(
            *manager_, *absent_transaction );
    EXPECT_EQ( pending.check, ConsensusManager::Check::Pending );
    ASSERT_EQ( pending.dependencies.size(), 1U );
    EXPECT_EQ(
        pending.dependencies.front().type,
        ConsensusManager::PendingDependencyKey::Type::Certificate );
    EXPECT_EQ( pending.dependencies.front().value, absent_hash );

    FailCertificateReads( storage::DatabaseError::CORRUPTION );
    auto corrupt =
        TransactionManagerPendingLifecycleTestAccess::EvaluateReplayProtection(
            *manager_, *winning_transaction );
    EXPECT_EQ( corrupt.check, ConsensusManager::Check::Reject );
    EXPECT_TRUE( corrupt.dependencies.empty() );

    FailCertificateReads( storage::DatabaseError::IO_ERROR );
    auto unavailable =
        TransactionManagerPendingLifecycleTestAccess::EvaluateReplayProtection(
            *manager_, *winning_transaction );
    EXPECT_EQ( unavailable.check, ConsensusManager::Check::Reject );
    EXPECT_TRUE( unavailable.dependencies.empty() );
    UseRealCertificateReader();
}

TEST_F( TransactionManagerPendingLifecycleTest,
        ProducerUTXOCertificateLookupPreservesConsumerSemantics )
{
    ASSERT_NE( blockchain_, nullptr );
    ASSERT_NE( consensus_, nullptr );
    const std::string producer_hash( 64, 'c' );
    auto witness_case = MakeWitnessCase( producer_hash );
    ASSERT_TRUE( witness_case.has_value() );
    auto certificate = MakeCertificate(
        producer_hash, 7, witness_case.value().producer_commitment );
    ASSERT_TRUE( certificate.has_value() );
    ASSERT_TRUE( consensus_->SubmitCertificate( certificate.value() ).has_value() );

    GeniusInputValidator validator;
    EXPECT_TRUE( validator.ValidateWitness(
        witness_case.value().subject,
        witness_case.value().transaction,
        witness_case.value().parameters,
        blockchain_ ) );

    FailCertificateReads( storage::DatabaseError::NOT_FOUND );
    EXPECT_FALSE( validator.ValidateWitness(
        witness_case.value().subject,
        witness_case.value().transaction,
        witness_case.value().parameters,
        blockchain_ ) );

    FailCertificateReads( storage::DatabaseError::CORRUPTION );
    EXPECT_FALSE( validator.ValidateWitness(
        witness_case.value().subject,
        witness_case.value().transaction,
        witness_case.value().parameters,
        blockchain_ ) );

    FailCertificateReads( storage::DatabaseError::IO_ERROR );
    EXPECT_FALSE( validator.ValidateWitness(
        witness_case.value().subject,
        witness_case.value().transaction,
        witness_case.value().parameters,
        blockchain_ ) );
    UseRealCertificateReader();
}
