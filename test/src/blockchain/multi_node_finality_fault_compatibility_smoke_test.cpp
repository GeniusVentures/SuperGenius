/**
 * @file multi_node_finality_fault_compatibility_smoke_test.cpp
 * @brief Production-constructor and lifecycle compatibility proof for Phase 12.
 *
 * This target deliberately selects the supported D-04 fallback: peers are stopped,
 * rebuilt from their unchanged RocksDB roots, then reconnected through AddPeers.
 * GossipPubSub exposes no installed public peer-disconnect operation suitable for
 * this test, so no transport mock or unverified host API is used.
 */

#include <gtest/gtest.h>

#include "account/GeniusAccount.hpp"
#include "account/TransactionManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"

#include <boost/filesystem.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>

#include <chrono>
#include <memory>
#include <thread>

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

        static void ResetConsensus( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return;
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager->fault_test_counters_ = {};
            manager->active_vote_persisted_barrier_ = {};
            manager->certificate_persisted_barrier_ = {};
            manager->accepted_certificate_barrier_ = {};
        }

        static void ArmActiveVotePersistenceBarrier( const std::shared_ptr<ConsensusManager> &manager )
        {
            SetArmed( manager, manager ? &ConsensusManager::active_vote_persisted_barrier_ : nullptr );
        }

        static void ArmCertificatePersistenceBarrier( const std::shared_ptr<ConsensusManager> &manager )
        {
            SetArmed( manager, manager ? &ConsensusManager::certificate_persisted_barrier_ : nullptr );
        }

        static void ArmAcceptedCertificateBarrier( const std::shared_ptr<ConsensusManager> &manager )
        {
            SetArmed( manager, manager ? &ConsensusManager::accepted_certificate_barrier_ : nullptr );
        }

        static void ReleaseAllConsensusBarriers( const std::shared_ptr<ConsensusManager> &manager )
        {
            if ( !manager ) return;
            {
                std::lock_guard lock( manager->fault_test_mutex_ );
                for ( auto *barrier : { &manager->active_vote_persisted_barrier_,
                                        &manager->certificate_persisted_barrier_,
                                        &manager->accepted_certificate_barrier_ } )
                {
                    barrier->released = true;
                    barrier->armed = false;
                }
            }
            manager->fault_test_cv_.notify_all();
        }

        static bool ActiveVotePersistenceBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            return BarrierEntered( manager, manager ? &ConsensusManager::active_vote_persisted_barrier_ : nullptr );
        }

        static bool CertificatePersistenceBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            return BarrierEntered( manager, manager ? &ConsensusManager::certificate_persisted_barrier_ : nullptr );
        }

        static bool AcceptedCertificateBarrierEntered( const std::shared_ptr<ConsensusManager> &manager )
        {
            return BarrierEntered( manager, manager ? &ConsensusManager::accepted_certificate_barrier_ : nullptr );
        }

        static uint64_t VotePublications( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? ReadCounter( manager, &ConsensusManager::FinalityFaultCounters::vote_publications ) : 0;
        }

        static uint64_t CertificateWriteAttempts( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? ReadCounter( manager, &ConsensusManager::FinalityFaultCounters::certificate_write_attempts ) : 0;
        }

        static uint64_t CertificateWriteSuccesses( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? ReadCounter( manager, &ConsensusManager::FinalityFaultCounters::certificate_write_successes ) : 0;
        }

        static uint64_t CertificateNotificationPublications( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? ReadCounter( manager, &ConsensusManager::FinalityFaultCounters::certificate_notification_publications ) : 0;
        }

        static uint64_t CertificateNotificationsReceived( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? ReadCounter( manager, &ConsensusManager::FinalityFaultCounters::certificate_notifications_received ) : 0;
        }

        static uint64_t AcceptedCertificateReadbacks( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? ReadCounter( manager, &ConsensusManager::FinalityFaultCounters::accepted_certificate_readbacks ) : 0;
        }

        static uint64_t CurrentRound( const std::shared_ptr<ConsensusManager> &manager, uint64_t proposal_timestamp_ms )
        {
            return manager ? manager->GetCurrentRound( proposal_timestamp_ms ) : 0;
        }

        static bool CertificatesPending( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager && manager->certificates_pending_.load();
        }

        static int AggregatorRole( const std::shared_ptr<ConsensusManager> &manager,
                                   const ConsensusManager::Proposal &proposal,
                                   const ValidatorRegistry::Registry &registry )
        {
            return manager ? static_cast<int>( manager->GetAggregatorRole( proposal, registry ) ) : -1;
        }

        static void ResetMintEffects( TransactionManager &manager )
        {
            std::lock_guard lock( manager.fault_test_mutex_ );
            manager.mint_effects_for_test_ = 0;
            manager.mint_effects_barrier_ = {};
        }

        static uint64_t MintEffects( const TransactionManager &manager )
        {
            std::lock_guard lock( manager.fault_test_mutex_ );
            return manager.mint_effects_for_test_;
        }

        static void ArmMintEffectsBarrier( TransactionManager &manager )
        {
            std::lock_guard lock( manager.fault_test_mutex_ );
            manager.mint_effects_barrier_ = { true, false, false };
        }

        static bool MintEffectsBarrierEntered( const TransactionManager &manager )
        {
            std::lock_guard lock( manager.fault_test_mutex_ );
            return manager.mint_effects_barrier_.entered;
        }

        static void ReleaseMintEffectsBarrier( TransactionManager &manager )
        {
            {
                std::lock_guard lock( manager.fault_test_mutex_ );
                manager.mint_effects_barrier_.released = true;
                manager.mint_effects_barrier_.armed = false;
            }
            manager.fault_test_cv_.notify_all();
        }

    private:
        using BarrierMember = ConsensusManager::FinalityFaultBarrier ConsensusManager::*;
        using CounterMember = uint64_t ConsensusManager::FinalityFaultCounters::*;

        static void SetArmed( const std::shared_ptr<ConsensusManager> &manager, BarrierMember member )
        {
            if ( !manager || !member ) return;
            std::lock_guard lock( manager->fault_test_mutex_ );
            manager.get()->*member = { true, false, false };
        }

        static bool BarrierEntered( const std::shared_ptr<ConsensusManager> &manager, BarrierMember member )
        {
            if ( !manager || !member ) return false;
            std::lock_guard lock( manager->fault_test_mutex_ );
            return ( manager.get()->*member ).entered;
        }

        static uint64_t ReadCounter( const std::shared_ptr<ConsensusManager> &manager, CounterMember member )
        {
            std::lock_guard lock( manager->fault_test_mutex_ );
            return manager->fault_test_counters_.*member;
        }
    };
} // namespace sgns

