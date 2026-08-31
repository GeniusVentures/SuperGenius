/**
 * @file multi_node_finality_fault_test.cpp
 * @brief Persistent four-peer production-route audit for Phase 12.
 */

#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "account/MintTransactionV2.hpp"
#include "account/TransactionManager.hpp"
#include "account/UTXOMerkle.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/crdt_work_journal.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crypto/hasher.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"

#include <algorithm>
#include <array>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <memory>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace sgns
{
    class MultiNodeFinalityFaultTestAccess
    {
    public:
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
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.vote_publications;
        }

        static uint64_t CertificateNotificationsReceived( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_notifications_received;
        }

        static uint64_t CertificateNotificationsPublished( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_notification_publications;
        }

        static uint64_t CertificateWriteAttempts( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_write_attempts;
        }

        static uint64_t CertificateWriteSuccesses( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.certificate_write_successes;
        }

        static uint64_t AcceptedCertificateReadbacks( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.accepted_certificate_readbacks;
        }

        static uint64_t ActiveVoteReleaseAttempts( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.active_vote_release_attempts;
        }

        static uint64_t ActiveVoteReleaseSuccesses( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return 0;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.active_vote_release_successes;
        }

        static std::optional<std::string> DurableActiveVoteProposalId( const std::shared_ptr<ConsensusManager> &manager,
                                                                       const std::string &slot )
        {
            if ( !manager || !manager->db_ ) return std::nullopt;
            crdt::GlobalDB::Buffer key;
            key.put( manager->ActiveVoteStorageKey( slot ) );
            const auto stored = manager->db_->GetDataStore()->get( key );
            if ( stored.has_error() ) return std::nullopt;
            const auto decoded = manager->DecodeActiveVoteRecord( slot, stored.value().toString() );
            return decoded.has_value() ? std::optional<std::string>( decoded.value().proposal.proposal_id() ) : std::nullopt;
        }

        static std::string ActiveVoteStorageKey( const std::shared_ptr<ConsensusManager> &manager,
                                                 const std::string &slot )
        {
            return manager ? manager->ActiveVoteStorageKey( slot ) : std::string{};
        }

        static std::optional<std::string> RecoveredActiveVoteProposalId( const std::shared_ptr<ConsensusManager> &manager,
                                                                         const std::string &slot )
        {
            if ( !manager ) return std::nullopt;
            std::lock_guard lock( manager->proposals_mutex_ );
            const auto        found = manager->active_votes_.find( slot );
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
            return manager && manager->certificate_work_journal_ &&
                   !manager->certificate_work_journal_->ListUnfinished( manager->CERT_KEY_PATTERN ).empty();
        }

        static int TrackedTransactionState( const TransactionManager &transactions, const std::string &transaction_id )
        {
            std::shared_lock lock( transactions.tx_mutex_m );
            for ( const auto &[_, tracked] : transactions.tx_processed_m )
                if ( tracked.tx && tracked.tx->GetHash() == transaction_id ) return static_cast<int>( tracked.status );
            return -1;
        }

        static std::string SlotKey( const ConsensusManager::Proposal &proposal )
        {
            return ConsensusManager::GetSlotKey( proposal );
        }

        static std::optional<std::string> NonceTransactionHash( const ConsensusManager::Proposal &proposal )
        {
            const auto nonce_subject = ConsensusManager::DecodeNonceSubject( proposal.subject() );
            return nonce_subject.has_value() ? std::optional<std::string>( nonce_subject.value().tx_hash() ) : std::nullopt;
        }

        static std::vector<std::string> OrderedActiveValidators( const std::shared_ptr<ConsensusManager> &manager,
                                                                 const ValidatorRegistry::Registry &registry )
        {
            return manager->GetOrderedActiveValidators( registry );
        }

        static uint64_t CurrentRound( const std::shared_ptr<ConsensusManager> &manager, uint64_t proposal_timestamp )
        {
            return manager->GetCurrentRound( proposal_timestamp );
        }

        static bool IsCurrentAggregator( const std::shared_ptr<ConsensusManager> &manager,
                                         const ConsensusManager::Proposal &proposal,
                                         const ValidatorRegistry::Registry &registry )
        {
            return manager->GetAggregatorRole( proposal, registry ) == ConsensusManager::AggregatorRole::CurrentAggregator;
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
            manager->certificate_persisted_barrier_ = { true, false, false };
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

        static bool CertificatePersistedBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->certificate_persisted_barrier_.entered;
        }

        static void ReleaseConsensusBarriers( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return;
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->active_vote_persisted_barrier_.released = true;
            manager->certificate_persisted_barrier_.released = true;
            manager->accepted_certificate_barrier_.released = true;
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
            if ( !transactions ) return;
            std::lock_guard lock( transactions->fault_test_mutex_ );
            transactions->mint_effects_barrier_.released = true;
            transactions->fault_test_cv_.notify_all();
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
            ActiveVoteLifecycleSnapshot                       after_consensus_manager_close;
            ActiveVoteLifecycleSnapshot                       after_manager_ownership_release;
            ActiveVoteLifecycleSnapshot                       after_same_root_globaldb_reopen_before_manager;

            Peer() = default;
            Peer( const Peer & ) = delete;
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
                after_consensus_manager_close( std::move( other.after_consensus_manager_close ) ),
                after_manager_ownership_release( std::move( other.after_manager_ownership_release ) ),
                after_same_root_globaldb_reopen_before_manager(
                    std::move( other.after_same_root_globaldb_reopen_before_manager ) )
            {
            }

            Peer &operator=( Peer &&other ) noexcept
            {
                if ( this == &other ) return *this;
                Stop();
                name         = std::move( other.name );
                port         = other.port;
                root         = std::move( other.root );
                io           = std::move( other.io );
                pubsub       = std::move( other.pubsub );
                db           = std::move( other.db );
                account      = std::move( other.account );
                blockchain   = std::move( other.blockchain );
                transactions = std::move( other.transactions );
                consensus    = std::move( other.consensus );
                io_thread    = std::move( other.io_thread );
                active_vote_diagnostic_slot = std::move( other.active_vote_diagnostic_slot );
                active_vote_diagnostic_key  = std::move( other.active_vote_diagnostic_key );
                after_consensus_manager_close = std::move( other.after_consensus_manager_close );
                after_manager_ownership_release = std::move( other.after_manager_ownership_release );
                after_same_root_globaldb_reopen_before_manager =
                    std::move( other.after_same_root_globaldb_reopen_before_manager );
                return *this;
            }

            static ActiveVoteLifecycleSnapshot Snapshot( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                                                          const std::string &storage_key )
            {
                ActiveVoteLifecycleSnapshot snapshot;
                snapshot.storage_key = storage_key;
                if ( !db || storage_key.empty() ) return snapshot;
                sgns::crdt::GlobalDB::Buffer key;
                key.put( storage_key );
                const auto stored = db->GetDataStore()->get( key );
                if ( stored.has_error() ) return snapshot;
                const auto bytes = stored.value().toString();
                snapshot.record_present = true;
                snapshot.record_digest  = sgns::crypto::sha2_256( bytes.data(), bytes.size() ).toHex();
                return snapshot;
            }

            ~Peer()
            {
                Stop();
            }

            void Stop() noexcept
            {
                if ( transactions ) transactions->Stop();
                transactions.reset();
                if ( blockchain ) (void) blockchain->Stop();
                if ( consensus ) consensus->Close();
                after_consensus_manager_close = Snapshot( db, active_vote_diagnostic_key );
                consensus.reset();
                blockchain.reset();
                after_manager_ownership_release = Snapshot( db, active_vote_diagnostic_key );
                if ( io ) io->stop();
                if ( io_thread.joinable() ) io_thread.join();
                if ( pubsub ) pubsub->Stop();
                db.reset();
                pubsub.reset();
                account.reset();
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
            MintRecoveryDiagnostics( Network &network, const std::string &slot, const sgns::MintTransactionV2 &winner,
                                     const sgns::MintTransactionV2 &loser ) :
                network_( network ), slot_( slot ), winner_( winner ), loser_( loser ), run_( std::getenv( "P12_07_RUN" ) )
            {
            }

            ~MintRecoveryDiagnostics()
            {
                if ( !run_ || !*run_ ) return;
                const auto snapshot = completed_snapshot_.has_value() ? completed_snapshot_.value() : Capture();
                const auto outcome  = completed_ ? "pass" : "failure";
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
                snapshot.canonical_slot_id     = slot_;
                snapshot.winning_transaction_id = winner_.GetHash();
                snapshot.sequence               = 1;
                snapshot.certificate_present = network_.first.consensus && network_.first.consensus->CheckCertificateForSlot( slot_ );
                snapshot.sequence++;
                if ( snapshot.certificate_present )
                {
                    const auto certificate = network_.first.consensus->GetCertificateBySlot( slot_ );
                    snapshot.exact_binding = certificate.has_value() &&
                                             sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ) ==
                                                 winner_.GetHash();
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
                                                   *network_.first.transactions, winner_.GetHash() )
                                             : -1;
                return snapshot;
            }

            static Diagnosis Classify( const MintRecoverySnapshot &snapshot )
            {
                if ( !snapshot.certificate_present ) return { "certificate-readback", "absent", "not-found", 1 };
                if ( !snapshot.exact_binding ) return { "exact-transaction-binding", "mismatch", "invalid-binding", 2 };
                if ( !snapshot.winner_outpoint ) return { "utxo-outpoint", "missing", "not-found", 3 };
                if ( !snapshot.bridge_marker ) return { "bridge-marker-read", "absent", "not-found", 4 };
                if ( snapshot.journal_unfinished || snapshot.tracked_state != static_cast<int>( sgns::TransactionManager::TransactionStatus::CONFIRMED ) )
                    return { "journal-tracked-state", "unfinished", "retryable", 5 };
                return { "none", "complete", "none", 6 };
            }

            Network &                         network_;
            const std::string &               slot_;
            const sgns::MintTransactionV2 &   winner_;
            const sgns::MintTransactionV2 &   loser_;
            const char *                      run_       = nullptr;
            bool                              completed_ = false;
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
                const auto peers = Peers( network_ );
                successful_diagnosis_ = WithNetworkSnapshot(
                    Describe( peers.front(), "none", "ready", "none", IntendedConnectedness( peers ) ), peers );
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
                if ( !peer || !peer->pubsub ) return "unavailable";
                const auto host = peer->pubsub->GetHost();
                return host ? host->getId().toBase58() : "unavailable";
            }

            static std::string ListenerState( const Peer *peer )
            {
                if ( !peer ) return "missing";
                return "port-" + std::to_string( peer->port ) + "-pubsub-" +
                       ( peer->pubsub && peer->pubsub->IsStarted() ? "started" : "stopped" );
            }

            static std::string RootLifecycle( const Peer *peer )
            {
                if ( !peer ) return "missing";
                return std::string( boost::filesystem::exists( peer->root ) ? "root-present" : "root-missing" ) +
                       "-io-thread-" + ( peer->io_thread.joinable() ? "joinable" : "not-joinable" );
            }

            static size_t ConsensusMesh( const Peer *peer )
            {
                if ( !peer || !peer->pubsub || !peer->pubsub->IsStarted() || !peer->consensus ||
                     !peer->pubsub->GetHost() )
                    return 0;
                return peer->pubsub->getPeerCount( sgns::MultiNodeFinalityFaultTestAccess::ConsensusTopic( peer->consensus ) );
            }

            static std::string IntendedConnectedness( const std::array<Peer *, 4> &peers )
            {
                std::string connections;
                for ( size_t source_index = 0; source_index < peers.size(); ++source_index )
                    for ( size_t target_index = 0; target_index < peers.size(); ++target_index )
                    {
                        if ( source_index == target_index ) continue;
                        if ( !connections.empty() ) connections += ',';
                        const auto *source = peers[source_index];
                        const auto *target = peers[target_index];
                        connections += ( source ? source->name : "missing" ) + std::string( "-to-" ) +
                                       ( target ? target->name : "missing" ) + '-';
                        const auto source_host = source && source->pubsub ? source->pubsub->GetHost() : nullptr;
                        const auto target_host = target && target->pubsub ? target->pubsub->GetHost() : nullptr;
                        const bool connected = source_host && target_host &&
                                               source_host->connectedness( target_host->getPeerInfo() ) ==
                                                   libp2p::Host::Connectedness::CONNECTED;
                        connections += connected ? "connected" : "not-connected";
                    }
                return connections;
            }

            static Diagnosis Describe( const Peer *peer, std::string boundary, std::string state, std::string error,
                                       std::string intended_connectedness = "not-evaluated" )
            {
                return { std::move( boundary ), std::move( state ), std::move( error ), PeerIdentity( peer ),
                         ListenerState( peer ), RootLifecycle( peer ), std::move( intended_connectedness ),
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
                        diagnosis.peer_identity += ',';
                        diagnosis.listener += ',';
                        diagnosis.root_lifecycle += ',';
                        diagnosis.consensus_mesh += ',';
                    }
                    const auto name = peer ? peer->name : "missing";
                    diagnosis.peer_identity += name + "-" + PeerIdentity( peer );
                    diagnosis.listener += name + "-" + ListenerState( peer );
                    diagnosis.root_lifecycle += name + "-" + RootLifecycle( peer );
                    diagnosis.consensus_mesh += name + "-" + std::to_string( ConsensusMesh( peer ) );
                }
                return diagnosis;
            }

            Diagnosis Classify() const
            {
                const auto peers = Peers( network_ );
                const auto intended_connectedness = IntendedConnectedness( peers );
                for ( const auto *peer : peers )
                {
                    if ( !peer )
                        return WithNetworkSnapshot( Describe( peer, "missing-peer", "absent", "not-found",
                                                              intended_connectedness ),
                                                    peers );
                    if ( !peer->pubsub || !peer->pubsub->IsStarted() )
                        return WithNetworkSnapshot( Describe( peer, "pubsub-not-started", "stopped", "not-started",
                                                              intended_connectedness ),
                                                    peers );
                    if ( !peer->consensus )
                        return WithNetworkSnapshot( Describe( peer, "missing-consensus", "absent", "not-found",
                                                              intended_connectedness ),
                                                    peers );
                    if ( !peer->pubsub->GetHost() )
                        return WithNetworkSnapshot( Describe( peer, "missing-host", "absent", "not-found",
                                                              intended_connectedness ),
                                                    peers );
                    if ( ConsensusMesh( peer ) < 1 )
                        return WithNetworkSnapshot( Describe( peer, "zero-consensus-topic-mesh", "zero",
                                                              "no-consensus-neighbor", intended_connectedness ),
                                                    peers );
                }

                std::array<bool, 4> reachable{};
                reachable.front() = true;
                for ( size_t pass = 0; pass < peers.size(); ++pass )
                    for ( size_t source_index = 0; source_index < peers.size(); ++source_index )
                    {
                        if ( !reachable[source_index] ) continue;
                        for ( size_t target_index = 0; target_index < peers.size(); ++target_index )
                        {
                            if ( reachable[target_index] || source_index == target_index ) continue;
                            const auto source_host = peers[source_index]->pubsub->GetHost();
                            const auto target_host = peers[target_index]->pubsub->GetHost();
                            if ( source_host->connectedness( target_host->getPeerInfo() ) == libp2p::Host::Connectedness::CONNECTED ||
                                 target_host->connectedness( source_host->getPeerInfo() ) == libp2p::Host::Connectedness::CONNECTED )
                                reachable[target_index] = true;
                        }
                    }

                for ( size_t source_index = 0; source_index < peers.size(); ++source_index )
                    if ( reachable[source_index] )
                        for ( size_t target_index = 0; target_index < peers.size(); ++target_index )
                            if ( !reachable[target_index] && source_index != target_index )
                                return WithNetworkSnapshot(
                                    Describe( peers[source_index], "disconnected-intended-peer", "disconnected",
                                              "no-intended-peer-link", intended_connectedness ),
                                    peers );

                return WithNetworkSnapshot(
                    Describe( peers.front(), "none", "recovered-after-deadline", "unknown-first-readiness-boundary",
                              intended_connectedness ),
                    peers );
            }

            Network &   network_;
            bool        ready_ = false;
            Diagnosis   successful_diagnosis_;
        };

        class PublisherReadinessObserver
        {
        public:
            explicit PublisherReadinessObserver( Network &network ) : network_( network )
            {
                const char *run_token = std::getenv( "P12_09_RUN_TOKEN" );
                if ( !run_token || !*run_token ) return;
                try
                {
                    const auto executable = boost::filesystem::canonical( boost::dll::program_location() );
                    fingerprint_.run_token = run_token;
                    fingerprint_.path = executable.string();
                    fingerprint_.size = boost::filesystem::file_size( executable );
                    fingerprint_.mtime = boost::filesystem::last_write_time( executable );
                    fingerprint_.pid = static_cast<long>( ::getpid() );
                    valid_ = !fingerprint_.path.empty();
                }
                catch ( const boost::filesystem::filesystem_error & )
                {
                    invalid_reason_ = "fingerprint-unavailable";
                }
            }

            void EmitStart()
            {
                if ( !valid_ || emitted_start_ ) return;
                Write( "P12_PUBLISHER_OBSERVER_START " + Header() );
                emitted_start_ = true;
            }

            void MarkReady()
            {
                diagnosis_ = { "none", "ready", "none" };
                ready_ = true;
            }

            void MarkFailure()
            {
                PublisherReadinessSnapshot snapshot( network_ );
                const auto diagnosis = snapshot.FirstFailure();
                diagnosis_ = { diagnosis[0], diagnosis[1], diagnosis[2] };
                readiness_failure_ = true;
            }

            void MarkUnclassifiedExit()
            {
                if ( readiness_failure_ ) return;
                ready_ = false;
                diagnosis_.reset();
            }

            void EmitTerminal( bool released )
            {
                if ( emitted_terminal_ || !emitted_start_ ) return;
                emitted_terminal_ = true;
                if ( released && ready_ )
                {
                    Write( "P12_PUBLISHER_OBSERVER_TERMINAL " + Header() +
                           " outcome=pass terminal=complete peer_release=all-four-runtime-handles-released boundary=none state=ready error=none" );
                    return;
                }
                if ( released && diagnosis_.has_value() )
                {
                    Write( "P12_PUBLISHER_OBSERVER_TERMINAL " + Header() + " outcome=failure terminal=complete "
                           "peer_release=all-four-runtime-handles-released boundary=" + diagnosis_->boundary +
                           " state=" + diagnosis_->state + " error=" + diagnosis_->error );
                    return;
                }
                EmitIncomplete( released ? "unclassified-scenario-exit" : "peer-release-unproven" );
            }

            void EmitIncomplete( const std::string &reason )
            {
                if ( !emitted_start_ || incomplete_emitted_ ) return;
                incomplete_emitted_ = true;
                Write( "P12_PUBLISHER_OBSERVER_TERMINAL " + Header() + " outcome=incomplete terminal=incomplete peer_release=" +
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

            std::string Header() const
            {
                std::ostringstream record;
                record << "run_token=" << fingerprint_.run_token << " schema=p12-observer-v1 pid=" << fingerprint_.pid
                       << " exe_path=" << fingerprint_.path << " exe_size=" << fingerprint_.size
                       << " exe_mtime=" << fingerprint_.mtime;
                return record.str();
            }

            static void Write( const std::string &record )
            {
                static std::mutex writer_mutex;
                std::lock_guard lock( writer_mutex );
                std::cerr << record << '\n' << std::flush;
            }

            Network &                    network_;
            Fingerprint                  fingerprint_;
            std::optional<Diagnosis>     diagnosis_;
            bool                         valid_              = false;
            bool                         emitted_start_      = false;
            bool                         emitted_terminal_   = false;
            bool                         incomplete_emitted_ = false;
            bool                         ready_              = false;
            bool                         readiness_failure_  = false;
            std::string                  invalid_reason_;
        };

        FinalityFaultNetwork() : CRDTFixture( "multi_node_finality_fault" )
        {
        }

        Peer StartPeer( const std::string &name, uint16_t port, std::string active_vote_diagnostic_key = {} )
        {
            Peer peer;
            peer.name = name;
            peer.port = port;
            peer.active_vote_diagnostic_key = std::move( active_vote_diagnostic_key );
            peer.root = ( base_path / name ).string();
            boost::filesystem::create_directories( peer.root );
            peer.io = std::make_shared<boost::asio::io_context>();
            auto keypair = sgns::crdt::KeyPairFileStorage( peer.root + "/keypair" ).GetKeyPair();
            EXPECT_TRUE( keypair.has_value() );
            if ( keypair.has_error() ) return peer;
            peer.pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keypair.value() );
            EXPECT_TRUE( peer.pubsub );
            if ( !peer.pubsub ) return peer;
            EXPECT_FALSE( peer.pubsub->Start( port, { peer.pubsub->GetLocalAddress() } ).get() );
            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( peer.io ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto graphsync = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( peer.pubsub->GetHost(), scheduler );
            auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
            auto db = sgns::crdt::GlobalDB::New( peer.io, peer.root + "/rocksdb", peer.pubsub,
                                                 sgns::crdt::CrdtOptions::DefaultOptions(), graphsync, scheduler, generator );
            EXPECT_TRUE( db.has_value() );
            if ( db.has_error() ) return peer;
            peer.db = std::move( db.value() );
            peer.db->Start();
            peer.after_same_root_globaldb_reopen_before_manager =
                Peer::Snapshot( peer.db, peer.active_vote_diagnostic_key );
            peer.io_thread = std::thread( [io = peer.io] { io->run(); } );
            peer.account = sgns::GeniusAccount::New( kToken, peer.root + "/account" );
            EXPECT_TRUE( peer.account );
            if ( !peer.account ) return peer;
            EXPECT_TRUE( peer.account->GetUTXOManager().LoadUTXOs( peer.db->GetDataStore() ).has_value() );
            peer.blockchain = sgns::Blockchain::New( peer.db, peer.account, peer.pubsub, []( outcome::result<void> ) {} );
            EXPECT_TRUE( peer.blockchain );
            if ( !peer.blockchain ) return peer;
            peer.transactions = sgns::TransactionManager::New( peer.db, peer.io, peer.account, peer.blockchain, false );
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
            const auto name = peer.name;
            const auto port = peer.port;
            const auto active_vote_diagnostic_key = peer.active_vote_diagnostic_key;
            StopPeer( peer );
            auto after_consensus_manager_close = std::move( peer.after_consensus_manager_close );
            auto after_manager_ownership_release = std::move( peer.after_manager_ownership_release );
            auto restarted = StartPeer( name, port, active_vote_diagnostic_key );
            restarted.after_consensus_manager_close   = std::move( after_consensus_manager_close );
            restarted.after_manager_ownership_release = std::move( after_manager_ownership_release );
            peer = std::move( restarted );
        }

        Network StartNetwork( const std::string &prefix, uint16_t first_port )
        {
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
                ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
            ASSERT_WAIT_FOR_CONDITION( [&] {
                return network.first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network.second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network.third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
                       network.passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
            }, std::chrono::seconds( 5 ), "every peer durably stored the validator registry", nullptr );
        }

        void RestartAndReconnect( Network &network )
        {
            RestartPeer( network.first );
            RestartPeer( network.second );
            RestartPeer( network.third );
            RestartPeer( network.passive );
            ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
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
            for ( auto *peer : peers )
                if ( !peer || !peer->pubsub || !peer->pubsub->IsStarted() || !peer->consensus ||
                     !peer->pubsub->GetHost() ||
                     peer->pubsub->getPeerCount(
                         sgns::MultiNodeFinalityFaultTestAccess::ConsensusTopic( peer->consensus ) ) < 1 )
                    return false;

            reachable.front() = true;
            for ( size_t pass = 0; pass < Count; ++pass )
                for ( size_t source_index = 0; source_index < Count; ++source_index )
                {
                    if ( !reachable[source_index] ) continue;
                    for ( size_t target_index = 0; target_index < Count; ++target_index )
                    {
                        if ( reachable[target_index] || source_index == target_index ) continue;
                        const auto source_host = peers[source_index]->pubsub->GetHost();
                        const auto target_host = peers[target_index]->pubsub->GetHost();
                        if ( source_host->connectedness( target_host->getPeerInfo() ) == libp2p::Host::Connectedness::CONNECTED ||
                             target_host->connectedness( source_host->getPeerInfo() ) == libp2p::Host::Connectedness::CONNECTED )
                            reachable[target_index] = true;
                    }
                }
            return std::all_of( reachable.begin(), reachable.end(), []( bool connected ) { return connected; } );
        }

        template <size_t Count>
        static void ConnectAndWaitForPeers( const std::array<Peer *, Count> &peers )
        {
            for ( auto *source : peers )
                for ( auto *target : peers )
                    if ( source != target ) source->pubsub->AddPeers( { target->pubsub->GetInterfaceAddress() } );
            ASSERT_WAIT_FOR_CONDITION( [&] { return PeersFormConnectedTopology( peers ); },
                                       std::chrono::seconds( 5 ),
                                       "every peer is started in one public libp2p topology with a consensus-topic neighbor",
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
            auto signature = validators.front()->account->Sign( std::vector<uint8_t>( bytes.begin(), bytes.end() ) );
            auto *entry = update.add_signatures();
            entry->set_validator_id( validators.front()->account->GetAddress() );
            entry->set_signature( signature.data(), signature.size() );
            return update;
        }

        static std::shared_ptr<sgns::MintTransactionV2> MintFor( const Peer &peer, uint64_t timestamp_offset = 0 )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "mint-v2" );
            dag.set_source_addr( peer.account->GetAddress() );
            dag.set_nonce( 0 );
            dag.set_timestamp( std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch() ).count() + timestamp_offset );
            const auto burn_hash = sgns::base::Hash256::fromReadableString( std::string( 64, 'c' ) );
            EXPECT_TRUE( burn_hash.has_value() );
            dag.set_uncle_hash( burn_hash.value().toReadableString() );
            auto mint = std::make_shared<sgns::MintTransactionV2>( sgns::MintTransactionV2::New(
                42, "test", kToken, std::move( dag ), { { burn_hash.value(), 0, {} } }, peer.account->GetAddress() ) );
            mint->MakeSignature( *peer.account );
            return mint;
        }

        static sgns::UTXOTransitionCommitment CommitmentFor( const sgns::MintTransactionV2 &mint )
        {
            sgns::UTXOTransitionCommitment commitment;
            const auto params = mint.GetUTXOParameters();
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
                const auto &output = params.second[index];
                sgns::GeniusUTXO produced( mint_hash.value(), index, output.encrypted_amount, output.token_id, output.dest_address );
                auto *committed = commitment.add_produced_outputs();
                committed->set_tx_id_hash( mint_hash.value().data(), mint_hash.value().size() );
                committed->set_output_index( index );
                committed->set_owner_address( output.dest_address );
                const auto token_bytes = output.token_id.bytes();
                committed->set_token_id( token_bytes.data(), token_bytes.size() );
                committed->set_amount( output.encrypted_amount );
                produced_payloads.push_back( sgns::utxo_merkle::SerializeUTXOLeafPayload( produced ) );
            }

            const auto consumed_root = sgns::utxo_merkle::ComputeMerkleRootFromPayloads( std::move( consumed_payloads ) );
            const auto produced_root = sgns::utxo_merkle::ComputeMerkleRootFromPayloads( std::move( produced_payloads ) );
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

        static bool HasOnlyWinnerOutput( const Peer &peer, const sgns::MintTransactionV2 &winner,
                                         const sgns::MintTransactionV2 &loser )
        {
            const auto outputs = peer.account->GetUTXOManager().GetUTXOs( winner.dag_st.source_addr() );
            return outputs.size() == 1 && outputs.front().GetTxID().toReadableString() == winner.GetHash() &&
                   outputs.front().GetTxID().toReadableString() != loser.GetHash();
        }

        static bool HasSingleDurableMint( const Peer &peer, const std::string &slot,
                                          const sgns::MintTransactionV2 &winner,
                                          const sgns::MintTransactionV2 &loser )
        {
            if ( !peer.consensus || !peer.consensus->CheckCertificateForSlot( slot ) ) return false;
            const auto certificate = peer.consensus->GetCertificateBySlot( slot );
            return certificate.has_value() &&
                   sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ) ==
                       winner.GetHash() &&
                   HasOnlyWinnerOutput( peer, winner, loser ) && HasBridgeMarker( peer, winner );
        }

        static void AssertSingleDurableMint( const Peer &peer, const std::string &slot,
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

        static void AssertOneLiveMintEffect( const Peer &peer )
        {
            ASSERT_TRUE( peer.transactions );
            EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *peer.transactions ), 1u );
        }
    };
} // namespace

