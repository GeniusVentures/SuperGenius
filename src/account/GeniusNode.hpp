#ifndef _GENIUS_NODE_HPP_
#define _GENIUS_NODE_HPP_
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
#include <boost/algorithm/string/replace.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>

typedef struct DevConfig
{
    char        Addr[255];
    std::string Cut;
    std::string TokenValueInGNUS;
    TokenID     TokenID;
    char        BaseWritePath[1024];
} DevConfig_st;

extern DevConfig_st DEV_CONFIG;

#define OUTGOING_TIMEOUT_MILLISECONDS 50000  // just communication time
#define INCOMING_TIMEOUT_MILLISECONDS 150000 // communication + verify proof

namespace sgns
{
    class GeniusNode : public IComponent, public std::enable_shared_from_this<GeniusNode>
    {
    public:
        static std::shared_ptr<GeniusNode> New( const DevConfig_st &dev_config,
                                                const char         *eth_private_key,
                                                bool                autodht      = true,
                                                bool                isprocessor  = true,
                                                uint16_t            base_port    = 40001,
                                                bool                is_full_node = false );

        ~GeniusNode() override;

        /**
         * @brief      GeniusNode Error class
         */
        enum class Error
        {
            INSUFFICIENT_FUNDS       = 1,  ///< Insufficient funds for a transaction
            DATABASE_WRITE_ERROR     = 2,  ///< Error writing data into the database
            INVALID_TRANSACTION_HASH = 3,  ///< Input transaction hash is invalid
            INVALID_CHAIN_ID         = 4,  ///< Chain ID is invalid
            INVALID_TOKEN_ID         = 5,  ///< Token ID is invalid
            TOKEN_ID_MISMATCH        = 6,  ///< Informed Token ID doesn't match initialized ID
            PROCESS_COST_ERROR       = 7,  ///< The calculated Processing cost was negative
            PROCESS_INFO_MISSING     = 8,  ///< Processing information missing on JSON file
            INVALID_JSON             = 9,  ///< JSON cannot be parsed>
            INVALID_BLOCK_PARAMETERS = 10, ///< JSON params for blocks incorrect or missing>
            NO_PROCESSOR             = 11, ///< No processor for this type>
            NO_PRICE                 = 12, ///< Couldn't get price of gnus>
        };

#ifdef SGNS_DEBUG
        static constexpr uint64_t TIMEOUT_ESCROW_PAY = 50000;
        static constexpr uint64_t TIMEOUT_TRANSFER   = 50000;
        static constexpr uint64_t TIMEOUT_MINT       = 50000;
#else
        static constexpr uint64_t TIMEOUT_ESCROW_PAY = 30000;
        static constexpr uint64_t TIMEOUT_TRANSFER   = 30000;
        static constexpr uint64_t TIMEOUT_MINT       = 30000;
#endif

        outcome::result<std::string> ProcessImage( const std::string &jsondata );

        uint64_t GetProcessCost( const std::string &json_data );

        outcome::result<double> GetGNUSPrice();

        std::string GetName() override
        {
            return "GeniusNode";
        }

        std::string GetVersion();

        /**
         * @brief       Mints tokens by converting a string amount to fixed-point representation
         * @param[in]   amount: Numeric value with amount in Minion Tokens (1e-6 GNUS Token)
         * @return      Outcome of mint token operation
         */
        outcome::result<std::pair<std::string, uint64_t>> MintTokens(
            uint64_t                  amount,
            const std::string        &transaction_hash,
            const std::string        &chainid,
            TokenID                   tokenid,
            std::chrono::milliseconds timeout = std::chrono::milliseconds( TIMEOUT_MINT ) );

        void     AddPeer( const std::string &peer );
        void     RefreshUPNP( uint16_t pubsubport );
        uint64_t GetBalance();
        uint64_t GetBalance( const TokenID token_id );
        uint64_t GetBalance( const std::string &address );
        uint64_t GetBalance( const TokenID token_id, const std::string &address );

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