namespace
{
    const auto kSmokeToken = sgns::TokenID::FromBytes( { 0x00 } );

    struct ComponentPeer
    {
        std::string                                      root;
        std::shared_ptr<boost::asio::io_context>         io;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
        std::shared_ptr<sgns::crdt::GlobalDB>            db;
        std::shared_ptr<sgns::GeniusAccount>             account;
        std::shared_ptr<sgns::Blockchain>                blockchain;
        std::shared_ptr<sgns::TransactionManager>        transactions;
        std::shared_ptr<sgns::ConsensusManager>          consensus;
        std::thread                                      io_thread;

        ComponentPeer() = default;
        ComponentPeer( const ComponentPeer & ) = delete;
        ComponentPeer &operator=( const ComponentPeer & ) = delete;
        ComponentPeer( ComponentPeer &&other ) noexcept :
            root( std::move( other.root ) ),
            io( std::move( other.io ) ),
            pubsub( std::move( other.pubsub ) ),
            db( std::move( other.db ) ),
            account( std::move( other.account ) ),
            blockchain( std::move( other.blockchain ) ),
            transactions( std::move( other.transactions ) ),
            consensus( std::move( other.consensus ) ),
            io_thread( std::move( other.io_thread ) )
        {
        }

        ComponentPeer &operator=( ComponentPeer &&other ) noexcept
        {
            if ( this == &other ) return *this;
            Stop();
            root         = std::move( other.root );
            io           = std::move( other.io );
            pubsub       = std::move( other.pubsub );
            db           = std::move( other.db );
            account      = std::move( other.account );
            blockchain   = std::move( other.blockchain );
            transactions = std::move( other.transactions );
            consensus    = std::move( other.consensus );
            io_thread    = std::move( other.io_thread );
            return *this;
        }

        ~ComponentPeer()
        {
            Stop();
        }

        void Stop() noexcept
        {
            if ( transactions ) transactions->Stop();
            transactions.reset();
            if ( blockchain ) (void) blockchain->Stop();
            consensus.reset();
            blockchain.reset();
            if ( io ) io->stop();
            if ( io_thread.joinable() ) io_thread.join();
            /*
             * Teardown invariant (asio), mirroring Peer::Stop in
             * multi_node_finality_fault_test.cpp:389-405: the io_context owned
             * by GossipPubSub must outlive every I/O object that touches it.
             * GlobalDB's graphsync chain co-owns the libp2p host wired from
             * pubsub->GetHost() at StartPeer, and the account holds db-backed
             * handles, so BOTH must be reset BEFORE pubsub->Stop(). Otherwise
             * StopImpl destroys m_host/m_context while external co-owners keep
             * the host alive, and the later ~BasicHost deregisters leftover
             * TcpConnections from the freed kqueue reactor — the teardown
             * SIGSEGV closed by 12-14. With db and account released first,
             * pubsub->Stop() is the FINAL host release.
             */
            db.reset();
            account.reset();
            if ( pubsub ) pubsub->Stop();
            pubsub.reset();
            io.reset();
        }
    };

