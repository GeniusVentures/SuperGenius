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
#include <processingbase/ProcessingManager.hpp>

typedef struct DevConfig
{
    char        Addr[255];
    std::string Cut;
    std::string TokenValueInGNUS;
    std::string TokenID;
    char        BaseWritePath[1024];
} DevConfig_st;

extern DevConfig_st DEV_CONFIG;

#define OUTGOING_TIMEOUT_MILLISECONDS 50000  // just communication time
#define INCOMING_TIMEOUT_MILLISECONDS 150000 // communication + verify proof

namespace sgns
{
    class GeniusNode : public IComponent
    {
    public:
        GeniusNode( const DevConfig_st &dev_config,
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
        static constexpr uint64_t TIMEOUT_ESCROW_PAY = 10000;
        static constexpr uint64_t TIMEOUT_TRANSFER   = 10000;
        static constexpr uint64_t TIMEOUT_MINT       = 10000;
#endif

        outcome::result<std::string> ProcessImage( const std::string &jsondata );

        uint64_t GetProcessCost( std::shared_ptr<ProcessingManager> procmgr );

        outcome::result<double> GetGNUSPrice();

        std::string GetName() override
        {
            return "GeniusNode";
        }

        std::string GetVersion( void );

        void DHTInit();
        /**
         * @brief       Mints tokens by converting a string amount to fixed-point representation
         * @param[in]   amount: Numeric value with amount in Minion Tokens (1e-6 GNUS Token)
         * @return      Outcome of mint token operation
         */
        outcome::result<std::pair<std::string, uint64_t>> MintTokens(
            uint64_t                  amount,
            const std::string        &transaction_hash,
            const std::string        &chainid,
            const std::string        &tokenid,
            std::chrono::milliseconds timeout = std::chrono::milliseconds( TIMEOUT_MINT ) );

        void     AddPeer( const std::string &peer );
        void     RefreshUPNP( int pubsubport );
        uint64_t GetBalance();

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

        std::string GetTokenID() const
        {
            return dev_config_.TokenID;
        }

        outcome::result<std::pair<std::string, uint64_t>> TransferFunds(
            uint64_t                  amount,
            const std::string        &destination,
            std::chrono::milliseconds timeout = std::chrono::milliseconds( TIMEOUT_TRANSFER ) );

        outcome::result<std::pair<std::string, uint64_t>> PayDev(
            uint64_t                  amount,
            std::chrono::milliseconds timeout = std::chrono::milliseconds( TIMEOUT_TRANSFER ) );

        std::shared_ptr<ipfs_pubsub::GossipPubSub> GetPubSub()
        {
            return pubsub_;
        }

        /**
         * @brief       Formats a fixed-point amount into a human-readable string.
         * @param[in]   amount Amount in Minion Tokens (1e-6 GNUS).
         * @return      Formatted string representation in GNUS.
         */
        static std::string FormatTokens( uint64_t amount );

        /**
         * @brief       Parses a human-readable string into a fixed-point amount.
         * @param[in]   str String representation of an amount in GNUS.
         * @return      Outcome result with the parsed amount in Minion Tokens (1e-6 GNUS) or error.
         */
        static outcome::result<uint64_t> ParseTokens( const std::string &str );

        static std::vector<uint8_t> GetImageByCID( const std::string &cid );

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
        bool WaitForTransactionIncoming( const std::string &txId, std::chrono::milliseconds timeout );
        // Wait for a outgoing transaction to be processed with a timeout
        bool WaitForTransactionOutgoing( const std::string &txId, std::chrono::milliseconds timeout );

        bool WaitForEscrowRelease( const std::string &originalEscrowId, std::chrono::milliseconds timeout );

        /**
         * @brief Format a child-token amount into a human-readable string.
         * @param amount Amount of child tokens (in minions).
         * @return Formatted string, e.g. "1.234 GChild".
         */
        std::string FormatChildTokens( uint64_t amount ) const;

        /**
         * @brief Parse a formatted child-token string back into minions.
         * @param amount_str Formatted string, e.g. "1.234 GChild".
         * @return outcome::result<uint64_t> of the parsed minions, or error.
         */
        outcome::result<uint64_t> ParseChildTokens( const std::string &amount_str ) const;

    protected:
        friend class TransactionSyncTest;

        void SendTransactionAndProof( std::shared_ptr<IGeniusTransactions> tx, std::vector<uint8_t> proof );
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
        std::shared_ptr<soralog::LoggingSystem>               logging_system;
        std::string                                           write_base_path_;
        bool                                                  autodht_;
        bool                                                  isprocessor_;
        base::Logger                                          node_logger;
        DevConfig_st                                          dev_config_;
        std::string                                           gnus_network_full_path_;
        std::string                                           processing_channel_topic_;
        std::string                                           processing_grid_chanel_topic_;

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

        outcome::result<std::pair<std::string, uint64_t>> PayEscrow(
            const std::string                       &escrow_path,
            const SGProcessing::TaskResult          &taskresult,
            std::shared_ptr<crdt::AtomicTransaction> crdt_transaction,
            std::chrono::milliseconds                timeout = std::chrono::milliseconds( TIMEOUT_ESCROW_PAY ) );

        void ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult );
        void ProcessingError( const std::string &task_id );

        static constexpr std::string_view db_path_                = "bc-%d/";
        static constexpr std::uint16_t    MAIN_NET                = 369;
        static constexpr std::uint16_t    TEST_NET                = 963;
        static constexpr std::size_t      MAX_NODES_COUNT         = 1;
        static constexpr std::string_view PROCESSING_GRID_CHANNEL = "SGNUS.Jobs.2a.%02d";
        static constexpr std::string_view PROCESSING_CHANNEL      = "SGNUS.TestNet.Channel.2a.%02d";
        static constexpr std::string_view GNUS_NETWORK_PATH       = "SuperGNUSNode.TestNet.2a.%02d.%s";


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
      level: debug
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
