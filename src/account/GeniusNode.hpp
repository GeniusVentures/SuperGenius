/**
 * @file       GeniusNode.hpp
 * @brief      
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _ACCOUNT_MANAGER_HPP_
#define _ACCOUNT_MANAGER_HPP_
#include <memory>
#include <boost/asio.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include "account/GeniusAccount.hpp"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/globaldb/proto/broadcast.pb.h"
#include "account/TransactionManager.hpp"
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include "crypto/hasher/hasher_impl.hpp"
#include "blockchain/impl/key_value_block_header_repository.hpp"
#include "blockchain/impl/key_value_block_storage.hpp"
#include "integration/IComponent.hpp"
#include "processing/impl/processing_task_queue_impl.hpp"
#include "processing/impl/processing_core_impl.hpp"
#include "processing/processing_service.hpp"

#ifndef __cplusplus
extern "C"
{
#endif
    typedef struct DevConfig
    {
        char  Addr[255];
        float Cut;
    } DevConfig_st;

    typedef char AccountKey[255];
#ifndef __cplusplus
}
#endif

namespace sgns
{
    class GeniusNode : public IComponent
    {
    public:
        GeniusNode( const AccountKey &priv_key_data, const DevConfig_st &dev_config );
        //static GeniusNode &GetInstance();
        ~GeniusNode();

        void ProcessImage( const std::string &image_path, uint16_t funds );

        std::string GetName() override
        {
            return "GeniusNode";
        }

        void DHTInit();
        void MintTokens( uint64_t amount );
        void AddPeer( const std::string &peer );

        std::vector<uint8_t> GetImageByCID( std::string cid );

    private:
        std::shared_ptr<GeniusAccount>                             account_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                 pubsub_;
        std::shared_ptr<boost::asio::io_context>                   io_;
        std::shared_ptr<crdt::GlobalDB>                            globaldb_;
        std::shared_ptr<crypto::HasherImpl>                        hasher_;
        std::shared_ptr<blockchain::KeyValueBlockHeaderRepository> header_repo_;
        std::shared_ptr<blockchain::KeyValueBlockStorage>          block_storage_;
        std::shared_ptr<TransactionManager>                        transaction_manager_;
        std::shared_ptr<processing::ProcessingTaskQueueImpl>       task_queue_;
        std::shared_ptr<processing::ProcessingCoreImpl>            processing_core_;
        std::shared_ptr<processing::ProcessingServiceImpl>         processing_service_;
        std::shared_ptr<soralog::LoggingSystem>                    logging_system;

        std::thread io_thread;

        DevConfig_st dev_config_;
        std::string  node_base_addr_;

        uint16_t GenerateRandomPort( const std::string &address );

        static constexpr std::string_view db_path_                = "bc-%d/";
        static constexpr std::uint16_t    MAIN_NET                = 369;
        static constexpr std::uint16_t    TEST_NET                = 963;
        static constexpr std::size_t      MAX_NODES_COUNT         = 1;
        static constexpr std::string_view PROCESSING_GRID_CHANNEL = "GRID_CHANNEL_ID";
        static constexpr std::string_view PROCESSING_CHANNEL      = "SGNUS.TestNet.Channel";
    };
};

#endif