    class MultiNodeFinalityFaultCompatibilitySmokeTest : public ::test::CRDTFixture
    {
    protected:
        MultiNodeFinalityFaultCompatibilitySmokeTest() : CRDTFixture( "multi_node_finality_fault_smoke" )
        {
        }

        ComponentPeer StartPeer( const std::string &name, uint16_t port )
        {
            ComponentPeer peer;
            peer.root = ( base_path / name ).string();
            boost::filesystem::create_directories( peer.root );
            peer.io = std::make_shared<boost::asio::io_context>();

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
            EXPECT_FALSE( peer.pubsub->Start( port, { peer.pubsub->GetLocalAddress() } ).get() );

            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( peer.io ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto graphsync = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( peer.pubsub->GetHost(), scheduler );
            auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
            auto db_result = sgns::crdt::GlobalDB::New( peer.io,
                                                        peer.root + "/rocksdb",
                                                        peer.pubsub,
                                                        sgns::crdt::CrdtOptions::DefaultOptions(),
                                                        graphsync,
                                                        scheduler,
                                                        generator );
            EXPECT_TRUE( db_result.has_value() );
            if ( db_result.has_error() )
            {
                return peer;
            }
            peer.db = std::move( db_result.value() );
            peer.db->Start();
            peer.io_thread = std::thread( [io = peer.io] { io->run(); } );

            peer.account = sgns::GeniusAccount::New( kSmokeToken, peer.root + "/account" );
            EXPECT_TRUE( peer.account );
            if ( !peer.account )
            {
                return peer;
            }
            EXPECT_TRUE( peer.account->GetUTXOManager().LoadUTXOs( peer.db->GetDataStore() ).has_value() );
            peer.blockchain = sgns::Blockchain::New( peer.db, peer.account, peer.pubsub, []( outcome::result<void> ) {} );
            EXPECT_TRUE( peer.blockchain );
            peer.consensus = sgns::MultiNodeFinalityFaultTestAccess::Manager( peer.blockchain );
            EXPECT_TRUE( peer.consensus );
            peer.transactions = sgns::TransactionManager::New( peer.db,
                                                                peer.io,
                                                                peer.account,
                                                                peer.blockchain,
                                                                false,
                                                                0,
                                                                std::chrono::milliseconds( 300000 ),
                                                                std::chrono::milliseconds( 600000 ) );
            EXPECT_TRUE( peer.transactions );
            return peer;
        }

        void StopPeer( ComponentPeer &peer )
        {
            peer.Stop();
        }

        static bool PeersAreConnectedAndMeshed( const ComponentPeer &first, const ComponentPeer &second )
        {
            if ( !first.pubsub || !first.consensus || !second.pubsub || !second.consensus ) return false;
            const auto first_host  = first.pubsub->GetHost();
            const auto second_host = second.pubsub->GetHost();
            if ( !first_host || !second_host ||
                 first_host->connectedness( second_host->getPeerInfo() ) != libp2p::Host::Connectedness::CONNECTED ||
                 second_host->connectedness( first_host->getPeerInfo() ) != libp2p::Host::Connectedness::CONNECTED )
                return false;
            const auto &topic = sgns::MultiNodeFinalityFaultTestAccess::ConsensusTopic( first.consensus );
            return !topic.empty() && first.pubsub->getPeerCount( topic ) >= 1 && second.pubsub->getPeerCount( topic ) >= 1;
        }

        static void ConnectPeers( ComponentPeer &first, ComponentPeer &second )
        {
            first.pubsub->AddPeers( { second.pubsub->GetInterfaceAddress() } );
            second.pubsub->AddPeers( { first.pubsub->GetInterfaceAddress() } );
            ASSERT_WAIT_FOR_CONDITION( [&] { return PeersAreConnectedAndMeshed( first, second ); },
                                       std::chrono::seconds( 5 ),
                                       "component peers have a public libp2p connection and consensus-topic mesh",
                                       nullptr );
        }
    };
} // namespace

TEST_F( MultiNodeFinalityFaultCompatibilitySmokeTest, ProductionCompositionAndLifecycleUseOnlyPublicRoutes )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    auto first = StartPeer( "first", 54201 );
    auto second = StartPeer( "second", 54202 );
    ASSERT_TRUE( first.pubsub && first.db && first.blockchain && first.transactions && first.consensus );
    ASSERT_TRUE( second.pubsub && second.db && second.blockchain && second.transactions && second.consensus );

    // Real transport lifecycle: AddPeers, StopPeer, rebuild at the same root, AddPeers.
    ConnectPeers( first, second );
    const auto first_root = first.root;
    StopPeer( first );
    first = StartPeer( "first", 54201 );
    ASSERT_EQ( first.root, first_root );
    ASSERT_TRUE( first.pubsub && first.db && first.blockchain && first.transactions && first.consensus );
    ConnectPeers( first, second );

    // The new accessor is observation-only; this asserts it can read no fabricated protocol progress.
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::VotePublications( nullptr ), 0u );
    StopPeer( first );
    StopPeer( second );
}
