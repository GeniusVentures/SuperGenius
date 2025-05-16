#include "testutil/storage/base_crdt_test.hpp"

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
#include "libp2p/protocol/common/asio/asio_scheduler.hpp"

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
    const std::string             CRDTFixture::basePath      = "CRDT.Datastore.TEST";
    bool                          CRDTFixture::initializedDb = false;
    std::shared_ptr<io_context>   CRDTFixture::io_;
    std::shared_ptr<GossipPubSub> CRDTFixture::pubs_;
    std::shared_ptr<GlobalDB>     CRDTFixture::db_;

    CRDTFixture::CRDTFixture( fs::path path ) : FSFixture( std::move( path ) ) {}

    CRDTFixture::~CRDTFixture()
    {
        fs::remove_all( basePath );
        fs::remove_all( basePath + ".unit" );
    }

    void CRDTFixture::SetUpTestSuite()
    {
        // Logging antics
        {
            auto logging_system = std::make_shared<soralog::LoggingSystem>(
                std::make_shared<soralog::ConfiguratorFromYAML>( std::make_shared<libp2p::log::Configurator>(),
                                                                 logger_config ) );

            BOOST_ASSERT( !logging_system->configure().has_error );

            libp2p::log::setLoggingSystem( std::move( logging_system ) );
            libp2p::log::setLevelOfGroup( "account_handling_test", soralog::Level::ERROR_ );

            auto loggerGlobalDB = sgns::base::createLogger( "GlobalDB" );
            loggerGlobalDB->set_level( spdlog::level::debug );

            auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer" );
            loggerDAGSyncer->set_level( spdlog::level::debug );
        }

        if ( !initializedDb )
        {
            io_ = std::make_shared<io_context>();

            pubs_ = std::make_shared<GossipPubSub>(
                KeyPairFileStorage( basePath + "/unit_test" ).GetKeyPair().value() );

            BOOST_ASSERT_MSG( pubs_ != nullptr, "could not create GossibPubSub for some reason" );

            auto crdtOptions      = sgns::crdt::CrdtOptions::DefaultOptions();
            auto scheduler        = std::make_shared<libp2p::protocol::AsioScheduler>( io_,
                                                                                libp2p::protocol::SchedulerConfig{} );
            auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
            auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(),
                                                                                                 scheduler );

            auto globaldb_ret =
                GlobalDB::New( io_, basePath + ".unit", pubs_, crdtOptions, graphsyncnetwork, scheduler, generator );
            BOOST_ASSERT( globaldb_ret.has_value() );
            db_ = std::move( globaldb_ret.value() );

            db_->AddListenTopic( "CRDT.Datastore.TEST.Channel" );
            db_->AddBroadcastTopic( "CRDT.Datastore.TEST.Channel" );
            db_->Start();

            // Start GossipPubSub after Init
            auto future = pubs_->Start( 40001, { pubs_->GetLocalAddress() } );
            auto result = future.get();
            BOOST_ASSERT_MSG( !result, ( "GossipPubSub::Start failed: " + result.message() ).c_str() );

            initializedDb = true;
        }
    }
}
