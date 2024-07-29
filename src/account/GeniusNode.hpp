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
#include "singleton/IComponent.hpp"
#include "processing/impl/processing_task_queue_impl.hpp"
#include "processing/impl/processing_core_impl.hpp"
#include "processing/impl/processing_subtask_result_storage_impl.hpp"
#include "processing/processing_service.hpp"
#include "upnp.hpp"

#ifndef __cplusplus
extern "C"
{
#endif
    typedef struct DevConfig
    {
        char   Addr[255];
        double Cut;
        double TokenValueInGNUS;
        int    TokenID;
        char   BaseWritePath[1024];
    } DevConfig_st;
#ifndef __cplusplus
}
#endif

#ifndef __cplusplus
extern "C"
{
#endif
    extern DevConfig_st DEV_CONFIG;
#ifndef __cplusplus
}
#endif

namespace sgns
{
    class GeniusNode : public IComponent
    {
    public:
        GeniusNode( const DevConfig_st &dev_config );
        // static GeniusNode &GetInstance()
        // {
        //     return instance;
        // }
        ~GeniusNode();

        void ProcessImage( const std::string &image_path, uint16_t funds );

        std::string GetName() override
        {
            return "GeniusNode";
        }

        void     DHTInit();
        void     MintTokens( uint64_t amount );
        void     AddPeer( const std::string &peer );
        uint64_t GetBalance();

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
        std::shared_ptr<processing::SubTaskResultStorageImpl>      task_result_storage_;
        std::shared_ptr<soralog::LoggingSystem>                    logging_system;
        std::string                                                write_base_path_;

        std::thread io_thread;

        DevConfig_st dev_config_;

        uint16_t GenerateRandomPort( const std::string &address );

        void ProcessingDone( const std::string &subtask_id );
        void ProcessingError( const std::string &subtask_id );
        void ProcessingFinished( const std::string &task_id, const std::set<std::string> &subtasks_ids );

        static constexpr std::string_view db_path_                = "bc-%d/";
        static constexpr std::uint16_t    MAIN_NET                = 369;
        static constexpr std::uint16_t    TEST_NET                = 963;
        static constexpr std::size_t      MAX_NODES_COUNT         = 1;
        static constexpr std::string_view PROCESSING_GRID_CHANNEL = "GRID_CHANNEL_ID";
        static constexpr std::string_view PROCESSING_CHANNEL      = "SGNUS.TestNet.Channel";

        static const std::string &GetLoggingSystem()
        {
            static const std::string logger_config = R"(
        # ----------------
        sinks:
            - name: console
            type: console
            color: true
        groups:
            - name: SuperGeniusDemo
            sink: console
            level: info
            children:
                - name: libp2p
                - name: Gossip
        # ----------------
            )";
            return logger_config;
        }

        //static GeniusNode instance;
    };

};

#endif