namespace
{
    class PublisherObserverEvidenceEvaluator
    {
    public:
        struct ExpectedProcess
        {
            std::string run_token;
            std::string schema;
            std::string pid;
            std::string exe_path;
            std::string exe_size;
            std::string exe_mtime;
        };

        class Record
        {
        public:
            const std::string &Classification() const
            {
                return classification_;
            }

            bool IsEligibleObserverLifecycleFailure() const
            {
                return classification_ == "fully_attributed_complete_failure" && observer_lifecycle_eligible_;
            }

            const std::string &Boundary() const
            {
                return boundary_;
            }

            const std::string &State() const
            {
                return state_;
            }

            const std::string &Error() const
            {
                return error_;
            }

        private:
            friend class PublisherObserverEvidenceEvaluator;

            Record() = default;

            std::string classification_ = "invalid_or_partial_blocked";
            std::string boundary_;
            std::string state_;
            std::string error_;
            bool        observer_lifecycle_eligible_ = false;
        };

        static Record Evaluate( const std::vector<std::string> &lines, int process_exit,
                                const ExpectedProcess &expected_process )
        {
            std::vector<ParsedRecord> starts;
            std::vector<ParsedRecord> terminals;
            bool                      gtest_passed = false;
            bool                      gtest_failed = false;
            bool                      malformed_observer_line = false;

            for ( const auto &line : lines )
            {
                if ( line.find( "P12_PUBLISHER_OBSERVER_" ) == 0 )
                {
                    const auto parsed = ParseObserverLine( line );
                    if ( !parsed.has_value() )
                    {
                        malformed_observer_line = true;
                        continue;
                    }
                    ( parsed->kind == "START" ? starts : terminals ).push_back( parsed.value() );
                }
                else if ( line.find( "[  PASSED  ] 1 test" ) == 0 )
                    gtest_passed = true;
                else if ( line.find( "[  FAILED  ] 1 test" ) == 0 )
                    gtest_failed = true;
            }

            Record record;
            if ( malformed_observer_line || starts.size() != 1 || terminals.size() != 1 || gtest_passed == gtest_failed )
                return record;

            const auto &start    = starts.front();
            const auto &terminal = terminals.front();
            if ( !SameFingerprint( start, terminal ) ) return record;
            if ( !MatchesExpectedProcess( start, expected_process ) )
            {
                record.classification_ = "tooling_attribution_rebuild";
                return record;
            }
            if ( terminal.fields.at( "terminal" ) != "complete" ||
                 terminal.fields.at( "peer_release" ) != "all-four-runtime-handles-released" )
                return record;

            record.boundary_ = terminal.fields.at( "boundary" );
            record.state_    = terminal.fields.at( "state" );
            record.error_    = terminal.fields.at( "error" );
            if ( terminal.fields.at( "outcome" ) == "pass" && process_exit == 0 && gtest_passed )
                record.classification_ = "fully_attributed_complete_pass";
            else if ( terminal.fields.at( "outcome" ) == "failure" && process_exit != 0 && gtest_failed )
            {
                record.classification_ = "fully_attributed_complete_failure";
                record.observer_lifecycle_eligible_ = IsAllowedObserverLifecycleTriple(
                    record.boundary_, record.state_, record.error_ );
            }
            return record;
        }

