/**
 * @file multi_node_finality_fault_test.cpp
 * @brief Persistent four-peer production-route audit for Phase 12.
 */

#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/EscrowTransaction.hpp"
#include "account/EscrowReleaseTransactionV2.hpp"
#include "account/TransferTransaction.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TransactionConsensusHandler.hpp"
#include "account/UTXOMerkle.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/crdt_work_journal.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"

#include <algorithm>
#include <array>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <memory>
#include <map>
#include <iostream>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <vector>

namespace sgns
{
    class MultiNodeFinalityFaultTestAccess
    {
    public:
        /// @brief Reads the configured proposal-attempt lifetime without changing the test clock.
        static std::chrono::milliseconds EscrowProposalTTL( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager->pending_config_.pending_ttl;
        }

        /// @brief Reads the pending-vote lifetime for aged historical certificate data.
        static std::chrono::milliseconds EscrowVoteTTL()
        {
            return ConsensusManager::kPendingVoteQueueTTL;
        }

        /// @brief Exercises the manager-selected local input validator.
        static IInputValidator::WitnessVerdict EscrowAdmission( TransactionManager      &manager,
                                                                const GeniusTransaction &transaction )
        {
            const auto parameters = transaction.GetUTXOParametersOpt();
            if ( !parameters.has_value() )
            {
                return IInputValidator::WitnessVerdict::kInvalid;
            }
            const auto &[chain, validator] = manager.SelectInputValidator( transaction );
            return validator.ValidateTransactionUTXOParameters( transaction,
                                                                parameters.value(),
                                                                transaction.GetSrcAddress(),
                                                                manager.account_m->GetUTXOManager(),
                                                                *manager.blockchain_ );
        }

        /// @brief Replays canonical evidence through the production consensus collaborator.
        static outcome::result<ConsensusManager::Check> EscrowApplyCertificate(
            TransactionManager                  &manager,
            const std::string                   &transaction_hash,
            const ConsensusManager::Certificate &certificate )
        {
            return manager.consensus_m_->OnConsensusCertificate( transaction_hash, certificate );
        }

        /// @brief Checks canonical subject identity independently of signed-byte application evidence.
        static bool EscrowCertificateMatches( const ConsensusManager::Certificate &certificate,
                                              const GeniusTransaction             &transaction )
        {
            return TransactionManager::CertificateMatchesTransaction( certificate, transaction );
        }

        /// @brief Reserves the normal outgoing nonce and predecessor for a known local intent.
        static SGTransaction::DAGStruct EscrowIntentDAG( TransactionManager &manager, const std::string &lock )
        {
            return manager.FillDAGStruct( lock );
        }

        /// @brief Checks the private signed-byte application binding in addition to subject identity.
        static bool EscrowExactHistory( TransactionManager &manager, const GeniusTransaction &transaction )
        {
            return manager.GetCertifiedApplicationEvidence( transaction ).has_value();
        }

        /// @brief Derives recipients from retained complete results using the existing payout owner.
        static outcome::result<std::vector<OutputDestInfo>> EscrowPayoutOutputs(
            const SGProcessing::TaskResult &results,
            uint64_t                        amount,
            const TokenID                  &token )
        {
            return TransactionManager::BuildPayoutOutputs( results,
                                                           amount,
                                                           token,
                                                           TransactionManager::BURN_BASIS_POINTS_DEFAULT );
        }

        /// @brief Retains an old signed local intent in the existing outgoing queue.
        static void EscrowEnqueueIntent( TransactionManager                       &manager,
                                         const std::shared_ptr<GeniusTransaction> &transaction )
        {
            TransactionManager::TransactionBatch batch;
            batch.emplace_back( transaction, std::nullopt );
            manager.EnqueueTransaction( TransactionManager::TransactionItem{ std::move( batch ), nullptr } );
        }

        /// @brief Snapshots queued signed transactions under the queue owner's lock.
        static std::vector<std::shared_ptr<GeniusTransaction>> EscrowQueuedTransactions( TransactionManager &manager )
        {
            std::lock_guard                                 lock( manager.mutex_m );
            std::vector<std::shared_ptr<GeniusTransaction>> result;
            for ( const auto &item : manager.tx_queue_m )
            {
                for ( const auto &entry : item.first )
                {
                    result.push_back( entry.first );
                }
            }
            return result;
        }

        // Same rollback + successful dequeue used by TickOnce, with no timer or
        // publish side effects. The test must establish absence of certification
        // before invoking this seam; this is not a general migration operation.
        static outcome::result<void> EscrowRollbackQueuedIntent( TransactionManager &manager,
                                                                 const std::string  &transaction_hash )
        {
            std::lock_guard lock( manager.mutex_m );
            if ( manager.tx_queue_m.empty() || manager.tx_queue_m.front().first.size() != 1 ||
                 manager.tx_queue_m.front().first.front().first->GetHash() != transaction_hash )
            {
                return outcome::failure( std::errc::invalid_argument );
            }
            auto rolled_back = manager.RollbackTransactions( manager.tx_queue_m.front() );
            if ( rolled_back.has_value() )
            {
                manager.tx_queue_m.pop_front();
            }
            return rolled_back;
        }

        /// @brief Applies real certified fixture producers through the manager's normal effects path.
        static outcome::result<void> Confirm( TransactionManager                       &manager,
                                              const std::shared_ptr<GeniusTransaction> &transaction )
        {
            return manager.ChangeTransactionState( transaction, TransactionManager::TransactionStatus::CONFIRMED );
        }

        /// @brief Reads a payout constructed by PayEscrow without replacing its signed bytes.
        static std::shared_ptr<GeniusTransaction> QueuedPayment( TransactionManager &manager, const std::string &hash )
        {
            std::lock_guard lock( manager.mutex_m );
            for ( const auto &item : manager.tx_queue_m )
            {
                for ( const auto &entry : item.first )
                {
                    if ( entry.first && entry.first->GetHash() == hash )
                    {
                        return entry.first;
                    }
                }
            }
            return nullptr;
        }

        static std::shared_ptr<ConsensusManager> Manager( const std::shared_ptr<Blockchain> &blockchain )
        {
            return blockchain ? blockchain->consensus_manager_ : nullptr;
        }

        static const std::string &ConsensusTopic( const std::shared_ptr<ConsensusManager> &manager )
        {
            static const std::string empty;
            return manager ? manager->consensus_messages_topic_ : empty;
        }

        static uint64_t VotePublications( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.vote_publications;
        }

        static uint64_t CertificateNotificationsReceived( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_notifications_received;
        }

        static uint64_t CertificateNotificationsPublished( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_notification_publications;
        }

        static uint64_t CertificateWriteAttempts( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_write_attempts;
        }

        static uint64_t CertificateWriteSuccesses( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_write_successes;
        }

        static uint64_t AcceptedCertificateReadbacks( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.accepted_certificate_readbacks;
        }

        static uint64_t ActiveVoteReleaseAttempts( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.active_vote_release_attempts;
        }

        static uint64_t ActiveVoteReleaseSuccesses( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.active_vote_release_successes;
        }

        static std::optional<std::string> DurableActiveVoteProposalId( const std::shared_ptr<ConsensusManager> &manager,
                                                                       const std::string                       &slot )
        {
            if ( !manager || !manager->db_ )
            {
                return std::nullopt;
            }
            crdt::GlobalDB::Buffer key;
            key.put( manager->ActiveVoteStorageKey( slot ) );
            const auto stored = manager->db_->GetDataStore()->get( key );
            if ( stored.has_error() )
            {
                return std::nullopt;
            }
            const auto decoded = manager->DecodeActiveVoteRecord( slot, stored.value().toString() );
            return decoded.has_value() ? std::optional<std::string>( decoded.value().proposal.proposal_id() )
                                       : std::nullopt;
        }

        static std::string ActiveVoteStorageKey( const std::shared_ptr<ConsensusManager> &manager,
                                                 const std::string                       &slot )
        {
            return manager ? manager->ActiveVoteStorageKey( slot ) : std::string{};
        }

        static std::optional<std::string> RecoveredActiveVoteProposalId(
            const std::shared_ptr<ConsensusManager> &manager,
            const std::string                       &slot )
        {
            if ( !manager )
            {
                return std::nullopt;
            }
            std::lock_guard lock( manager->proposals_mutex_ );
            const auto      found = manager->active_votes_.find( slot );
            return found == manager->active_votes_.end()
                       ? std::nullopt
                       : std::optional<std::string>( found->second.proposal.proposal_id() );
        }

        static uint64_t MintEffects( const TransactionManager &transactions )
        {
            std::lock_guard lock( transactions.fault_test_mutex_ );
            return transactions.mint_effects_for_test_;
        }

        static bool HasUnfinishedCertificateWork( const std::shared_ptr<ConsensusManager> &manager )
        {
            // CRDTWorkJournal patterns are compiled std::regex (optional<regex> parameter).
            static const std::regex cert_pattern{ manager->CERT_KEY_PATTERN.data(), manager->CERT_KEY_PATTERN.size() };
            return manager && manager->certificate_work_journal_ &&
                   !manager->certificate_work_journal_->ListUnfinished( cert_pattern ).empty();
        }

        static int TrackedTransactionState( const TransactionManager &transactions, const std::string &transaction_id )
        {
            std::shared_lock lock( transactions.tx_mutex_m );
            for ( const auto &[_, tracked] : transactions.tx_processed_m )
            {
                if ( tracked.tx && tracked.tx->GetHash() == transaction_id )
                {
                    return static_cast<int>( tracked.status );
                }
            }
            return -1;
        }

        static std::string SlotKey( const ConsensusManager::Proposal &proposal )
        {
            return ConsensusManager::GetSlotKey( proposal );
        }

        static std::optional<std::string> NonceTransactionHash( const ConsensusManager::Proposal &proposal )
        {
            const auto nonce_subject = ConsensusManager::DecodeNonceSubject( proposal.subject() );
            return nonce_subject.has_value() ? std::optional<std::string>( nonce_subject.value().tx_hash() )
                                             : std::nullopt;
        }

        static std::vector<std::string> OrderedActiveValidators( const std::shared_ptr<ConsensusManager> &manager,
                                                                 const ValidatorRegistry::Registry       &registry )
        {
            return manager->GetOrderedActiveValidators( registry );
        }

        static uint64_t CurrentRound( const std::shared_ptr<ConsensusManager> &manager, uint64_t proposal_timestamp )
        {
            return manager->GetCurrentRound( proposal_timestamp );
        }

        static bool IsCurrentAggregator( const std::shared_ptr<ConsensusManager> &manager,
                                         const ConsensusManager::Proposal        &proposal,
                                         const ValidatorRegistry::Registry       &registry,
                                         uint64_t                                 decision_round = 0 )
        {
            if ( decision_round == 0 )
            {
                return manager->GetAggregatorRole( proposal, registry ) ==
                       ConsensusManager::AggregatorRole::CurrentAggregator;
            }
            // Decision-round variant: same formula GetAggregatorRole applies, but
            // with the snapshot taken at the decision so a post-pause round
            // boundary cannot flip the verdict.
            auto ordered = manager->GetOrderedActiveValidators( registry );
            if ( ordered.empty() )
            {
                return false;
            }
            const auto &address = manager->account_address_;
            if ( std::find( ordered.begin(), ordered.end(), address ) == ordered.end() )
            {
                return false;
            }
            auto     hash = sgns::crypto::sha2_256( proposal.proposal_id().data(), proposal.proposal_id().size() );
            uint64_t base_index = 0;
            for ( size_t i = 0; i < sizeof( uint64_t ) && i < hash.size(); ++i )
            {
                base_index = ( base_index << 8 ) | hash[i];
            }
            base_index       = base_index % ordered.size();
            const auto index = ( base_index + decision_round ) % ordered.size();
            return ordered[index] == address;
        }

        static void ArmActiveVoteBarrier( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->active_vote_persisted_barrier_ = { true, false, false };
        }

        static void ArmAcceptedCertificateBarrier( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->accepted_certificate_barrier_ = { true, false, false };
        }

        static void ArmCertificatePersistedBarrier( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->certificate_persisted_barrier_ = { true, false, false, /*entered_round=*/0 };
        }

        static bool ActiveVoteBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->active_vote_persisted_barrier_.entered;
        }

        static bool AcceptedCertificateBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->accepted_certificate_barrier_.entered;
        }

        static uint64_t CertificatePersistedBarrierDecisionRound( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->certificate_persisted_barrier_.entered_round;
        }

        static bool CertificatePersistedBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->certificate_persisted_barrier_.entered;
        }

        static void ReleaseConsensusBarriers( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->active_vote_persisted_barrier_.released = true;
            manager->certificate_persisted_barrier_.released = true;
            manager->accepted_certificate_barrier_.released  = true;
            manager->fault_test_cv_.notify_all();
        }

        static void ArmMintEffectsBarrier( const std::shared_ptr<TransactionManager> &transactions )
        {
            std::lock_guard lock( transactions->fault_test_mutex_ );
            transactions->mint_effects_barrier_ = { true, false, false };
        }

        static bool MintEffectsBarrierEntered( const std::shared_ptr<TransactionManager> &transactions )
        {
            std::lock_guard lock( transactions->fault_test_mutex_ );
            return transactions->mint_effects_barrier_.entered;
        }

        static void ReleaseMintEffectsBarrier( const std::shared_ptr<TransactionManager> &transactions )
        {
            if ( !transactions )
            {
                return;
            }
            std::lock_guard lock( transactions->fault_test_mutex_ );
            transactions->mint_effects_barrier_.released = true;
            transactions->fault_test_cv_.notify_all();
        }

        /// Direct private ValidateCertificate access (TRUST-01 21-04 section).
        static sgns::ConsensusManager::Check ValidateCertificate( const std::shared_ptr<ConsensusManager> &manager,
                                                                  const ConsensusManager::Certificate     &certificate )
        {
            return manager->ValidateCertificate( certificate );
        }

        /// Number of memoized verdicts the certificate cache holds for one
        /// durable certificate key (/cert/<slot>). Retryable outcomes (Pending /
        /// Stalled) must never leave an entry behind (Pitfall 1).
        static size_t VerdictCacheEntryCount( const std::shared_ptr<ConsensusManager> &manager,
                                              const std::string                       &certificate_key )
        {
            std::lock_guard lock( manager->certificate_verdict_cache_mutex_ );
            return manager->certificate_verdict_cache_.count( certificate_key );
        }

        /// D-01 grandfather probes that found a durable final slot record.
        static uint64_t GrandfatheredFinalityProbes( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager )
            {
                return 0;
            }
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.grandfathered_finality_probes;
        }
    };
} // namespace sgns

namespace
{
    const auto kToken = sgns::TokenID::FromBytes( { 0x00 } );

    class FinalityFaultNetwork : public ::test::CRDTFixture
    {
    protected:
        struct ActiveVoteLifecycleSnapshot
        {
            std::string                storage_key;
            bool                       record_present = false;
            std::string                record_digest;
            std::optional<std::string> decoded_proposal_id;
            bool                       accepted_certificate = false;
        };

        struct Peer
        {
            std::string                                      name;
            uint16_t                                         port = 0;
            std::string                                      root;
            std::shared_ptr<boost::asio::io_context>         io;
            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
            std::shared_ptr<sgns::crdt::GlobalDB>            db;
            std::shared_ptr<sgns::GeniusAccount>             account;
            std::shared_ptr<sgns::Blockchain>                blockchain;
            std::shared_ptr<sgns::TransactionManager>        transactions;
            std::shared_ptr<sgns::ConsensusManager>          consensus;
            std::thread                                      io_thread;
            std::string                                      active_vote_diagnostic_slot;
            std::string                                      active_vote_diagnostic_key;
            std::string                                      certificate_diagnostic_key;
            ActiveVoteLifecycleSnapshot                      after_consensus_manager_close;
            ActiveVoteLifecycleSnapshot                      after_manager_ownership_release;
            ActiveVoteLifecycleSnapshot                      after_same_root_globaldb_reopen_before_manager;

            Peer()                          = default;
            Peer( const Peer & )            = delete;
            Peer &operator=( const Peer & ) = delete;

            Peer( Peer &&other ) noexcept :
                name( std::move( other.name ) ),
                port( other.port ),
                root( std::move( other.root ) ),
                io( std::move( other.io ) ),
                pubsub( std::move( other.pubsub ) ),
                db( std::move( other.db ) ),
                account( std::move( other.account ) ),
                blockchain( std::move( other.blockchain ) ),
                transactions( std::move( other.transactions ) ),
                consensus( std::move( other.consensus ) ),
                io_thread( std::move( other.io_thread ) ),
                active_vote_diagnostic_slot( std::move( other.active_vote_diagnostic_slot ) ),
                active_vote_diagnostic_key( std::move( other.active_vote_diagnostic_key ) ),
                certificate_diagnostic_key( std::move( other.certificate_diagnostic_key ) ),
                after_consensus_manager_close( std::move( other.after_consensus_manager_close ) ),
                after_manager_ownership_release( std::move( other.after_manager_ownership_release ) ),
                after_same_root_globaldb_reopen_before_manager(
                    std::move( other.after_same_root_globaldb_reopen_before_manager ) )
            {
            }

            Peer &operator=( Peer &&other ) noexcept
            {
                if ( this == &other )
                {
                    return *this;
                }
                Stop();
                name                                           = std::move( other.name );
                port                                           = other.port;
                root                                           = std::move( other.root );
                io                                             = std::move( other.io );
                pubsub                                         = std::move( other.pubsub );
                db                                             = std::move( other.db );
                account                                        = std::move( other.account );
                blockchain                                     = std::move( other.blockchain );
                transactions                                   = std::move( other.transactions );
                consensus                                      = std::move( other.consensus );
                io_thread                                      = std::move( other.io_thread );
                active_vote_diagnostic_slot                    = std::move( other.active_vote_diagnostic_slot );
                active_vote_diagnostic_key                     = std::move( other.active_vote_diagnostic_key );
                certificate_diagnostic_key                     = std::move( other.certificate_diagnostic_key );
                after_consensus_manager_close                  = std::move( other.after_consensus_manager_close );
                after_manager_ownership_release                = std::move( other.after_manager_ownership_release );
                after_same_root_globaldb_reopen_before_manager = std::move(
                    other.after_same_root_globaldb_reopen_before_manager );
                return *this;
            }

            static ActiveVoteLifecycleSnapshot Snapshot( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                                                         const std::string                           &storage_key )
            {
                ActiveVoteLifecycleSnapshot snapshot;
                snapshot.storage_key = storage_key;
                if ( !db || storage_key.empty() )
                {
                    return snapshot;
                }
                sgns::crdt::GlobalDB::Buffer key;
                key.put( storage_key );
                const auto stored = db->GetDataStore()->get( key );
                if ( stored.has_error() )
                {
                    return snapshot;
                }
                const auto bytes        = stored.value().toString();
                snapshot.record_present = true;
                snapshot.record_digest  = sgns::crypto::sha2_256( bytes.data(), bytes.size() ).toHex();
                return snapshot;
            }

            ~Peer()
            {
                Stop();
            }

            void PrintCertificateWork( const char *boundary ) const
            {
                if ( !db || certificate_diagnostic_key.empty() )
                {
                    return;
                }
                const auto entry = db->GetWorkJournal()->GetEntry( certificate_diagnostic_key );
                std::cout << "ESCROW_RECOVERY peer=" << name << " boundary=" << boundary
                          << " work=" << ( entry ? static_cast<int>( entry->state ) : -1 )
                          << " attempts=" << ( entry ? entry->attempt_count : 0 )
                          << " updated=" << ( entry ? entry->updated_at_ms : 0 )
                          << " lease=" << ( entry ? entry->lease_until_ms : 0 ) << '\n';
            }

