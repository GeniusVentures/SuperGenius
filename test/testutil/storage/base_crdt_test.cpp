#include "testutil/storage/base_crdt_test.hpp"

#include <chrono>
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

using boost::asio::io_context;
using sgns::crdt::GlobalDB;
using sgns::crdt::KeyPairFileStorage;
using sgns::ipfs_pubsub::GossipPubSub;
using sgns::ipfs_pubsub::GossipPubSubTopic;

namespace
{
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
}

namespace test
{
    const std::string                       CRDTFixture::basePath = "CRDT.Datastore.TEST";
    std::shared_ptr<soralog::LoggingSystem> CRDTFixture::logging_system_;
    std::atomic<uint64_t>                   CRDTFixture::fixture_counter_{ 0 };

    CRDTFixture::CRDTFixture( fs::path path ) : FSFixture( std::move( path ) )
    {
        const auto fixture_id = fixture_counter_.fetch_add( 1, std::memory_order_relaxed ) + 1;
        const auto suffix     = std::to_string( fixture_id );
        keypair_path_         = basePath + "/unit_test_" + suffix;
        db_path_              = basePath + ".unit_" + suffix;

        io_ = std::make_shared<io_context>();

        pubs_ = std::make_shared<GossipPubSub>( KeyPairFileStorage( keypair_path_ ).GetKeyPair().value() );

        BOOST_ASSERT_MSG( pubs_ != nullptr, "could not create GossibPubSub for some reason" );
        auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
        auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>( std::make_shared<libp2p::basic::AsioSchedulerBackend>(io_), libp2p::basic::Scheduler::Config{std::chrono::milliseconds(100)} );
        auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
        auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(),
                                                                                             scheduler );

        auto globaldb_ret = GlobalDB::New( io_, db_path_, pubs_, crdtOptions, graphsyncnetwork, scheduler, generator );
        BOOST_ASSERT( globaldb_ret.has_value() );
        db_ = std::move( globaldb_ret.value() );

        db_->AddListenTopic( "CRDT.Datastore.TEST.Channel" );
        db_->AddBroadcastTopic( "CRDT.Datastore.TEST.Channel" );
        db_->Start();

        // Start GossipPubSub after Init — derive unique port from fixture_id to avoid
        // TIME_WAIT / rebind delays when tests share port 40001.
        const auto port = static_cast<uint16_t>( 40001 + ( fixture_id % 1000 ) );
        auto       future = pubs_->Start( port, { pubs_->GetLocalAddress() } );
        auto result = future.get();
        BOOST_ASSERT_MSG( !result, ( "GossipPubSub::Start failed: " + result.message() ).c_str() );
    }

    CRDTFixture::~CRDTFixture()
    {
        if ( pubs_ )
        {
            pubs_->Stop();
        }
        db_.reset();
        pubs_.reset();
        io_.reset();

        try
        {
            fs::remove_all( keypair_path_ );
            fs::remove_all( db_path_ );
        }
        catch ( const fs::filesystem_error &err )
        {
            std::cerr << err.what() << std::endl;
        }
    }

    void CRDTFixture::SetUpTestSuite()
    {
        // Defensive cleanup: a prior run's process-exit segfault (a known, pre-existing,
        // unrelated lifecycle issue) can skip the destructor's fs::remove_all cleanup below,
        // leaving "CRDT.Datastore.TEST" / "CRDT.Datastore.TEST.unit_N" directories behind.
        // fixture_counter_ restarts at 0 every process, so a fresh run's unit_N paths can
        // collide with stale leftovers from a previous crashed run - clear them up front.
        try
        {
            fs::remove_all( basePath );
            for ( const auto &entry : fs::directory_iterator( fs::current_path() ) )
            {
                const auto filename = entry.path().filename().string();
                if ( filename.rfind( basePath + ".unit_", 0 ) == 0 )
                {
                    fs::remove_all( entry.path() );
                }
            }
        }
        catch ( const fs::filesystem_error &err )
        {
            std::cerr << err.what() << std::endl;
        }

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