    private:
        struct ParsedRecord
        {
            std::string kind;
            std::map<std::string, std::string> fields;
        };

        static std::optional<ParsedRecord> ParseObserverLine( const std::string &line )
        {
            constexpr const char *kPrefix = "P12_PUBLISHER_OBSERVER_";
            std::istringstream    input( line );
            std::string           marker;
            if ( !( input >> marker ) || marker.find( kPrefix ) != 0 ) return std::nullopt;

            ParsedRecord record;
            record.kind = marker.substr( std::char_traits<char>::length( kPrefix ) );
            if ( record.kind != "START" && record.kind != "TERMINAL" ) return std::nullopt;
            std::string field;
            while ( input >> field )
            {
                const auto delimiter = field.find( '=' );
                if ( delimiter == std::string::npos || delimiter == 0 || delimiter == field.size() - 1 ||
                     !record.fields.emplace( field.substr( 0, delimiter ), field.substr( delimiter + 1 ) ).second )
                    return std::nullopt;
            }

            static const std::array<std::string, 6> kFingerprintFields{
                "run_token", "schema", "pid", "exe_path", "exe_size", "exe_mtime"
            };
            for ( const auto &name : kFingerprintFields )
                if ( record.fields.find( name ) == record.fields.end() || record.fields.at( name ).empty() )
                    return std::nullopt;
            if ( record.kind == "START" )
                return record.fields.size() == kFingerprintFields.size() ? std::optional<ParsedRecord>( record ) : std::nullopt;

            static const std::array<std::string, 6> kTerminalFields{
                "outcome", "terminal", "peer_release", "boundary", "state", "error"
            };
            for ( const auto &name : kTerminalFields )
                if ( record.fields.find( name ) == record.fields.end() || record.fields.at( name ).empty() )
                    return std::nullopt;
            return record.fields.size() == kFingerprintFields.size() + kTerminalFields.size()
                       ? std::optional<ParsedRecord>( record )
                       : std::nullopt;
        }