            void Stop() noexcept
            {
                if ( transactions )
                {
                    transactions->Stop();
                }
                transactions.reset();
                if ( blockchain )
                {
                    (void) blockchain->Stop();
                }
                if ( consensus )
                {
                    consensus->Close();
                }
                PrintCertificateWork( "consensus-closed" );
                after_consensus_manager_close = Snapshot( db, active_vote_diagnostic_key );
                consensus.reset();
                blockchain.reset();
                after_manager_ownership_release = Snapshot( db, active_vote_diagnostic_key );
                if ( io )
                {
                    io->stop();
                }
                if ( io_thread.joinable() )
                {
                    io_thread.join();
                }
                /*
                 * Teardown invariant (asio): the io_context owned by GossipPubSub must
                 * outlive every I/O object that touches it. GlobalDB's graphsync chain
                 * (GlobalDB -> CrdtDatastore -> GraphsyncDAGSyncer -> GraphsyncImpl ->
                 * graphsync::Network::host_) co-owns the libp2p host wired from
                 * pubsub->GetHost() at StartPeer, and the account holds db-backed
                 * handles, so BOTH must be reset BEFORE pubsub->Stop(). Otherwise
                 * GossipPubSub::StopImpl destroys m_host/m_context while external
                 * co-owners keep the host alive, and the later ~BasicHost (reverse
                 * member order: transport_manager_ before network_) deregisters
                 * leftover TcpConnections from the freed kqueue reactor — the
                 * teardown SIGSEGV seen in crash reports 2026-08-26..2026-09-02
                 * (e.g. multi_node_finality_fault_test-2026-09-02-075842.ips).
                 * With db and account released first, pubsub->Stop() is the FINAL
                 * host release: StopImpl cleanly closes connections and only then
                 * resets m_host/m_context with no external owners left.
                 */
                db.reset();
                account.reset();
                if ( pubsub )
                {
                    pubsub->Stop();
                }
                pubsub.reset();
                io.reset();
            }
        };

        struct Network
        {
            Peer first;
            Peer second;
            Peer third;
            Peer passive;
        };

        struct MintRecoverySnapshot
        {
            uint64_t    sequence = 0;
            std::string canonical_slot_id;
            std::string winning_transaction_id;
            bool        certificate_present = false;
            bool        exact_binding       = false;
            bool        winner_outpoint     = false;
            bool        bridge_marker       = false;
            bool        journal_unfinished  = false;
            int         tracked_state       = -1;
        };

        class MintRecoveryDiagnostics
        {
        public:
            MintRecoveryDiagnostics( Network                       &network,
                                     const std::string             &slot,
                                     const sgns::MintTransactionV2 &winner,
                                     const sgns::MintTransactionV2 &loser ) :
                network_( network ),
                slot_( slot ),
                winner_( winner ),
                loser_( loser ),
                run_( std::getenv( "P12_07_RUN" ) )
            {
            }

            ~MintRecoveryDiagnostics()
            {
                if ( !run_ || !*run_ )
                {
                    return;
                }
                const auto snapshot  = completed_snapshot_.has_value() ? completed_snapshot_.value() : Capture();
                const auto outcome   = completed_ ? "pass" : "failure";
                const auto diagnosis = completed_ ? Diagnosis{ "none", "complete", "none", 6 } : Classify( snapshot );
                std::cerr << "P12_MINT_MARKER_DIAG run=" << run_ << " outcome=" << outcome
                          << " boundary=" << diagnosis.boundary << " state=" << diagnosis.state
                          << " error=" << diagnosis.error << " sequence=" << diagnosis.sequence << '\n';
            }

            void MarkCompleted()
            {
                completed_snapshot_ = Capture();
                completed_          = true;
            }

        private:
            struct Diagnosis
            {
                const char *boundary;
                const char *state;
                const char *error;
                uint64_t    sequence;
            };

            MintRecoverySnapshot Capture() const
            {
                MintRecoverySnapshot snapshot;
                snapshot.canonical_slot_id      = slot_;
                snapshot.winning_transaction_id = winner_.GetHash();
                snapshot.sequence               = 1;
                snapshot.certificate_present    = network_.first.consensus &&
                                               network_.first.consensus->CheckCertificateForSlot( slot_ );
                snapshot.sequence++;
                if ( snapshot.certificate_present )
                {
                    const auto certificate = network_.first.consensus->GetCertificateBySlot( slot_ );
                    snapshot.exact_binding = certificate.has_value() &&
                                             sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash(
                                                 certificate.value().proposal() ) == winner_.GetHash();
                }
                snapshot.sequence++;
                snapshot.winner_outpoint = HasOnlyWinnerOutput( network_.first, winner_, loser_ );
                snapshot.sequence++;
                snapshot.bridge_marker = HasBridgeMarker( network_.first, winner_ );
                snapshot.sequence++;
                snapshot.journal_unfinished = sgns::MultiNodeFinalityFaultTestAccess::HasUnfinishedCertificateWork(
                    network_.first.consensus );
                snapshot.tracked_state = network_.first.transactions
                                             ? sgns::MultiNodeFinalityFaultTestAccess::TrackedTransactionState(
                                                   *network_.first.transactions,
                                                   winner_.GetHash() )
                                             : -1;
                return snapshot;
            }

            static Diagnosis Classify( const MintRecoverySnapshot &snapshot )
            {
                if ( !snapshot.certificate_present )
                {
                    return { "certificate-readback", "absent", "not-found", 1 };
                }
                if ( !snapshot.exact_binding )
                {
                    return { "exact-transaction-binding", "mismatch", "invalid-binding", 2 };
                }
                if ( !snapshot.winner_outpoint )
                {
                    return { "utxo-outpoint", "missing", "not-found", 3 };
                }
                if ( !snapshot.bridge_marker )
                {
                    return { "bridge-marker-read", "absent", "not-found", 4 };
                }
                if ( snapshot.journal_unfinished ||
                     snapshot.tracked_state !=
                         static_cast<int>( sgns::TransactionManager::TransactionStatus::CONFIRMED ) )
                {
                    return { "journal-tracked-state", "unfinished", "retryable", 5 };
                }
                return { "none", "complete", "none", 6 };
            }

            Network                            &network_;
            const std::string                  &slot_;
            const sgns::MintTransactionV2      &winner_;
            const sgns::MintTransactionV2      &loser_;
            const char                         *run_       = nullptr;
            bool                                completed_ = false;
            std::optional<MintRecoverySnapshot> completed_snapshot_;
        };

        class PublisherReadinessSnapshot
        {
        public:
            explicit PublisherReadinessSnapshot( Network &network ) : network_( network )
            {
            }

            void MarkReady()
            {
                const auto peers      = Peers( network_ );
                successful_diagnosis_ = WithNetworkSnapshot(
                    Describe( peers.front(), "none", "ready", "none", IntendedConnectedness( peers ) ),
                    peers );
                ready_ = true;
            }

            std::array<std::string, 3> FirstFailure() const
            {
                const auto diagnosis = Classify();
                return { diagnosis.boundary, diagnosis.state, diagnosis.error };
            }

        private:
            struct Diagnosis
            {
                std::string boundary;
                std::string state;
                std::string error;
                std::string peer_identity;
                std::string listener;
                std::string root_lifecycle;
                std::string intended_connectedness;
                std::string consensus_mesh;
            };

            static std::string PeerIdentity( const Peer *peer )
            {
                if ( !peer || !peer->pubsub )
                {
                    return "unavailable";
                }
                const auto host = peer->pubsub->GetHost();
                return host ? host->getId().toBase58() : "unavailable";
            }

            static std::string ListenerState( const Peer *peer )
            {
                if ( !peer )
                {
                    return "missing";
                }
                return "port-" + std::to_string( peer->port ) + "-pubsub-" +
                       ( peer->pubsub && peer->pubsub->IsStarted() ? "started" : "stopped" );
            }

            static std::string RootLifecycle( const Peer *peer )
            {
                if ( !peer )
                {
                    return "missing";
                }
                return std::string( boost::filesystem::exists( peer->root ) ? "root-present" : "root-missing" ) +
                       "-io-thread-" + ( peer->io_thread.joinable() ? "joinable" : "not-joinable" );
            }

            static size_t ConsensusMesh( const Peer *peer )
            {
                if ( !peer || !peer->pubsub || !peer->pubsub->IsStarted() || !peer->consensus ||
                     !peer->pubsub->GetHost() )
                {
                    return 0;
                }
                return peer->pubsub->getPeerCount(
                    sgns::MultiNodeFinalityFaultTestAccess::ConsensusTopic( peer->consensus ) );
            }

            static std::string IntendedConnectedness( const std::array<Peer *, 4> &peers )
            {
                std::string connections;
                for ( size_t source_index = 0; source_index < peers.size(); ++source_index )
                {
                    for ( size_t target_index = 0; target_index < peers.size(); ++target_index )
                    {
                        if ( source_index == target_index )
                        {
                            continue;
                        }
                        if ( !connections.empty() )
                        {
                            connections += ',';
                        }
                        const auto *source  = peers[source_index];
                        const auto *target  = peers[target_index];
                        connections        += ( source ? source->name : "missing" ) + std::string( "-to-" ) +
                                       ( target ? target->name : "missing" ) + '-';
                        const auto source_host = source && source->pubsub ? source->pubsub->GetHost() : nullptr;
                        const auto target_host = target && target->pubsub ? target->pubsub->GetHost() : nullptr;
                        const bool connected   = source_host && target_host &&
                                               source_host->connectedness( target_host->getPeerInfo() ) ==
                                                   libp2p::Host::Connectedness::CONNECTED;
                        connections += connected ? "connected" : "not-connected";
                    }
                }
                return connections;
            }

            static Diagnosis Describe( const Peer *peer,
                                       std::string boundary,
                                       std::string state,
                                       std::string error,
                                       std::string intended_connectedness = "not-evaluated" )
            {
                return { std::move( boundary ),
                         std::move( state ),
                         std::move( error ),
                         PeerIdentity( peer ),
                         ListenerState( peer ),
                         RootLifecycle( peer ),
                         std::move( intended_connectedness ),
                         std::to_string( ConsensusMesh( peer ) ) };
            }

            static Diagnosis WithNetworkSnapshot( Diagnosis diagnosis, const std::array<Peer *, 4> &peers )
            {
                diagnosis.peer_identity.clear();
                diagnosis.listener.clear();
                diagnosis.root_lifecycle.clear();
                diagnosis.consensus_mesh.clear();
                for ( const auto *peer : peers )
                {
                    if ( !diagnosis.peer_identity.empty() )
                    {
                        diagnosis.peer_identity  += ',';
                        diagnosis.listener       += ',';
                        diagnosis.root_lifecycle += ',';
                        diagnosis.consensus_mesh += ',';
                    }
                    const auto name           = peer ? peer->name : "missing";
                    diagnosis.peer_identity  += name + "-" + PeerIdentity( peer );
                    diagnosis.listener       += name + "-" + ListenerState( peer );
                    diagnosis.root_lifecycle += name + "-" + RootLifecycle( peer );
                    diagnosis.consensus_mesh += name + "-" + std::to_string( ConsensusMesh( peer ) );
                }
                return diagnosis;
            }

            Diagnosis Classify() const
            {
                const auto peers                  = Peers( network_ );
                const auto intended_connectedness = IntendedConnectedness( peers );
                for ( const auto *peer : peers )
                {
                    if ( !peer )
                    {
                        return WithNetworkSnapshot(
                            Describe( peer, "missing-peer", "absent", "not-found", intended_connectedness ),
                            peers );
                    }
                    if ( !peer->pubsub || !peer->pubsub->IsStarted() )
                    {
                        return WithNetworkSnapshot(
                            Describe( peer, "pubsub-not-started", "stopped", "not-started", intended_connectedness ),
                            peers );
                    }
                    if ( !peer->consensus )
                    {
                        return WithNetworkSnapshot(
                            Describe( peer, "missing-consensus", "absent", "not-found", intended_connectedness ),
                            peers );
                    }
                    if ( !peer->pubsub->GetHost() )
                    {
                        return WithNetworkSnapshot(
                            Describe( peer, "missing-host", "absent", "not-found", intended_connectedness ),
                            peers );
                    }
                    if ( ConsensusMesh( peer ) < 1 )
                    {
                        return WithNetworkSnapshot( Describe( peer,
                                                              "zero-consensus-topic-mesh",
                                                              "zero",
                                                              "no-consensus-neighbor",
                                                              intended_connectedness ),
                                                    peers );
                    }
                }

                std::array<bool, 4> reachable{};
                reachable.front() = true;
                for ( size_t pass = 0; pass < peers.size(); ++pass )
                {
                    for ( size_t source_index = 0; source_index < peers.size(); ++source_index )
                    {
                        if ( !reachable[source_index] )
                        {
                            continue;
                        }
                        for ( size_t target_index = 0; target_index < peers.size(); ++target_index )
                        {
                            if ( reachable[target_index] || source_index == target_index )
                            {
                                continue;
                            }
                            const auto source_host = peers[source_index]->pubsub->GetHost();
                            const auto target_host = peers[target_index]->pubsub->GetHost();
                            if ( source_host->connectedness( target_host->getPeerInfo() ) ==
                                     libp2p::Host::Connectedness::CONNECTED ||
                                 target_host->connectedness( source_host->getPeerInfo() ) ==
                                     libp2p::Host::Connectedness::CONNECTED )
                            {
                                reachable[target_index] = true;
                            }
                        }
                    }
                }

                for ( size_t source_index = 0; source_index < peers.size(); ++source_index )
                {
                    if ( reachable[source_index] )
                    {
                        for ( size_t target_index = 0; target_index < peers.size(); ++target_index )
                        {
                            if ( !reachable[target_index] && source_index != target_index )
                            {
                                return WithNetworkSnapshot( Describe( peers[source_index],
                                                                      "disconnected-intended-peer",
                                                                      "disconnected",
                                                                      "no-intended-peer-link",
                                                                      intended_connectedness ),
                                                            peers );
                            }
                        }
                    }
                }

                return WithNetworkSnapshot( Describe( peers.front(),
                                                      "none",
                                                      "recovered-after-deadline",
                                                      "unknown-first-readiness-boundary",
                                                      intended_connectedness ),
                                            peers );
            }

            Network  &network_;
            bool      ready_ = false;
            Diagnosis successful_diagnosis_;
        };

        class PublisherReadinessObserver
        {
        public:
            explicit PublisherReadinessObserver( Network &network ) : network_( network )
            {
                const char *run_token = std::getenv( "P12_10_RUN_TOKEN" );
                if ( !run_token || !*run_token )
                {
                    run_token = std::getenv( "P12_09_RUN_TOKEN" );
                }
                if ( !run_token || !*run_token )
                {
                    return;
                }
                try
                {
                    const auto executable  = boost::filesystem::canonical( boost::dll::program_location() );
                    fingerprint_.run_token = run_token;
                    fingerprint_.path      = executable.string();
                    fingerprint_.size      = boost::filesystem::file_size( executable );
                    fingerprint_.mtime     = boost::filesystem::last_write_time( executable );
                    fingerprint_.pid       = static_cast<long>( ::getpid() );
                    valid_                 = !fingerprint_.path.empty();
                }
                catch ( const boost::filesystem::filesystem_error & )
                {
                    invalid_reason_ = "fingerprint-unavailable";
                }
            }

            void EmitStart()
            {
                if ( !valid_ || emitted_start_ )
                {
                    return;
                }
                Write( "P12_PUBLISHER_OBSERVER_START " + Header() );
                emitted_start_ = true;
            }

            void MarkReady()
            {
                diagnosis_ = { "none", "ready", "none" };
                ready_     = true;
            }

            void MarkFailure()
            {
                PublisherReadinessSnapshot snapshot( network_ );
                const auto                 diagnosis = snapshot.FirstFailure();
                diagnosis_                           = { diagnosis[0], diagnosis[1], diagnosis[2] };
                readiness_failure_                   = true;
            }

            void MarkUnclassifiedExit()
            {
                if ( readiness_failure_ )
                {
                    return;
                }
                ready_ = false;
                diagnosis_.reset();
            }

            void EmitTerminal( bool released )
            {
                if ( emitted_terminal_ || !emitted_start_ )
                {
                    return;
                }
                emitted_terminal_ = true;
                if ( released && ready_ )
                {
                    Write(
                        "P12_PUBLISHER_OBSERVER_TERMINAL " + Header() +
                        " outcome=pass terminal=complete peer_release=all-four-runtime-handles-released boundary=none state=ready error=none" );
                    return;
                }
                if ( released && diagnosis_.has_value() )
                {
                    Write( "P12_PUBLISHER_OBSERVER_TERMINAL " + Header() +
                           " outcome=failure terminal=complete "
                           "peer_release=all-four-runtime-handles-released boundary=" +
                           diagnosis_->boundary + " state=" + diagnosis_->state + " error=" + diagnosis_->error );
                    return;
                }
                EmitIncomplete( released ? "unclassified-scenario-exit" : "peer-release-unproven" );
            }

            void EmitIncomplete( const std::string &reason )
            {
                if ( !emitted_start_ || incomplete_emitted_ )
                {
                    return;
                }
                incomplete_emitted_ = true;
                Write( "P12_PUBLISHER_OBSERVER_TERMINAL " + Header() +
                       " outcome=incomplete terminal=incomplete peer_release=" +
                       std::string( reason == "peer-release-unproven" ? "unproven" : "released" ) +
                       " boundary=observer-output state=incomplete error=" + reason );
            }

        private:
            struct Fingerprint
            {
                std::string run_token;
                std::string path;
                uintmax_t   size  = 0;
                std::time_t mtime = 0;
                long        pid   = 0;
            };

            struct Diagnosis
            {
                std::string boundary;
                std::string state;
                std::string error;
            };

            static std::string HeaderFor( const Fingerprint &fingerprint )
            {
                std::ostringstream record;
                record << "run_token=" << fingerprint.run_token << " schema=p12-observer-v1 pid=" << fingerprint.pid
                       << " exe_path=" << fingerprint.path << " exe_size=" << fingerprint.size
                       << " exe_mtime=" << fingerprint.mtime;
                return record.str();
            }

            std::string Header() const
            {
                return HeaderFor( fingerprint_ );
            }

            // Synchronized observer emission: scenarios may still print the
            // P12_PUBLISHER_OBSERVER_* telemetry lines when a run token is
            // present; nothing parses them.
            static void Write( const std::string &record )
            {
                static std::mutex writer_mutex;
                std::lock_guard   lock( writer_mutex );
                std::cerr << record << '\n' << std::flush;
            }

            Network                 &network_;
            Fingerprint              fingerprint_;
            std::optional<Diagnosis> diagnosis_;
            bool                     valid_              = false;
            bool                     emitted_start_      = false;
            bool                     emitted_terminal_   = false;
            bool                     incomplete_emitted_ = false;
            bool                     ready_              = false;
            bool                     readiness_failure_  = false;
            std::string              invalid_reason_;
        };

        FinalityFaultNetwork() : CRDTFixture( "multi_node_finality_fault" )
        {
        }

        /// Genesis authority pinned to the first-started peer (TRUST-01; see
        /// StartPeer). Fixture-scoped so restarts keep the original.
        std::string genesis_authority_address_;

