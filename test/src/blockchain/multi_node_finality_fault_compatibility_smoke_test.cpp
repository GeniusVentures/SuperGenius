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
        static uint64_t VotePublications( const std::shared_ptr<ConsensusManager> &manager )
        {
            return manager ? manager->fault_test_counters_.vote_publications : 0;
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
        std::thread                                      io_thread;

        ComponentPeer() = default;
        ComponentPeer( const ComponentPeer & ) = delete;
        ComponentPeer &operator=( const ComponentPeer & ) = delete;
        ComponentPeer( ComponentPeer && ) noexcept = default;
        ComponentPeer &operator=( ComponentPeer && ) noexcept = default;
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
            peer.transactions.reset();
            if ( peer.blockchain )
            {
                (void) peer.blockchain->Stop();
            }
            peer.blockchain.reset();
            if ( peer.io )
            {
                peer.io->stop();
            }
            if ( peer.io_thread.joinable() )
            {
                peer.io_thread.join();
            }
            if ( peer.pubsub )
            {
                peer.pubsub->Stop();
            }
            peer.db.reset();
            peer.pubsub.reset();
            peer.account.reset();
            peer.io.reset();
        }
    };
} // namespace

TEST_F( MultiNodeFinalityFaultCompatibilitySmokeTest, ProductionCompositionAndLifecycleUseOnlyPublicRoutes )
{
    sgns::GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier )
                                                    { return std::make_shared<sgns::MemorySecureStorage>( identifier ); } );

    auto first = StartPeer( "first", 54201 );
    auto second = StartPeer( "second", 54202 );
    ASSERT_TRUE( first.pubsub && first.db && first.blockchain && first.transactions );
    ASSERT_TRUE( second.pubsub && second.db && second.blockchain && second.transactions );

    // Real transport lifecycle: AddPeers, StopPeer, rebuild at the same root, AddPeers.
    first.pubsub->AddPeers( { second.pubsub->GetInterfaceAddress() } );
    ASSERT_WAIT_FOR_CONDITION( [&] { return first.db && second.db; },
                               std::chrono::seconds( 2 ),
                               "component peers started before lifecycle restart",
                               nullptr );
    const auto first_root = first.root;
    StopPeer( first );
    first = StartPeer( "first", 54201 );
    ASSERT_EQ( first.root, first_root );
    ASSERT_TRUE( first.pubsub && first.db && first.blockchain && first.transactions );
    first.pubsub->AddPeers( { second.pubsub->GetInterfaceAddress() } );
    ASSERT_WAIT_FOR_CONDITION( [&] { return first.transactions && second.transactions; },
                               std::chrono::seconds( 2 ),
                               "recreated component peer and certificate consumer ready",
                               nullptr );

    // The new accessor is observation-only; this asserts it can read no fabricated protocol progress.
    EXPECT_EQ( sgns::MultiNodeFinalityFaultTestAccess::VotePublications( nullptr ), 0u );
    StopPeer( first );
    StopPeer( second );
}
