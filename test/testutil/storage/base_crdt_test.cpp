

#include "testutil/storage/base_crdt_test.hpp"
#include <crdt/globaldb/keypair_file_storage.hpp>
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>
#include <crdt/globaldb/proto/broadcast.pb.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
const std::string logger_config(R"(
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
  )");
namespace test
{

    void CRDTFixture::open()
    {

        auto logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
        //    // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
        //    // Additional logging config for application
            logger_config ) );
        logging_system->configure();

        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "account_handling_test", soralog::Level::ERROR_ );

        auto loggerGlobalDB = sgns::base::createLogger( "GlobalDB" );
        loggerGlobalDB->set_level( spdlog::level::debug );

        auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer" );
        loggerDAGSyncer->set_level( spdlog::level::debug );
        pubs_ = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
            sgns::crdt::KeyPairFileStorage( "CRDT.Datastore.TEST/unit_test" ).GetKeyPair().value() ); //Make Host Pubsubs
        //std::vector<std::string> receivedMessages;

        //Start Pubsubs, add peers of other addresses. We'll probably use DHT Discovery bootstrapping in the future.
        pubs_->Start( 40001,  {pubs_->GetLocalAddress()});

        //Asio Context
        io_ = std::make_shared<boost::asio::io_context>();

        //Add to GlobalDB
        auto globalDB =
            std::make_shared<sgns::crdt::GlobalDB>( io_,"CRDT.Datastore.TEST.unit" , 40010,
                                                    std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>( pubs_, "CRDT.Datastore.TEST.Channel" ) );

        auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
        globalDB->Init( crdtOptions );
        db_ = std::move(globalDB);
        ASSERT_TRUE(db_) << "BaseCRDT_Test: db is nullptr";
    }

    CRDTFixture::CRDTFixture( fs::path path ) : FSFixture( std::move( path ) )
    {
    }

    void CRDTFixture::SetUp()
    {
        open();
    }

    void CRDTFixture::TearDown()
    {
        // clear();
    }
} // namespace test