        Peer StartPeer( const std::string &name,
                        uint16_t           port,
                        std::string        active_vote_diagnostic_key = {},
                        std::string        certificate_diagnostic_key = {} )
        {
            Peer peer;
            peer.name                       = name;
            peer.port                       = port;
            peer.active_vote_diagnostic_key = std::move( active_vote_diagnostic_key );
            peer.certificate_diagnostic_key = std::move( certificate_diagnostic_key );
            peer.root                       = ( base_path / name ).string();
            boost::filesystem::create_directories( peer.root );
            peer.io      = std::make_shared<boost::asio::io_context>();
            auto keypair = sgns::crdt::KeyPairFileStorage( peer.root + "/keypair" ).GetKeyPair();
            EXPECT_TRUE( keypair.has_value() );
            if ( keypair.has_error() )
            {
                return peer;
            }
            peer.pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keypair.value() );
            EXPECT_TRUE( peer.pubsub );
            if ( !peer.pubsub )
            {
                return peer;
            }
            // Advertise the concrete IPv4 address discovered by the fixture's
            // ephemeral listener (wildcard listeners can produce empty PeerInfo
            // addresses and silently skip the certificate broadcast even after
            // gossip peers connected) — but keep the LISTEN on the wildcard: the
            // phase-12 runner's controlled-cancellation gate connects on
            // INADDR_LOOPBACK, and binding only the NIC IP refuses it
            // (socket-gate-not-connected).
            const auto fixture_address = libp2p::multi::Multiaddress::create( pubs_->GetLocalAddress() );
            EXPECT_TRUE( fixture_address.has_value() );
            if ( fixture_address.has_error() )
            {
                return peer;
            }
            const auto bind_address = fixture_address.value().getFirstValueForProtocol(
                libp2p::multi::Protocol::Code::IP4 );
            EXPECT_TRUE( bind_address.has_value() );
            if ( !bind_address.has_value() )
            {
                return peer;
            }
            EXPECT_FALSE(
                peer.pubsub->Start( port, {}, /*bindAddresses=*/"", /*addAddresses=*/{ bind_address.value() } ).get() );
            EXPECT_FALSE( peer.pubsub->GetHost()->getPeerInfo().addresses.empty() );
            // GraphSync writes to libp2p streams from its scheduler thread, and libp2p is
            // single-threaded per host, so the scheduler has to run on the host's
            // io_context. A private one here races yamux's WriteQueue — the same split
            // GeniusNode (405513df5) and MakeSecureCrdtTestNode (6b5ee4aa3) use; this
            // fixture was the remaining unfixed instance. `peer.io` stays the
            // application pool (TransactionManager).
            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( peer.pubsub->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto graphsync = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( peer.pubsub->GetHost(),
                                                                                          scheduler );
            auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
            auto db        = sgns::crdt::GlobalDB::New( peer.io,
                                                 peer.root + "/rocksdb",
                                                 peer.pubsub,
                                                 sgns::crdt::CrdtOptions::DefaultOptions(),
                                                 graphsync,
                                                 scheduler,
                                                 generator );
            EXPECT_TRUE( db.has_value() );
            if ( db.has_error() )
            {
                return peer;
            }
            peer.db = std::move( db.value() );
            peer.PrintCertificateWork( "database-reopened-before-manager" );
            peer.db->Start();
            peer.after_same_root_globaldb_reopen_before_manager = Peer::Snapshot( peer.db,
                                                                                  peer.active_vote_diagnostic_key );
            // The scheduler no longer keeps `peer.io` busy, so without a work guard
            // run() would return before any application work is posted to it.
            peer.io_thread = std::thread(
                [io = peer.io]
                {
                    auto guard = boost::asio::make_work_guard( *io );
                    io->run();
                } );
            peer.account = sgns::GeniusAccount::New( kToken, peer.root + "/account" );
            EXPECT_TRUE( peer.account );
            if ( !peer.account )
            {
                return peer;
            }
            EXPECT_TRUE( peer.account->GetUTXOManager().LoadUTXOs( peer.db->GetDataStore() ).has_value() );
            // TRUST-01 (21-04): ValidateCertificate now roots every
            // non-grandfathered registry load at the genesis authority, so the
            // fixture pins the process-global authorized-full-node address to
            // the FIRST-started peer -- exactly the account StoreRegistry
            // signs with (validators.front()) and the same wiring
            // multi_account_sync / policy_lifetime tests apply. Without the
            // pin every peer's registry constructs with the shipped dev
            // default and the fixture's honest updates classify as
            // unauthorized. The fixture member keeps restarts on the original
            // authority even though the restarted peer builds a fresh account.
            if ( genesis_authority_address_.empty() )
            {
                genesis_authority_address_ = peer.account->GetAddress();
            }
            sgns::Blockchain::SetAuthorizedFullNodeAddress( genesis_authority_address_ );
            peer.blockchain = sgns::Blockchain::New( peer.db,
                                                     peer.account,
                                                     peer.pubsub,
                                                     []( outcome::result<void> ) {} );
            EXPECT_TRUE( peer.blockchain );
            if ( !peer.blockchain )
            {
                return peer;
            }
            peer.transactions = sgns::TransactionManager::New( peer.db,
                                                               peer.io,
                                                               peer.account,
                                                               peer.blockchain,
                                                               sgns::NodeType::Light );
            EXPECT_TRUE( peer.transactions );
            peer.consensus = sgns::MultiNodeFinalityFaultTestAccess::Manager( peer.blockchain );
            EXPECT_TRUE( peer.consensus );
            return peer;
        }

        void StopPeer( Peer &peer )
        {
            peer.Stop();
        }

        void RestartPeer( Peer &peer )
        {
            const auto name                       = peer.name;
            const auto port                       = peer.port;
            const auto active_vote_diagnostic_key = peer.active_vote_diagnostic_key;
            const auto certificate_diagnostic_key = peer.certificate_diagnostic_key;
            StopPeer( peer );
            auto after_consensus_manager_close        = std::move( peer.after_consensus_manager_close );
            auto after_manager_ownership_release      = std::move( peer.after_manager_ownership_release );
            auto restarted = StartPeer( name, port, active_vote_diagnostic_key, certificate_diagnostic_key );
            restarted.after_consensus_manager_close   = std::move( after_consensus_manager_close );
            restarted.after_manager_ownership_release = std::move( after_manager_ownership_release );
            peer                                      = std::move( restarted );
        }

        Network StartNetwork( const std::string &prefix, uint16_t first_port )
        {
            // Per-network authority (TRUST-01): a new network's registries root
            // at ITS first-started peer. Tests with several in-test networks
            // (RestartAtVote's boundary segments) must re-pin, or every later
            // segment's peers would construct registries rooted at the first
            // segment's authority and fail the strict verified walk.
            genesis_authority_address_.clear();
            return { StartPeer( prefix + "-validator-one", first_port ),
                     StartPeer( prefix + "-validator-two", static_cast<uint16_t>( first_port + 1 ) ),
                     StartPeer( prefix + "-validator-three", static_cast<uint16_t>( first_port + 2 ) ),
                     StartPeer( prefix + "-passive", static_cast<uint16_t>( first_port + 3 ) ) };
        }

        static std::array<Peer *, 4> Peers( Network &network )
        {
            return { &network.first, &network.second, &network.third, &network.passive };
        }

        void StoreRegistry( Network &network )
        {
            const auto update = RegistryUpdate( { &network.first, &network.second, &network.third } );
            for ( auto *peer : Peers( network ) )
            {
                ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
            }
            ASSERT_WAIT_FOR_CONDITION(
                [&]
                {
                    return network.first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                           network.second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                           network.third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                           network.passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
                },
                std::chrono::seconds( 5 ),
                "every peer durably stored the validator registry",
                nullptr );
        }

        void RestartAndReconnect( Network &network )
        {
            RestartPeer( network.first );
            RestartPeer( network.second );
            RestartPeer( network.third );
            RestartPeer( network.passive );
            ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                         network.passive.consensus );
            ConnectPeers( Peers( network ) );
        }

        void StopNetwork( Network &network )
        {
            StopPeer( network.first );
            StopPeer( network.second );
            StopPeer( network.third );
            StopPeer( network.passive );
        }

        template <size_t Count>
        static bool PeersFormConnectedTopology( const std::array<Peer *, Count> &peers )
        {
            std::array<bool, Count> reachable{};
            // A connected libp2p topology can still contain disjoint gossip
            // meshes. With these three/four-peer fixtures, this minimum degree
            // proves that every consensus-topic subscriber is in one component.
            for ( auto *peer : peers )
            {
                if ( !peer || !peer->pubsub || !peer->pubsub->IsStarted() || !peer->consensus ||
                     !peer->pubsub->GetHost() ||
                     peer->pubsub->getPeerCount(
                         sgns::MultiNodeFinalityFaultTestAccess::ConsensusTopic( peer->consensus ) ) < Count / 2 )
                {
                    return false;
                }
            }

            reachable.front() = true;
            for ( size_t pass = 0; pass < Count; ++pass )
            {
                for ( size_t source_index = 0; source_index < Count; ++source_index )
                {
                    if ( !reachable[source_index] )
                    {
                        continue;
                    }
                    for ( size_t target_index = 0; target_index < Count; ++target_index )
                    {
                        if ( reachable[target_index] || source_index == target_index )
                        {
                            continue;
                        }
                        const auto source_host = peers[source_index]->pubsub->GetHost();
                        const auto target_host = peers[target_index]->pubsub->GetHost();
                        if ( source_host->connectedness( target_host->getPeerInfo() ) ==
                                 libp2p::Host::Connectedness::CONNECTED ||
                             target_host->connectedness( source_host->getPeerInfo() ) ==
                                 libp2p::Host::Connectedness::CONNECTED )
                        {
                            reachable[target_index] = true;
                        }
                    }
                }
            }
            return std::all_of( reachable.begin(), reachable.end(), []( bool connected ) { return connected; } );
        }