        static bool SameFingerprint( const ParsedRecord &left, const ParsedRecord &right )
        {
            return left.fields.at( "run_token" ) == right.fields.at( "run_token" ) &&
                   left.fields.at( "schema" ) == right.fields.at( "schema" ) &&
                   left.fields.at( "pid" ) == right.fields.at( "pid" ) &&
                   left.fields.at( "exe_path" ) == right.fields.at( "exe_path" ) &&
                   left.fields.at( "exe_size" ) == right.fields.at( "exe_size" ) &&
                   left.fields.at( "exe_mtime" ) == right.fields.at( "exe_mtime" );
        }

        static bool MatchesExpectedProcess( const ParsedRecord &record, const ExpectedProcess &expected )
        {
            return record.fields.at( "run_token" ) == expected.run_token && record.fields.at( "schema" ) == expected.schema &&
                   record.fields.at( "pid" ) == expected.pid && record.fields.at( "exe_path" ) == expected.exe_path &&
                   record.fields.at( "exe_size" ) == expected.exe_size && record.fields.at( "exe_mtime" ) == expected.exe_mtime;
        }

        static bool IsAllowedObserverLifecycleTriple( const std::string &boundary, const std::string &state,
                                                      const std::string &error )
        {
            return boundary == "observer-output" && state == "flush" &&
                   ( error == "write-failed" || error == "stream-closed" );
        }
    };

