#ifndef _ACCOUNT_MANAGER_HPP_
#define _ACCOUNT_MANAGER_HPP_
#include <memory>
#include <cstdint>
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
#include "coinprices/coinprices.hpp"

typedef struct DevConfig
{
    char        Addr[255];
    std::string Cut;
    double      TokenValueInGNUS;
    int         TokenID;
    char        BaseWritePath[1024];
} DevConfig_st;

extern DevConfig_st DEV_CONFIG;

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

        ~GeniusNode() override;

        /**
         * @brief      GeniusNode Error class
         */
        enum class Error
        {
            INSUFFICIENT_FUNDS       = 1, ///<Insufficient funds for a transaction
            DATABASE_WRITE_ERROR     = 2, ///<Error writing data into the database
            INVALID_TRANSACTION_HASH = 3, ///<Input transaction hash is invalid
            INVALID_CHAIN_ID         = 4, ///<Chain ID is invalid
            INVALID_TOKEN_ID         = 5, ///<Token ID is invalid
            TOKEN_ID_MISMATCH        = 6, ///<Informed Token ID doesn't match initialized ID
            PROCESS_COST_ERROR       = 7, ///<The calculated Processing cost was negative
            PROCESS_INFO_MISSING     = 8, ///<Processing information missing on JSON file
            INVALID_JSON             = 9, ///<JSON cannot be parsed>
            INVALID_BLOCK_PARAMETERS =10, ///<JSON params for blocks incorrect or missing>
            NO_PROCESSOR             =11, ///<No processor for this type>
        };

        outcome::result<void> ProcessImage( const std::string &jsondata );
        outcome::result<void> CheckProcessValidity( const std::string& jsondata );

        uint64_t GetProcessCost( const std::string &json_data );

        double GetGNUSPrice();

        std::string GetName() override
        {
            return "GeniusNode";
        }

        void DHTInit();
        /**
         * @brief       Mints tokens by converting a string amount to fixed-point representation
         * @param[in]   amount: Numeric value with amount in Minion Tokens (1e-9 GNUS Token)
         * @return      Outcome of mint token operation
         */
        outcome::result<void> MintTokens( uint64_t          amount,
                                          const std::string &transaction_hash,
                                          const std::string &chainid,
                                          const std::string &tokenid );
        void                  AddPeer( const std::string &peer );
        void                  RefreshUPNP( int pubsubport, int graphsyncport );
        uint64_t              GetBalance();

        [[nodiscard]] const std::vector<std::vector<uint8_t>> GetInTransactions() const
        {
            return transaction_manager_->GetInTransactions();
        }

        [[nodiscard]] const std::vector<std::vector<uint8_t>> GetOutTransactions() const
        {
            return transaction_manager_->GetOutTransactions();
        }

        std::string GetAddress() const
        {
            return account_->GetAddress();
        }

        bool TransferFunds( uint64_t amount, const std::string &destination )
        {
            return transaction_manager_->TransferFunds( amount, destination );
        }

        std::shared_ptr<ipfs_pubsub::GossipPubSub> GetPubSub()
        {
            return pubsub_;
        }

        /**
         * @brief       Formats a fixed-point amount into a human-readable string.
         * @param[in]   amount Amount in Minion Tokens (1e-9 GNUS).
         * @return      Formatted string representation in GNUS.
         */
        static std::string FormatTokens( uint64_t amount );

        /**
         * @brief       Parses a human-readable string into a fixed-point amount.
         * @param[in]   str String representation of an amount in GNUS.
         * @return      Outcome result with the parsed amount in Minion Tokens (1e-9 GNUS) or error.
         */
        static outcome::result<uint64_t> ParseTokens( const std::string &str );

        static std::vector<uint8_t> GetImageByCID( const std::string &cid );

        void PrintDataStore();
        void StopProcessing();
        void StartProcessing();

        outcome::result<std::map<std::string, double>> GetCoinprice(const std::vector<std::string>& tokenIds);
        outcome::result<std::map<std::string, std::map<int64_t, double>>> GetCoinPriceByDate(
            const std::vector<std::string>& tokenIds,
            const std::vector<int64_t>& timestamps);
        outcome::result<std::map<std::string, std::map<int64_t, double>>> GetCoinPricesByDateRange(
            const std::vector<std::string>& tokenIds,
            int64_t from,
            int64_t to);

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
    };

}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, GeniusNode::Error );

#endif