        template <size_t Count>
        static void ConnectAndWaitForPeers( const std::array<Peer *, Count> &peers )
        {
            // Join one source per round, spaced past the 100ms gossip heartbeat.
            // Every outbound dial in this stack binds the node's own LISTEN port as
            // its source port (libp2p RouteHelper::getBestSourceAddresses carries the
            // listener's port into the dial's bind endpoint, and this suite gives
            // every peer a fixed port). When two peers dial each other inside the
            // same heartbeat the crossed SYNs collide on the one shared tuple: the
            // merged connection runs the Noise initiator role on both ends
            // ("Noise handshake failed, Message is too short", CI 2026-09-09), and
            // while that dying socket still owns the tuple any redial of the same
            // pair gets EADDRINUSE -> graphsync CANNOT_CONNECT -> blacklist + route
            // erasure -> "No usable route candidates left" — the exact CI failure
            // signature of SameBurnContention and RestartAtVote. Joining sources
            // sequentially makes every reverse dial find an already-established
            // connection to reuse (DialerImpl reuses connections per peer id), so
            // the colliding simultaneous open never forms.
            for ( auto *source : peers )
            {
                for ( auto *target : peers )
                {
                    if ( source != target )
                    {
                        source->pubsub->AddPeers( { target->pubsub->GetInterfaceAddress() } );
                    }
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
            }
            ASSERT_WAIT_FOR_CONDITION( [&] { return PeersFormConnectedTopology( peers ); },
                                       std::chrono::seconds( 5 ),
                                       "every peer is started in one connected libp2p and consensus-topic topology",
                                       nullptr );
        }

        static void ConnectPeers( const std::array<Peer *, 4> &peers )
        {
            ConnectAndWaitForPeers( peers );
        }

        static void ConnectValidatorPeers( const std::array<Peer *, 3> &peers )
        {
            ConnectAndWaitForPeers( peers );
        }

        static sgns::ValidatorRegistry::RegistryUpdate RegistryUpdate( const std::array<Peer *, 3> &validators )
        {
            sgns::ValidatorRegistry::RegistryUpdate update;
            update.mutable_registry()->set_epoch( 1 );
            for ( auto *peer : validators )
            {
                auto *validator = update.mutable_registry()->add_validators();
                validator->set_validator_id( peer->account->GetAddress() );
                validator->set_weight( 1 );
                validator->set_role( sgns::ValidatorRegistry::Role::REGULAR );
                validator->set_status( sgns::ValidatorRegistry::Status::ACTIVE );
            }
            sgns::validator::RegistrySigningPayload payload;
            *payload.mutable_registry() = update.registry();
            std::string bytes;
            EXPECT_TRUE( payload.SerializeToString( &bytes ) );
            auto  signature = validators.front()->account->Sign( std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            auto *entry     = update.add_signatures();
            entry->set_validator_id( validators.front()->account->GetAddress() );
            entry->set_signature( signature.data(), signature.size() );
            return update;
        }

        // TRUST-01 (21-04) attacker authority: a key that is neither the
        // pinned genesis authority nor a member of the honest registry.
        std::shared_ptr<sgns::GeniusAccount> MakeAttackerAccount( const std::string &name )
        {
            auto attacker = sgns::GeniusAccount::New( kToken, ( base_path / name ).string() );
            EXPECT_TRUE( attacker );
            return attacker;
        }

        // Self-certified registry update (TRUST-01): genesis shape (empty prev
        // link), the attacker as its only validator, signed by the attacker --
        // not rooted in the genesis authority. Raw-put into the DAG at the
        // registry key (bypassing the element filter exactly like a malicious
        // peer supplying the delta would) and return the resulting CID.
        std::string MakeForgedRegistryCid( Peer &peer, const std::shared_ptr<sgns::GeniusAccount> &attacker )
        {
            auto registry = peer.blockchain->GetValidatorRegistry();
            EXPECT_TRUE( registry );
            sgns::ValidatorRegistry::RegistryUpdate forged;
            forged.mutable_registry()->set_epoch( 0 );
            auto *validator_entry = forged.mutable_registry()->add_validators();
            validator_entry->set_validator_id( attacker->GetAddress() );
            validator_entry->set_weight( 1 );
            validator_entry->set_role( sgns::ValidatorRegistry::Role::REGULAR );
            validator_entry->set_status( sgns::ValidatorRegistry::Status::ACTIVE );
            forged.clear_prev_registry_hash();
            sgns::validator::RegistrySigningPayload payload;
            *payload.mutable_registry() = forged.registry();
            std::string payload_bytes;
            EXPECT_TRUE( payload.SerializeToString( &payload_bytes ) );
            auto  signature = attacker->Sign( std::vector<uint8_t>( payload_bytes.begin(), payload_bytes.end() ) );
            auto *signature_entry = forged.add_signatures();
            signature_entry->set_validator_id( attacker->GetAddress() );
            signature_entry->set_signature( signature.data(), signature.size() );

            std::string serialized_update;
            EXPECT_TRUE( forged.SerializeToString( &serialized_update ) );
            sgns::crdt::GlobalDB::Buffer update_buffer;
            update_buffer.put( serialized_update );
            auto put = peer.db->Put( { registry->RegistryKeyValue() },
                                     update_buffer,
                                     { registry->ValidatorTopicValue() } );
            EXPECT_FALSE( put.has_error() );
            if ( put.has_error() )
            {
                return {};
            }
            const auto cid = put.value().toString();
            EXPECT_TRUE( cid.has_value() );
            return cid.has_value() ? cid.value() : std::string{};
        }

        // Fully signed certificate referencing an arbitrary registry CID/epoch
        // (same construction the pending-lifecycle harness uses): the proposal
        // id replicates ConsensusManager::CreateProposalId (hash of the signing
        // bytes with the id cleared) and the vote is the signer's own approve.
        sgns::ConsensusManager::Certificate MakeCertificateForRegistry(
            const std::shared_ptr<sgns::GeniusAccount> &account,
            const std::string                          &registry_cid,
            uint64_t                                    nonce,
            const std::string                          &tx_hash )
        {
            const auto now_ms = static_cast<uint64_t>( std::chrono::duration_cast<std::chrono::milliseconds>(
                                                           std::chrono::system_clock::now().time_since_epoch() )
                                                           .count() );

            auto subject = sgns::ConsensusManager::CreateNonceSubject( account->GetAddress(),
                                                                       nonce,
                                                                       tx_hash,
                                                                       sgns::EmbeddedTransaction{},
                                                                       std::nullopt,
                                                                       std::nullopt );
            EXPECT_TRUE( subject.has_value() );

            sgns::ConsensusManager::Proposal proposal;
            *proposal.mutable_subject() = subject.value();
            proposal.set_proposer_id( account->GetAddress() );
            proposal.set_registry_cid( registry_cid );
            proposal.set_registry_epoch( 0 );
            proposal.set_timestamp( now_ms );
            sgns::ConsensusManager::Proposal id_copy = proposal;
            id_copy.clear_proposal_id();
            auto id_signing = sgns::ConsensusManager::ProposalSigningBytes( id_copy );
            EXPECT_TRUE( id_signing.has_value() );
            auto id_hash = sgns::crypto::sha2_256( id_signing.value().data(), id_signing.value().size() );
            proposal.set_proposal_id(
                sgns::base::hex_lower( gsl::span<const uint8_t>( id_hash.data(), id_hash.size() ) ) );
            auto proposal_signing = sgns::ConsensusManager::ProposalSigningBytes( proposal );
            EXPECT_TRUE( proposal_signing.has_value() );
            auto proposal_signature = account->Sign( proposal_signing.value() );
            proposal.set_signature( proposal_signature.data(), proposal_signature.size() );

            sgns::ConsensusManager::Vote vote;
            vote.set_proposal_id( proposal.proposal_id() );
            vote.set_voter_id( account->GetAddress() );
            vote.set_approve( true );
            vote.set_timestamp( now_ms );
            auto vote_signing = sgns::ConsensusManager::VoteSigningBytes( vote );
            EXPECT_TRUE( vote_signing.has_value() );
            auto vote_signature = account->Sign( vote_signing.value() );
            vote.set_signature( vote_signature.data(), vote_signature.size() );

            sgns::ConsensusManager::Certificate certificate;
            certificate.set_proposal_id( proposal.proposal_id() );
            certificate.set_registry_cid( registry_cid );
            certificate.set_registry_epoch( 0 );
            certificate.set_total_weight( 50000 );
            certificate.set_approved_weight( 50000 );
            certificate.set_timestamp( now_ms );
            *certificate.add_votes()        = vote;
            *certificate.mutable_proposal() = proposal;
            return certificate;
        }

        // Raw durable-record probe on the peer's store (record-path assertion
        // for the adversarial case: not just the validation return value).
        static bool HasDurableCertificateRecord( Peer &peer, const std::string &certificate_key )
        {
            sgns::crdt::GlobalDB::Buffer key;
            key.put( certificate_key );
            const auto stored = peer.db->GetDataStore()->get( key );
            return stored.has_value();
        }

        static std::shared_ptr<sgns::MintTransactionV2> MintFor( const Peer &peer, uint64_t timestamp_offset = 0 )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "mint-v2" );
            dag.set_source_addr( peer.account->GetAddress() );
            dag.set_nonce( 0 );
            dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() )
                                   .count() +
                               timestamp_offset );
            const auto burn_hash = sgns::base::Hash256::fromReadableString( std::string( 64, 'c' ) );
            EXPECT_TRUE( burn_hash.has_value() );
            dag.set_uncle_hash( burn_hash.value().toReadableString() );
            auto mint = std::make_shared<sgns::MintTransactionV2>(
                sgns::MintTransactionV2::New( 42,
                                              "test",
                                              kToken,
                                              std::move( dag ),
                                              { { burn_hash.value(), 0, {} } },
                                              peer.account->GetAddress() ) );
            mint->MakeSignature( *peer.account );
            return mint;
        }

        static sgns::UTXOTransitionCommitment CommitmentFor( const sgns::MintTransactionV2 &mint )
        {
            sgns::UTXOTransitionCommitment    commitment;
            const auto                        params = mint.GetUTXOParameters();
            std::vector<std::vector<uint8_t>> consumed_payloads;
            for ( const auto &input : params.first )
            {
                auto *consumed = commitment.add_consumed_outpoints();
                consumed->set_tx_id_hash( input.txid_hash_.data(), input.txid_hash_.size() );
                consumed->set_output_index( input.output_idx_ );

                std::vector<uint8_t> payload( input.txid_hash_.begin(), input.txid_hash_.end() );
                sgns::utxo_merkle::AppendUInt32BE( payload, input.output_idx_ );
                consumed_payloads.push_back( std::move( payload ) );
            }

            const auto mint_hash = sgns::base::Hash256::fromReadableString( mint.GetHash() );
            EXPECT_TRUE( mint_hash.has_value() );
            std::vector<std::vector<uint8_t>> produced_payloads;
            for ( uint32_t index = 0; index < params.second.size(); ++index )
            {
                const auto      &output = params.second[index];
                sgns::GeniusUTXO produced( mint_hash.value(),
                                           index,
                                           output.encrypted_amount,
                                           output.token_id,
                                           output.dest_address );
                auto            *committed = commitment.add_produced_outputs();
                committed->set_tx_id_hash( mint_hash.value().data(), mint_hash.value().size() );
                committed->set_output_index( index );
                committed->set_owner_address( output.dest_address );
                const auto token_bytes = output.token_id.bytes();
                committed->set_token_id( token_bytes.data(), token_bytes.size() );
                committed->set_amount( output.encrypted_amount );
                produced_payloads.push_back( sgns::utxo_merkle::SerializeUTXOLeafPayload( produced ) );
            }

            const auto consumed_root = sgns::utxo_merkle::ComputeMerkleRootFromPayloads(
                std::move( consumed_payloads ) );
            const auto produced_root = sgns::utxo_merkle::ComputeMerkleRootFromPayloads(
                std::move( produced_payloads ) );
            commitment.set_consumed_outpoints_root( consumed_root.data(), consumed_root.size() );
            commitment.set_produced_outputs_root( produced_root.data(), produced_root.size() );
            return commitment;
        }

        static bool HasBridgeMarker( const Peer &peer, const sgns::MintTransactionV2 &mint )
        {
            sgns::crdt::GlobalDB::Buffer marker_key;
            marker_key.put( "/bridge/executed/" + mint.GetChainId() + ":" + mint.dag_st.uncle_hash() );
            return peer.db && peer.db->GetDataStore()->get( marker_key ).has_value();
        }

        static bool HasOnlyWinnerOutput( const Peer                    &peer,
                                         const sgns::MintTransactionV2 &winner,
                                         const sgns::MintTransactionV2 &loser )
        {
            const auto outputs = peer.account->GetUTXOManager().GetUTXOs( winner.dag_st.source_addr() );
            return outputs.size() == 1 && outputs.front().GetTxID().toReadableString() == winner.GetHash() &&
                   outputs.front().GetTxID().toReadableString() != loser.GetHash();
        }

        static bool HasSingleDurableMint( const Peer                    &peer,
                                          const std::string             &slot,
                                          const sgns::MintTransactionV2 &winner,
                                          const sgns::MintTransactionV2 &loser )
        {
            if ( !peer.consensus || !peer.consensus->CheckCertificateForSlot( slot ) )
            {
                return false;
            }
            const auto certificate = peer.consensus->GetCertificateBySlot( slot );
            return certificate.has_value() &&
                   sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ) ==
                       winner.GetHash() &&
                   HasOnlyWinnerOutput( peer, winner, loser ) && HasBridgeMarker( peer, winner );
        }

        static void AssertSingleDurableMint( const Peer                    &peer,
                                             const std::string             &slot,
                                             const sgns::MintTransactionV2 &winner,
                                             const sgns::MintTransactionV2 &loser )
        {
            ASSERT_TRUE( peer.consensus->CheckCertificateForSlot( slot ) );
            const auto certificate = peer.consensus->GetCertificateBySlot( slot );
            ASSERT_TRUE( certificate.has_value() );
            EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ),
                       winner.GetHash() );
            EXPECT_TRUE( HasOnlyWinnerOutput( peer, winner, loser ) );
            EXPECT_TRUE( HasBridgeMarker( peer, winner ) );
        }

        /// @brief Creates the canonical nonce subject with the manager-owned commitment and witness builders.
        static outcome::result<sgns::ConsensusManager::Proposal> EscrowProposal(
            Peer                          &peer,
            const sgns::GeniusTransaction &transaction )
        {
            sgns::TransactionConsensusHandler handler( *peer.transactions,
                                                       sgns::base::createLogger( "EscrowNetwork" ) );
            BOOST_OUTCOME_TRY(
                auto subject,
                sgns::ConsensusManager::CreateNonceSubject( transaction.GetSrcAddress(),
                                                            transaction.GetNonce(),
                                                            transaction.GetHash(),
                                                            transaction.SerializeToEmbeddedTransaction(),
                                                            handler.BuildUTXOTransitionCommitment( transaction ),
                                                            handler.BuildUTXOWitness( transaction ) ) );
            const auto registry = peer.blockchain->GetValidatorRegistry();
            return peer.consensus->CreateProposal( subject,
                                                   peer.account->GetAddress(),
                                                   registry->GetRegistryCid(),
                                                   registry->GetRegistryEpoch() );
        }

        /// @brief Builds genuine quorum signatures against the configured authorized registry.
        static outcome::result<sgns::ConsensusManager::Certificate> SignedCertificate(
            Peer                          &peer,
            const sgns::GeniusTransaction &transaction,
            const std::vector<Peer *>     &voters,
            std::optional<uint64_t>        timestamp = std::nullopt )
        {
            BOOST_OUTCOME_TRY( auto proposal, EscrowProposal( peer, transaction ) );
            if ( timestamp )
            {
                proposal.set_timestamp( timestamp.value() );
                proposal.clear_proposal_id();
                BOOST_OUTCOME_TRY( auto id_bytes, sgns::ConsensusManager::ProposalSigningBytes( proposal ) );
                const auto hash = sgns::crypto::sha2_256( id_bytes.data(), id_bytes.size() );
                proposal.set_proposal_id(
                    sgns::base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) ) );
                BOOST_OUTCOME_TRY( auto signed_bytes, sgns::ConsensusManager::ProposalSigningBytes( proposal ) );
                const auto signature = peer.account->Sign( std::move( signed_bytes ) );
                proposal.set_signature( signature.data(), signature.size() );
            }
            std::vector<sgns::ConsensusManager::Vote> votes;
            for ( auto *voter : voters )
            {
                BOOST_OUTCOME_TRY(
                    auto vote,
                    voter->consensus->CreateVote( proposal.proposal_id(),
                                                  voter->account->GetAddress(),
                                                  true,
                                                  [account = voter->account]( std::vector<uint8_t> bytes )
                                                  { return account->Sign( std::move( bytes ) ); } ) );
                if ( timestamp )
                {
                    vote.set_timestamp( timestamp.value() );
                    BOOST_OUTCOME_TRY( auto vote_bytes, sgns::ConsensusManager::VoteSigningBytes( vote ) );
                    const auto signature = voter->account->Sign( std::move( vote_bytes ) );
                    vote.set_signature( signature.data(), signature.size() );
                }
                votes.push_back( std::move( vote ) );
            }
            return peer.consensus->CreateCertificate( proposal, votes );
        }

        /// @brief The producer is a signed escrow transaction, never an escrow-release status marker.
        static std::shared_ptr<sgns::EscrowTransaction> EscrowHold( Network &network, uint64_t discriminator = 0 )
        {
            SGTransaction::DAGStruct dag;
            dag.set_source_addr( network.first.account->GetAddress() );
            dag.set_nonce( discriminator );
            dag.set_timestamp( static_cast<int64_t>( std::chrono::duration_cast<std::chrono::milliseconds>(
                                                         std::chrono::system_clock::now().time_since_epoch() )
                                                         .count() ) +
                               static_cast<int64_t>( discriminator ) );
            const auto lock = "0x" + std::string( 64, 'a' );
            dag.set_uncle_hash( lock );
            const auto funding_hash =
                sgns::base::Hash256::fromHex( std::string( 63, 'f' ) + std::to_string( discriminator ) ).value();
            const sgns::GeniusUTXO funding( funding_hash, 0, 10003, kToken, network.first.account->GetAddress() );
            for ( auto *peer : Peers( network ) )
            {
                EXPECT_TRUE(
                    peer->account->GetUTXOManager().PutUTXO( funding, funding.GetOwnerAddress() ).has_value() );
            }
            sgns::InputUTXOInfo input{ funding_hash, 0, {} };
            input.signature_ = network.first.account->Sign( input.SerializeForSigning() );
            auto hold        = std::make_shared<sgns::EscrowTransaction>(
                sgns::EscrowTransaction::New( { { input }, { { 10003, lock, kToken } } }, 10003, std::move( dag ) ) );
            hold->MakeSignature( *network.first.account );
            return hold;
        }

        /// @brief Installs exact signed producer/certificate evidence and applies its real output on each fresh store.
        static void EstablishEscrow( Network &network, const std::shared_ptr<sgns::EscrowTransaction> &hold )
        {
            for ( auto *peer : Peers( network ) )
            {
                const auto certificate = SignedCertificate( *peer,
                                                            *hold,
                                                            { &network.first, &network.second, &network.third } );
                ASSERT_TRUE( certificate.has_value() );
                sgns::base::Buffer bytes;
                bytes.put( hold->SerializeByteVector() );
                ASSERT_TRUE(
                    peer->db->Put( { sgns::TransactionManager::GetTransactionPath( *hold ) }, bytes, {} ).has_value() );
                sgns::base::Buffer cert;
                cert.put( certificate.value().SerializeAsString() );
                ASSERT_TRUE( peer->db->Put( { "/cert/" + hold->GetSlotID() }, cert, {} ).has_value() );
                ASSERT_TRUE( peer->blockchain->GetCertificateBySlot( hold->GetSlotID() ).has_value() );
                ASSERT_TRUE( sgns::MultiNodeFinalityFaultTestAccess::Confirm( *peer->transactions, hold ).has_value() );
            }
        }

        /// @brief Constructs the complete entitled recipient set with the production payout owner.
        static std::shared_ptr<sgns::EscrowReleaseTransactionV2> EscrowPayment( Peer &processor,
                                                                                const sgns::EscrowTransaction &hold,
                                                                                const std::string &recipient,
                                                                                uint64_t = 0 )
        {
            SGProcessing::TaskResult results;
            auto                    *result = results.add_subtask_results();
            result->set_subtaskid( "complete-validated-result" );
            result->set_node_address( recipient );
            result->set_developer_address( hold.GetSrcAddress() );
            result->set_developer_cut( 500000 );
            const auto token = kToken.bytes();
            result->set_token_id( token.data(), token.size() );
            const auto submitted = processor.transactions->PayEscrow(
                sgns::TransactionManager::GetTransactionPath( hold ),
                results,
                nullptr );
            EXPECT_TRUE( submitted.has_value() );
            return submitted.has_value()
                       ? std::dynamic_pointer_cast<sgns::EscrowReleaseTransactionV2>(
                             sgns::MultiNodeFinalityFaultTestAccess::QueuedPayment( *processor.transactions,
                                                                                    submitted.value() ) )
                       : nullptr;
        }

        /// @brief Checks the actual hold spender, every produced outpoint and exact aggregate recipient balances.
        static bool HasEscrowPayment( const Peer                             &peer,
                                      const sgns::EscrowTransaction          &hold,
                                      const sgns::EscrowReleaseTransactionV2 &winner,
                                      const sgns::EscrowReleaseTransactionV2 *loser      = nullptr,
                                      uint64_t                                multiplier = 1 )
        {
            const auto &ledger    = peer.account->GetUTXOManager();
            const auto  hold_hash = sgns::base::Hash256::fromHex( hold.GetHash() ).value();
            const auto  spender   = ledger.GetOutPointSpender( hold_hash, 0 );
            if ( !spender || spender->toReadableString() != winner.GetHash() ||
                 !ledger.IsOutPointGenuinelyConsumed( hold_hash, 0 ) )
            {
                return false;
            }
            std::map<std::string, uint64_t> balances;
            const auto                      outputs = winner.GetProducedUTXOs();
            if ( !outputs )
            {
                return false;
            }
            for ( const auto &expected : *outputs )
            {
                balances[expected.GetOwnerAddress()] += expected.GetAmount();
                const auto actual                     = ledger.GetUTXOs( expected.GetOwnerAddress() );
                if ( std::none_of( actual.begin(),
                                   actual.end(),
                                   [&]( const auto &utxo )
                                   {
                                       return utxo.GetTxID() == expected.GetTxID() &&
                                              utxo.GetOutputIdx() == expected.GetOutputIdx() &&
                                              utxo.GetAmount() == expected.GetAmount() &&
                                              utxo.GetTokenID() == expected.GetTokenID();
                                   } ) ||
                     ( loser && std::any_of( actual.begin(),
                                             actual.end(),
                                             [&]( const auto &utxo )
                                             { return utxo.GetTxID().toReadableString() == loser->GetHash(); } ) ) )
                {
                    return false;
                }
            }
            return std::all_of( balances.begin(),
                                balances.end(),
                                [&]( const auto &entry )
                                { return ledger.GetBalance( kToken, entry.first ) == entry.second * multiplier; } );
        }

        static void PrintEscrowRecovery( Peer                                   &peer,
                                         const char                             *boundary,
                                         const sgns::EscrowTransaction          &hold,
                                         const sgns::EscrowReleaseTransactionV2 &winner )
        {
            peer.certificate_diagnostic_key = "/cert/" + winner.GetSlotID();
            peer.PrintCertificateWork( boundary );
            const auto &ledger = peer.account->GetUTXOManager();
            const auto  hash   = sgns::base::Hash256::fromHex( hold.GetHash() );
            ASSERT_TRUE( hash.has_value() );
            const auto spender = ledger.GetOutPointSpender( hash.value(), 0 );
            std::cout << "ESCROW_LEDGER peer=" << peer.name << " boundary=" << boundary
                      << " spender=" << ( spender ? spender->toReadableString() : "absent" )
                      << " genuine=" << ledger.IsOutPointGenuinelyConsumed( hash.value(), 0 ) << " tracked="
                      << sgns::MultiNodeFinalityFaultTestAccess::TrackedTransactionState( *peer.transactions,
                                                                                          winner.GetHash() )
                      << '\n';
            const auto outputs = winner.GetProducedUTXOs();
            if ( !outputs.has_value() )
            {
                FAIL() << "The canonical winner has no output description";
            }
            for ( const auto &output : outputs.value() )
            {
                const auto actual  = ledger.GetUTXOs( output.GetOwnerAddress() );
                const auto present = std::any_of(
                    actual.begin(),
                    actual.end(),
                    [&]( const auto &utxo )
                    { return utxo.GetTxID() == output.GetTxID() && utxo.GetOutputIdx() == output.GetOutputIdx(); } );
                std::cout << "ESCROW_RECOVERY_OUTPUT peer=" << peer.name << " boundary=" << boundary
                          << " index=" << output.GetOutputIdx() << " present=" << present
                          << " balance=" << ledger.GetBalance( kToken, output.GetOwnerAddress() ) << '\n';
            }
        }

        static void AssertOneLiveMintEffect( const Peer &peer )
        {
            ASSERT_TRUE( peer.transactions );
            EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *peer.transactions ), 1u );
        }
    };
} // namespace