    static bool IsObserverRepairAuthorized( const std::array<PublisherObserverEvidenceEvaluator::Record, 2> &records )
    {
        const auto &[first, second] = records;
        return first.IsEligibleObserverLifecycleFailure() && second.IsEligibleObserverLifecycleFailure() &&
               first.Boundary() == second.Boundary() && first.State() == second.State() && first.Error() == second.Error();
    }
}

TEST( PublisherObserverRecordClassifier, DistinguishesCompletePassFailurePartialAndForeignEvidence )
{
    using Evaluator = PublisherObserverEvidenceEvaluator;
    const Evaluator::ExpectedProcess expected{ "run-42", "p12-observer-v1", "4242", "/tmp/p12-test", "123", "456" };
    const std::string start = "P12_PUBLISHER_OBSERVER_START run_token=run-42 schema=p12-observer-v1 pid=4242 "
                              "exe_path=/tmp/p12-test exe_size=123 exe_mtime=456";
    const std::string pass_terminal = start.substr( 0, start.find( "START" ) ) +
                                      "TERMINAL " + start.substr( start.find( "run_token=" ) ) +
                                      " outcome=pass terminal=complete peer_release=all-four-runtime-handles-released "
                                      "boundary=none state=ready error=none";
    const std::string failure_terminal = start.substr( 0, start.find( "START" ) ) +
                                         "TERMINAL " + start.substr( start.find( "run_token=" ) ) +
                                         " outcome=failure terminal=complete peer_release=all-four-runtime-handles-released "
                                         "boundary=observer-output state=flush error=write-failed";

    EXPECT_EQ( Evaluator::Evaluate( { start, pass_terminal, "[  PASSED  ] 1 test." }, 0, expected ).Classification(),
               "fully_attributed_complete_pass" );
    const auto complete_failure = Evaluator::Evaluate( { start, failure_terminal, "[  FAILED  ] 1 test, listed below:" }, 1,
                                                       expected );
    EXPECT_EQ( complete_failure.Classification(), "fully_attributed_complete_failure" );
    EXPECT_TRUE( complete_failure.IsEligibleObserverLifecycleFailure() );
    EXPECT_EQ( Evaluator::Evaluate( { start, failure_terminal, "[  PASSED  ] 1 test." }, 1, expected ).Classification(),
               "invalid_or_partial_blocked" );
    EXPECT_EQ( Evaluator::Evaluate( { start, failure_terminal }, 1, expected ).Classification(),
               "invalid_or_partial_blocked" );
    EXPECT_EQ( Evaluator::Evaluate( { start, "P12_PUBLISHER_OBSERVER_TERMINAL run_token=run-42" }, 1, expected ).Classification(),
               "invalid_or_partial_blocked" );
    const Evaluator::ExpectedProcess foreign{ "foreign-run", "p12-observer-v1", "4242", "/tmp/p12-test", "123", "456" };
    EXPECT_EQ( Evaluator::Evaluate( { start, pass_terminal, "[  PASSED  ] 1 test." }, 0, foreign ).Classification(),
               "tooling_attribution_rebuild" );
}

TEST( PublisherObserverRecordClassifier, AuthorizesRepairOnlyForMatchingEligibleObserverLifecycleFailures )
{
    using Evaluator = PublisherObserverEvidenceEvaluator;
    const Evaluator::ExpectedProcess expected{ "run-77", "p12-observer-v1", "777", "/tmp/p12-test", "123", "456" };
    const std::string start = "P12_PUBLISHER_OBSERVER_START run_token=run-77 schema=p12-observer-v1 pid=777 "
                              "exe_path=/tmp/p12-test exe_size=123 exe_mtime=456";
    const auto complete_failure = [&]( const std::string &terminal )
    {
        return Evaluator::Evaluate( { start, terminal, "[  FAILED  ] 1 test, listed below:" }, 1, expected );
    };
    const std::string matching_terminal = "P12_PUBLISHER_OBSERVER_TERMINAL run_token=run-77 schema=p12-observer-v1 pid=777 "
                                          "exe_path=/tmp/p12-test exe_size=123 exe_mtime=456 outcome=failure terminal=complete "
                                          "peer_release=all-four-runtime-handles-released boundary=observer-output state=flush "
                                          "error=write-failed";
    const auto first  = complete_failure( matching_terminal );
    const auto second = complete_failure( matching_terminal );
    EXPECT_TRUE( IsObserverRepairAuthorized( { first, second } ) );

    const auto triple_mismatch = complete_failure(
        "P12_PUBLISHER_OBSERVER_TERMINAL run_token=run-77 schema=p12-observer-v1 pid=777 exe_path=/tmp/p12-test "
        "exe_size=123 exe_mtime=456 outcome=failure terminal=complete "
        "peer_release=all-four-runtime-handles-released boundary=observer-output state=flush error=stream-closed" );
    EXPECT_TRUE( triple_mismatch.IsEligibleObserverLifecycleFailure() );
    EXPECT_FALSE( IsObserverRepairAuthorized( { first, triple_mismatch } ) );

    const auto non_observer = complete_failure(
        "P12_PUBLISHER_OBSERVER_TERMINAL run_token=run-77 schema=p12-observer-v1 pid=777 exe_path=/tmp/p12-test "
        "exe_size=123 exe_mtime=456 outcome=failure terminal=complete "
        "peer_release=all-four-runtime-handles-released boundary=zero-consensus-topic-mesh state=zero "
        "error=no-consensus-neighbor" );
    EXPECT_FALSE( non_observer.IsEligibleObserverLifecycleFailure() );
    EXPECT_FALSE( IsObserverRepairAuthorized( { first, non_observer } ) );
}