        TokenID GetTokenID() const
        {
            return dev_config_.TokenID;
        }

        outcome::result<std::pair<std::string, uint64_t>> TransferFunds(
            uint64_t                  amount,
            const std::string        &destination,
            TokenID                   token_id,
            std::chrono::milliseconds timeout = std::chrono::milliseconds( TIMEOUT_TRANSFER ) );

        outcome::result<std::pair<std::string, uint64_t>> PayDev(
            uint64_t                  amount,
            TokenID                   token_id,
            std::chrono::milliseconds timeout = std::chrono::milliseconds( TIMEOUT_TRANSFER ) );

        std::shared_ptr<ipfs_pubsub::GossipPubSub> GetPubSub()
        {
            return pubsub_;
        }

        /**
         * @brief       Formats a fixed-point amount into a human-readable string.
         * @param[in]   amount  Amount in Minion Tokens (1e-6 GNUS).
         * @param[in]   tokenId Optional token identifier:
         *                         – empty: default (minion to GNUS) formatting
         *                         – matches DevConfig.TokenID: child-token formatting
         *                         – otherwise: returns Error::TOKEN_ID_MISMATCH
         * @return      Outcome result with the formatted string in GNUS or an error.
         */
        outcome::result<std::string> FormatTokens( uint64_t amount, const TokenID tokenId );

        /**
         * @brief       Parses a human-readable string into a fixed-point amount.
         * @param[in]   str      String representation of an amount in GNUS.
         * @param[in]   tokenId  Optional token identifier:
         *                          – empty: default (GNUS to minion) parsing
         *                          – matches DevConfig.TokenID: child-token parsing
         *                          – otherwise: returns Error::TOKEN_ID_MISMATCH
         * @return      Outcome result with the parsed amount in Minion Tokens (1e-6 GNUS) or an error.
         */
        outcome::result<uint64_t> ParseTokens( const std::string &str, const TokenID tokenId );

        void PrintDataStore();
        void StopProcessing();
        void StartProcessing();

        outcome::result<std::map<std::string, double>> GetCoinprice( const std::vector<std::string> &tokenIds );
        outcome::result<std::map<std::string, std::map<int64_t, double>>> GetCoinPriceByDate(
            const std::vector<std::string> &tokenIds,
            const std::vector<int64_t>     &timestamps );
        outcome::result<std::map<std::string, std::map<int64_t, double>>> GetCoinPricesByDateRange(
            const std::vector<std::string> &tokenIds,
            int64_t                         from,
            int64_t                         to );
        // Wait for an incoming transaction to be processed with a timeout
        TransactionManager::TransactionStatus WaitForTransactionIncoming( const std::string        &txId,
                                                                          std::chrono::milliseconds timeout );
        // Wait for a outgoing transaction to be processed with a timeout
        TransactionManager::TransactionStatus WaitForTransactionOutgoing( const std::string        &txId,
                                                                          std::chrono::milliseconds timeout );

        TransactionManager::TransactionStatus WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                                    std::chrono::milliseconds timeout );

        TransactionManager::State GetTransactionManagerState() const;

        TransactionManager::TransactionStatus GetTransactionStatus( const std::string &txId ) const;

    protected:
        friend class TransactionSyncTest;

        void SendTransactionAndProof( std::shared_ptr<IGeniusTransactions> tx, std::vector<uint8_t> proof );
        void ConfigureTransactionFilterTimeoutsMs( uint64_t timeframe_limit_ms, uint64_t mutability_window_ms );

        std::shared_ptr<GeniusAccount> account_;

