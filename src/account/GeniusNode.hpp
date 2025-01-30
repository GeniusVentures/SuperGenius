#ifndef _ACCOUNT_MANAGER_HPP_
#define _ACCOUNT_MANAGER_HPP_
#include <memory>

#include <boost/asio.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>

#include "account/GeniusAccount.hpp"
#include "base/buffer.hpp"
#include "account/TransactionManager.hpp"
#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include "crypto/hasher/hasher_impl.hpp"
#include "processing/impl/processing_core_impl.hpp"
#include "processing/impl/processing_subtask_result_storage_impl.hpp"
#include "processing/processing_service.hpp"
#include "singleton/IComponent.hpp"
#include "processing/impl/processing_task_queue_impl.hpp"

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
        GeniusNode( const DevConfig_st &dev_config,
                    const char         *eth_private_key,
                    bool                autodht     = true,
                    bool                isprocessor = true,
                    uint16_t            base_port   = 40001 );
        // static GeniusNode &GetInstance()
        // {
        //     return instance;
        // }
        ~GeniusNode() override;

        void ProcessImage( const std::string &image_path );

        double GetProcessCost( const std::string &json_data );

        std::string GetName() override
        {
            return "GeniusNode";
        }

        void     DHTInit();
        void     MintTokens( double amount, std::string transaction_hash, std::string chainid, std::string tokenid );
        void     AddPeer( const std::string &peer );
        void     RefreshUPNP( int pubsubport, int graphsyncport );
        double GetBalance();

        [[nodiscard]] const std::vector<std::vector<uint8_t>> GetInTransactions() const
        {
            return transaction_manager_->GetInTransactions();
        }

        [[nodiscard]] const std::vector<std::vector<uint8_t>> GetOutTransactions() const
        {
            return transaction_manager_->GetOutTransactions();
        }

        template <typename T>
        T GetAddress() const;

        template <>
        std::string GetAddress() const
        {
            return account_->GetAddress<std::string>();
        }

        template <>
        uint256_t GetAddress() const
        {
            return account_->GetAddress<uint256_t>();
        }

        bool TransferFunds( double amount, const uint256_t &destination )
        {
            return transaction_manager_->TransferFunds( amount, destination );
        }

        std::shared_ptr<ipfs_pubsub::GossipPubSub> GetPubSub()
        {
            return pubsub_;
        }

        static std::vector<uint8_t> GetImageByCID( const std::string &cid );

    private:
        std::shared_ptr<GeniusAccount>                        account_;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>            pubsub_;
        std::shared_ptr<boost::asio::io_context>              io_;
        std::shared_ptr<crdt::GlobalDB>                       globaldb_;
        std::shared_ptr<TransactionManager>                   transaction_manager_;
        std::shared_ptr<processing::ProcessingTaskQueueImpl>  task_queue_;
        std::shared_ptr<processing::ProcessingCoreImpl>       processing_core_;
        std::shared_ptr<processing::ProcessingServiceImpl>    processing_service_;
        std::shared_ptr<processing::SubTaskResultStorageImpl> task_result_storage_;
        std::shared_ptr<soralog::LoggingSystem>               logging_system;
        std::string                                           write_base_path_;
        bool                                                  autodht_;
        bool                                                  isprocessor_;

        std::thread       io_thread;
        std::thread       upnp_thread;
        std::atomic<bool> stop_upnp{ false };

        DevConfig_st dev_config_;

        void ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult );
        void ProcessingError( const std::string &task_id );

        static constexpr std::string_view db_path_                = "bc-%d/";
        static constexpr std::uint16_t    MAIN_NET                = 369;
        static constexpr std::uint16_t    TEST_NET                = 963;
        static constexpr std::size_t      MAX_NODES_COUNT         = 1;
        static constexpr std::string_view PROCESSING_GRID_CHANNEL = "SGNUS.Jobs.1a.03";
        static constexpr std::string_view PROCESSING_CHANNEL      = "SGNUS.TestNet.Channel.1a.03";

        static const std::string &GetLoggingSystem()
        {
            //static const std::string logger_config = R"(
            //# ----------------
            //sinks:
            //  - name: console
            //    type: console
            //    color: true
            //groups:
            //  - name: SuperGeniusDemo
            //    sink: console
            //    level: info
            //    children:
            //      - name: libp2p
            //      - name: Gossip
            //# ----------------
            //)";
            static const std::string logger_config = R"(
            # ----------------
            sinks:
              - name: file
                type: file
                capacity: 40480
                path: sgnslog.log
            groups:
              - name: SuperGeniusDemo
                sink: file
                level: debug
                children:
                  - name: libp2p
                  - name: Gossip
            # ----------------
            )";
            return logger_config;
        }

        //static GeniusNode instance;
    };

}

#endif