TEST_F( FinalityFaultNetwork, ProductionRouteAuditUsesOnlyPubSubCrdtPersistenceAndMintIngress )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first = StartPeer( "validator-one", 54301 );
    auto second = StartPeer( "validator-two", 54302 );
    auto third = StartPeer( "validator-three", 54303 );
    auto passive = StartPeer( "passive-recipient", 54304 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION( [&] { return first.db && second.db && third.db && passive.db; },
                               std::chrono::seconds( 3 ), "four real peers started and connected", nullptr );

    const auto update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : { &first, &second, &third, &passive })
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
    }, std::chrono::seconds( 5 ), "validator registry durably available on every peer", nullptr );

    auto mint = MintFor( first );
    ASSERT_TRUE( mint );
    auto subject = sgns::ConsensusManager::CreateNonceSubject( first.account->GetAddress(), mint->GetNonce(), mint->GetHash(),
                                                                mint->SerializeToEmbeddedTransaction(), CommitmentFor( *mint ), sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = first.consensus->CreateProposal( subject.value(), first.account->GetAddress(),
                                                     first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                     first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto canonical_slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );
    ASSERT_FALSE( canonical_slot.empty() );
    ASSERT_TRUE( first.consensus->SubmitProposal( proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::VotePublications( first.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::VotePublications( second.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::VotePublications( third.consensus ) > 0;
    }, std::chrono::seconds( 15 ), "public proposal reached durable votes on all validators", nullptr );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot ) &&
               passive.consensus->CheckCertificateForSlot( canonical_slot );
    }, std::chrono::seconds( 20 ), "one canonical certificate reached every peer through CRDT", nullptr );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( first.consensus ) +
                   sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( second.consensus ) +
                   sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( third.consensus ) >
               0 &&
               sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsReceived( passive.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *first.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *second.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *third.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1;
    }, std::chrono::seconds( 20 ), "certificate notification reached the registered Mint consumers", nullptr );

    const auto losing_mint = MintFor( first, 1 );
    ASSERT_TRUE( losing_mint );
    ASSERT_EQ( mint->GetSlotID(), losing_mint->GetSlotID() );
    ASSERT_NE( mint->GetHash(), losing_mint->GetHash() );
    auto losing_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), losing_mint->GetNonce(), losing_mint->GetHash(),
        losing_mint->SerializeToEmbeddedTransaction(), CommitmentFor( *losing_mint ), sgns::UTXOWitness{} );
    ASSERT_TRUE( losing_subject.has_value() );
    auto losing_proposal = second.consensus->CreateProposal( losing_subject.value(), second.account->GetAddress(),
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
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot ) &&
               passive.consensus->CheckCertificateForSlot( canonical_slot ) && HasOnlyWinnerOutput( first, *mint, *losing_mint ) &&
               HasOnlyWinnerOutput( second, *mint, *losing_mint ) && HasOnlyWinnerOutput( third, *mint, *losing_mint ) &&
               HasOnlyWinnerOutput( passive, *mint, *losing_mint ) && HasBridgeMarker( first, *mint ) &&
               HasBridgeMarker( second, *mint ) && HasBridgeMarker( third, *mint ) && HasBridgeMarker( passive, *mint );
    }, std::chrono::seconds( 20 ), "certificate and exact winner Mint survived every peer recreation", nullptr );
    StopPeer( first ); StopPeer( second ); StopPeer( third ); StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, SameBurnContentionUsesOneCanonicalSlotAndExactMint )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first = StartPeer( "contention-validator-one", 54401 );
    auto second = StartPeer( "contention-validator-two", 54402 );
    auto third = StartPeer( "contention-validator-three", 54403 );
    auto passive = StartPeer( "contention-passive-recipient", 54404 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    const auto update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : peers )
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
    }, std::chrono::seconds( 5 ), "disconnected peers durably stored the validator registry", nullptr );

    auto first_mint = MintFor( first );
    auto second_mint = MintFor( first, 1 );
    ASSERT_TRUE( first_mint && second_mint );
    ASSERT_EQ( first_mint->GetSlotID(), second_mint->GetSlotID() );
    ASSERT_NE( first_mint->GetHash(), second_mint->GetHash() );

    auto first_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), first_mint->GetNonce(), first_mint->GetHash(),
        first_mint->SerializeToEmbeddedTransaction(), CommitmentFor( *first_mint ), sgns::UTXOWitness{} );
    auto second_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), second_mint->GetNonce(), second_mint->GetHash(),
        second_mint->SerializeToEmbeddedTransaction(), CommitmentFor( *second_mint ), sgns::UTXOWitness{} );
    ASSERT_TRUE( first_subject.has_value() && second_subject.has_value() );
    auto first_proposal = first.consensus->CreateProposal( first_subject.value(), first.account->GetAddress(),
                                                            first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                            first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    auto second_proposal = second.consensus->CreateProposal( second_subject.value(), second.account->GetAddress(),
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
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot );
    }, std::chrono::seconds( 20 ), "connected validators converged on one durable canonical certificate", nullptr );

    const auto certificate = first.consensus->GetCertificateBySlot( canonical_slot );
    ASSERT_TRUE( certificate.has_value() );
    const auto winner_hash = sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() );
    ASSERT_TRUE( winner_hash.has_value() );
    const auto winner = winner_hash.value() == first_mint->GetHash() ? first_mint : second_mint;
    const auto loser = winner_hash.value() == first_mint->GetHash() ? second_mint : first_mint;
    ASSERT_NE( winner->GetHash(), loser->GetHash() );

    for ( auto *validator : { &first, &second, &third } )
    {
        const auto durable = validator->consensus->GetCertificateBySlot( canonical_slot );
        ASSERT_TRUE( durable.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( durable.value().proposal() ), winner_hash );
    }

    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *first.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *second.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *third.transactions ) == 1 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1;
    }, std::chrono::seconds( 20 ), "passive peer received and recovered the exact canonical winner", nullptr );
    for ( auto *peer : peers )
    {
        EXPECT_TRUE( HasOnlyWinnerOutput( *peer, *winner, *loser ) );
        EXPECT_TRUE( HasBridgeMarker( *peer, *winner ) );
    }

    RestartPeer( first ); RestartPeer( second ); RestartPeer( third ); RestartPeer( passive );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );
    ConnectPeers( { &first, &second, &third, &passive } );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot ) &&
               passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
               HasOnlyWinnerOutput( first, *winner, *loser ) && HasOnlyWinnerOutput( second, *winner, *loser ) &&
               HasOnlyWinnerOutput( third, *winner, *loser ) && HasOnlyWinnerOutput( passive, *winner, *loser ) &&
               HasBridgeMarker( first, *winner ) && HasBridgeMarker( second, *winner ) &&
               HasBridgeMarker( third, *winner ) && HasBridgeMarker( passive, *winner );
    }, std::chrono::seconds( 20 ), "durable canonical winner survived every peer recreation", nullptr );
    StopPeer( first ); StopPeer( second ); StopPeer( third ); StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, LateContenderAndPassiveRecipientRemainReceiveOnly )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto first = StartPeer( "late-validator-one", 54501 );
    auto second = StartPeer( "late-validator-two", 54502 );
    auto third = StartPeer( "late-validator-three", 54503 );
    auto passive = StartPeer( "late-passive-recipient", 54504 );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );

    const std::array<Peer *, 4> peers{ &first, &second, &third, &passive };
    const auto update = RegistryUpdate( { &first, &second, &third } );
    for ( auto *peer : peers )
        ASSERT_TRUE( peer->blockchain->GetValidatorRegistry()->StoreRegistryUpdate( update ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               second.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               third.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value() &&
               passive.blockchain->GetValidatorRegistry()->LoadCurrentRegistry().has_value();
    }, std::chrono::seconds( 5 ), "every isolated peer durably stored the validator registry", nullptr );

    auto winner = MintFor( first );
    auto loser = MintFor( first, 1 );
    ASSERT_TRUE( winner && loser );
    ASSERT_EQ( winner->GetSlotID(), loser->GetSlotID() );
    auto winner_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
        CommitmentFor( *winner ), sgns::UTXOWitness{} );
    auto loser_subject = sgns::ConsensusManager::CreateNonceSubject(
        first.account->GetAddress(), loser->GetNonce(), loser->GetHash(), loser->SerializeToEmbeddedTransaction(),
        CommitmentFor( *loser ), sgns::UTXOWitness{} );
    ASSERT_TRUE( winner_subject.has_value() && loser_subject.has_value() );
    auto winner_proposal = first.consensus->CreateProposal( winner_subject.value(), first.account->GetAddress(),
                                                             first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
                                                             first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    auto loser_proposal = second.consensus->CreateProposal( loser_subject.value(), second.account->GetAddress(),
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
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::VotePublications( first.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::VotePublications( second.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::VotePublications( third.consensus ) > 0;
    }, std::chrono::seconds( 15 ), "durable active-vote publications reached each validator", nullptr );

    ASSERT_TRUE( second.consensus->SubmitProposal( loser_proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( first.consensus, canonical_slot ) ==
                   winner_proposal.value().proposal_id() &&
               sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( second.consensus, canonical_slot ) ==
                   winner_proposal.value().proposal_id() &&
               sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( third.consensus, canonical_slot ) ==
                   winner_proposal.value().proposal_id();
    }, std::chrono::seconds( 10 ), "late contender did not replace any durable active vote", nullptr );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot );
    }, std::chrono::seconds( 20 ), "initial winner reached the accepted authoritative certificate boundary", nullptr );

    ASSERT_TRUE( third.consensus->SubmitProposal( loser_proposal.value() ).has_value() );

    ConnectPeers( peers );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
               sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsReceived( passive.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::AcceptedCertificateReadbacks( passive.consensus ) > 0 &&
               sgns::MultiNodeFinalityFaultTestAccess::MintEffects( *passive.transactions ) == 1;
    }, std::chrono::seconds( 20 ), "passive peer received notification then completed committed durable recovery", nullptr );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateWriteAttempts( passive.consensus ), 0u );

    for ( auto *peer : peers )
    {
        const auto certificate = peer->consensus->GetCertificateBySlot( canonical_slot );
        ASSERT_TRUE( certificate.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ), winner->GetHash() );
        EXPECT_TRUE( HasOnlyWinnerOutput( *peer, *winner, *loser ) );
        EXPECT_TRUE( HasBridgeMarker( *peer, *winner ) );
    }

    RestartPeer( first ); RestartPeer( second ); RestartPeer( third ); RestartPeer( passive );
    ASSERT_TRUE( first.consensus && second.consensus && third.consensus && passive.consensus );
    ConnectPeers( { &first, &second, &third, &passive } );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return first.consensus->CheckCertificateForSlot( canonical_slot ) &&
               second.consensus->CheckCertificateForSlot( canonical_slot ) &&
               third.consensus->CheckCertificateForSlot( canonical_slot ) &&
               passive.consensus->CheckCertificateForSlot( canonical_slot ) &&
               HasOnlyWinnerOutput( first, *winner, *loser ) && HasOnlyWinnerOutput( second, *winner, *loser ) &&
               HasOnlyWinnerOutput( third, *winner, *loser ) && HasOnlyWinnerOutput( passive, *winner, *loser ) &&
               HasBridgeMarker( first, *winner ) && HasBridgeMarker( second, *winner ) &&
               HasBridgeMarker( third, *winner ) && HasBridgeMarker( passive, *winner );
    }, std::chrono::seconds( 20 ), "late contender remained absent after reopened-store recovery", nullptr );
    StopPeer( first ); StopPeer( second ); StopPeer( third ); StopPeer( passive );
}