TEST_F( FinalityFaultNetwork, EscrowReleaseSameHoldCrossPeerContentionPaysOnce )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    // Explicit incident-shaped local configurations; deployed staging membership is not inferred.
    struct Membership
    {
        const char             *name;
        std::array<uint64_t, 4> weights;
        size_t                  genesis_count;
    };

    const std::array<Membership, 3> configurations{
        { { "A", { 50000, 1, 1, 1 }, 1 }, { "B", { 50000, 50000, 1, 1 }, 2 }, { "C", { 1, 1, 1, 1 }, 0 } } };
    for ( size_t leg = 0; leg < configurations.size(); ++leg )
    {
        const auto &configuration = configurations[leg];
        SCOPED_TRACE( configuration.name );
        auto                        network = StartNetwork( std::string( "escrow-contention-" ) + configuration.name,
                                     static_cast<uint16_t>( 54801 + leg * 10 ) );
        auto                        fourth  = StartPeer( std::string( "escrow-fourth-" ) + configuration.name,
                                 static_cast<uint16_t>( 54805 + leg * 10 ) );
        const std::array<Peer *, 5> peers{ &network.first, &network.second, &network.third, &fourth, &network.passive };
        sgns::ValidatorRegistry::RegistryUpdate update;
        update.mutable_registry()->set_epoch( 1 );
        for ( size_t index = 0; index < configuration.weights.size(); ++index )
        {
            auto *entry = update.mutable_registry()->add_validators();
            entry->set_validator_id( peers[index]->account->GetAddress() );
            entry->set_weight( configuration.weights[index] );
            entry->set_role( index < configuration.genesis_count ? sgns::ValidatorRegistry::Role::GENESIS
                                                                 : sgns::ValidatorRegistry::Role::REGULAR );
            entry->set_status( sgns::ValidatorRegistry::Status::ACTIVE );
        }
        sgns::validator::RegistrySigningPayload payload;
        *payload.mutable_registry() = update.registry();
        const auto serialized       = payload.SerializeAsString();
        const auto signature        = network.first.account->Sign( { serialized.begin(), serialized.end() } );
        auto      *signer           = update.add_signatures();
        signer->set_validator_id( network.first.account->GetAddress() );
        signer->set_signature( signature.data(), signature.size() );
        for ( auto *peer : peers )
        {
            ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
            ASSERT_WAIT_FOR_CONDITION(
                [&]
                {
                    const auto loaded = peer->blockchain->GetValidatorRegistry()->LoadCurrentRegistry();
                    return loaded.has_value() &&
                           loaded.value().SerializeAsString() == update.registry().SerializeAsString();
                },
                std::chrono::seconds( 5 ),
                "configured authorized membership is active",
                nullptr );
        }
        const auto hold = EscrowHold( network );
        EstablishEscrow( network, hold );
        // The fourth voter receives the same exact producer evidence and real hold output.
        const auto producer_certificate = SignedCertificate( fourth,
                                                             *hold,
                                                             { &network.first, &network.second, &network.third } );
        ASSERT_TRUE( producer_certificate.has_value() );
        sgns::base::Buffer producer_bytes;
        producer_bytes.put( hold->SerializeByteVector() );
        ASSERT_TRUE( fourth.db->Put( { sgns::TransactionManager::GetTransactionPath( *hold ) }, producer_bytes, {} )
                         .has_value() );
        sgns::base::Buffer certificate_bytes;
        certificate_bytes.put( producer_certificate.value().SerializeAsString() );
        ASSERT_TRUE( fourth.db->Put( { "/cert/" + hold->GetSlotID() }, certificate_bytes, {} ).has_value() );
        const auto producer_outputs = hold->GetProducedUTXOs();
        if ( !producer_outputs )
        {
            FAIL() << "Certified hold has no outputs";
        }
        for ( const auto &output : producer_outputs.value() )
        {
            ASSERT_TRUE( fourth.account->GetUTXOManager().PutUTXO( output, output.GetOwnerAddress() ).has_value() );
        }
        const auto first  = EscrowPayment( network.second, *hold, network.passive.account->GetAddress() );
        const auto second = EscrowPayment( network.third, *hold, network.passive.account->GetAddress() );
        ASSERT_TRUE( first && second );
        ASSERT_NE( first->GetSrcAddress(), second->GetSrcAddress() );
        ASSERT_EQ( first->GetSlotID(), second->GetSlotID() );
        ASSERT_NE( first->SerializeByteVector(), second->SerializeByteVector() );
        const auto proposal  = EscrowProposal( network.second, *first );
        const auto contender = EscrowProposal( network.third, *second );
        ASSERT_TRUE( proposal.has_value() && contender.has_value() );
        // Both real submitting peers publish before the real mesh barrier, without creating split votes.
        ASSERT_TRUE( network.second.consensus->SubmitProposal( proposal.value(), false ).has_value() );
        ASSERT_TRUE( network.third.consensus->SubmitProposal( contender.value(), false ).has_value() );
        ASSERT_TRUE( network.second.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_TRUE( network.third.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.third.consensus,
                                                                                            first->GetSlotID() ) ==
                       proposal.value().proposal_id();
            },
            std::chrono::seconds( 5 ),
            "a real validator durably locks the winner before competing admission",
            nullptr );
        ASSERT_TRUE( network.third.consensus->SubmitProposal( contender.value() ).has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.third.consensus,
                                                                                        first->GetSlotID() ),
                   proposal.value().proposal_id() );
        ConnectAndWaitForPeers( peers );
        ASSERT_TRUE( network.second.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&]
                                   { return network.first.consensus->CheckCertificateForSlot( first->GetSlotID() ); },
                                   std::chrono::seconds( 25 ),
                                   "conforming configured voters certify one exact payout",
                                   nullptr );
        const auto first_canonical = network.first.consensus->GetCertificateBySlot( first->GetSlotID() );
        ASSERT_TRUE( first_canonical.has_value() );
        const auto winner_hash = sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash(
            first_canonical.value().proposal() );
        ASSERT_TRUE( winner_hash == first->GetHash() || winner_hash == second->GetHash() );
        const auto  winner          = winner_hash == first->GetHash() ? first : second;
        const auto  loser           = winner_hash == first->GetHash() ? second : first;
        const auto &winner_proposal = winner == first ? proposal.value() : contender.value();
        ASSERT_TRUE( network.third.consensus->SubmitProposal( contender.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return std::all_of( peers.begin(),
                                    peers.end(),
                                    [&]( const auto *peer )
                                    {
                                        return peer->consensus->CheckCertificateForSlot( first->GetSlotID() ) &&
                                               HasEscrowPayment( *peer, *hold, *winner, loser.get() );
                                    } );
            },
            std::chrono::seconds( 25 ),
            "every validator and passive recipient retain one complete payment",
            nullptr );
        for ( auto *peer : peers )
        {
            const auto certificate = peer->consensus->GetCertificateBySlot( first->GetSlotID() );
            ASSERT_TRUE( certificate.has_value() );
            EXPECT_EQ( certificate.value().proposal_id(), first_canonical.value().proposal_id() );
            EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ),
                       winner->GetHash() );
            EXPECT_EQ( certificate.value().proposal().subject().SerializeAsString(),
                       winner_proposal.subject().SerializeAsString() );
            const auto shared_registry = peer->blockchain->GetValidatorRegistry()->LoadVerifiedRegistryByCid(
                certificate.value().registry_cid() );
            ASSERT_TRUE( shared_registry.has_value() );
            EXPECT_EQ( shared_registry.value().SerializeAsString(), update.registry().SerializeAsString() );
            EXPECT_TRUE( peer->blockchain->GetValidatorRegistry()->IsQuorum( certificate.value().approved_weight(),
                                                                             certificate.value().total_weight() ) );
            // Nonmembers may send signed votes; the owning tally excludes their weight.
            EXPECT_TRUE( std::none_of( shared_registry.value().validators().begin(),
                                       shared_registry.value().validators().end(),
                                       [&]( const auto &member )
                                       { return member.validator_id() == network.passive.account->GetAddress(); } ) );
            std::cout << "ESCROW_EVIDENCE membership=" << configuration.name << " peer=" << peer->name
                      << " registry=" << certificate.value().registry_cid()
                      << " epoch=" << certificate.value().registry_epoch() << " hold=" << hold->GetHash()
                      << ":0 slot=" << winner->GetSlotID() << " winner=" << winner->GetHash()
                      << " loser=" << loser->GetHash() << " certificate=" << certificate.value().proposal_id()
                      << " verdict=Approve ledger=once\n";
            const auto outputs = winner->GetProducedUTXOs();
            if ( !outputs )
            {
                FAIL() << "Certified winner has no outputs";
            }
            for ( const auto &output : outputs.value() )
            {
                std::cout << "ESCROW_OUTPUT membership=" << configuration.name << " peer=" << peer->name
                          << " outpoint=" << output.GetTxID().toReadableString() << ':' << output.GetOutputIdx()
                          << " owner=" << output.GetOwnerAddress() << " amount=" << output.GetAmount() << " balance="
                          << peer->account->GetUTXOManager().GetBalance( kToken, output.GetOwnerAddress() ) << '\n';
            }
            if ( peer == &network.first )
            {
                for ( const auto &member : shared_registry.value().validators() )
                {
                    std::cout << "ESCROW_MEMBER membership=" << configuration.name << " id=" << member.validator_id()
                              << " weight=" << member.weight() << " role=" << member.role() << '\n';
                }
                const auto                 hold_bytes   = hold->SerializeByteVector();
                const auto                 winner_bytes = winner->SerializeByteVector();
                const auto                 loser_bytes  = loser->SerializeByteVector();
                const auto                 certified    = certificate.value().SerializeAsString();
                const std::vector<uint8_t> certified_bytes( certified.begin(), certified.end() );
                std::cout << "ESCROW_SIGNED membership=" << configuration.name
                          << " hold=" << sgns::base::hex_lower( gsl::span<const uint8_t>( hold_bytes ) )
                          << " winner=" << sgns::base::hex_lower( gsl::span<const uint8_t>( winner_bytes ) )
                          << " loser=" << sgns::base::hex_lower( gsl::span<const uint8_t>( loser_bytes ) )
                          << " certificate=" << sgns::base::hex_lower( gsl::span<const uint8_t>( certified_bytes ) )
                          << '\n';
            }
        }
        StopPeer( fourth );
        StopNetwork( network );
    }
}

TEST_F( FinalityFaultNetwork, EscrowReleaseDifferentHoldsRemainIndependent )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto network = StartNetwork( "escrow-independent", 54841 );
    StoreRegistry( network );
    const auto first_hold  = EscrowHold( network );
    const auto second_hold = EscrowHold( network, 1 );
    EstablishEscrow( network, first_hold );
    EstablishEscrow( network, second_hold );
    const auto first  = EscrowPayment( network.second, *first_hold, network.passive.account->GetAddress() );
    const auto second = EscrowPayment( network.third, *second_hold, network.passive.account->GetAddress() );
    ASSERT_TRUE( first && second );
    ASSERT_NE( first->GetSlotID(), second->GetSlotID() );
    ConnectPeers( Peers( network ) );
    const auto first_proposal  = EscrowProposal( network.second, *first );
    const auto second_proposal = EscrowProposal( network.third, *second );
    ASSERT_TRUE( first_proposal.has_value() && second_proposal.has_value() );
    ASSERT_TRUE( network.second.consensus->SubmitProposal( first_proposal.value() ).has_value() );
    ASSERT_TRUE( network.third.consensus->SubmitProposal( second_proposal.value() ).has_value() );
    const auto peers = Peers( network );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return std::all_of( peers.begin(),
                                peers.end(),
                                [&]( const auto *peer )
                                {
                                    return peer->consensus->CheckCertificateForSlot( first->GetSlotID() ) &&
                                           peer->consensus->CheckCertificateForSlot( second->GetSlotID() ) &&
                                           HasEscrowPayment( *peer, *first_hold, *first, nullptr, 2 ) &&
                                           HasEscrowPayment( *peer, *second_hold, *second, nullptr, 2 );
                                } );
        },
        std::chrono::seconds( 25 ),
        "different hold slots each produce their complete payment",
        nullptr );
    StopNetwork( network );
}

TEST_F( FinalityFaultNetwork, EscrowReleaseDelayedDeliveryRestartAndCatchupPaysOnce )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    for ( uint16_t leg = 0; leg < 2; ++leg )
    {
        SCOPED_TRACE( leg == 0 ? "D-partition-certificate-first" : "E-restart-passive-catchup" );
        auto network = StartNetwork( "escrow-delay-" + std::to_string( leg ),
                                     static_cast<uint16_t>( 54851 + leg * 10 ) );
        StoreRegistry( network );
        const auto hold = EscrowHold( network );
        EstablishEscrow( network, hold );
        auto winner = EscrowPayment( network.second, *hold, network.passive.account->GetAddress() );
        auto loser  = EscrowPayment( network.third, *hold, network.passive.account->GetAddress() );
        ASSERT_TRUE( winner && loser );
        const auto proposal  = EscrowProposal( network.second, *winner );
        const auto contender = EscrowProposal( network.third, *loser );
        ASSERT_TRUE( proposal.has_value() && contender.has_value() );
        ASSERT_TRUE( network.second.consensus->SubmitProposal( proposal.value() ).has_value() );
        EXPECT_FALSE( network.second.consensus->CheckCertificateForSlot( winner->GetSlotID() ) );
        EXPECT_FALSE( HasEscrowPayment( network.second, *hold, *winner, loser.get() ) );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.second.consensus,
                                                                                            winner->GetSlotID() ) ==
                       proposal.value().proposal_id();
            },
            std::chrono::seconds( 5 ),
            "the isolated first submitter durably votes its own payment",
            nullptr );
        // D begins with split votes; E reorders a contender behind a durable matching vote.
        const auto &third_proposal = leg == 0 ? contender.value() : proposal.value();
        ASSERT_TRUE( network.third.consensus->SubmitProposal( third_proposal ).has_value() );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.third.consensus,
                                                                                            winner->GetSlotID() ) ==
                       third_proposal.proposal_id();
            },
            std::chrono::seconds( 5 ),
            "the second submitter has its explicit partitioned vote",
            nullptr );
        ASSERT_TRUE( network.third.consensus->SubmitProposal( contender.value() ).has_value() );
        ConnectValidatorPeers( { &network.first, &network.second, &network.third } );
        ASSERT_TRUE( network.second.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&]
                                   { return network.first.consensus->CheckCertificateForSlot( winner->GetSlotID() ); },
                                   std::chrono::seconds( 25 ),
                                   "partitioned validators finalize exactly one payment",
                                   nullptr );
        const auto certificate = network.first.consensus->GetCertificateBySlot( winner->GetSlotID() );
        ASSERT_TRUE( certificate.has_value() );
        const auto certified_hash = sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash(
            certificate.value().proposal() );
        ASSERT_TRUE( certified_hash == winner->GetHash() || certified_hash == loser->GetHash() );
        if ( certified_hash == loser->GetHash() )
        {
            std::swap( winner, loser );
        }
        for ( auto *peer : Peers( network ) )
        {
            PrintEscrowRecovery( *peer, "before-restart", *hold, *winner );
        }
        EXPECT_FALSE( network.passive.consensus->CheckCertificateForSlot( winner->GetSlotID() ) );
        if ( leg == 0 )
        {
            // The public certificate intake precedes transaction/CRDT catch-up on the isolated receiver.
            ASSERT_TRUE( network.passive.consensus->SubmitCertificate( certificate.value() ).has_value() );
            ASSERT_TRUE( network.passive.consensus->SubmitProposal( contender.value() ).has_value() );
        }
        if ( leg == 1 )
        {
            // Recreate every owner before reconnecting, so surviving old hosts cannot cross-dial fresh listeners.
            RestartAndReconnect( network );
        }
        else
        {
            RestartPeer( network.third );
            ConnectPeers( Peers( network ) );
        }
        const auto monitored = network.first.db->GetMonitoredTopics();
        ASSERT_TRUE( monitored.has_value() );
        ASSERT_TRUE(
            network.first.db
                ->RequestHeadBroadcast( std::set<std::string>( monitored.value().begin(), monitored.value().end() ) )
                .has_value() );
        const auto peers = Peers( network );
        for ( auto *peer : peers )
        {
            PrintEscrowRecovery( *peer, "after-restart", *hold, *winner );
        }
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return std::all_of( peers.begin(),
                                    peers.end(),
                                    [&]( const auto *peer )
                                    {
                                        return peer->consensus->CheckCertificateForSlot( winner->GetSlotID() ) &&
                                               HasEscrowPayment( *peer, *hold, *winner, loser.get() );
                                    } );
            },
            std::chrono::seconds( 25 ),
            "restart and passive catch-up preserve one complete payment",
            nullptr );
        for ( auto *peer : peers )
        {
            PrintEscrowRecovery( *peer, "after-catchup", *hold, *winner );
        }
        for ( auto *peer : peers )
        {
            const auto canonical = peer->consensus->GetCertificateBySlot( winner->GetSlotID() );
            ASSERT_TRUE( canonical.has_value() );
            EXPECT_EQ( canonical.value().proposal_id(), certificate.value().proposal_id() );
            EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( canonical.value().proposal() ),
                       winner->GetHash() );
            ASSERT_TRUE( peer->consensus->SubmitCertificate( certificate.value() ).has_value() );
            EXPECT_TRUE( HasEscrowPayment( *peer, *hold, *winner, loser.get() ) );
        }
        StopNetwork( network );
    }
}

TEST_F( FinalityFaultNetwork, EscrowReleaseLegacyCertificateOutlivesProposalTTL )
{
    using Access = sgns::MultiNodeFinalityFaultTestAccess;
    using Check  = sgns::ConsensusManager::Check;
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto network = StartNetwork( "escrow-legacy-ttl", 54951 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                 network.passive.consensus );
    StoreRegistry( network );
    ConnectPeers( Peers( network ) );
    const auto hold = EscrowHold( network );
    ASSERT_TRUE( hold );
    EstablishEscrow( network, hold );

    const auto payment_shape = EscrowPayment( network.second, *hold, network.passive.account->GetAddress() );
    ASSERT_TRUE( payment_shape );
    auto fresh = std::make_shared<sgns::TransferTransaction>(
        sgns::TransferTransaction::New( payment_shape->GetInputInfos(),
                                        payment_shape->GetDstInfos(),
                                        payment_shape->dag_st ) );
    fresh->MakeSignature( *network.second.account );
    ASSERT_TRUE( fresh->CheckHash() && fresh->CheckSignature() );
    for ( auto *peer : Peers( network ) )
    {
        // This is the actual selected local admission validator with certified
        // hold evidence present. Fresh old-format escrow spending is invalid.
        EXPECT_EQ( Access::EscrowAdmission( *peer->transactions, *fresh ),
                   sgns::IInputValidator::WitnessVerdict::kInvalid );
    }

    const auto now = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
            .count() );
    const auto attempt_age = std::max( Access::EscrowProposalTTL( network.first.consensus ), Access::EscrowVoteTTL() ) +
                             std::chrono::hours( 1 );
    ASSERT_GT( now, static_cast<uint64_t>( attempt_age.count() ) );
    const auto aged_at = now - static_cast<uint64_t>( attempt_age.count() );
    auto       old_dag = fresh->dag_st;
    old_dag.set_timestamp( static_cast<int64_t>( aged_at ) );
    auto historical = std::make_shared<sgns::TransferTransaction>(
        sgns::TransferTransaction::New( fresh->GetInputInfos(), fresh->GetDstInfos(), std::move( old_dag ) ) );
    historical->MakeSignature( *network.second.account );
    const auto original_bytes = historical->SerializeByteVector();
    ASSERT_TRUE( historical->CheckHash() && historical->CheckSignature() );
    ASSERT_NE( historical->GetHash(), fresh->GetHash() );
    const auto old_slot = historical->GetSrcAddress() + ":" + std::to_string( historical->GetNonce() );
    ASSERT_EQ( historical->GetSlotID(), old_slot );
    const std::vector<Peer *> voters{ &network.first, &network.second, &network.third };
    const auto                old_certificate = SignedCertificate( network.second, *historical, voters, aged_at );
    ASSERT_TRUE( old_certificate.has_value() );
    ASSERT_GT( now - old_certificate.value().proposal().timestamp(),
               static_cast<uint64_t>( Access::EscrowProposalTTL( network.first.consensus ).count() ) );
    ASSERT_GT( now - old_certificate.value().votes( 0 ).timestamp(),
               static_cast<uint64_t>( Access::EscrowVoteTTL().count() ) );
    EXPECT_EQ( old_certificate.value().timestamp(), aged_at );

    const auto v2 = EscrowPayment( network.third, *hold, network.passive.account->GetAddress() );
    ASSERT_TRUE( v2 );
    ASSERT_NE( v2->GetSlotID(), old_slot );
    const auto v2_certificate = SignedCertificate( network.third, *v2, voters );
    ASSERT_TRUE( v2_certificate.has_value() );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            for ( auto *peer : Peers( network ) )
            {
                auto registry = peer->blockchain->GetValidatorRegistry();
                if ( registry->LoadVerifiedRegistryByCid( old_certificate.value().registry_cid() ).has_error() ||
                     registry->LoadVerifiedRegistryByCid( v2_certificate.value().registry_cid() ).has_error() )
                {
                    return false;
                }
            }
            return true;
        },
        std::chrono::seconds( 20 ),
        "every peer loaded both exact certificate registries through the authorized registry chain",
        nullptr );
    EXPECT_EQ( Access::SlotKey( old_certificate.value().proposal() ), old_slot );
    EXPECT_EQ( Access::SlotKey( v2_certificate.value().proposal() ), v2->GetSlotID() );
    EXPECT_TRUE( Access::EscrowCertificateMatches( old_certificate.value(), *historical ) );
    EXPECT_FALSE( Access::EscrowCertificateMatches( old_certificate.value(), *v2 ) );
    auto altered_history = sgns::TransferTransaction::DeSerializeByteVector( original_bytes );
    ASSERT_TRUE( altered_history );
    altered_history->dag_st.set_signature( "altered-history-signature" );
    ASSERT_TRUE( altered_history->CheckHash() );
    EXPECT_NE( historical->SerializeByteVector(), altered_history->SerializeByteVector() );

    const auto hold_hash = sgns::base::Hash256::fromReadableString( hold->GetHash() );
    const auto old_hash  = sgns::base::Hash256::fromReadableString( historical->GetHash() );
    const auto v2_hash   = sgns::base::Hash256::fromReadableString( v2->GetHash() );
    ASSERT_TRUE( hold_hash.has_value() && old_hash.has_value() && v2_hash.has_value() );
    const auto                                     peers = Peers( network );
    std::array<std::map<std::string, uint64_t>, 4> balances_before;
    std::map<std::string, uint64_t>                credits;
    for ( const auto &output : historical->GetDstInfos() )
    {
        ASSERT_EQ( output.token_id, kToken );
        credits[output.dest_address] += output.encrypted_amount;
    }
    for ( size_t index = 0; index < peers.size(); ++index )
    {
        for ( const auto &[recipient, amount] : credits )
        {
            balances_before[index][recipient] = peers[index]->account->GetUTXOManager().GetBalance( kToken, recipient );
        }
    }
    // The old certificate is cryptographically valid on every peer despite
    // expiring proposal/vote retry windows. It is retained historical finality,
    // not a newly admitted old transfer proposal.
    for ( auto *peer : peers )
    {
        ASSERT_EQ( Access::ValidateCertificate( peer->consensus, old_certificate.value() ), Check::Approve );
        ASSERT_EQ( Access::ValidateCertificate( peer->consensus, v2_certificate.value() ), Check::Approve );
        sgns::crdt::GlobalDB::Buffer history;
        history.put( original_bytes );
        ASSERT_TRUE(
            peer->db->Put( { sgns::TransactionManager::GetTransactionPath( *historical ) }, history, {} ).has_value() );
        ASSERT_TRUE( peer->consensus->SubmitCertificate( old_certificate.value() ).has_value() );
        EXPECT_TRUE( Access::EscrowExactHistory( *peer->transactions, *historical ) );
        EXPECT_FALSE( Access::EscrowExactHistory( *peer->transactions, *altered_history ) );
        const auto applied = Access::EscrowApplyCertificate( *peer->transactions,
                                                             historical->GetHash(),
                                                             old_certificate.value() );
        ASSERT_TRUE( applied.has_value() );
        ASSERT_EQ( applied.value(), Check::Approve );
    }
    for ( size_t index = 0; index < peers.size(); ++index )
    {
        auto &ledger = peers[index]->account->GetUTXOManager();
        ASSERT_EQ( ledger.GetOutPointSpender( hold_hash.value(), 0 ), std::optional{ old_hash.value() } );
        for ( const auto &[recipient, amount] : credits )
        {
            EXPECT_EQ( ledger.GetBalance( kToken, recipient ), balances_before[index][recipient] + amount );
        }
    }

    for ( size_t index = 0; index < peers.size(); ++index )
    {
        auto *peer = peers[index];
        // Both eras' certificate slots can coexist: slot identity is not an
        // authenticated cross-era fence. Local input-conflict/duplicate rules
        // still prevent this sequential redelivery from crediting twice.
        ASSERT_TRUE( peer->consensus->SubmitCertificate( v2_certificate.value() ).has_value() );
        (void) Access::EscrowApplyCertificate( *peer->transactions, v2->GetHash(), v2_certificate.value() );
        ASSERT_TRUE( peer->consensus->GetCertificateBySlot( old_slot ).has_value() );
        ASSERT_TRUE( peer->consensus->GetCertificateBySlot( v2->GetSlotID() ).has_value() );
        ASSERT_EQ( Access::ValidateCertificate( peer->consensus, old_certificate.value() ), Check::Approve );
        auto &ledger = peer->account->GetUTXOManager();
        EXPECT_EQ( ledger.GetOutPointSpender( hold_hash.value(), 0 ), std::optional{ old_hash.value() } );
        for ( const auto &[recipient, amount] : credits )
        {
            EXPECT_EQ( ledger.GetBalance( kToken, recipient ), balances_before[index][recipient] + amount );
        }
        for ( uint32_t output = 0; output < v2->GetDstInfos().size(); ++output )
        {
            EXPECT_FALSE( ledger.GetOutPointState( v2_hash.value(), output ).has_value() );
        }
        const auto retained = peer->db->Get( { sgns::TransactionManager::GetTransactionPath( *historical ) } );
        ASSERT_TRUE( retained.has_value() );
        EXPECT_EQ( retained.value().toString(), std::string( original_bytes.begin(), original_bytes.end() ) );
    }
    EXPECT_EQ( historical->SerializeByteVector(), original_bytes );
    std::cerr << "P21_1_LEGACY_TTL old_hash=" << historical->GetHash() << " old_slot=" << old_slot
              << " aged_ms=" << attempt_age.count()
              << " proposal_ttl_ms=" << Access::EscrowProposalTTL( network.first.consensus ).count()
              << " v2_hash=" << v2->GetHash() << " v2_slot=" << v2->GetSlotID()
              << " cross_era_exclusivity=not-proven activation=pending\n";
    StopNetwork( network );
}

