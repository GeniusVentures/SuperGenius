#ifndef _GENIUS_NODE_HPP_
#define _GENIUS_NODE_HPP_

#include "UTXOManager.hpp"

#include <memory>
#include <cstdint>
#include <functional>
#include <vector>
#include <thread>
#include <optional>

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
#include "blockchain/Blockchain.hpp"
#include <boost/algorithm/string/replace.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <processingbase/ProcessingManager.hpp>

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
                                                bool                is_full_node = false,
                                                bool                use_upnp     = true );

        ~GeniusNode() override;

        enum class NodeState
        {
            CREATING = 0,
            MIGRATING_DATABASE,
            INITIALIZING_DATABASE,
            INITIALIZING_PROCESSING,
            INITIALIZING_BLOCKCHAIN,
            INITIALIZING_TRANSACTIONS,
            INITIALIZING_DHT,
            READY,
        };

        /**
         * @brief      GeniusNode Error class
         */
        enum class Error
        {
            INSUFFICIENT_FUNDS        = 1,  ///< Insufficient funds for a transaction
            DATABASE_WRITE_ERROR      = 2,  ///< Error writing data into the database
            INVALID_TRANSACTION_HASH  = 3,  ///< Input transaction hash is invalid
            INVALID_CHAIN_ID          = 4,  ///< Chain ID is invalid
            INVALID_TOKEN_ID          = 5,  ///< Token ID is invalid
            TOKEN_ID_MISMATCH         = 6,  ///< Informed Token ID doesn't match initialized ID
            PROCESS_COST_ERROR        = 7,  ///< The calculated Processing cost was negative
            PROCESS_INFO_MISSING      = 8,  ///< Processing information missing on JSON file
            INVALID_JSON              = 9,  ///< JSON cannot be parsed>
            INVALID_BLOCK_PARAMETERS  = 10, ///< JSON params for blocks incorrect or missing>
            NO_PROCESSOR              = 11, ///< No processor for this type
            NO_PRICE                  = 12, ///< Couldn't get price of gnus
            TRANSACTIONS_NOT_READY    = 13, ///< Transactions aren't ready
            TRANSACTION_NOT_FINALIZED = 14, ///< Requested transaction not finalized within timeout
            TRANSACTION_FAILED        = 15, ///< Requested transaction failed
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

        uint64_t GetProcessCost( std::shared_ptr<sgns::sgprocessing::ProcessingManager> &procmgr );

        outcome::result<double> GetGNUSPrice();

        std::string GetName() override
        {
            return "GeniusNode";
        }

        std::string GetVersion();

        /** Reloads log level overrides from log_config.json at runtime. */
        void LoadLogConfig();

        /**
         * @brief       Fires of a Mint transaction and returns the transaction hash
         * @param[in]   amount Amount to be minted
         * @param[in]   transaction_hash Hash of the transaction that burned the tokens elsewhere
         * @param[in]   chainid The chain ID where the burn transaction took place
         * @param[in]   tokenid The token ID of the tokens to mint 
         * @return      The transaction hash of the mint transaction if successful, or an error otherwise
         */
        outcome::result<std::string> MintTokens( uint64_t           amount,
                                                 const std::string &transaction_hash,
                                                 const std::string &chainid,
                                                 TokenID            tokenid );

        /**
         * @brief       Mints tokens by converting a string amount to fixed-point representation
         * @param[in]   amount: Numeric value with amount in Minion Tokens (1e-6 GNUS Token)
         * @return      Outcome of mint token operation
         */
        outcome::result<std::pair<std::string, uint64_t>> MintTokens( uint64_t                  amount,
                                                                      const std::string        &transaction_hash,
                                                                      const std::string        &chainid,
                                                                      TokenID                   tokenid,
                                                                      std::chrono::milliseconds timeout );

        void AddPeer( const std::string &peer );
        void RefreshUPNP( uint16_t pubsubport );

        uint64_t GetBalance();
        uint64_t GetBalance( TokenID token_id );
        uint64_t GetBalance( const std::string &address );
        uint64_t GetBalance( TokenID token_id, const std::string &address );

        [[nodiscard]] const std::vector<std::vector<uint8_t>> GetInTransactions() const
        {
            auto manager_result = GetTransactionManager();
            if ( !manager_result.has_value() )
            {
                return {};
            }
            return manager_result.value()->GetInTransactions();
        }

        [[nodiscard]] const std::vector<std::vector<uint8_t>> GetOutTransactions() const
        {
            auto manager_result = GetTransactionManager();
            if ( !manager_result.has_value() )
            {
                return {};
            }
            return manager_result.value()->GetOutTransactions();
        }

        std::string GetAddress() const
        {
            return account_->GetAddress();
        }

        TokenID GetTokenID() const
        {
            return dev_config_.TokenID;
        }

        [[nodiscard]] processing::ProcessingServiceImpl::ProcessingStatus GetProcessingStatus() const
        {
            return processing_service_ == nullptr ? processing::ProcessingServiceImpl::ProcessingStatus(
                                                        processing::ProcessingServiceImpl::Status::DISABLED,
                                                        0.0f )
                                                  : processing_service_->GetProcessingStatus();
        }

        outcome::result<std::pair<std::string, uint64_t>> TransferFunds( uint64_t                  amount,
                                                                         const std::string        &destination,
                                                                         TokenID                   token_id,
                                                                         std::chrono::milliseconds timeout );

        outcome::result<std::string> TransferFunds( uint64_t amount, const std::string &destination, TokenID token_id );

        outcome::result<std::string> PayDev( uint64_t amount, TokenID token_id );

        outcome::result<std::pair<std::string, uint64_t>> PayDev( uint64_t                  amount,
                                                                  TokenID                   token_id,
                                                                  std::chrono::milliseconds timeout );

        outcome::result<std::pair<TransactionManager::TransactionStatus, uint64_t>> WaitForFinalized(
            const std::string        &tx_id,
            std::chrono::milliseconds timeout );

        std::optional<TransactionManager::TransactionStatus> IsFinalized( const std::string &tx_id );

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

        void PrintDataStore() const;
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

        /**
         * @brief Set the authorized full node address for blockchain genesis verification
         * @param pub_address The public address that is authorized to create genesis blocks
         */
        void SetAuthorizedFullNodeAddress( const std::string &pub_address );

        /**
         * @brief Get the current authorized full node public address
         * @return The authorized full node public address
         */
        const std::string &GetAuthorizedFullNodeAddress() const;

        NodeState GetState() const
        {
            return state_.load();
        }

    protected:
        friend class TransactionSyncTest;

        void SendTransactionAndProof( std::shared_ptr<IGeniusTransactions> tx, std::vector<uint8_t> proof );
        void ConfigureTransactionFilterTimeoutsMs( uint64_t timeframe_limit_ms, uint64_t mutability_window_ms );

        std::string                    write_base_path_;
        std::shared_ptr<GeniusAccount> account_;
        UTXOManager                    utxo_manager_;

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
        bool                                                  autodht_;
        bool                                                  isprocessor_;
        bool                                                  is_full_node_;
        base::Logger                                          node_logger_;
        DevConfig_st                                          dev_config_;
        std::string                                           gnus_network_full_path_;
        std::string                                           processing_channel_topic_;
        std::string                                           processing_grid_chanel_topic_;
        std::vector<std::string>                              bootstrap_peers_;
        std::vector<std::string>                              bootstrap_fullnodes_;
        uint16_t                                              pubsubport_;
        std::shared_ptr<Blockchain>                           blockchain_;

        GeniusNode( const DevConfig_st &dev_config,
                    const char         *eth_private_key,
                    bool                autodht,
                    bool                isprocessor,
                    uint16_t            base_port,
                    bool                is_full_node,
                    bool                use_upnp );
        void         InitOpenSSL();
        bool         InitLoggers( const std::string &base_path );
        base::Logger ConfigureLogger( const std::string        &tag,
                                      const std::string        &logdir,
                                      spdlog::level::level_enum level );
        bool         InitNetwork( uint16_t base_port, bool is_full_node );
        void         LoadCrdtConfig();
        bool         InitUPNP();
        bool         InitDatabase();
        bool         InitProcessingModules();
        void         BeginDBInitialization();
        void         StateTransition( NodeState next_state );
        void         MigrateDatabase( std::function<void( outcome::result<void> )> callback );
        void         ScheduleMigrationRetry();
        void         ScheduleBlockchainRetry();
        void         ShutdownForDestruction();
        outcome::result<std::shared_ptr<TransactionManager>> GetTransactionManager() const;
        outcome::result<void>                                CheckProcessValidity( const std::string &jsondata );

        void DHTInit();

        struct PriceInfo
        {
            double                                             price;
            std::chrono::time_point<std::chrono::system_clock> lastUpdate;
        };

        std::map<std::string, PriceInfo>                   m_tokenPriceCache;
        const std::chrono::minutes                         m_cacheValidityDuration{ 1 };
        std::chrono::time_point<std::chrono::system_clock> m_lastApiCall{};
        static constexpr std::chrono::seconds              m_minApiCallInterval{ 5 };

        using IoWorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
        static constexpr unsigned                                       DEFAULT_IO_THREADS = 4;
        unsigned                                                        io_thread_count_{ DEFAULT_IO_THREADS };
        std::optional<IoWorkGuard>                                      io_work_guard_;
        std::vector<std::thread>                                        io_threads_;
        std::thread                                                     upnp_thread;
        std::atomic<bool>                                               stop_upnp{ false };
        std::string                                                     base58key_;
        std::shared_ptr<libp2p::basic::SchedulerImpl>                  scheduler_;
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork_;

        std::unique_ptr<boost::asio::thread_pool> processing_callback_pool_;

        std::atomic<NodeState> state_{ NodeState::CREATING };
        std::atomic_bool       shutdown_started_{ false };
        bool                   use_upnp_;

        struct CrdtBackupConfig
        {
            bool     enabled{ true };
            uint32_t interval_minutes{ 15 };
            uint32_t keep_count{ 12 };
            bool     auto_restore_on_repair_failure{ true };
        };

        CrdtBackupConfig crdt_backup_config_{};

        outcome::result<std::pair<std::string, uint64_t>> PayEscrow(
            const std::string                       &escrow_path,
            const SGProcessing::TaskResult          &taskresult,
            std::shared_ptr<crdt::AtomicTransaction> crdt_transaction,
            std::chrono::milliseconds                timeout = std::chrono::milliseconds( TIMEOUT_ESCROW_PAY ) );

        void ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult );
        void ProcessingError( const std::string &task_id );

        void RotateLogFiles( const std::string &base_path );
        /**
         * @brief Parse and sum all "block_len" values from the JSON.
         * @param json_data JSON string containing an "input" array.
         * @return outcome::result<uint64_t> with total bytes, or an error code.
         */
        outcome::result<uint64_t> ParseBlockSize( const std::string &json_data );

        void TransactionStateChanged( TransactionManager::State old_state, TransactionManager::State new_state );

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