TEST_F( FinalityFaultNetwork, ActiveVoteRestartDiagnosticClassifiesLifecycleBoundary )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    auto network = StartNetwork( "active-vote-diagnostic", 54641 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
    StoreRegistry( network );

    auto winner = MintFor( network.first );
    ASSERT_TRUE( winner );
    auto subject = sgns::ConsensusManager::CreateNonceSubject(
        network.first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
        CommitmentFor( *winner ), sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = network.first.consensus->CreateProposal(
        subject.value(), network.first.account->GetAddress(), network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );
    network.first.active_vote_diagnostic_slot = slot;
    network.first.active_vote_diagnostic_key =
        sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteStorageKey( network.first.consensus, slot );

    sgns::MultiNodeFinalityFaultTestAccess::ArmActiveVoteBarrier( network.first.consensus );
    ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteBarrierEntered( network.first.consensus );
    }, std::chrono::seconds( 15 ), "isolated validator paused after direct active-vote persistence", nullptr );

    auto before_restart = Peer::Snapshot( network.first.db, network.first.active_vote_diagnostic_key );
    before_restart.decoded_proposal_id =
        sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.first.consensus, slot );
    before_restart.accepted_certificate = network.first.consensus->CheckCertificateForSlot( slot );
    const auto release_attempts_before_restart =
        sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteReleaseAttempts( network.first.consensus );
    const auto release_successes_before_restart =
        sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteReleaseSuccesses( network.first.consensus );

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
        return;

    auto recovered_raw = Peer::Snapshot( network.first.db, network.first.active_vote_diagnostic_key );
    recovered_raw.decoded_proposal_id =
        sgns::MultiNodeFinalityFaultTestAccess::DurableActiveVoteProposalId( network.first.consensus, slot );
    recovered_raw.accepted_certificate = network.first.consensus->CheckCertificateForSlot( slot );
    const auto recovered_in_memory =
        sgns::MultiNodeFinalityFaultTestAccess::RecoveredActiveVoteProposalId( network.first.consensus, slot );
    if ( !require_raw( recovered_raw, "after-recover-active-votes" ) ||
         recovered_raw.decoded_proposal_id != proposal.value().proposal_id() ||
         recovered_in_memory != proposal.value().proposal_id() )
    {
        if ( recovered_raw.record_present && recovered_raw.record_digest == before_restart.record_digest &&
             !recovered_raw.accepted_certificate )
            fail( "after-recover-active-votes", "recovery-missed-record" );
        return;
    }
    EXPECT_EQ( release_attempts_before_restart, 0U );
    EXPECT_EQ( release_successes_before_restart, 0U );
    StopNetwork( network );
}

TEST_F( FinalityFaultNetwork, RestartAtVoteCertificateAndMintDurableBoundariesRecoversExactlyOnce )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    {
        auto network = StartNetwork( "restart-vote", 54601 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
        StoreRegistry( network );
        // Keep the vote owner isolated until after its same-root recreation.
        // Otherwise the other validators can finalize this slot and legitimately
        // release the exact active-vote record before the restart boundary is
        // observed.

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject(
            network.first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
            CommitmentFor( *winner ), sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(), network.first.account->GetAddress(), network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

        sgns::MultiNodeFinalityFaultTestAccess::ArmActiveVoteBarrier( network.first.consensus );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return sgns::MultiNodeFinalityFaultTestAccess::ActiveVoteBarrierEntered( network.first.consensus );
        }, std::chrono::seconds( 15 ), "first validator paused after durable active-vote persistence", nullptr );
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
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.passive, slot, *winner, *loser );
        }, std::chrono::seconds( 25 ), "recreated vote owner recovered only its durable vote before exact finality", nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
            AssertOneLiveMintEffect( *peer );
        }

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.passive, slot, *winner, *loser );
        }, std::chrono::seconds( 20 ), "vote-boundary finality remained exact after reopening every durable root", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser );
        StopNetwork( network );
    }

    {
        auto network = StartNetwork( "restart-certificate", 54611 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
        StoreRegistry( network );
        ConnectPeers( Peers( network ) );

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject(
            network.first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
            CommitmentFor( *winner ), sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(), network.first.account->GetAddress(), network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

        sgns::MultiNodeFinalityFaultTestAccess::ArmAcceptedCertificateBarrier( network.second.consensus );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return sgns::MultiNodeFinalityFaultTestAccess::AcceptedCertificateBarrierEntered( network.second.consensus );
        }, std::chrono::seconds( 25 ), "second validator paused after accepted certificate durable readback", nullptr );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return network.first.consensus->CheckCertificateForSlot( slot ) &&
                   network.third.consensus->CheckCertificateForSlot( slot ) &&
                   network.passive.consensus->CheckCertificateForSlot( slot );
        }, std::chrono::seconds( 20 ),
                                   "surviving peers retained the accepted certificate before receiver restart", nullptr );

        RestartPeer( network.second );
        ASSERT_TRUE( network.second.consensus );
        ConnectPeers( Peers( network ) );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.passive, slot, *winner, *loser );
        }, std::chrono::seconds( 25 ), "recreated certificate recipient recovered the exact durable winner", nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
            AssertOneLiveMintEffect( *peer );
        }

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.passive, slot, *winner, *loser );
        }, std::chrono::seconds( 20 ), "certificate-boundary recovery remained exact after reopening every durable root", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser );
        StopNetwork( network );
    }

    {
        auto network = StartNetwork( "restart-mint", 54621 );
        ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
        StoreRegistry( network );
        ConnectPeers( Peers( network ) );

        auto winner = MintFor( network.first );
        auto loser  = MintFor( network.first, 1 );
        ASSERT_TRUE( winner && loser );
        auto subject = sgns::ConsensusManager::CreateNonceSubject(
            network.first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
            CommitmentFor( *winner ), sgns::UTXOWitness{} );
        ASSERT_TRUE( subject.has_value() );
        auto proposal = network.first.consensus->CreateProposal(
            subject.value(), network.first.account->GetAddress(), network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
            network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
        ASSERT_TRUE( proposal.has_value() );
        const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );
        MintRecoveryDiagnostics diagnostics( network, slot, *winner, *loser );

        sgns::MultiNodeFinalityFaultTestAccess::ArmMintEffectsBarrier( network.first.transactions );
        ASSERT_TRUE( network.first.consensus->SubmitProposal( proposal.value() ).has_value() );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return sgns::MultiNodeFinalityFaultTestAccess::MintEffectsBarrierEntered( network.first.transactions );
        }, std::chrono::seconds( 25 ), "first validator paused after durable Mint effects and before bridge marker", nullptr );
        EXPECT_TRUE( HasOnlyWinnerOutput( network.first, *winner, *loser ) );
        EXPECT_FALSE( HasBridgeMarker( network.first, *winner ) );

        RestartPeer( network.first );
        ASSERT_TRUE( network.first.consensus );
        ConnectPeers( Peers( network ) );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.passive, slot, *winner, *loser );
        }, std::chrono::seconds( 25 ), "recreated Mint peer repaired its marker through normal certificate recovery", nullptr );
        for ( auto *peer : Peers( network ) )
        {
            AssertSingleDurableMint( *peer, slot, *winner, *loser );
            AssertOneLiveMintEffect( *peer );
        }

        RestartAndReconnect( network );
        ASSERT_WAIT_FOR_CONDITION( [&] {
            return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
                   HasSingleDurableMint( network.passive, slot, *winner, *loser );
        }, std::chrono::seconds( 20 ), "Mint-boundary recovery stayed exact after reopening every durable root", nullptr );
        for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser );
        diagnostics.MarkCompleted();
        StopNetwork( network );
    }
}