TEST_F( FinalityFaultNetwork, EscrowReleasePendingIntentRebuildPreservesNonceAndHistory )
{
    using Access = sgns::MultiNodeFinalityFaultTestAccess;
    using Status = sgns::TransactionManager::TransactionStatus;
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto network = StartNetwork( "escrow-pending-rebuild", 54961 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                 network.passive.consensus );
    StoreRegistry( network );
    ConnectPeers( Peers( network ) );
    const auto hold = EscrowHold( network );
    ASSERT_TRUE( hold );
    EstablishEscrow( network, hold );
    auto      &sender    = network.second;
    const auto hold_hash = sgns::base::Hash256::fromReadableString( hold->GetHash() );
    ASSERT_TRUE( hold_hash.has_value() );

    // The caller retains the complete processing-result set it has already
    // validated. No partial/empty results or replacement recipient policy is
    // invented during reconstruction; PayEscrow uses all retained entries.
    SGProcessing::TaskResult complete_results;
    for ( const auto &[subtask, recipient] : std::array<std::pair<std::string, std::string>, 2>{
              std::make_pair( "complete-subtask-a", network.third.account->GetAddress() ),
              std::make_pair( "complete-subtask-b", network.passive.account->GetAddress() ) } )
    {
        auto *result = complete_results.add_subtask_results();
        result->set_subtaskid( subtask );
        result->set_node_address( recipient );
        result->set_developer_address( network.first.account->GetAddress() );
        result->set_developer_cut( 500000 );
        result->set_token_id( kToken.bytes().data(), kToken.size() );
    }
    ASSERT_EQ( complete_results.subtask_results_size(), 2 );
    const auto retained_results = complete_results.SerializeAsString();
    const auto payout_outputs   = Access::EscrowPayoutOutputs( complete_results, hold->GetAmount(), kToken );
    ASSERT_TRUE( payout_outputs.has_value() );
    sgns::InputUTXOInfo input{ hold_hash.value(), 0, {} };
    input.signature_ = sender.account->Sign( input.SerializeForSigning() );
    auto old_intent  = std::make_shared<sgns::TransferTransaction>(
        sgns::TransferTransaction::New( { input },
                                        payout_outputs.value(),
                                        Access::EscrowIntentDAG( *sender.transactions, hold->GetUncleHash() ) ) );
    old_intent->MakeSignature( *sender.account );
    const auto old_bytes = old_intent->SerializeByteVector();
    const auto old_nonce = old_intent->GetNonce();
    const auto old_slot  = old_intent->GetSlotID();
    const auto old_hash  = sgns::base::Hash256::fromReadableString( old_intent->GetHash() );
    ASSERT_TRUE( old_hash.has_value() );
    ASSERT_TRUE( old_intent->CheckHash() && old_intent->CheckSignature() );
    Access::EscrowEnqueueIntent( *sender.transactions, old_intent );
    sender.account->GetUTXOManager().ReserveUTXOs( { input }, old_intent->GetHash() );
    ASSERT_TRUE( sender.account->GetUTXOManager().IsOutPointReserved( hold_hash.value(), 0 ) );
    sgns::crdt::GlobalDB::Buffer history;
    history.put( old_bytes );
    const auto old_path = sgns::TransactionManager::GetTransactionPath( *old_intent );
    ASSERT_TRUE( sender.db->Put( { old_path }, history, {} ).has_value() );
    for ( auto *peer : Peers( network ) )
    {
        ASSERT_FALSE( peer->consensus->CheckCertificateForSlot( old_slot ) );
        ASSERT_FALSE( Access::DurableActiveVoteProposalId( peer->consensus, old_slot ).has_value() );
    }

    ASSERT_TRUE( Access::EscrowRollbackQueuedIntent( *sender.transactions, old_intent->GetHash() ).has_value() );
    EXPECT_EQ( sender.transactions->GetTransactionStatusByTxId( old_intent->GetHash() ), Status::FAILED );
    ASSERT_TRUE( Access::EscrowQueuedTransactions( *sender.transactions ).empty() );
    EXPECT_FALSE( sender.account->GetUTXOManager().IsOutPointReserved( hold_hash.value(), 0 ) );
    EXPECT_FALSE( sender.account->GetUTXOManager().IsOutPointConsumed( hold_hash.value(), 0 ) );
    EXPECT_EQ( sender.account->GetProposedNonce(), old_nonce );

    const auto rebuilt_hash = sender.transactions->PayEscrow( sgns::TransactionManager::GetTransactionPath( *hold ),
                                                              complete_results,
                                                              nullptr );
    ASSERT_TRUE( rebuilt_hash.has_value() );
    const auto queued = Access::EscrowQueuedTransactions( *sender.transactions );
    ASSERT_EQ( queued.size(), 1U );
    const auto rebuilt = std::dynamic_pointer_cast<sgns::EscrowReleaseTransactionV2>( queued.front() );
    ASSERT_TRUE( rebuilt );
    EXPECT_EQ( rebuilt->GetHash(), rebuilt_hash.value() );
    EXPECT_NE( rebuilt->GetHash(), old_intent->GetHash() );
    EXPECT_NE( rebuilt->GetSlotID(), old_slot );
    EXPECT_EQ( rebuilt->GetNonce(), old_nonce );
    ASSERT_TRUE( rebuilt->CheckHash() && rebuilt->CheckSignature() );
    ASSERT_EQ( rebuilt->GetInputInfos().size(), 1U );
    EXPECT_EQ( rebuilt->GetInputInfos().front().txid_hash_, hold_hash.value() );
    EXPECT_EQ( rebuilt->GetInputInfos().front().output_idx_, 0U );
    ASSERT_EQ( rebuilt->GetDstInfos().size(), payout_outputs.value().size() );
    for ( size_t index = 0; index < payout_outputs.value().size(); ++index )
    {
        EXPECT_EQ( rebuilt->GetDstInfos()[index].dest_address, payout_outputs.value()[index].dest_address );
        EXPECT_EQ( rebuilt->GetDstInfos()[index].encrypted_amount, payout_outputs.value()[index].encrypted_amount );
        EXPECT_EQ( rebuilt->GetDstInfos()[index].token_id, payout_outputs.value()[index].token_id );
    }
    EXPECT_EQ( complete_results.SerializeAsString(), retained_results );
    EXPECT_EQ( old_intent->SerializeByteVector(), old_bytes );

    const auto                                     peers = Peers( network );
    std::map<std::string, uint64_t>                credits;
    std::array<std::map<std::string, uint64_t>, 4> balances_before;
    for ( const auto &output : payout_outputs.value() )
    {
        credits[output.dest_address] += output.encrypted_amount;
    }
    for ( size_t index = 0; index < peers.size(); ++index )
    {
        for ( const auto &[recipient, amount] : credits )
        {
            balances_before[index][recipient] = peers[index]->account->GetUTXOManager().GetBalance( kToken, recipient );
        }
    }
    const std::vector<Peer *> voters{ &network.first, &network.second, &network.third };
    const auto                certificate = SignedCertificate( sender, *rebuilt, voters );
    ASSERT_TRUE( certificate.has_value() );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            for ( auto *peer : peers )
            {
                if ( peer->blockchain->GetValidatorRegistry()
                         ->LoadVerifiedRegistryByCid( certificate.value().registry_cid() )
                         .has_error() )
                {
                    return false;
                }
            }
            return true;
        },
        std::chrono::seconds( 20 ),
        "every peer loaded the rebuilt payment's exact authorized registry",
        nullptr );
    const auto new_hash = sgns::base::Hash256::fromReadableString( rebuilt->GetHash() );
    ASSERT_TRUE( new_hash.has_value() );
    for ( auto *peer : peers )
    {
        ASSERT_TRUE( peer->consensus->SubmitCertificate( certificate.value() ).has_value() );
        for ( unsigned redelivery = 0; redelivery < 2; ++redelivery )
        {
            const auto applied = Access::EscrowApplyCertificate( *peer->transactions,
                                                                 rebuilt->GetHash(),
                                                                 certificate.value() );
            ASSERT_TRUE( applied.has_value() );
            EXPECT_EQ( applied.value(), sgns::ConsensusManager::Check::Approve );
        }
    }
    for ( size_t index = 0; index < peers.size(); ++index )
    {
        auto &ledger = peers[index]->account->GetUTXOManager();
        EXPECT_EQ( ledger.GetOutPointSpender( hold_hash.value(), 0 ), std::optional{ new_hash.value() } );
        EXPECT_FALSE( ledger.GetOutPointState( old_hash.value(), 0 ).has_value() );
        EXPECT_FALSE( peers[index]->consensus->CheckCertificateForSlot( old_slot ) );
        for ( const auto &[recipient, amount] : credits )
        {
            EXPECT_EQ( ledger.GetBalance( kToken, recipient ), balances_before[index][recipient] + amount );
        }
    }
    const auto retained_history = sender.db->Get( { old_path } );
    ASSERT_TRUE( retained_history.has_value() );
    EXPECT_EQ( retained_history.value().toString(), std::string( old_bytes.begin(), old_bytes.end() ) );
    EXPECT_EQ( old_intent->SerializeByteVector(), old_bytes );
    EXPECT_NE( sender.transactions->GetTransactionStatusByTxId( old_intent->GetHash() ), Status::CONFIRMED );
    const auto confirmed_nonce = sender.account->GetLocalConfirmedNonce();
    const auto confirmed_hash  = sender.account->GetLocalConfirmedTxHash( old_nonce );
    ASSERT_TRUE( confirmed_nonce.has_value() && confirmed_hash.has_value() );
    EXPECT_EQ( confirmed_nonce.value(), old_nonce );
    EXPECT_EQ( confirmed_hash.value(), rebuilt->GetHash() );
    std::cerr << "P21_1_PENDING_REBUILD old_hash=" << old_intent->GetHash() << " old_slot=" << old_slot
              << " reused_nonce=" << old_nonce << " v2_hash=" << rebuilt->GetHash()
              << " v2_slot=" << rebuilt->GetSlotID() << " old_signed_history=preserved results=complete\n";
    StopNetwork( network );
}