    private:
        std::shared_ptr<ipfs_pubsub::GossipPubSub>            pubsub_;
        std::shared_ptr<boost::asio::io_context>              io_;
        std::shared_ptr<crdt::GlobalDB>                       tx_globaldb_;
        std::shared_ptr<crdt::GlobalDB>                       job_globaldb_;
        std::shared_ptr<TransactionManager>                   transaction_manager_;
        std::shared_ptr<processing::ProcessingTaskQueueImpl>  task_queue_;
        std::shared_ptr<processing::ProcessingCoreImpl>       processing_core_;
        std::shared_ptr<processing::ProcessingServiceImpl>    processing_service_;
        std::shared_ptr<processing::SubTaskResultStorageImpl> task_result_storage_;
        std::shared_ptr<soralog::LoggingSystem>               logging_system_;
        std::string                                           write_base_path_;
        bool                                                  autodht_;
        bool                                                  isprocessor_;
        base::Logger                                          node_logger_;
        DevConfig_st                                          dev_config_;
        std::string                                           gnus_network_full_path_;
        std::string                                           processing_channel_topic_;
        std::string                                           processing_grid_chanel_topic_;
        uint16_t                                              pubsubport_;

        GeniusNode( const DevConfig_st &dev_config,
                    const char         *eth_private_key,
                    bool                autodht,
                    bool                isprocessor,
                    uint16_t            base_port,
                    bool                is_full_node );
        bool                  InitLoggers( const std::string &base_path );
        outcome::result<void> CheckProcessValidity( const std::string &jsondata );
        void                  DHTInit();

        struct PriceInfo
        {
            double                                             price;
            std::chrono::time_point<std::chrono::system_clock> lastUpdate;
        };

        std::map<std::string, PriceInfo>                   m_tokenPriceCache;
        const std::chrono::minutes                         m_cacheValidityDuration{ 1 };
        std::chrono::time_point<std::chrono::system_clock> m_lastApiCall{};
        static constexpr std::chrono::seconds              m_minApiCallInterval{ 5 };

        std::thread       io_thread;
        std::thread       upnp_thread;
        std::atomic<bool> stop_upnp{ false };

        std::unique_ptr<boost::asio::thread_pool> processing_callback_pool_;

        outcome::result<std::pair<std::string, uint64_t>> PayEscrow(
            const std::string                       &escrow_path,
            const SGProcessing::TaskResult          &taskresult,
            std::shared_ptr<crdt::AtomicTransaction> crdt_transaction,
            std::chrono::milliseconds                timeout = std::chrono::milliseconds( TIMEOUT_ESCROW_PAY ) );

        void ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult );
        void ProcessingError( const std::string &task_id );

        void rotateLogFiles( const std::string &base_path );
        /**
         * @brief Parse and sum all "block_len" values from the JSON.
         * @param json_data JSON string containing an "input" array.
         * @return outcome::result<uint64_t> with total bytes, or an error code.
         */
        outcome::result<uint64_t> ParseBlockSize( const std::string &json_data );

        void TransactionStateChanged( TransactionManager::State old_state,
                                      TransactionManager::State new_state );

        static constexpr std::string_view db_path_        = "bc-%d/";
        static constexpr std::uint16_t    MAIN_NET        = 369;
        static constexpr std::uint16_t    TEST_NET        = 963;
        static constexpr std::size_t      MAX_NODES_COUNT = 1;

        static constexpr std::string_view PROCESSING_GRID_CHANNEL = "SGNUS.Jobs.Channel";
        static constexpr std::string_view PROCESSING_CHANNEL      = "SGNUS.Processing.Channel";
        static constexpr std::string_view GNUS_NETWORK_PATH       = "SuperGNUSNode.Node";

        static std::string GetLoggingSystem( const std::string &base_path )
        {
            std::string config( R"(
# ----------------
sinks:
    - name: file
      type: file
      capacity: 1000
      path: [basepath]/sgnslog.log
groups:
    - name: SuperGeniusNode
      sink: file
      level: error
      children:
        - name: libp2p
        - name: Gossip
        - name: yx-stream
# ----------------
  )" );

            boost::replace_all( config, "[basepath]", base_path );
            return config;
        }
    };

}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, GeniusNode::Error );

#endif
