#include "testutil/storage/base_crdt_test.hpp"

#include <atomic>
#include <chrono>
#include <boost/dll/runtime_symbol_info.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <memory>

#include <boost/asio/io_context.hpp>
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/proto/broadcast.pb.h"
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include "testutil/remove_all.hpp"

using boost::asio::io_context;
using sgns::crdt::GlobalDB;
using sgns::crdt::KeyPairFileStorage;
using sgns::ipfs_pubsub::GossipPubSub;
using sgns::ipfs_pubsub::GossipPubSubTopic;

namespace
{
    std::atomic<uint64_t> fixture_counter{ 0 };

    const std::string logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: gossip_pubsub_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )" );

    fs::path UniqueFixturePath( const fs::path &path )
    {
        const auto fixture_id = fixture_counter.fetch_add( 1, std::memory_order_relaxed ) + 1;
        return path.string() + "." + boost::dll::program_location().stem().string() + "." + std::to_string( fixture_id );
    }
}

namespace test
{
    std::shared_ptr<soralog::LoggingSystem> CRDTFixture::logging_system_;

    CRDTFixture::CRDTFixture( fs::path path ) : FSFixture( UniqueFixturePath( path ) )
    {
        keypair_path_ = ( base_path / "keypair" ).string();
        db_path_      = ( base_path / "db" ).string();

        // Application-work pool, mirroring GeniusNode::io_. Tests drive it by hand
        // (io_->restart()/poll()), so it must stay separate from the host's context.
        io_ = std::make_shared<io_context>();

        pubs_ = std::make_shared<GossipPubSub>( KeyPairFileStorage( keypair_path_ ).GetKeyPair().value() );

        BOOST_ASSERT_MSG( pubs_ != nullptr, "could not create GossibPubSub for some reason" );
        auto future = pubs_->Start( 0, {} );
        auto result = future.get();
        BOOST_ASSERT_MSG( !result, ( "GossipPubSub::Start failed: " + result.message() ).c_str() );

        auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
        // GraphSync writes to libp2p streams from its scheduler thread, and libp2p is
        // single-threaded per host, so the scheduler has to run on the host's
        // io_context. A private one here races yamux's WriteQueue.
        auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>( std::make_shared<libp2p::basic::AsioSchedulerBackend>(pubs_->GetAsioContext()), libp2p::basic::Scheduler::Config{std::chrono::milliseconds(100)} );
        auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
        auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(),
                                                                                             scheduler );

        auto globaldb_ret = GlobalDB::New( io_, db_path_, pubs_, crdtOptions, graphsyncnetwork, scheduler, generator );
        BOOST_ASSERT( globaldb_ret.has_value() );
        db_ = std::move( globaldb_ret.value() );

        db_->AddListenTopic( "CRDT.Datastore.TEST.Channel" );
        db_->AddBroadcastTopic( "CRDT.Datastore.TEST.Channel" );
        db_->Start();
    }

    CRDTFixture::~CRDTFixture()
    {
        // GossipPubSub::Stop() releases the libp2p host and its io_context, and
        // CRDT/GraphSync teardown still talks to both, so the DB has to be shut down
        // first. Same order as globaldb_integration.cpp's TestNodeCollection.
        if ( db_ )
        {
            db_->ShutdownNow();
        }
        db_.reset();
        try
        {
            if ( pubs_ )
            {
                pubs_->Stop();
            }
        }
        catch ( const std::exception &err )
        {
            std::cerr << "GossipPubSub::Stop() exception: " << err.what() << std::endl;
        }
        try
        {
            pubs_.reset();
        }
        catch ( const std::exception &err )
        {
            std::cerr << "GossipPubSub destructor exception: " << err.what() << std::endl;
        }
        io_.reset();

        try
        {
            sgns::test::removeAllWithRetry( keypair_path_ );
            sgns::test::removeAllWithRetry( db_path_ );
        }
        catch ( const std::exception &err )
        {
            std::cerr << err.what() << std::endl;
        }
    }

    void CRDTFixture::SetUpTestSuite()
    {
        if ( !logging_system_ )
        {
            logging_system_ = std::make_shared<soralog::LoggingSystem>(
                std::make_shared<soralog::ConfiguratorFromYAML>( std::make_shared<libp2p::log::Configurator>(),
                                                                 logger_config ) );

            const auto config_result = logging_system_->configure();
            if ( config_result.has_error )
            {
                throw std::runtime_error( "CRDTFixture logging system configure failed" );
            }

            libp2p::log::setLoggingSystem( logging_system_ );
            libp2p::log::setLevelOfGroup( "account_handling_test", soralog::Level::ERROR_ );

            auto loggerGlobalDB = sgns::base::createLogger( "GlobalDB" );
            loggerGlobalDB->set_level( spdlog::level::debug );

            auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer" );
            loggerDAGSyncer->set_level( spdlog::level::debug );
        }
    }

    void CRDTFixture::TearDownTestSuite()
    {
        logging_system_.reset();
    }

}