TEST_F( FinalityFaultNetwork, ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first   = StartPeer( "validator-one", 54301 );
    auto second  = StartPeer( "validator-two", 54302 );
    auto third   = StartPeer( "validator-three", 54303 );
    auto passive = StartPeer( "passive-recipient", 54304 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION( [&] { return first.db && second.db && third.db && passive.db; },
                               std::chrono::seconds( 3 ),
                               "four real peers started and connected",
                               nullptr );

    const auto update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : { &first, &second, &third, &passive } )
    {
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    }
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
        },
        std::chrono::seconds( 5 ),
        "validator registry durably available on every peer",
        nullptr );

    auto mint = MintFor( first );
    ASSERT_TRUE( mint );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(),
                                                               mint->GetNonce(),
                                                               mint->GetHash(),
                                                               mint->SerializeToEmbeddedTransaction(),
                                                               CommitmentFor( *mint ),
                                                               sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = first.consensus->CreateProposal( subject.value(),
                                                     first.account->GetAddress(),
                                                     first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                     first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto canonical_slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );
    ASSERT_FALSE( canonical_slot.empty() );
    ASSERT_TRUE( first.consensus->SubmitProposal( proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return sgns::MultiNodeFinalityFaultTestAccess::VotePublications( first.consensus ) > 0 &&
                   sgns::MultiNodeFinalityFaultTestAccess::VotePublications( second.consensus ) > 0 &&
                   sgns::MultiNodeFinalityFaultTestAccess::VotePublications( third.consensus ) > 0;
        },
        std::chrono::seconds( 15 ),
        "public proposal reached durable votes on all validators",
        nullptr );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   second.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   third.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   passive.consensus->CheckCertificateForSlot( canonical_slot );
        },
        std::chrono::seconds( 20 ),
        "one canonical certificate reached every peer through CRDT",
        nullptr );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( first.consensus ) +
                           sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished(
                               second.consensus ) +
                           sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished(
                               third.consensus ) >
                       0 &&
                   sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsReceived( passive.consensus ) > 0 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *first.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *second.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *third.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1;
        },
        std::chrono::seconds( 20 ),
        "certificate notification reached the registered Mint consumers",
        nullptr );

    const auto losing_mint = MintFor( first, 1 );
    ASSERT_TRUE( losing_mint );
    ASSERT_EQ( mint->GetSlotID(), losing_mint->GetSlotID() );
    ASSERT_NE( mint->GetHash(), losing_mint->GetHash() );
    auto losing_subject = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(),
                                                                      losing_mint->GetNonce(),
                                                                      losing_mint->GetHash(),
                                                                      losing_mint->SerializeToEmbeddedTransaction(),
                                                                      CommitmentFor( *losing_mint ),
                                                                      sgns::UTXOWitness{} );
    ASSERT_TRUE( losing_subject.has_value() );
    auto losing_proposal = second.consensus->CreateProposal(
        losing_subject.value(),
        second.account->GetAddress(),
        second.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        second.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( losing_proposal.has_value() );
    ASSERT_TRUE( second.consensus->SubmitProposal( losing_proposal.value() ).has_value() );

    for ( auto *peer : peers )
    {
        const auto certificate = peer->consensus->GetCertificateBySlot( canonical_slot );
        ASSERT_TRUE( certificate.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ),
                   mint->GetHash() );
        EXPECT_TRUE( HasOnlyWinnerOutput( *peer, *mint, *losing_mint ) );
        EXPECT_TRUE( HasBridgeMarker( *peer, *mint ) );
    }

    RestartPeer( first );
    RestartPeer( second );
    RestartPeer( third );
    RestartPeer( passive );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );
    ConnectPeers( { &first, &second, &third, &passive } );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   second.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   third.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   HasOnlyWinnerOutput( first, *mint, *losing_mint ) &&
                   HasOnlyWinnerOutput( second, *mint, *losing_mint ) &&
                   HasOnlyWinnerOutput( third, *mint, *losing_mint ) &&
                   HasOnlyWinnerOutput( passive, *mint, *losing_mint ) && HasBridgeMarker( first, *mint ) &&
                   HasBridgeMarker( second, *mint ) && HasBridgeMarker( third, *mint ) &&
                   HasBridgeMarker( passive, *mint );
        },
        std::chrono::seconds( 20 ),
        "certificate and exact winner Mint survived every peer recreation",
        nullptr );
    StopPeer( first );
    StopPeer( second );
    StopPeer( third );
    StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, SameBurnContentionUsesOneCanonicalSlotAndExactMint )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first   = StartPeer( "contention-validator-one", 54401 );
    auto second  = StartPeer( "contention-validator-two", 54402 );
    auto third   = StartPeer( "contention-validator-three", 54403 );
    auto passive = StartPeer( "contention-passive-recipient", 54404 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    const auto                  update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : peers )
    {
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    }
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
        },
        std::chrono::seconds( 5 ),
        "disconnected peers durably stored the validator registry",
        nullptr );

    auto first_mint  = MintFor( first );
    auto second_mint = MintFor( first, 1 );
    ASSERT_TRUE( first_mint && second_mint );
    ASSERT_EQ( first_mint->GetSlotID(), second_mint->GetSlotID() );
    ASSERT_NE( first_mint->GetHash(), second_mint->GetHash() );

    auto first_subject  = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(),
                                                                     first_mint->GetNonce(),
                                                                     first_mint->GetHash(),
                                                                     first_mint->SerializeToEmbeddedTransaction(),
                                                                     CommitmentFor( *first_mint ),
                                                                     sgns::UTXOWitness{} );
    auto second_subject = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(),
                                                                      second_mint->GetNonce(),
                                                                      second_mint->GetHash(),
                                                                      second_mint->SerializeToEmbeddedTransaction(),
                                                                      CommitmentFor( *second_mint ),
                                                                      sgns::UTXOWitness{} );
    ASSERT_TRUE( first_subject.has_value() && second_subject.has_value() );
    auto first_proposal = first.consensus->CreateProposal(
        first_subject.value(),
        first.account->GetAddress(),
        first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    auto second_proposal = second.consensus->CreateProposal(
        second_subject.value(),
        second.account->GetAddress(),
        second.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        second.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( first_proposal.has_value() && second_proposal.has_value() );
    const auto canonical_slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( first_proposal.value() );
    ASSERT_EQ( canonical_slot, sgns::MultiNodeFinalityFaultTestAccess::SlotKey( second_proposal.value() ) );
    ASSERT_TRUE( first.consensus->SubmitProposal( first_proposal.value() ).has_value() );
    ASSERT_TRUE( second.consensus->SubmitProposal( second_proposal.value() ).has_value() );

    // Named real-peer barrier: the three validators connect only after both
    // public proposal submissions have completed on their isolated peers.
    ConnectValidatorPeers( { &first, &second, &third } );
    // GossipPubSub does not replay publications made before a peer link exists.
    // Re-advertise the same public proposals after the real link barrier; this
    // exercises ordinary production ingress without synthesizing delivery.
    ASSERT_TRUE( first.consensus->SubmitProposal( first_proposal.value() ).has_value() );
    ASSERT_TRUE( second.consensus->SubmitProposal( second_proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   second.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   third.consensus->CheckCertificateForSlot( canonical_slot );
        },
        std::chrono::seconds( 20 ),
        "connected validators converged on one durable canonical certificate",
        nullptr );

    const auto certificate = first.consensus->GetCertificateBySlot( canonical_slot );
    ASSERT_TRUE( certificate.has_value() );
    const auto winner_hash = sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash(
        certificate.value().proposal() );
    ASSERT_TRUE( winner_hash.has_value() );
    const auto winner = winner_hash.value() == first_mint->GetHash() ? first_mint : second_mint;
    const auto loser  = winner_hash.value() == first_mint->GetHash() ? second_mint : first_mint;
    ASSERT_NE( winner->GetHash(), loser->GetHash() );

    for ( auto *validator : { &first, &second, &third } )
    {
        const auto durable = validator->consensus->GetCertificateBySlot( canonical_slot );
        ASSERT_TRUE( durable.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( durable.value().proposal() ),
                   winner_hash );
    }

    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *first.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *second.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *third.transactions ) == 1 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1 &&
                   HasBridgeMarker( first, *winner ) && HasBridgeMarker( second, *winner ) &&
                   HasBridgeMarker( third, *winner ) && HasBridgeMarker( passive, *winner );
        },
        std::chrono::seconds( 20 ),
        "passive peer received and recovered the exact canonical winner",
        nullptr );
    for ( auto *peer : peers )
    {
        EXPECT_TRUE( HasOnlyWinnerOutput( *peer, *winner, *loser ) );
        EXPECT_TRUE( HasBridgeMarker( *peer, *winner ) );
    }

    RestartPeer( first );
    RestartPeer( second );
    RestartPeer( third );
    RestartPeer( passive );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );
    ConnectPeers( { &first, &second, &third, &passive } );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   second.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   third.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   HasOnlyWinnerOutput( first, *winner, *loser ) && HasOnlyWinnerOutput( second, *winner, *loser ) &&
                   HasOnlyWinnerOutput( third, *winner, *loser ) && HasOnlyWinnerOutput( passive, *winner, *loser ) &&
                   HasBridgeMarker( first, *winner ) && HasBridgeMarker( second, *winner ) &&
                   HasBridgeMarker( third, *winner ) && HasBridgeMarker( passive, *winner );
        },
        std::chrono::seconds( 20 ),
        "durable canonical winner survived every peer recreation",
        nullptr );
    StopPeer( first );
    StopPeer( second );
    StopPeer( third );
    StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, LateContenderAndPassiveRecipientRemainReceiveOnly )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first   = StartPeer( "late-validator-one", 54501 );
    auto second  = StartPeer( "late-validator-two", 54502 );
    auto third   = StartPeer( "late-validator-three", 54503 );
    auto passive = StartPeer( "late-passive-recipient", 54504 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    const auto                  update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : peers )
    {
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    }
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
        },
        std::chrono::seconds( 5 ),
        "every isolated peer durably stored the validator registry",
        nullptr );

    auto winner = MintFor( first );
    auto loser  = MintFor( first, 1 );
    ASSERT_TRUE( winner && loser );
    ASSERT_EQ( winner->GetSlotID(), loser->GetSlotID() );
    auto winner_subject = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(),
                                                                      winner->GetNonce(),
                                                                      winner->GetHash(),
                                                                      winner->SerializeToEmbeddedTransaction(),
                                                                      CommitmentFor( *winner ),
                                                                      sgns::UTXOWitness{} );
    auto loser_subject  = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(),
                                                                     loser->GetNonce(),
                                                                     loser->GetHash(),
                                                                     loser->SerializeToEmbeddedTransaction(),
                                                                     CommitmentFor( *loser ),
                                                                     sgns::UTXOWitness{} );
    ASSERT_TRUE( winner_subject.has_value() && loser_subject.has_value() );
    auto winner_proposal = first.consensus->CreateProposal(
        winner_subject.value(),
        first.account->GetAddress(),
        first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    auto loser_proposal = second.consensus->CreateProposal(
        loser_subject.value(),
        second.account->GetAddress(),
        second.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        second.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( winner_proposal.has_value() && loser_proposal.has_value() );
    const auto canonical_slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( winner_proposal.value() );
    ASSERT_EQ( canonical_slot, sgns::MultiNodeFinalityFaultTestAccess::SlotKey( loser_proposal.value() ) );

    // Named topology barrier: only validators join before the winning public
    // submission. The passive recipient remains disconnected until certificate
    // persistence and the post-active-vote late-submission boundary are proven.
    ConnectValidatorPeers( { &first, &second, &third } );
    ASSERT_TRUE( first.consensus->SubmitProposal( winner_proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return sgns::MultiNodeFinalityFaultTestAccess::VotePublications( first.consensus ) > 0 &&
                   sgns::MultiNodeFinalityFaultTestAccess::VotePublications( second.consensus ) > 0 &&
                   sgns::MultiNodeFinalityFaultTestAccess::VotePublications( third.consensus ) > 0;
        },
        std::chrono::seconds( 15 ),
        "durable active-vote publications reached each validator",
        nullptr );

    ASSERT_TRUE( second.consensus->SubmitProposal( loser_proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( first.consensus,
                                                                                        canonical_slot ) ==
                       winner_proposal.value().proposal_id() &&
                   sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( second.consensus,
                                                                                        canonical_slot ) ==
                       winner_proposal.value().proposal_id() &&
                   sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( third.consensus,
                                                                                        canonical_slot ) ==
                       winner_proposal.value().proposal_id();
        },
        std::chrono::seconds( 10 ),
        "late contender did not replace any durable active vote",
        nullptr );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   second.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   third.consensus->CheckCertificateForSlot( canonical_slot );
        },
        std::chrono::seconds( 20 ),
        "initial winner reached the accepted authoritative certificate boundary",
        nullptr );

    ASSERT_TRUE( third.consensus->SubmitProposal( loser_proposal.value() ).has_value() );

    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsReceived( passive.consensus ) > 0 &&
                   sgns::MultiNodeFinalityFaultTestAccess::AcceptedCertificateReadbacks( passive.consensus ) > 0 &&
                   sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1;
        },
        std::chrono::seconds( 20 ),
        "passive peer received notification then completed committed durable recovery",
        nullptr );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateWriteAttempts( passive.consensus ), 0u );

    for ( auto *peer : peers )
    {
        const auto certificate = peer->consensus->GetCertificateBySlot( canonical_slot );
        ASSERT_TRUE( certificate.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ),
                   winner->GetHash() );
        EXPECT_TRUE( HasOnlyWinnerOutput( *peer, *winner, *loser ) );
        EXPECT_TRUE( HasBridgeMarker( *peer, *winner ) );
    }

    RestartPeer( first );
    RestartPeer( second );
    RestartPeer( third );
    RestartPeer( passive );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );
    ConnectPeers( { &first, &second, &third, &passive } );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   second.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   third.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
                   HasOnlyWinnerOutput( first, *winner, *loser ) && HasOnlyWinnerOutput( second, *winner, *loser ) &&
                   HasOnlyWinnerOutput( third, *winner, *loser ) && HasOnlyWinnerOutput( passive, *winner, *loser ) &&
                   HasBridgeMarker( first, *winner ) && HasBridgeMarker( second, *winner ) &&
                   HasBridgeMarker( third, *winner ) && HasBridgeMarker( passive, *winner );
        },
        std::chrono::seconds( 20 ),
        "late contender remained absent after reopened-store recovery",
        nullptr );
    StopPeer( first );
    StopPeer( second );
    StopPeer( third );
    StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    auto network = StartNetwork( "active-vote-diagnostic", 54641 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                 network.passive.consensus );
    StoreRegistry( network );

    auto winner = MintFor( network.first );
    ASSERT_TRUE( winner );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( network.first.account->GetAddress(),
                                                               winner->GetNonce(),
                                                               winner->GetHash(),
                                                               winner->SerializeToEmbeddedTransaction(),
                                                               CommitmentFor( *winner ),
                                                               sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = network.first.consensus->CreateProposal(
        subject.value(),
        network.first.account->GetAddress(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto slot                           = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );
    network.first.active_vote_diagnostic_slot = slot;
    network.first.active_vote_diagnostic_key  = sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteStorageKey(
        network.first.consensus,
        slot );

    sgns::MultiNodeFinalityFaultTestAccess::ArmActiveVoteBarrier( network.first.consensus );
    ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION(
        [&] { return sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteBarrierEntered( network.first.consensus ); },
        std::chrono::seconds( 15 ),
        "isolated validator paused after direct active-vote persistence",
        nullptr );

    auto before_restart                = Peer::Snapshot( network.first.db, network.first.active_vote_diagnostic_key );
    before_restart.decoded_proposal_id = sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId(
        network.first.consensus,
        slot );
    before_restart.accepted_certificate        = network.first.consensus->CheckCertificateForSlot( slot );
    const auto release_attempts_before_restart = sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteReleaseAttempts(
        network.first.consensus );
    const auto release_successes_before_restart = sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteReleaseSuccesses(
        network.first.consensus );

    auto fail = [&]( const char *stage, const char *classification )
    {
        std::cout << "ACTIVE_VOTE_RED stage=" << stage << " classification=" << classification << std::endl;
        ADD_FAILURE() << "active-vote lifecycle diagnostic failed";
        StopNetwork( network );
    };
    auto require_raw = [&]( const ActiveVoteLifecycleSnapshot &snapshot, const char *stage )
    {
        if ( snapshot.accepted_certificate )
        {
            fail( stage, "release-without-certificate" );
            return false;
        }
        if ( !snapshot.record_present || snapshot.record_digest != before_restart.record_digest )
        {
            fail( stage, "record-lost" );
            return false;
        }
        return true;
    };

    if ( !before_restart.record_present || before_restart.decoded_proposal_id != proposal.value().proposal_id() ||
         before_restart.accepted_certificate )
    {
        fail( "after-pre-broadcast-persistence", "record-lost" );
        return;
    }

    RestartPeer( network.first );
    ASSERT_TRUE( network.first.consensus );
    if ( !require_raw( network.first.after_consensus_manager_close, "after-consensus-manager-close" ) ||
         !require_raw( network.first.after_manager_ownership_release, "after-manager-ownership-release" ) ||
         !require_raw( network.first.after_same_root_globaldb_reopen_before_manager,
                       "after-same-root-globaldb-reopen-before-manager" ) )
    {
        return;
    }

    auto recovered_raw                = Peer::Snapshot( network.first.db, network.first.active_vote_diagnostic_key );
    recovered_raw.decoded_proposal_id = sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId(
        network.first.consensus,
        slot );
    recovered_raw.accepted_certificate = network.first.consensus->CheckCertificateForSlot( slot );
    const auto recovered_in_memory     = sgns::MultiNodeFinalityFaultTestAccess::RecoveredActiveVoteProposalId(
        network.first.consensus,
        slot );
    if ( !require_raw( recovered_raw, "after-recover-active-votes" ) ||
         recovered_raw.decoded_proposal_id != proposal.value().proposal_id() ||
         recovered_in_memory != proposal.value().proposal_id() )
    {
        if ( recovered_raw.record_present && recovered_raw.record_digest == before_restart.record_digest &&
             !recovered_raw.accepted_certificate )
        {
            fail( "after-recover-active-votes", "recovery-missed-record" );
        }
        return;
    }
    EXPECT_EQ( release_attempts_before_restart, 0U );
    EXPECT_EQ( release_successes_before_restart, 0U );
    StopNetwork( network );
}

TEST_F( FinalityFaultNetwork, RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    // 2026-09-03 developer directive (round-4 gap closure): activate the
    // test-only GraphsyncDAGSyncer blacklist-backoff seam to exercise the
    // boot-window masking hypothesis — after RestartPeer recreates a peer from
    // the same root (same PeerId/port), fetchers that hit CANNOT_CONNECT
    // against the still-booting host blacklist the reused identity for the
    // production 5-30s backoff and stay skipped even once the host is
    // reachable inside this suite's 25s recovery window. A flat 100 ms
    // backoff lets fetchers retry the restarting peer within the window;
    // production defaults are unchanged. The RAII guard restores the override
    // to 0 on every exit path (including ASSERT fatalities) so later tests in
    // this binary (PublisherLoss and the audit) inherit no sub-second backoff.
    sgns::crdt::GraphsyncDAGSyncer::SetBlacklistBackoffTimeoutForTest( 100 );

    struct BlacklistBackoffOverrideGuard
    {
        ~BlacklistBackoffOverrideGuard()
        {
            sgns::crdt::GraphsyncDAGSyncer::SetBlacklistBackoffTimeoutForTest( 0 );
        }
    } blacklist_backoff_override_guard;

    {
        auto network = StartNetwork( "restart-vote", 54601 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                     network.passive.consensus );
        StoreRegistry( network );
        // Keep the vote owner isolated until after its same-root recreation.
        // Otherwise the other validators can finalize this slot and legitimately
        // release the exact active-vote record before the restart boundary is
        // observed.

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject( network.first.account->GetAddress(),
                                                                   winner->GetNonce(),
                                                                   winner->GetHash(),
                                                                   winner->SerializeToEmbeddedTransaction(),
                                                                   CommitmentFor( *winner ),
                                                                   sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(),
            network.first.account->GetAddress(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

        sgns::MultiNodeFinalityFaultTestAccess::ArmActiveVoteBarrier( network.first.consensus );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION(
            [&] { return sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteBarrierEntered( network.first.consensus ); },
            std::chrono::seconds( 15 ),
            "first validator paused after durable active-vote persistence",
            nullptr );
        ASSERT_EQ( sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.first.consensus, slot ),
                   proposal.value().proposal_id() );

        RestartPeer( network.first );
        ASSERT_TRUE( network.first.consensus );
        ASSERT_EQ( sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.first.consensus, slot ),
                   proposal.value().proposal_id() );
        ConnectPeers( Peers( network ) );
        // The original isolated publisher was stopped before its normal vote
        // announcement; re-advertise the unchanged signed proposal through the
        // public route after reconnecting so peers can drive normal recovery.
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.passive, slot, *winner, *loser );
            },
            std::chrono::seconds( 25 ),
            "recreated vote owner recovered only its durable vote before exact finality",
            nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
            AssertOneLiveMintEffect( *peer );
        }

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.passive, slot, *winner, *loser );
            },
            std::chrono::seconds( 20 ),
            "vote-boundary finality remained exact after reopening every durable root",
            nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
        }
        StopNetwork( network );
    }

    {
        auto network = StartNetwork( "restart-certificate", 54611 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                     network.passive.consensus );
        StoreRegistry( network );
        ConnectPeers( Peers( network ) );

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject( network.first.account->GetAddress(),
                                                                   winner->GetNonce(),
                                                                   winner->GetHash(),
                                                                   winner->SerializeToEmbeddedTransaction(),
                                                                   CommitmentFor( *winner ),
                                                                   sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(),
            network.first.account->GetAddress(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

        sgns::MultiNodeFinalityFaultTestAccess::ArmAcceptedCertificateBarrier( network.second.consensus );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return sgns::MultiNodeFinalityFaultTestAccess::AcceptedCertificateBarrierEntered(
                    network.second.consensus );
            },
            std::chrono::seconds( 25 ),
            "second validator paused after accepted certificate durable readback",
            nullptr );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return network.first.consensus->CheckCertificateForSlot( slot ) &&
                       network.third.consensus->CheckCertificateForSlot( slot ) &&
                       network.passive.consensus->CheckCertificateForSlot( slot );
            },
            std::chrono::seconds( 20 ),
            "surviving peers retained the accepted certificate before receiver restart",
            nullptr );

        RestartPeer( network.second );
        ASSERT_TRUE( network.second.consensus );
        ConnectPeers( Peers( network ) );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.passive, slot, *winner, *loser );
            },
            std::chrono::seconds( 25 ),
            "recreated certificate recipient recovered the exact durable winner",
            nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
            AssertOneLiveMintEffect( *peer );
        }

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.passive, slot, *winner, *loser );
            },
            std::chrono::seconds( 20 ),
            "certificate-boundary recovery remained exact after reopening every durable root",
            nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
        }
        StopNetwork( network );
    }

    {
        auto network = StartNetwork( "restart-mint", 54621 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                     network.passive.consensus );
        StoreRegistry( network );
        ConnectPeers( Peers( network ) );

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject( network.first.account->GetAddress(),
                                                                   winner->GetNonce(),
                                                                   winner->GetHash(),
                                                                   winner->SerializeToEmbeddedTransaction(),
                                                                   CommitmentFor( *winner ),
                                                                   sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(),
            network.first.account->GetAddress(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto              slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );
        MintRecoveryDiagnostics diagnostics( network, slot, *winner, *loser );

        sgns::MultiNodeFinalityFaultTestAccess::ArmMintEffectsBarrier( network.first.transactions );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            { return sgns::MultiNodeFinalityFaultTestAccess::MintEffectsBarrierEntered( network.first.transactions ); },
            std::chrono::seconds( 25 ),
            "first validator paused after durable Mint effects and before bridge marker",
            nullptr );
        EXPECT_TRUE( HasOnlyWinnerOutput( network.first, *winner, *loser ) );
        EXPECT_FALSE( HasBridgeMarker( network.first, *winner ) );

        // 2026-09-03 developer decision Option A (round-5 gap closure,
        // STATE.md:143, commit 633d6ff1): "add a pre-restart
        // CheckCertificateForSlot wait on second/third peers first (making a
        // true surviving replica)". 12-19 attributed the block-3 residual to
        // route_count=1 — only the restarted publisher served the certificate
        // CID, and no pre-restart holder was proven in this block. This wait
        // both proves and creates the surviving replicas: the 20s window runs
        // while first is still alive and routable, so recipients' CRDT CID
        // fetches from first succeed even when the gossip publication raced
        // the still-forming mesh — the certificate's convergent-immutable
        // CRDT put broadcasting on consensus_datastore_topic_ and the
        // certificate Publish both fire before the mint-effects barrier
        // pauses first (src/blockchain/Consensus.cpp SubmitCertificate
        // :2126-2152).
        // Passive is included alongside the named second/third following the
        // block-2 precedent, which waits on every non-restarted peer.
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return network.second.consensus->CheckCertificateForSlot( slot ) &&
                       network.third.consensus->CheckCertificateForSlot( slot ) &&
                       network.passive.consensus->CheckCertificateForSlot( slot );
            },
            std::chrono::seconds( 20 ),
            "surviving peers retained the accepted certificate before publisher restart",
            nullptr );

        RestartPeer( network.first );
        ASSERT_TRUE( network.first.consensus );
        ConnectPeers( Peers( network ) );
        // ConnectPeers waits for the consensus-topic mesh before re-advertising;
        // GossipPubSub does not replay publications made before that readiness.
        // 2026-09-03 developer directive fallback (round-4 gap closure):
        // "post-restart certificate re-publication / surviving-replica serving".
        // The blacklist-duration hypothesis was DISPROVEN (round4-traces/
        // hypothesis-verdict.md): after RestartPeer the certificate-CID
        // graphsync route to the recreated publisher is erased on the first
        // blacklist, the boot-window re-dial fails transport-level, and no
        // surviving replica or re-publication exists for the completed
        // certificate. The recreated publisher durably holds the accepted
        // certificate (the Mint-effects barrier is downstream of certificate
        // acceptance and persistence-before-advertisement), so re-advertise the
        // UNCHANGED durable certificate through the public ingress route after
        // reconnecting, mirroring the recorded SubmitProposal re-advertisement
        // precedent (STATE.md: offline GossipPubSub broadcasts are not
        // replayed). The byte-identical replay is idempotent (CERT-05) and
        // recipients process it receive-only through validation (CERT-02) —
        // only the deterministic publisher authority inside SubmitCertificate
        // writes the certificate key.
        {
            const auto durable_certificate = network.first.consensus->GetCertificateBySlot( slot );
            ASSERT_TRUE( durable_certificate.has_value() );
            ASSERT_TRUE( network.first.consensus->SubmitCertificate( durable_certificate.value() ).has_value() );
        }
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.passive, slot, *winner, *loser );
            },
            std::chrono::seconds( 25 ),
            "recreated Mint peer repaired its marker through normal certificate recovery",
            nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
            AssertOneLiveMintEffect( *peer );
        }

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.passive, slot, *winner, *loser );
            },
            std::chrono::seconds( 20 ),
            "Mint-boundary recovery stayed exact after reopening every durable root",
            nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
        }
        diagnostics.MarkCompleted();
        StopNetwork( network );
    }
}