TEST_F( FinalityFaultNetwork, PublisherLossAfterPersistenceUsesDeterministicFailover )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );
    auto network = StartNetwork( "publisher-loss", 54631 );
    ASSERT_TRUE( network.first.consensus && network.second.consensus && network.third.consensus && network.passive.consensus );
    StoreRegistry( network );

    auto winner = MintFor( network.first );
    auto loser  = MintFor( network.first, 1 );
    ASSERT_TRUE( winner && loser );
    auto subject = sgns::ConsensusManager::CreateNonceSubject(
        network.first.account->GetAddress(), winner->GetNonce(), winner->GetHash(), winner->SerializeToEmbeddedTransaction(),
        CommitmentFor( *winner ), sgns::UTXOWitness{} );
    ASSERT_TRUE( subject.has_value() );
    auto proposal = network.first.consensus->CreateProposal(
        subject.value(), network.first.account->GetAddress(), network.first.blockchain->GetValidatorRegistry()->GetRegistryCid(),
        network.first.blockchain->GetValidatorRegistry()->GetRegistryEpoch() );
    ASSERT_TRUE( proposal.has_value() );
    const auto slot = sgns::MultiNodeFinalityFaultTestAccess::SlotKey( proposal.value() );

    const auto proposal_registry = network.first.blockchain->GetValidatorRegistry()->LoadRegistryByCid( proposal.value().registry_cid() );
    ASSERT_TRUE( proposal_registry.has_value() );
    ASSERT_EQ( proposal_registry.value().epoch(), proposal.value().registry_epoch() );
    const auto active_validators = sgns::MultiNodeFinalityFaultTestAccess::OrderedActiveValidators(
        network.first.consensus, proposal_registry.value() );
    ASSERT_EQ( active_validators.size(), 3u );
    const auto proposal_hash = sgns::crypto::sha2_256( proposal.value().proposal_id().data(), proposal.value().proposal_id().size() );
    uint64_t base_index = 0;
    for ( size_t index = 0; index < sizeof( uint64_t ) && index < proposal_hash.size(); ++index )
        base_index = ( base_index << 8 ) | proposal_hash[index];
    base_index %= active_validators.size();

    // Round selection happens after the normal candidate and certificate delay.
    // Arm the same post-write observer on validators solely to discover which
    // production-selected peer reached that completed action; only that peer
    // is stopped and the unused observers are released immediately.
    for ( auto *peer : { &network.first, &network.second, &network.third } )
        sgns::MultiNodeFinalityFaultTestAccess::ArmCertificatePersistedBarrier( peer->consensus );
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
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierEntered( network.first.consensus ) ||
               sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierEntered( network.second.consensus ) ||
               sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierEntered( network.third.consensus );
    }, std::chrono::seconds( 25 ), "production-selected publisher paused after immutable certificate persistence and before notification", nullptr );
    if ( ::testing::Test::HasFatalFailure() )
    {
        return;
    }
    Peer *publisher = nullptr;
    for ( auto *peer : { &network.first, &network.second, &network.third } )
        if ( sgns::MultiNodeFinalityFaultTestAccess::CertificatePersistedBarrierEntered( peer->consensus ) ) publisher = peer;
    ASSERT_NE( publisher, nullptr );
    const auto persisted_round = sgns::MultiNodeFinalityFaultTestAccess::CurrentRound(
        publisher->consensus, proposal.value().timestamp() );
    ASSERT_EQ( publisher->account->GetAddress(), active_validators[( base_index + persisted_round ) % active_validators.size()] );
    ASSERT_TRUE( sgns::MultiNodeFinalityFaultTestAccess::IsCurrentAggregator(
        publisher->consensus, proposal.value(), proposal_registry.value() ) );
    ASSERT_TRUE( publisher->consensus->CheckCertificateForSlot( slot ) );
    const auto durable_certificate = publisher->consensus->GetCertificateBySlot( slot );
    ASSERT_TRUE( durable_certificate.has_value() );
    const auto durable_bytes = durable_certificate.value().SerializeAsString();
    ASSERT_FALSE( durable_bytes.empty() );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateWriteSuccesses( publisher->consensus ), 1u );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( publisher->consensus ), 0u );
    Peer *crdt_blind = nullptr;
    for ( auto *peer : { &network.first, &network.second, &network.third } )
        if ( peer != publisher && !peer->consensus->CheckCertificateForSlot( slot ) ) crdt_blind = peer;
    ASSERT_NE( crdt_blind, nullptr );

    auto stopped_publisher = publisher->consensus;
    StopPeer( *publisher );
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::CertificateNotificationsPublished( stopped_publisher ), 0u );
    for ( auto *peer : { &network.first, &network.second, &network.third } )
        if ( peer != publisher ) sgns::MultiNodeFinalityFaultTestAccess::ReleaseConsensusBarriers( peer->consensus );

    // The peer that did not have the record at the durability boundary remains
    // eligible for ordinary later-round aggregation.  CRDT precedence may make
    // the record visible first; in that case existing ProcessCertificates
    // clears/recoveries without a replacement write or vote.
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return sgns::MultiNodeFinalityFaultTestAccess::CurrentRound( crdt_blind->consensus, proposal.value().timestamp() ) >
                   persisted_round &&
               sgns::MultiNodeFinalityFaultTestAccess::IsCurrentAggregator(
                   crdt_blind->consensus, proposal.value(), proposal_registry.value() );
    }, std::chrono::seconds( 25 ), "later production round kept the CRDT-blind peer eligible for deterministic aggregation", nullptr );
    stopped_publisher.reset();
    RestartPeer( *publisher );
    ASSERT_TRUE( publisher->consensus );
    ConnectPeers( Peers( network ) );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
               HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
               HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
               HasSingleDurableMint( network.passive, slot, *winner, *loser );
    }, std::chrono::seconds( 25 ), "restarted publisher made the authoritative CRDT certificate normally recoverable on every peer", nullptr );
    for ( auto *peer : Peers( network ) )
    {
        EXPECT_LE( sgns::MultiNodeFinalityFaultTestAccess::CertificateWriteAttempts( peer->consensus ), 1u );
        const auto certificate = peer->consensus->GetCertificateBySlot( slot );
        ASSERT_TRUE( certificate.has_value() );
        EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::NonceTransactionHash( certificate.value().proposal() ), winner->GetHash() );
        AssertSingleDurableMint( *peer, slot, *winner, *loser );
        AssertOneLiveMintEffect( *peer );
    }

    RestartAndReconnect( network );
    ASSERT_WAIT_FOR_CONDITION( [&] {
        return HasSingleDurableMint( network.first, slot, *winner, *loser ) &&
               HasSingleDurableMint( network.second, slot, *winner, *loser ) &&
               HasSingleDurableMint( network.third, slot, *winner, *loser ) &&
               HasSingleDurableMint( network.passive, slot, *winner, *loser );
    }, std::chrono::seconds( 25 ), "publisher-loss finality remained exact after reopening every peer root", nullptr );
    for ( auto *peer : Peers( network ) ) AssertSingleDurableMint( *peer, slot, *winner, *loser );
    };
    scenario();
    if ( ::testing::Test::HasFatalFailure() ) observer.MarkUnclassifiedExit();
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