TEST_F( FinalityFaultNetwork, PublisherLossAfterPersistenceUsesDeterministicFailover )
{
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto network = StartNetwork( "publisher-loss", 54631 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                 network.passive.consensus );
    StoreRegistry( network );

    auto winner = MintFor( network.first );
    auto loser  = MintFor( network.first, 1 );
    ASSERT_TRUE( winner && loser );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( network.first.account->GetAddress(),
                                                               winner->GetNonce(),
                                                               winner->GetHash(),
                                                               winner->SerializeToEmbeddedTransaction(),
                                                               CommitmentFor( *winner ),
                                                               sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = network.first.consensus->CreateProposal(
        subject.value(),
        network.first.account->GetAddress(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

    const auto proposal_registry = network.first.blockchain->GetValidatorRegistry()->LoadVerifiedRegistryByCid(
        proposal.value().registry_cid() );
    ASSERT_TRUE( proposal_registry.has_value() );
    ASSERT_EQ( proposal_registry.value().epoch(), proposal.value().registry_epoch() );
    const auto active_validators = sgns::MultiNodeFinalityFaultTestAccess::OrderedActiveValidators(
        network.first.consensus,
        proposal_registry.value() );
    ASSERT_EQ( active_validators.size(), 3u );
    const auto proposal_hash = sgns::crypto::sha2_256( proposal.value().proposal_id().data(),
                                                       proposal.value().proposal_id().size() );
    uint64_t   base_index    = 0;
    for ( size_t index = 0; index < sizeof( uint64_t ) && index < proposal_hash.size(); ++index )
    {
        base_index = ( base_index << 8 ) | proposal_hash[index];
    }
    base_index %= active_validators.size();

    // Round selection happens after the normal candidate and certificate delay.
    // Arm the same post-write observer on validators solely to discover which
    // production-selected peer reached that completed action; only that peer
    // is stopped and the unused observers are released immediately.
    for ( auto *peer : { &network.first, &network.second, &network.third } )
    {
        sgns::MultiNodeFinalityFaultTestAccess::ArmCertificatePersistedBarrier( peer->consensus );
    }
    PublisherReadinessObserver observer( network );
    observer.EmitStart();
    auto scenario = [&]
    {
        ConnectPeers( { &network.first, &network.second, &network.third, &network.passive } );
        if ( ::testing::Test::HasFatalFailure() )
        {
            observer.MarkFailure();
            return;
        }
        observer.MarkReady();
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierEntered(
                           network.first.consensus ) ||
                       sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierEntered(
                           network.second.consensus ) ||
                       sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierEntered(
                           network.third.consensus );
            },
            std::chrono::seconds( 25 ),
            "production-selected publisher paused after immutable certificate persistence and before notification",
            nullptr );
        if ( ::testing::Test::HasFatalFailure() )
        {
            return;
        }
        Peer *publisher = nullptr;
        for ( auto *peer : { &network.first, &network.second, &network.third } )
        {
            if ( sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierEntered( peer->consensus ) )
            {
                publisher = peer;
            }
        }
        ASSERT_NE( publisher, nullptr );
        // Decision-round snapshot taken at aggregator-role evaluation — reading the
        // wall-clock round here (post-pause) races round boundaries during the
        // persist->observe window and intermittently predicted the wrong publisher.
        const auto persisted_round = sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierDecisionRound(
            publisher->consensus );
        ASSERT_NE( persisted_round, 0u ) << "decision-round snapshot missing (barrier not armed at decision?)";
        ASSERT_EQ( publisher->account->GetAddress(),
                   active_validators[( base_index + persisted_round ) % active_validators.size()] );
        ASSERT_TRUE( sgns::MultiNodeFinalityFaultTestAccess::IsCurrentAggregator( publisher->consensus,
                                                                                  proposal.value(),
                                                                                  proposal_registry.value(),
                                                                                  persisted_round ) );
        ASSERT_TRUE( publisher->consensus->CheckCertificateForSlot( slot ) );
        const auto durable_certificate = publisher->consensus->GetCertificateBySlot( slot );
        ASSERT_TRUE( durable_certificate.has_value() );
        const auto durable_bytes = durable_certificate.value().SerializeAsString();
        ASSERT_FALSE( durable_bytes.empty() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateWriteSuccesses( publisher->consensus ), 1u );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( publisher->consensus ),
                   0u );
        Peer *crdt_blind = nullptr;
        for ( auto *peer : { &network.first, &network.second, &network.third } )
        {
            if ( peer != publisher && !peer->consensus->CheckCertificateForSlot( slot ) )
            {
                crdt_blind = peer;
            }
        }
        ASSERT_NE( crdt_blind, nullptr );

        auto stopped_publisher = publisher->consensus;
        StopPeer( *publisher );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( stopped_publisher ), 0u );
        for ( auto *peer : { &network.first, &network.second, &network.third } )
        {
            if ( peer != publisher )
            {
                sgns::MultiNodeFinalityFaultTestAccess::ReleaseConsensusBarriers( peer->consensus );
            }
        }

        // The peer that did not have the record at the durability boundary remains
        // eligible for ordinary later-round aggregation.  CRDT precedence may make
        // the record visible first; in that case existing ProcessCertificates
        // clears/recoveries without a replacement write or vote.
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return sgns::MultiNodeFinalityFaultTestAccess::CurrentRound( crdt_blind->consensus,
                                                                             proposal.value().timestamp() ) >
                           persisted_round &&
                       sgns::MultiNodeFinalityFaultTestAccess::IsCurrentAggregator( crdt_blind->consensus,
                                                                                    proposal.value(),
                                                                                    proposal_registry.value() );
            },
            std::chrono::seconds( 25 ),
            "later production round kept the CRDT-blind peer eligible for deterministic aggregation",
            nullptr );
        stopped_publisher.reset();
        RestartPeer( *publisher );
        ASSERT_TRUE( publisher->consensus );
        ConnectPeers( Peers( network ) );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.passive, slot, *winner, *loser );
            },
            std::chrono::seconds( 25 ),
            "restarted publisher made the authoritative CRDT certificate normally recoverable on every peer",
            nullptr );
        for ( auto *peer : Peers( network ) )
        {
            EXPECT_LE( sgns::MultiNodeFinalityFaultTestAccess::CertificateWriteAttempts( peer->consensus ), 1u );
            const auto certificate = peer->consensus->GetCertificateBySlot( slot );
            ASSERT_TRUE( certificate.has_value() );
            EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ),
                       winner->GetHash() );
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
            AssertOneLiveMintEffect( *peer );
        }

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                       HasSingleDurableMint( network.passive, slot, *winner, *loser );
            },
            std::chrono::seconds( 25 ),
            "publisher-loss finality remained exact after reopening every peer root",
            nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
        }
    };
    scenario();
    if ( ::testing::Test::HasFatalFailure() )
    {
        observer.MarkUnclassifiedExit();
    }
    StopNetwork( network );
    const auto released = []( const Peer &peer )
    {
        return !peer.consensus && !peer.blockchain && !peer.transactions && !peer.db && !peer.pubsub && !peer.io &&
               !peer.io_thread.joinable();
    };
    const bool all_released = released( network.first ) && released( network.second ) && released( network.third ) &&
                              released( network.passive );
    EXPECT_TRUE( all_released );
    observer.EmitTerminal( all_released );
}

TEST_F( FinalityFaultNetwork, ForgedRegistryCidCertificateIsRejectedWithNoDurableRecord )
{
    /**
     * TRUST-01 (T-21-0401/T-21-0402) adversarial multi-node pair. Attacker
     * authority: a peer supplying an unauthorized registry CID -- a
     * self-certified RegistryUpdate (attacker's own validator and signature,
     * genesis shape, not rooted in the pinned genesis authority) injected into
     * the DAG with a raw put that bypasses the element filter. Expected
     * rejection: the certificate referencing that CID validates Reject and
     * submission fails WITHOUT writing the durable /cert/<slot> record
     * (asserted on the record path, not just the return value). Honest
     * certificates on the same node keep Approving (negative control).
     */
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto network = StartNetwork( "trust01-forged", 54731 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                 network.passive.consensus );
    StoreRegistry( network );

    auto attacker = MakeAttackerAccount( "trust01-forged-attacker" );
    ASSERT_TRUE( attacker );
    const auto forged_cid = MakeForgedRegistryCid( network.first, attacker );
    ASSERT_FALSE( forged_cid.empty() );

    // The forged snapshot is locally fetchable (the attacker supplied the
    // delta) -- the attack surface is its authorization.
    auto raw_load = network.first.blockchain->GetValidatorRegistry()->LoadUnverifiedRegistryByCid( forged_cid );
    ASSERT_TRUE( raw_load.has_value() );

    auto       forged_certificate = MakeCertificateForRegistry( attacker, forged_cid, 211, "0xtrust01-forged-cid" );
    const auto durable_key        = sgns::ConsensusManager::GetExpectedCertificateSlotKey( forged_certificate );
    ASSERT_FALSE( durable_key.empty() );

    EXPECT_EQ(
        sgns::MultiNodeFinalityFaultTestAccess::ValidateCertificate( network.first.consensus, forged_certificate ),
        sgns::ConsensusManager::Check::Reject );
    EXPECT_TRUE( network.first.consensus->SubmitCertificate( forged_certificate ).has_error() );
    EXPECT_FALSE( HasDurableCertificateRecord( network.first, durable_key ) );
    EXPECT_FALSE( network.first.consensus->CheckCertificateForSlot(
        sgns::MultiNodeFinalityFaultTestAccess::SlotKey( forged_certificate.proposal() ) ) );

    // Negative control on the same node: an honest certificate for the honest
    // registry still finalizes through the production route.
    auto winner = MintFor( network.first );
    ASSERT_TRUE( winner );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( network.first.account->GetAddress(),
                                                               winner->GetNonce(),
                                                               winner->GetHash(),
                                                               winner->SerializeToEmbeddedTransaction(),
                                                               CommitmentFor( *winner ),
                                                               sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto honest_proposal = network.first.consensus->CreateProposal(
        subject.value(),
        network.first.account->GetAddress(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( honest_proposal.has_value() );
    const auto honest_slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( honest_proposal.value() );

    ConnectPeers( { &network.first, &network.second, &network.third, &network.passive } );
    ASSERT_TRUE( network.first.consensus->SubmitProposal( honest_proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] { return network.first.consensus->CheckCertificateForSlot( honest_slot ); },
                               std::chrono::seconds( 25 ),
                               "honest certificate still finalizes alongside the forged rejection",
                               nullptr );

    StopNetwork( network );
}

TEST_F( FinalityFaultNetwork, RestartedFinalCertificateRevalidatesThroughGrandfatherProbe )
{
    /**
     * TRUST-01 D-01 compat (restart). A certificate that reached durable
     * finality re-validates Approve unchanged after the node restarts: the
     * grandfather probe (durable slot record present) selects the unverified
     * load, proven by the probe counter incrementing -- the strict
     * authorization rule is never consulted for finalized history.
     */
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto network = StartNetwork( "trust01-restart", 54741 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                 network.passive.consensus );
    StoreRegistry( network );

    auto winner = MintFor( network.first );
    ASSERT_TRUE( winner );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( network.first.account->GetAddress(),
                                                               winner->GetNonce(),
                                                               winner->GetHash(),
                                                               winner->SerializeToEmbeddedTransaction(),
                                                               CommitmentFor( *winner ),
                                                               sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = network.first.consensus->CreateProposal(
        subject.value(),
        network.first.account->GetAddress(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

    ConnectPeers( { &network.first, &network.second, &network.third, &network.passive } );
    ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] { return network.first.consensus->CheckCertificateForSlot( slot ); },
                               std::chrono::seconds( 25 ),
                               "honest certificate finalized before the restart",
                               nullptr );
    const auto durable_certificate = network.first.consensus->GetCertificateBySlot( slot );
    ASSERT_TRUE( durable_certificate.has_value() );

    RestartPeer( network.first );
    ASSERT_TRUE( network.first.consensus );
    ConnectPeers( Peers( network ) );

    // Durable finality re-reads Approve on the restarted node...
    ASSERT_WAIT_FOR_CONDITION( [&] { return network.first.consensus->CheckCertificateForSlot( slot ); },
                               std::chrono::seconds( 25 ),
                               "restarted node recovered its durable final certificate",
                               nullptr );
    // ...through the grandfather probe (strict rule skipped), proven by the
    // probe counter on the fresh post-restart manager.
    const auto probes_before = sgns::MultiNodeFinalityFaultTestAccess::GrandfatheredFinalityProbes(
        network.first.consensus );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::ValidateCertificate( network.first.consensus,
                                                                            durable_certificate.value() ),
               sgns::ConsensusManager::Check::Approve );
    EXPECT_GT( sgns::MultiNodeFinalityFaultTestAccess::GrandfatheredFinalityProbes( network.first.consensus ),
               probes_before );

    StopNetwork( network );
}

TEST_F( FinalityFaultNetwork, FreshNodeCatchUpStallsThenConvergesWithNoPinnedReject )
{
    /**
     * TRUST-01 compat (catch-up, T-21-0405) + verdict-memo discipline (Pitfall
     * 1 / T-21-0403). A fresh node that receives certificate knowledge before
     * the ancestor registry deltas stalls (retryable, never memoized, never
     * Reject); once the registry arrives through real CRDT gossip the same
     * node converges to the approved durable record.
     */
    sgns::GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto network = StartNetwork( "trust01-catchup", 54751 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus &&
                 network.passive.consensus );

    // Registry reaches ONLY the three validators; the passive stays fresh.
    const auto update = RegistryUpdate( { &network.first, &network.second, &network.third } );
    for ( auto *peer : { &network.first, &network.second, &network.third } )
    {
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    }
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return network.first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   network.second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                   network.third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
        },
        std::chrono::seconds( 5 ),
        "validators durably stored the registry",
        nullptr );

    auto winner = MintFor( network.first );
    ASSERT_TRUE( winner );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( network.first.account->GetAddress(),
                                                               winner->GetNonce(),
                                                               winner->GetHash(),
                                                               winner->SerializeToEmbeddedTransaction(),
                                                               CommitmentFor( *winner ),
                                                               sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = network.first.consensus->CreateProposal(
        subject.value(),
        network.first.account->GetAddress(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

    ConnectValidatorPeers( { &network.first, &network.second, &network.third } );
    ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] { return network.first.consensus->CheckCertificateForSlot( slot ); },
                               std::chrono::seconds( 25 ),
                               "validators finalized the certificate without the passive",
                               nullptr );
    const auto finalized_certificate = network.first.consensus->GetCertificateBySlot( slot );
    ASSERT_TRUE( finalized_certificate.has_value() );
    const auto durable_key = sgns::ConsensusManager::GetExpectedCertificateSlotKey( finalized_certificate.value() );
    ASSERT_FALSE( durable_key.empty() );

    // The fresh node holds certificate knowledge before any registry delta:
    // strict validation stalls on the missing dependency and memoizes no
    // verdict (a pinned Reject here would never converge).
    const auto passive_verdicts_before = sgns::MultiNodeFinalityFaultTestAccess::VerdictCacheEntryCount(
        network.passive.consensus,
        durable_key );
    EXPECT_EQ( passive_verdicts_before, 0u );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::ValidateCertificate( network.passive.consensus,
                                                                            finalized_certificate.value() ),
               sgns::ConsensusManager::Check::Stalled );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::VerdictCacheEntryCount( network.passive.consensus, durable_key ),
               0u );

    // Deliver the missing deltas through the real CRDT gossip path: connect
    // the fresh node, then have a validator head-broadcast its monitored
    // topics so the passive graphsync-fetches the registry and certificate
    // deltas it missed while offline (the production catch-up delivery).
    ConnectPeers( Peers( network ) );
    {
        const auto monitored_result = network.first.db->GetMonitoredTopics();
        ASSERT_FALSE( monitored_result.has_error() );
        const auto           &monitored = monitored_result.value();
        std::set<std::string> topics( monitored.begin(), monitored.end() );
        ASSERT_FALSE( topics.empty() );
        ASSERT_FALSE( network.first.db->RequestHeadBroadcast( topics ).has_error() );
    }
    ASSERT_WAIT_FOR_CONDITION( [&] { return network.passive.consensus->CheckCertificateForSlot( slot ); },
                               std::chrono::seconds( 25 ),
                               "fresh node converged to the approved durable record once the registry synced",
                               nullptr );
    const auto converged = network.passive.consensus->GetCertificateBySlot( slot );
    ASSERT_TRUE( converged.has_value() );

    StopNetwork( network );
}
