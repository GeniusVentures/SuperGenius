/**
 * @file       GeniusNode.cpp
 * @brief
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <chrono>
#include <stdexcept>
#include <thread>
#include <memory>
#include <random>
#include <cctype>
#include <filesystem>
#include <set>
#include <string_view>

#include <boost/format.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/uuid/uuid.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/dll.hpp>
#include <boost/json.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/network/route_helper.hpp>
#include <WalletCore/HDWallet.h>
#include <WalletCore/Coin.h>

#include "account/GeniusAccount.hpp"
#include "base/sgns_version.hpp"
#include "account/TokenAmount.hpp"
#include "account/GeniusNode.hpp"
#include "account/BurnConfig.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"
#include "account/ChainRpcEndpointProvider.hpp"
#include "watcher/impl/bridge_catchup_watcher.hpp"
#include "migration/MigrationManager.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "upnp.hpp"
#include "processing/processing_tasksplit.hpp"
#include <eth/abi_decoder.hpp>
#include <base/parse_utility.hpp>
#include <eth/rpc_http_transport.hpp>
#include <eth/json_rpc.hpp>
#include <eth/event_filter.hpp>
#include <eth/eth_watch_cli.hpp>     // event_registry().params_for() — Bridge V2 ABI decode (D-13)
#include <eth/secp256k1_utility.hpp> // DecompressXOnlyPubkey() — X-only key decompression (D-13)
#include "processing/processing_subtask_enqueuer_impl.hpp"
#include "processing/impl/TaskQueueImpl.hpp"
#include "outcome/outcome.hpp"
#include <Generators.hpp>
#include <bitswap.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include "FileManager.hpp"

namespace
{
    uint16_t GenerateRandomPort( uint16_t base, const std::string &seed )
    {
        if ( base == 0 )
        {
            return 0;
        }

        uint32_t seed_hash = 0;
        for ( char c : seed )
        {
            seed_hash = seed_hash * 31 + static_cast<uint8_t>( c );
        }

        std::mt19937                            rng( seed_hash );
        std::uniform_int_distribution<uint16_t> dist( 0, 300 );

        return base + dist( rng );
    }

    const char *NodeStateToString( sgns::GeniusNode::NodeState state )
    {
        using State = sgns::GeniusNode::NodeState;
        switch ( state )
        {
            case State::CREATING:
                return "CREATING";
            case State::MIGRATING_DATABASE:
                return "MIGRATING_DATABASE";
            case State::INITIALIZING_DATABASE:
                return "INITIALIZING_DATABASE";
            case State::INITIALIZING_PROCESSING:
                return "INITIALIZING_PROCESSING";
            case State::INITIALIZING_BLOCKCHAIN:
                return "INITIALIZING_BLOCKCHAIN";
            case State::INITIALIZING_TRANSACTIONS:
                return "INITIALIZING_TRANSACTIONS";
            case State::READY:
                return "READY";
        }
        return "UNKNOWN";
    }

    // Case-insensitive parse of the "node_type" sgns_config.json value (CONTEXT D-02).
    // Returns nullopt for unrecognized values; the caller (LoadSgnsConfig) WARN-logs + defaults to Light.
    std::optional<sgns::GeniusNode::NodeType> NodeTypeFromString( std::string_view s )
    {
        std::string lower;
        lower.reserve( s.size() );
        for ( char c : s )
        {
            lower.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) );
        }
        if ( lower == "full" )
        {
            return sgns::GeniusNode::NodeType::Full;
        }
        if ( lower == "light" )
        {
            return sgns::GeniusNode::NodeType::Light;
        }
        if ( lower == "archive" )
        {
            return sgns::GeniusNode::NodeType::Archive;
        }
        return std::nullopt;
    }
}

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, GeniusNode::Error, e )
{
    switch ( e )
    {
        case sgns::GeniusNode::Error::INSUFFICIENT_FUNDS:
            return "Insufficient funds for the transaction";
        case sgns::GeniusNode::Error::DATABASE_WRITE_ERROR:
            return "Error writing data into the database";
        case sgns::GeniusNode::Error::INVALID_TRANSACTION_HASH:
            return "Input transaction hash is invalid";
        case sgns::GeniusNode::Error::INVALID_CHAIN_ID:
            return "Chain ID is invalid";
        case sgns::GeniusNode::Error::INVALID_TOKEN_ID:
            return "Token ID is invalid";
        case sgns::GeniusNode::Error::TOKEN_ID_MISMATCH:
            return "Informed Token ID doesn't match initialized ID";
        case sgns::GeniusNode::Error::PROCESS_COST_ERROR:
            return "The processing cost could not be calculated";
        case sgns::GeniusNode::Error::PROCESS_INFO_MISSING:
            return "Processing information missing on JSON file";
        case sgns::GeniusNode::Error::INVALID_JSON:
            return "Json cannot be parsed";
        case sgns::GeniusNode::Error::INVALID_BLOCK_PARAMETERS:
            return "Json missing block params";
        case sgns::GeniusNode::Error::NO_PROCESSOR:
            return "Json missing processor";
        case sgns::GeniusNode::Error::NO_PRICE:
            return "Could not get a price";
        case sgns::GeniusNode::Error::TRANSACTIONS_NOT_READY:
            return "Transaction manager is not ready";
        case sgns::GeniusNode::Error::TRANSACTION_NOT_FINALIZED:
            return "Requested transaction not finalized within timeout";
        case sgns::GeniusNode::Error::TRANSACTION_FAILED:
            return "Requested transaction failed";
        case sgns::GeniusNode::Error::INVALID_NODE_TYPE:
            return "sgns_config.json node_type was not Full/Light/Archive";
    }
    return "Unknown error";
}

using namespace boost::multiprecision;

namespace sgns
{
    base::Logger GeniusNodeLogger()
    {
        return base::createLogger( "GeniusNode" );
    }

    // Canonical factory (INTF-01). try/catch preserves the nullptr-on-failure contract (D-04):
    // the reordered ctor throws on account-restore/loggers/network failure; here it becomes nullptr.
    std::shared_ptr<GeniusNode> GeniusNode::New( const GeniusNodeConfig &dev_config, AccountSource source )
    {
        try
        {
            auto instance = std::shared_ptr<GeniusNode>( new GeniusNode( dev_config, source ) );
            if ( instance )
            {
                instance->BeginDBInitialization();
            }
            return instance;
        }
        catch ( const std::exception &e )
        {
            std::cerr << "GeniusNode initialization failed: " << e.what() << '\n';
            return nullptr;
        }
        catch ( ... )
        {
            std::cerr << "GeniusNode initialization failed with an unknown exception\n";
            return nullptr;
        }
    }

    outcome::result<void> GeniusNode::WriteNetworkConfig( const std::string &base_path,
                                                          uint16_t           port_seed,
                                                          bool               auto_dht )
    {
        std::error_code ec;
        std::filesystem::create_directories( base_path, ec ); // ofstream can't create dirs; ensure parent exists
        std::ofstream ofs( base_path + "/network_config.json" );
        if ( !ofs.good() )
        {
            return Error::DATABASE_WRITE_ERROR;
        }
        ofs << "{ \"port_seed\": " << port_seed << ", \"auto_dht\": " << ( auto_dht ? "true" : "false" )
            << ", \"upnp_enabled\": false }";
        return outcome::success();
    }

    outcome::result<void> GeniusNode::WriteSgnsConfig( const std::string &base_path,
                                                       const std::string &node_type,
                                                       bool               is_processor,
                                                       bool               rpc_catchup )
    {
        if ( !NodeTypeFromString( node_type ) ) // case-insensitive validation (Phase-2 D-02)
        {
            return Error::INVALID_NODE_TYPE;
        }
        std::error_code ec;
        std::filesystem::create_directories( base_path, ec ); // ofstream can't create dirs; ensure parent exists
        std::ofstream ofs( base_path + "/sgns_config.json" );
        if ( !ofs.good() )
        {
            return Error::DATABASE_WRITE_ERROR;
        }
        ofs << "{ \"node_type\": \"" << node_type << "\", \"is_processor\": " << ( is_processor ? "true" : "false" )
            << ", \"rpc_catchup\": " << ( rpc_catchup ? "true" : "false" ) << " }";
        return outcome::success();
    }

    // Reordered constructor (INTF-03 / CONTEXT D-05). Account is created via std::visit
    // AFTER LoadSgnsConfig() resolves node_type_ -> is_full_node_ (the init-order hinge fix).
    // account_ and is_full_node_ are default-init here (no source/param) and assigned in the
    // body; autodht_ defaults to true (Phase-1 config layer overrides from network_config.json).
    // Throws on account-restore failure; New(dev_config, AccountSource) catches -> nullptr (D-04).
    GeniusNode::GeniusNode( const GeniusNodeConfig &dev_config, AccountSource source ) :
        write_base_path_( dev_config.BaseWritePath ),
        io_( std::make_shared<boost::asio::io_context>() ),
        io_work_guard_( boost::asio::make_work_guard( *io_ ) ),
        autodht_( true ),
        isprocessor_( true ),
        dev_config_( dev_config ),
        processing_channel_topic_( std::string( PROCESSING_CHANNEL ) ),
        processing_grid_chanel_topic_( std::string( PROCESSING_GRID_CHANNEL ) ),
        m_lastApiCall( std::chrono::system_clock::now() - MIN_API_CALL_INTERVAL ),
        scheduler_( std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( io_ ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } ) ),
        generator_( std::make_shared<ipfs_lite::ipfs::graphsync::RequestIdGenerator>() )
    {
        // Rotate log files before initializing logging system
        RotateLogFiles( write_base_path_ );
        InitOpenSSL();

        if ( !InitLoggers( write_base_path_ ) )
        {
            throw std::runtime_error( "Could not configure loggers" );
        }

        node_logger_->info( sgns::version::SuperGeniusVersionText() );

        LoadSgnsConfig(); // resolves node_type_

        is_full_node_ = ( node_type_ != NodeType::Light ); // CFG-03 derivation

        // Create the account with is_full_node_ already known (the hinge fix).
        account_ = std::visit(
            [this]( auto &&src ) -> std::shared_ptr<GeniusAccount>
            {
                using T = std::decay_t<decltype( src )>;
                if constexpr ( std::is_same_v<T, NewAccount> )
                {
                    return GeniusAccount::New( dev_config_.TokenID, write_base_path_, is_full_node_ );
                }
                else if constexpr ( std::is_same_v<T, FromPrivateKey> )
                {
                    return GeniusAccount::NewFromPrivateKey( dev_config_.TokenID,
                                                             src.eth_private_key.c_str(),
                                                             write_base_path_,
                                                             is_full_node_ );
                }
                else if constexpr ( std::is_same_v<T, FromMnemonic> )
                {
                    return GeniusAccount::NewFromMnemonic( dev_config_.TokenID,
                                                           src.mnemonic,
                                                           write_base_path_,
                                                           is_full_node_ );
                }
                else if constexpr ( std::is_same_v<T, FromPublicKey> )
                {
                    // FromPublicKey carries a public_address; GeniusAccount::NewFromPublicKey
                    // takes no base_path and consumes an address-like string_view.
                    return GeniusAccount::NewFromPublicKey( dev_config_.TokenID, src.public_address, is_full_node_ );
                }
            },
            source );
        if ( !account_ )
        {
            throw std::runtime_error( "Account creation failed" ); // D-04: New() catches -> nullptr
        }

        // Default port_seed (40001); Phase-1 config layer overrides from network_config.json when present.
        if ( !InitNetwork( 40001, is_full_node_ ) )
        {
            throw std::runtime_error( "Network initialization error" );
        }
        node_logger_->debug( "Account Address {}", account_->GetAddress() );

        // Initializes the thread pool for IO context
        io_threads_.reserve( io_thread_count_ );
        for ( unsigned i = 0; i < io_thread_count_; ++i )
        {
            io_threads_.emplace_back( [ctx = io_] { ctx->run(); } );
        }

        LoadCrdtConfig();
    }

    void GeniusNode::LoadSgnsConfig()
    {
        const std::string config_path = write_base_path_ + "/sgns_config.json";
        std::ifstream     config_file( config_path );
        if ( !config_file.good() )
        {
            node_logger_->info( "sgns_config.json not found at {}, using defaults (net_id=144, is_processor=true)",
                                config_path );
            return;
        }

        std::stringstream buffer;
        buffer << config_file.rdbuf();

        rapidjson::Document config_json;
        config_json.Parse( buffer.str().c_str() );
        if ( config_json.HasParseError() || !config_json.IsObject() )
        {
            node_logger_->warn( "Invalid sgns_config.json at {}, using defaults", config_path );
            return;
        }

        if ( config_json.HasMember( "net_id" ) && config_json["net_id"].IsUint() )
        {
            auto net_id = static_cast<uint16_t>( config_json["net_id"].GetUint() );
            version::SetNetworkId( net_id );
            node_logger_->info( "sgns_config.json: net_id={}", net_id );
        }
        if ( config_json.HasMember( "is_processor" ) && config_json["is_processor"].IsBool() )
        {
            isprocessor_ = config_json["is_processor"].GetBool();
            node_logger_->info( "sgns_config.json: is_processor={}", isprocessor_ );
        }
        else
        {
            isprocessor_ = true;
            node_logger_->info( "sgns_config.json: is_processor not set, defaulting to true" );
        }
        if ( config_json.HasMember( "rpc_catchup" ) && config_json["rpc_catchup"].IsBool() )
        {
            rpc_catchup_ = config_json["rpc_catchup"].GetBool();
        }
        else
        {
            rpc_catchup_ = true;
        }
        node_logger_->info( "sgns_config.json: rpc_catchup={}", rpc_catchup_ );
        // node_type read (CFG-02 / CONTEXT D-02). Sets node_type_ ONLY — does NOT touch
        // is_full_node_ (the AccountSource ctor derives it; the retained old ctor keeps its param).
        if ( config_json.HasMember( "node_type" ) && config_json["node_type"].IsString() )
        {
            const auto parsed = NodeTypeFromString( config_json["node_type"].GetString() );
            if ( parsed )
            {
                node_type_ = *parsed;
                node_logger_->info( "sgns_config.json: node_type={}",
                                    *parsed == sgns::GeniusNode::NodeType::Full      ? "Full"
                                    : *parsed == sgns::GeniusNode::NodeType::Archive ? "Archive"
                                                                                     : "Light" );
            }
            else
            {
                node_type_ = sgns::GeniusNode::NodeType::Light; // default on unrecognized value
                node_logger_->warn( "sgns_config.json: node_type '{}' unrecognized, defaulting to Light",
                                    config_json["node_type"].GetString() );
            }
        }
        else
        {
            node_type_ = sgns::GeniusNode::NodeType::Light; // default on missing key
            node_logger_->info( "sgns_config.json: node_type not set, defaulting to Light" );
        }
        if ( config_json.HasMember( "subnet_id" ) && config_json["subnet_id"].IsUint() )
        {
            subnet_id_ = static_cast<uint16_t>( config_json["subnet_id"].GetUint() );
            node_logger_->info( "sgns_config.json: subnet_id={}", subnet_id_ );
        }
        if ( config_json.HasMember( "bootstrap_fullnodes" ) && config_json["bootstrap_fullnodes"].IsArray() )
        {
            for ( auto &v : config_json["bootstrap_fullnodes"].GetArray() )
            {
                if ( v.IsString() )
                {
                    bootstrap_fullnodes_.push_back( v.GetString() );
                }
            }
            node_logger_->info( "sgns_config.json: loaded {} bootstrap fullnodes", bootstrap_fullnodes_.size() );
        }
        // Read authorized_full_node and immediately set it
        if ( config_json.HasMember( "authorized_full_node" ) && config_json["authorized_full_node"].IsString() )
        {
            const std::string addr = config_json["authorized_full_node"].GetString();
            node_logger_->info( "sgns_config.json: setting authorized_full_node" );
            Blockchain::SetAuthorizedFullNodeAddress( addr );
        }
        // Parse-only: trusted_peers/bootstrapper_node are stored for a future phase's live
        // wiring, no live signer-set/genesis behavior is activated in this phase.
        if ( config_json.HasMember( "trusted_peers" ) && config_json["trusted_peers"].IsArray() )
        {
            for ( auto &v : config_json["trusted_peers"].GetArray() )
            {
                if ( v.IsString() )
                {
                    trusted_peers_genesis_.push_back( v.GetString() );
                }
            }
            node_logger_->info( "sgns_config.json: loaded {} trusted peers", trusted_peers_genesis_.size() );
        }
        if ( config_json.HasMember( "bootstrapper_node" ) && config_json["bootstrapper_node"].IsString() )
        {
            bootstrapper_node_address_ = config_json["bootstrapper_node"].GetString();
            node_logger_->info( "sgns_config.json: loaded bootstrapper_node" );
        }
        if ( config_json.HasMember( "trusted_peer_quorum_threshold" ) &&
             config_json["trusted_peer_quorum_threshold"].IsUint64() )
        {
            trusted_peer_quorum_threshold_ = config_json["trusted_peer_quorum_threshold"].GetUint64();
            node_logger_->info( "sgns_config.json: trusted_peer_quorum_threshold={}", trusted_peer_quorum_threshold_ );
        }
        if ( config_json.HasMember( "burn_config_quorum_threshold" ) &&
             config_json["burn_config_quorum_threshold"].IsUint64() )
        {
            burn_config_quorum_threshold_ = config_json["burn_config_quorum_threshold"].GetUint64();
            node_logger_->info( "sgns_config.json: burn_config_quorum_threshold={}", burn_config_quorum_threshold_ );
        }
        // Unset config never trips D-07's floor rejection: default to the exact majority
        // floor for the parsed genesis peer count (ceil(0.51*N)).
        const auto majority_floor =
            static_cast<uint64_t>( ( trusted_peers_genesis_.size() * 51 + 99 ) / 100 );
        if ( trusted_peer_quorum_threshold_ == 0 )
        {
            trusted_peer_quorum_threshold_ = majority_floor;
        }
        if ( burn_config_quorum_threshold_ == 0 )
        {
            burn_config_quorum_threshold_ = majority_floor;
        }
        if ( config_json.HasMember( "ipfs_cache_dir" ) && config_json["ipfs_cache_dir"].IsString() )
        {
            ipfs_cache_dir_ = config_json["ipfs_cache_dir"].GetString();
            node_logger_->info( "sgns_config.json: ipfs_cache_dir={}", ipfs_cache_dir_ );
        }
        if ( config_json.HasMember( "mirror_results" ) && config_json["mirror_results"].IsBool() )
        {
            mirror_results_ = config_json["mirror_results"].GetBool();
            node_logger_->info( "sgns_config.json: mirror_results={}", mirror_results_ );
        }
        if ( config_json.HasMember( "result_retention_hours" ) && config_json["result_retention_hours"].IsInt() )
        {
            result_retention_hours_ = config_json["result_retention_hours"].GetInt();
            node_logger_->info( "sgns_config.json: result_retention_hours={}", result_retention_hours_ );
        }
        if ( config_json.HasMember( "result_retention_max_mb" ) && config_json["result_retention_max_mb"].IsInt() )
        {
            result_retention_max_mb_ = config_json["result_retention_max_mb"].GetInt();
            node_logger_->info( "sgns_config.json: result_retention_max_mb={}", result_retention_max_mb_ );
        }
    }

    void GeniusNode::LoadCrdtConfig()
    {
        crdt_backup_config_ = crdt::GlobalDB::BackupOptions{ true, 15, 12, true };

        const std::string config_path = write_base_path_ + "crdt_config.json";
        std::ifstream     config_file( config_path );
        if ( !config_file.good() )
        {
            node_logger_->info( "crdt_config.json not found at {}, using defaults", config_path );
            return;
        }

        std::stringstream buffer;
        buffer << config_file.rdbuf();

        rapidjson::Document config_json;
        config_json.Parse( buffer.str().c_str() );
        if ( config_json.HasParseError() || !config_json.IsObject() )
        {
            node_logger_->warn( "Invalid crdt_config.json at {}, using defaults", config_path );
            return;
        }

        if ( config_json.HasMember( "backup_enabled" ) && config_json["backup_enabled"].IsBool() )
        {
            crdt_backup_config_.enabled = config_json["backup_enabled"].GetBool();
        }
        if ( config_json.HasMember( "backup_interval_minutes" ) && config_json["backup_interval_minutes"].IsUint() )
        {
            crdt_backup_config_.interval_minutes = config_json["backup_interval_minutes"].GetUint();
        }
        if ( config_json.HasMember( "backup_keep_count" ) && config_json["backup_keep_count"].IsUint() )
        {
            crdt_backup_config_.keep_count = config_json["backup_keep_count"].GetUint();
        }
        if ( config_json.HasMember( "backup_auto_restore_on_repair_failure" ) &&
             config_json["backup_auto_restore_on_repair_failure"].IsBool() )
        {
            crdt_backup_config_.auto_restore_on_repair_failure = config_json["backup_auto_restore_on_repair_failure"]
                                                                     .GetBool();
        }

        if ( crdt_backup_config_.interval_minutes == 0 )
        {
            crdt_backup_config_.interval_minutes = 15;
        }
        if ( crdt_backup_config_.keep_count == 0 )
        {
            crdt_backup_config_.keep_count = 12;
        }

        node_logger_->info(
            "CRDT backup config loaded: enabled={}, interval_minutes={}, keep_count={}, auto_restore={}",
            crdt_backup_config_.enabled,
            crdt_backup_config_.interval_minutes,
            crdt_backup_config_.keep_count,
            crdt_backup_config_.auto_restore_on_repair_failure );
    }

    void GeniusNode::LoadLogConfig()
    {
        const std::string config_path = write_base_path_ + "/log_config.json";
        std::ifstream     config_file( config_path );
        if ( !config_file.good() )
        {
            return;
        }

        std::stringstream buffer;
        buffer << config_file.rdbuf();

        rapidjson::Document config_json;
        config_json.Parse( buffer.str().c_str() );
        if ( config_json.HasParseError() || !config_json.IsObject() )
        {
            node_logger_->warn( "Invalid log_config.json at {}, using defaults", config_path );
            return;
        }

        if ( !config_json.HasMember( "loggers" ) || !config_json["loggers"].IsObject() )
        {
            node_logger_->warn( "log_config.json missing 'loggers' object at {}", config_path );
            return;
        }

        for ( auto it = config_json["loggers"].MemberBegin(); it != config_json["loggers"].MemberEnd(); ++it )
        {
            if ( !it->value.IsString() )
            {
                node_logger_->warn( "log_config.json: logger '{}' value is not a string, skipping",
                                    it->name.GetString() );
                continue;
            }

            std::string logger_name  = it->name.GetString();
            std::string level_string = it->value.GetString();

            auto logger = spdlog::get( logger_name );
            if ( !logger )
            {
                node_logger_->warn( "log_config.json: logger '{}' not found, skipping", logger_name );
                continue;
            }

            auto level = spdlog::level::from_str( level_string );
            logger->set_level( level );
            if ( level != spdlog::level::off )
            {
                logger->flush_on( level );
            }
            node_logger_->info( "log_config override: {} -> {}", logger_name, level_string );
        }
    }

    void GeniusNode::BeginDBInitialization()
    {
        StateTransition( NodeState::MIGRATING_DATABASE );
    }

    void GeniusNode::StateTransition( NodeState next_state )
    {
        state_.store( next_state );
        node_logger_->debug( "Transitioning to state {}", NodeStateToString( next_state ) );

        switch ( next_state )
        {
            case NodeState::MIGRATING_DATABASE:
            {
                if ( !bootstrap_fullnodes_.empty() )
                {
                    AddPeers( bootstrap_fullnodes_ );
                    node_logger_->info( "Added {} bootstrap fullnodes", bootstrap_fullnodes_.size() );
                }
                account_->InitMessenger( pubsub_ );
                MigrateDatabase(
                    [weak_self( weak_from_this() )]( outcome::result<void> result )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            if ( result.has_error() )
                            {
                                strong->node_logger_->error( "Database migration error: {}", result.error().message() );
                                if ( result.error() == MigrationManager::Error::BLOCKCHAIN_INIT_FAILED )
                                {
                                    strong->node_logger_->info( "Scheduling blockchain retry after failure" );
                                    strong->ScheduleMigrationRetry();
                                }
                                return;
                            }
                            strong->StateTransition( NodeState::INITIALIZING_DATABASE );
                        }
                    } );
                break;
            }
            case NodeState::INITIALIZING_DATABASE:
            {
                if ( !InitDatabase() )
                {
                    node_logger_->error( "GlobalDB initialization error" );
                    return;
                }
                account_->ConfigureDatabaseDependencies( tx_globaldb_ );
                tx_globaldb_->AddListenTopic( processing_channel_topic_ );
                StateTransition( NodeState::INITIALIZING_BLOCKCHAIN );
                break;
            }
            case NodeState::INITIALIZING_BLOCKCHAIN:
            {
                if ( !blockchain_ )
                {
                    auto weak_self = weak_from_this();
                    blockchain_    = Blockchain::New(
                        tx_globaldb_,
                        account_,
                        pubsub_,
                        [weak_self]( outcome::result<void> result )
                        {
                            if ( auto strong = weak_self.lock() )
                            {
                                if ( result.has_error() )
                                {
                                    strong->node_logger_->error( "Error starting blockchain: {}",
                                                                 result.error().message() );
                                    strong->node_logger_->info( "Scheduling blockchain retry after failure" );
                                    strong->ScheduleBlockchainRetry();
                                    return;
                                }
                                auto current_state = strong->state_.load();
                                if ( current_state != NodeState::INITIALIZING_BLOCKCHAIN )
                                {
                                    strong->node_logger_->debug(
                                        "Skipping transaction initialization, unexpected state: {}",
                                        NodeStateToString( current_state ) );
                                    return;
                                }
                                strong->node_logger_->debug(
                                    "Blockchain started successfully, starting transaction manager" );
                                if ( strong->is_full_node_ )
                                {
                                    strong->node_logger_->debug(
                                        "Full node: Setting blockchain to grab other account creation blocks" );
                                    strong->blockchain_->SetFullNodeMode();
                                }

                                // Move transaction initialization off the AccountMessenger worker thread.
                                boost::asio::post(
                                    *strong->io_,
                                    [weak_self]
                                    {
                                        if ( auto strong = weak_self.lock() )
                                        {
                                            auto current_state = strong->state_.load();
                                            if ( current_state != NodeState::INITIALIZING_BLOCKCHAIN )
                                            {
                                                strong->node_logger_->debug(
                                                    "Skipping transaction initialization, unexpected state: {}",
                                                    NodeStateToString( current_state ) );
                                                return;
                                            }
                                            strong->StateTransition( NodeState::INITIALIZING_TRANSACTIONS );
                                        }
                                    } );
                            }
                        } );
                }
                if ( blockchain_ )
                {
                    blockchain_->Start();
                    InitBootstrapReconnect();
                    StartBootstrapHealthCheck();
                }
                else
                {
                    node_logger_->warn( "Blockchain creation failed, scheduling delayed retry" );
                    ScheduleBlockchainRetry( std::chrono::seconds( 10 ) );
                }
                break;
            }

            case NodeState::INITIALIZING_TRANSACTIONS:
            {
                if ( !blockchain_ )
                {
                    node_logger_->error( "Blockchain not initialized, cannot initialize transactions" );
                    return;
                }

                // BURN-02/BURN-03: construct SecureCrdt -> TrustedPeerRegistry -> BurnConfig before
                // TransactionManager, so its cached burn-rate can be seeded from a live quorum-signed
                // value. Shares the same full-node topic TransactionManager listens on (trusted peers
                // are full nodes), ensuring proposals/signatures for these quorum-gated values propagate.
                const std::string quorum_topic = std::string( TransactionManager::GNUS_FULL_NODES_TOPIC );
                tx_globaldb_->AddListenTopic( quorum_topic );

                secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( tx_globaldb_, quorum_topic );
                secure_crdt_->RegisterFilters();

                auto tpr_result = sgns::trustedpeer::TrustedPeerRegistry::New( secure_crdt_,
                                                                               trusted_peers_genesis_,
                                                                               bootstrapper_node_address_,
                                                                               trusted_peer_quorum_threshold_ );
                if ( tpr_result.has_error() )
                {
                    node_logger_->error( "TrustedPeerRegistry construction failed (majority-floor violation): {}",
                                         tpr_result.error().message() );
                    return;
                }
                trusted_peer_registry_ = tpr_result.value();

                auto burn_config_result = sgns::account::BurnConfig::New( secure_crdt_,
                                                                          tx_globaldb_,
                                                                          trusted_peer_registry_,
                                                                          burn_config_quorum_threshold_,
                                                                          account_ );
                if ( burn_config_result.has_error() )
                {
                    node_logger_->error( "BurnConfig construction failed (majority-floor violation): {}",
                                         burn_config_result.error().message() );
                    return;
                }
                burn_config_ = burn_config_result.value();

                transaction_manager_ = TransactionManager::New( tx_globaldb_,
                                                                io_,
                                                                account_,
                                                                blockchain_,
                                                                is_full_node_,
                                                                subnet_id_,
                                                                std::chrono::milliseconds( 300000 ),
                                                                std::chrono::milliseconds( 0 ),
                                                                burn_config_->GetCachedBasisPoints(),
                                                                burn_config_ );

                transaction_manager_->RegisterStateChangeCallback(
                    [weak_self = weak_from_this()]( TransactionManager::State old_state,
                                                    TransactionManager::State new_state )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            strong->TransactionStateChanged( old_state, new_state );
                        }
                    } );
                transaction_manager_->Start();
                // TS-01: Wire configurable timestamp tolerance from GeniusNodeConfig
                // to TransactionManager's CheckTransactionTimestamp via SetTimeFrameToleranceMs.
                // Default: 300000ms (±5 minutes), overridable via GeniusNodeConfig aggregate init.
                transaction_manager_->SetTimeFrameToleranceMs( kDefaultTimestampToleranceMs );

                // Phase 6 (D-01..D-10): Wire slot-hash populator bridging
                // PublicChainInputValidator -> ConsensusManager::CreateVote, so
                // each signed vote commits to its RPC endpoint slot hashes.
                // Single-chain resolution: use the first configured chain id.

                blockchain_->SetSlotHashPopulator(
                    [weak_transaction_manager = std::weak_ptr<TransactionManager>( transaction_manager_ ),
                     logger                   = node_logger_]( sgns::ConsensusVote &vote )
                    {
                        auto transaction_manager = weak_transaction_manager.lock();
                        if ( !transaction_manager )
                        {
                            return;
                        }
                        auto      &validator = transaction_manager->GetPublicChainInputValidator();
                        const auto chain_id  = validator.GetFirstConfiguredChainId();
                        if ( !chain_id.has_value() )
                        {
                            logger->debug( "SlotHashPopulator: no configured chain; abstaining" );
                            return;
                        }
                        const auto slot0 = validator.GetSlotHash( 0, chain_id.value() );
                        const auto slot1 = validator.GetSlotHash( 1, chain_id.value() );
                        const auto slot2 = validator.GetSlotHash( 2, chain_id.value() );
                        if ( !slot0.empty() )
                        {
                            vote.set_slot_0_hash( slot0.data(), slot0.size() );
                        }
                        if ( !slot1.empty() )
                        {
                            vote.set_slot_1_hash( slot1.data(), slot1.size() );
                        }
                        if ( !slot2.empty() )
                        {
                            vote.set_slot_2_hash( slot2.data(), slot2.size() );
                        }

                        logger->debug( "SlotHashPopulator: populated chain_id={} slot0={} slot1={} slot2={}",
                                       chain_id.value(),
                                       !slot0.empty(),
                                       !slot1.empty(),
                                       !slot2.empty() );
                    } );

                // Initialize shared EthWatchService for EVM event detection
                eth_watch_service_ = std::make_shared<eth::EthWatchService>();

                // Initialize bridge relayer — wires evmrelay burn events → MintFunds
                bridge_relayer_ = BridgeRelayer::Create( std::weak_ptr<TransactionManager>( transaction_manager_ ),
                                                         eth_watch_service_ );

                // D-04: Launch async bridge initialization as NON-BLOCKING post.
                // ChainRpcEndpointProvider::Initialize() runs on the io_context
                // independently; observers (BridgeRelayer, catch-up scan) are
                // notified synchronously within Initialize. The node state
                // machine proceeds through INITIALIZING_PROCESSING → READY without waiting.
                boost::asio::post( *io_,
                                   [weak_self = weak_from_this()]
                                   {
                                       if ( auto strong = weak_self.lock() )
                                       {
                                           strong->InitializeAndStartBridge();
                                       }
                                   } );

                break;
            }
            case NodeState::INITIALIZING_PROCESSING:
            {
                ResetProcessingMembers();

                if ( !InitProcessingModules() )
                {
                    node_logger_->error( "Processing modules initialization error" );
                    return;
                }

                auto payout_address = account_->GetAddress();

                if ( auto address = account_->LoadFromSecureStorage( "payout_address" ); address.has_value() )
                {
                    payout_address = std::move( address.value() );
                    node_logger_->debug( "Using address {:.8} for payout", payout_address );
                }

                processing_service_ = std::make_shared<processing::ProcessingServiceImpl>(
                    pubsub_,
                    MAX_NODES_COUNT,
                    std::make_shared<processing::SubTaskEnqueuerImpl>( task_queue_ ),
                    task_result_storage_,
                    processing_core_,
                    [weak_self = weak_from_this()]( const std::string &var, const SGProcessing::TaskResult &taskresult )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            strong->ProcessingDone( var, taskresult );
                        }
                    },
                    [weak_self = weak_from_this()]( const std::string &var )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            strong->ProcessingError( var );
                        }
                    },
                    payout_address );

                processing_service_->SetChannelListRequestTimeout( boost::posix_time::milliseconds( 3000 ) );

                // Set up result mirroring for full/archive nodes
                if ( mirror_results_ && bitswap_ )
                {
                    auto weak_self = weak_from_this();
                    processing_service_->setMirrorResultCallback(
                        [weak_self]( const std::string &ipfs_results_data_id )
                        {
                            auto strong = weak_self.lock();
                            if ( !strong )
                            {
                                return;
                            }

                            auto bitswap = strong->bitswap_;
                            if ( !bitswap )
                            {
                                return;
                            }

                            static constexpr std::string_view kIpfsUriScheme = "ipfs://";

                            // Parse newline-separated CIDs and fetch any we don't already have
                            std::istringstream stream( ipfs_results_data_id );
                            std::string        line;
                            while ( std::getline( stream, line ) )
                            {
                                if ( line.empty() )
                                {
                                    continue;
                                }
                                // Strip "ipfs://" prefix if present
                                std::string cidStr = line;
                                if ( cidStr.compare( 0,
                                                     kIpfsUriScheme.size(),
                                                     kIpfsUriScheme.data(),
                                                     kIpfsUriScheme.size() ) == 0 )
                                {
                                    cidStr = cidStr.substr( kIpfsUriScheme.size() );
                                }
                                auto cid = libp2p::multi::ContentIdentifierCodec::fromString( cidStr );
                                if ( !cid )
                                {
                                    continue;
                                }
                                if ( bitswap->HasBlock( cid.value() ) )
                                {
                                    continue; // Already have it
                                }
                                strong->node_logger_->info( "Mirroring result data for CID: {}", cidStr );
                                bitswap->RequestContent(
                                    cid.value(),
                                    [weak_self,
                                     cidStr]( libp2p::outcome::result<sgns::ipfs_bitswap::UnixFSContent> content )
                                    {
                                        auto strong = weak_self.lock();
                                        if ( !strong )
                                        {
                                            return;
                                        }

                                        if ( content )
                                        {
                                            strong->node_logger_->info(
                                                "Successfully mirrored result data: {} ({} files)",
                                                cidStr,
                                                content.value().files.size() );
                                        }
                                        else
                                        {
                                            strong->node_logger_->warn( "Failed to mirror result data for CID {}: {}",
                                                                        cidStr,
                                                                        content.error().message() );
                                        }
                                    } );
                            }
                        } );
                }

                // Wire bitswap to processing service for data availability checks
                if ( bitswap_ )
                {
                    processing_service_->setBitswap( bitswap_ );
                }

                // Start periodic result cache GC
                StartResultGC();

                if ( isprocessor_ )
                {
                    StartProcessing();
                }
                StateTransition( NodeState::READY );
                break;
            }

            case NodeState::READY:
            {
                node_logger_->info( "GeniusNode READY" );
                break;
            }
            case NodeState::CREATING:
            default:
                break;
        }
    }

    void GeniusNode::InitOpenSSL()
    {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
    }

    bool GeniusNode::InitLoggers( const std::string &base_path )
    {
        logging_system_ = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            GetLoggingSystem( base_path ) ) );
        auto result     = logging_system_->configure();
        if ( result.has_error )
        {
            std::cerr << "Logger init error: " << result.message;
            return false;
        }

        libp2p::log::setLoggingSystem( logging_system_ );
        libp2p::log::setLevelOfGroup( "SuperGeniusDemo", soralog::Level::ERROR_ );

        std::string logdir = "";
#ifndef SGNS_DEBUGLOGS
        logdir = base_path + "/sgnslog2.log";
#endif
#ifdef SGNS_DEBUGLOGS
        // Debug mode
        node_logger_              = ConfigureLogger( "SuperGeniusNode", logdir, spdlog::level::debug );
        auto loggerGeniusNode     = ConfigureLogger( "GeniusNode", logdir, spdlog::level::debug );
        auto loggerGlobalDB       = ConfigureLogger( "GlobalDB", logdir, spdlog::level::err );
        auto loggerDAGSyncer      = ConfigureLogger( "GraphsyncDAGSyncer", logdir, spdlog::level::err );
        auto loggerGraphsync      = ConfigureLogger( "graphsync", logdir, spdlog::level::err );
        auto loggerBroadcaster    = ConfigureLogger( "PubSubBroadcasterExt", logdir, spdlog::level::err );
        auto loggerDataStore      = ConfigureLogger( "CrdtDatastore", logdir, spdlog::level::err );
        auto loggerCRDTHeads      = ConfigureLogger( "CrdtHeads", logdir, spdlog::level::err );
        auto loggerTransactions   = ConfigureLogger( "TransactionManager", logdir, spdlog::level::debug );
        auto loggerMigration      = ConfigureLogger( "MigrationManager", logdir, spdlog::level::err );
        auto loggerMigrationStep  = ConfigureLogger( "MigrationStep", logdir, spdlog::level::err );
        auto loggerQueue          = ConfigureLogger( "TaskQueueImpl", logdir, spdlog::level::err );
        auto loggerRocksDB        = ConfigureLogger( "rocksdb", logdir, spdlog::level::err );
        auto logkad               = ConfigureLogger( "Kademlia", logdir, spdlog::level::err );
        auto logNoise             = ConfigureLogger( "Noise", logdir, spdlog::level::err );
        auto logProcessingEngine  = ConfigureLogger( "ProcessingEngine", logdir, spdlog::level::err );
        auto loggerSubQueue       = ConfigureLogger( "ProcessingSubTaskQueueAccessorImpl", logdir, spdlog::level::err );
        auto loggerProcServ       = ConfigureLogger( "ProcessingService", logdir, spdlog::level::err );
        auto loggerProcqm         = ConfigureLogger( "ProcessingSubTaskQueueManager", logdir, spdlog::level::err );
        auto loggerUPNP           = ConfigureLogger( "UPNP", logdir, spdlog::level::err );
        auto loggerProcessingNode = ConfigureLogger( "ProcessingNode", logdir, spdlog::level::err );
        auto loggerGossipPubsub   = ConfigureLogger( "GossipPubSub", logdir, spdlog::level::err );
        auto loggerAccountMessenger = ConfigureLogger( "AccountMessenger", logdir, spdlog::level::err );
        auto loggerGeniusAccount    = ConfigureLogger( "GeniusAccount", logdir, spdlog::level::err );
        auto loggerKeyPair          = ConfigureLogger( "KeyPairFileStorage", logdir, spdlog::level::err );
        auto loggerBlockchain       = ConfigureLogger( "Blockchain", logdir, spdlog::level::debug );
        auto loggerValidator        = ConfigureLogger( "ValidatorRegistry", logdir, spdlog::level::debug );
        auto loggerProcMgr          = ConfigureLogger( "SGProcessingManager", logdir, spdlog::level::err );
        auto loggerProcessor        = ConfigureLogger( "SGProcessor", logdir, spdlog::level::err );
        auto loggerCrdtCallback     = ConfigureLogger( "CRDTCallbackManager", logdir, spdlog::level::err );
        auto loggerCoinPrices       = ConfigureLogger( "CoinPrices", logdir, spdlog::level::err );
        auto loggerUTXOManager      = ConfigureLogger( "UTXOManager", logdir, spdlog::level::err );
        auto loggerConsensusManager = ConfigureLogger( "ConsensusManager", logdir, spdlog::level::debug );
        auto loggerCRDTSet          = ConfigureLogger( "CRDTSet", logdir, spdlog::level::err );
        auto loggerInputValidator   = ConfigureLogger( "InputValidator", logdir, spdlog::level::err );
        auto loggerBitswap          = ConfigureLogger( "Bitswap", logdir, spdlog::level::err );

        // AsyncIOManager loggers
        auto asioFileCommon  = ConfigureLogger( "FILECommon", logdir, spdlog::level::err );
        auto asioFileManager = ConfigureLogger( "FileManager", logdir, spdlog::level::err );
        auto asioHttpCommon  = ConfigureLogger( "HTTPCommon", logdir, spdlog::level::err );
        auto asioIpfsCommon  = ConfigureLogger( "IPFSCommon", logdir, spdlog::level::err );
        auto asioIpfsLoader  = ConfigureLogger( "IPFSLoader", logdir, spdlog::level::err );
        auto asioFileLoader  = ConfigureLogger( "MNNLoader", logdir, spdlog::level::err );
        auto asioWSCommon    = ConfigureLogger( "WSCommon", logdir, spdlog::level::err );
        // libp2p loggers
        libp2p::log::setLevelOfGroup( "*", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "Gossip", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "crypto", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "identify", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "kademlia", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "libp2p", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "mplex", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "muxer", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "plaintext", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "protocols", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "secio", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "security", soralog::Level::DEBUG );
        libp2p::log::setLevelOfGroup( "yamux", soralog::Level::DEBUG );
#else
        // Release mode
        node_logger_              = ConfigureLogger( "SuperGeniusNode", logdir, spdlog::level::err );
        auto loggerGeniusNode     = ConfigureLogger( "GeniusNode", logdir, spdlog::level::err );
        auto loggerGlobalDB       = ConfigureLogger( "GlobalDB", logdir, spdlog::level::err );
        auto loggerDAGSyncer      = ConfigureLogger( "GraphsyncDAGSyncer", logdir, spdlog::level::err );
        auto loggerGraphsync      = ConfigureLogger( "graphsync", logdir, spdlog::level::err );
        auto loggerBroadcaster    = ConfigureLogger( "PubSubBroadcasterExt", logdir, spdlog::level::err );
        auto loggerDataStore      = ConfigureLogger( "CrdtDatastore", logdir, spdlog::level::err );
        auto loggerCRDTHeads      = ConfigureLogger( "CrdtHeads", logdir, spdlog::level::err );
        auto loggerTransactions   = ConfigureLogger( "TransactionManager", logdir, spdlog::level::err );
        auto loggerMigration      = ConfigureLogger( "MigrationManager", logdir, spdlog::level::err );
        auto loggerMigrationStep  = ConfigureLogger( "MigrationStep", logdir, spdlog::level::err );
        auto loggerQueue          = ConfigureLogger( "TaskQueueImpl", logdir, spdlog::level::err );
        auto loggerRocksDB        = ConfigureLogger( "rocksdb", logdir, spdlog::level::err );
        auto logkad               = ConfigureLogger( "Kademlia", logdir, spdlog::level::err );
        auto logNoise             = ConfigureLogger( "Noise", logdir, spdlog::level::err );
        auto logProcessingEngine  = ConfigureLogger( "ProcessingEngine", logdir, spdlog::level::err );
        auto loggerSubQueue       = ConfigureLogger( "ProcessingSubTaskQueueAccessorImpl", logdir, spdlog::level::err );
        auto loggerProcServ       = ConfigureLogger( "ProcessingService", logdir, spdlog::level::err );
        auto loggerProcqm         = ConfigureLogger( "ProcessingSubTaskQueueManager", logdir, spdlog::level::err );
        auto loggerUPNP           = ConfigureLogger( "UPNP", logdir, spdlog::level::err );
        auto loggerProcessingNode = ConfigureLogger( "ProcessingNode", logdir, spdlog::level::err );
        auto loggerGossipPubsub   = ConfigureLogger( "GossipPubSub", logdir, spdlog::level::err );
        auto loggerAccountMessenger = ConfigureLogger( "AccountMessenger", logdir, spdlog::level::err );
        auto loggerGeniusAccount    = ConfigureLogger( "GeniusAccount", logdir, spdlog::level::err );
        auto loggerKeyPair          = ConfigureLogger( "KeyPairFileStorage", logdir, spdlog::level::err );
        auto loggerBlockchain       = ConfigureLogger( "Blockchain", logdir, spdlog::level::err );
        auto loggerValidator        = ConfigureLogger( "ValidatorRegistry", logdir, spdlog::level::err );
        auto loggerProcMgr          = ConfigureLogger( "SGProcessingManager", logdir, spdlog::level::err );
        auto loggerProcessor        = ConfigureLogger( "SGProcessor", logdir, spdlog::level::err );
        auto loggerCrdtCallback     = ConfigureLogger( "CRDTCallbackManager", logdir, spdlog::level::err );
        auto loggerCoinPrices       = ConfigureLogger( "CoinPrices", logdir, spdlog::level::err );
        auto loggerUTXOManager      = ConfigureLogger( "UTXOManager", logdir, spdlog::level::err );
        auto loggerConsensusManager = ConfigureLogger( "ConsensusManager", logdir, spdlog::level::err );
        auto loggerCRDTSet          = ConfigureLogger( "CRDTSet", logdir, spdlog::level::err );
        auto loggerInputValidator   = ConfigureLogger( "InputValidator", logdir, spdlog::level::err );
        auto loggerBitswap          = ConfigureLogger( "Bitswap", logdir, spdlog::level::err );

        //AsyncIOManager Loggers
        auto asioFileCommon  = ConfigureLogger( "FILECommon", logdir, spdlog::level::err );
        auto asioFileManager = ConfigureLogger( "FileManager", logdir, spdlog::level::err );
        auto asioHttpCommon  = ConfigureLogger( "HTTPCommon", logdir, spdlog::level::err );
        auto asioIpfsCommon  = ConfigureLogger( "IPFSCommon", logdir, spdlog::level::err );
        auto asioIpfsLoader  = ConfigureLogger( "IPFSLoader", logdir, spdlog::level::err );
        auto asioFileLoader  = ConfigureLogger( "MNNLoader", logdir, spdlog::level::err );
        auto asioWSCommon    = ConfigureLogger( "WSCommon", logdir, spdlog::level::err );
#endif

        LoadLogConfig();

        // Logger initialization complete
        node_logger_->info( "Logger initialized successfully" );

        return true;
    }

    uint16_t GeniusNode::GetPubsubPort() const noexcept
    {
        return pubsubport_;
    }

    bool GeniusNode::IsAutodhtEnabled() const noexcept
    {
        return autodht_;
    }

    bool GeniusNode::IsFullNode() const noexcept
    {
        return is_full_node_;
    }

    GeniusNode::NodeType GeniusNode::GetNodeType() const noexcept
    {
        return node_type_;
    }

    bool GeniusNode::InitNetwork( uint16_t port_seed, bool is_full_node )
    {
        bool                ret         = true;
        std::string         config_path = write_base_path_ + "/network_config.json";
        rapidjson::Document config_json;
        std::string         pubsub_bind_address = "0.0.0.0";
        bool                upnp_enabled        = true;
        int                 high_water          = is_full_node ? 400 : 300;
        int                 low_water           = is_full_node ? 200 : 150;
        std::string         port_str;
        uint16_t            config_port = 0;

        bootstrap_peers_.clear();

        // Try to read config file
        std::ifstream config_file( config_path );
        if ( config_file.good() )
        {
            std::stringstream buffer;
            buffer << config_file.rdbuf();
            config_json.Parse( buffer.str().c_str() );
            if ( !config_json.HasParseError() && config_json.IsObject() )
            {
                if ( config_json.HasMember( "pubsub_port" ) && config_json["pubsub_port"].IsString() )
                {
                    port_str = config_json["pubsub_port"].GetString();
                    if ( !port_str.empty() )
                    {
                        try
                        {
                            config_port = static_cast<uint16_t>( std::stoi( port_str ) );
                        }
                        catch ( ... )
                        {
                            node_logger_->warn( "Invalid pubsub_port in config, using default" );
                        }
                    }
                }
                if ( config_json.HasMember( "pubsub_bind_address" ) && config_json["pubsub_bind_address"].IsString() )
                {
                    pubsub_bind_address = config_json["pubsub_bind_address"].GetString();
                }
                if ( config_json.HasMember( "bootstrap_addresses" ) && config_json["bootstrap_addresses"].IsArray() )
                {
                    for ( auto &v : config_json["bootstrap_addresses"].GetArray() )
                    {
                        if ( v.IsString() )
                        {
                            bootstrap_peers_.push_back( v.GetString() );
                        }
                    }
                }

                if ( config_json.HasMember( "upnp_enabled" ) && config_json["upnp_enabled"].IsBool() )
                {
                    upnp_enabled = config_json["upnp_enabled"].GetBool();
                }
                if ( config_json.HasMember( "high_water" ) && config_json["high_water"].IsInt() )
                {
                    high_water = config_json["high_water"].GetInt();
                }
                if ( config_json.HasMember( "low_water" ) && config_json["low_water"].IsInt() )
                {
                    low_water = config_json["low_water"].GetInt();
                }

                // ── port_seed: numeric read (intentional divergence from the legacy
                //    string-based pubsub_port read above — see HARD-01 / CONTEXT D-08).
                //    Config wins when present; the constructor param is the fallback.
                if ( config_json.HasMember( "port_seed" ) )
                {
                    if ( config_json["port_seed"].IsUint() )
                    {
                        port_seed = static_cast<uint16_t>( config_json["port_seed"].GetUint() );
                        node_logger_->info( "network_config.json: port_seed overridden to {}", port_seed );
                    }
                    else
                    {
                        node_logger_->warn( "network_config.json: port_seed is not a uint, using default/param {}",
                                            port_seed );
                    }
                }

                // ── auto_dht: bool read. JSON key "auto_dht" -> member autodht_ (D-07).
                //    Config wins when present; the constructor param (assigned in the ctor
                //    init-list) is the fallback.
                if ( config_json.HasMember( "auto_dht" ) )
                {
                    if ( config_json["auto_dht"].IsBool() )
                    {
                        autodht_ = config_json["auto_dht"].GetBool();
                        node_logger_->info( "network_config.json: auto_dht overridden to {}", autodht_ );
                    }
                    else
                    {
                        node_logger_->warn( "network_config.json: auto_dht is not a bool, using default/param {}",
                                            autodht_ );
                    }
                }

                // ── Parse reconnect config ──
                if ( config_json.HasMember( "bootstrap_reconnect_base_delay_sec" ) &&
                     config_json["bootstrap_reconnect_base_delay_sec"].IsInt() )
                {
                    reconnect_config_.base_delay = std::chrono::seconds(
                        config_json["bootstrap_reconnect_base_delay_sec"].GetInt() );
                }
                if ( config_json.HasMember( "bootstrap_reconnect_max_delay_sec" ) &&
                     config_json["bootstrap_reconnect_max_delay_sec"].IsInt() )
                {
                    reconnect_config_.max_delay = std::chrono::seconds(
                        config_json["bootstrap_reconnect_max_delay_sec"].GetInt() );
                }
                if ( config_json.HasMember( "bootstrap_health_check_interval_sec" ) &&
                     config_json["bootstrap_health_check_interval_sec"].IsInt() )
                {
                    reconnect_config_.health_check_interval = std::chrono::seconds(
                        config_json["bootstrap_health_check_interval_sec"].GetInt() );
                }
                if ( config_json.HasMember( "bootstrap_health_check_disconnected_interval_sec" ) &&
                     config_json["bootstrap_health_check_disconnected_interval_sec"].IsInt() )
                {
                    reconnect_config_.health_check_disconnected_interval = std::chrono::seconds(
                        config_json["bootstrap_health_check_disconnected_interval_sec"].GetInt() );
                }
                if ( config_json.HasMember( "bootstrap_background_multiplier" ) &&
                     config_json["bootstrap_background_multiplier"].IsDouble() )
                {
                    reconnect_config_.background_multiplier = config_json["bootstrap_background_multiplier"]
                                                                  .GetDouble();
                }
            }
        }

        // ── Parse bootstrap fullnode multiaddrs into PeerInfo cache for reconnection ──
        bootstrap_fullnode_infos_.clear();
        bootstrap_fullnode_ids_.clear();
        for ( const auto &addr : bootstrap_fullnodes_ )
        {
            auto peer_info = ParsePeerInfoFromString( addr );
            if ( peer_info )
            {
                bootstrap_fullnode_infos_.push_back( peer_info.value() );
                bootstrap_fullnode_ids_.insert( peer_info->id );
            }
            else
            {
                node_logger_->warn( "Failed to parse bootstrap fullnode multiaddr: {}", addr );
            }
        }
        if ( !bootstrap_fullnode_infos_.empty() )
        {
            node_logger_->info( "Parsed {} bootstrap fullnode(s) for reconnection tracking",
                                bootstrap_fullnode_infos_.size() );
        }

        // ── Parse bootstrap peer multiaddrs into PeerInfo cache for reconnection ──
        bootstrap_peer_infos_.clear();
        bootstrap_peer_ids_.clear();
        for ( const auto &addr : bootstrap_peers_ )
        {
            auto peer_info = ParsePeerInfoFromString( addr );
            if ( peer_info )
            {
                bootstrap_peer_infos_.push_back( peer_info.value() );
                bootstrap_peer_ids_.insert( peer_info->id );
            }
            else
            {
                node_logger_->warn( "Failed to parse bootstrap peer multiaddr: {}", addr );
            }
        }
        if ( !bootstrap_peer_infos_.empty() )
        {
            node_logger_->info( "Parsed {} bootstrap peer(s) for reconnection tracking", bootstrap_peer_infos_.size() );
        }

        // Port resolution priority (Doxygen: see InitNetwork declaration):
        //   1. pubsub_port (string override from network_config.json) -> config_port
        //   2. else: port_seed (constructor param, or network_config.json "port_seed"
        //      key when present) derives the port via GenerateRandomPort(port_seed, address);
        //      zero uses an OS-selected port because GossipPubSub cannot reliably start on zero.
        if ( config_port != 0 )
        {
            pubsubport_ = config_port;
        }
        else
        {
            pubsubport_ = GenerateRandomPort( port_seed, account_->GetAddress() );
        }

        do
        {
            // Never block node construction on UPnP/IGD discovery.
            // RefreshUPNP() runs on its own thread and will try immediately.
            if ( upnp_enabled )
            {
                //ret = InitUPNP();
                (void) InitUPNP(); // Ignore UPNP init result for now
            }

            // Make a base58 out of our address
            std::string                tempaddress = account_->GetAddress();
            std::vector<unsigned char> inputBytes( tempaddress.begin(), tempaddress.end() );
            std::vector<unsigned char> hash( SHA256_DIGEST_LENGTH );
            SHA256( inputBytes.data(), inputBytes.size(), hash.data() );

            auto key          = libp2p::multi::ContentIdentifierCodec::encodeCIDV0( hash.data(), hash.size() );
            auto acc_cid      = libp2p::multi::ContentIdentifierCodec::decode( key );
            auto maybe_base58 = libp2p::multi::ContentIdentifierCodec::toString( acc_cid.value() );
            if ( !maybe_base58 )
            {
                ret = false;
                node_logger_->error( "We couldn't convert the account {} to base58", account_->GetAddress() );
                break;
            }
            base58key_ = maybe_base58.value();

            gnus_network_full_path_ = std::string( GNUS_NETWORK_PATH ) + version::GetNetAndVersionAppendix() +
                                      base58key_;
            auto pubsubKeyPath = gnus_network_full_path_ + "/pubs_processor";

            //Set a pubsub config, use no signing because we can verify with proof and dag structure
            libp2p::protocol::gossip::Config config;
            config.echo_forward_mode       = false;
            config.sign_messages           = false;
            config.seen_cache_limit        = 10;
            config.heartbeat_interval_msec = std::chrono::milliseconds{ 500 };
            config.rw_timeout_msec         = std::chrono::seconds{ 30 };

            pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>(
                crdt::KeyPairFileStorage( write_base_path_ + pubsubKeyPath ).GetKeyPair().value(),
                config );

            auto pubs = pubsub_->Start( pubsubport_, bootstrap_peers_, pubsub_bind_address, {} );
            if ( auto pubsub_start_error = pubs.get(); pubsub_start_error )
            {
                node_logger_->error( "PubSub failed to start on {}:{}: {}",
                                     pubsub_bind_address,
                                     pubsubport_,
                                     pubsub_start_error.message() );
                pubsub_->Stop();
                pubsub_.reset();
                ret = false;
                break;
            }

            auto pubsub_interface_address = pubsub_->GetInterfaceAddress();
            if ( pubsub_interface_address.empty() )
            {
                node_logger_->error( "PubSub started without an interface address on {}:{}",
                                     pubsub_bind_address,
                                     pubsubport_ );
                pubsub_->Stop();
                pubsub_.reset();
                ret = false;
                break;
            }
            if ( pubsubport_ == 0 )
            {
                auto address = libp2p::multi::Multiaddress::create( pubsub_interface_address );
                if ( address )
                {
                    auto assigned_port = address.value().getFirstValueForProtocol<uint16_t>(
                        libp2p::multi::Protocol::Code::TCP,
                        []( const std::string &value ) { return static_cast<uint16_t>( std::stoul( value ) ); } );
                    if ( assigned_port )
                    {
                        pubsubport_ = assigned_port.value();
                    }
                }
                if ( pubsubport_ == 0 )
                {
                    node_logger_->error( "PubSub did not report its OS-assigned TCP port: {}",
                                         pubsub_interface_address );
                    pubsub_->Stop();
                    pubsub_.reset();
                    ret = false;
                    break;
                }
            }
            node_logger_->info( "PubSub started at address: {}", pubsub_interface_address );

            if ( upnp_enabled )
            {
                RefreshUPNP( pubsubport_ );
            }

            pubsub_->GetHost()->getConnectionManagerConfig().high_water = high_water;
            pubsub_->GetHost()->getConnectionManagerConfig().low_water  = low_water;

            // Initialize Bitswap for IPFS content-addressed data exchange
            bitswap_event_bus_ = std::make_shared<libp2p::event::Bus>();
            bitswap_ = std::make_shared<sgns::ipfs_bitswap::Bitswap>( *pubsub_->GetHost(), *bitswap_event_bus_, io_ );
            bitswap_->initialize();
            if ( !ipfs_cache_dir_.empty() )
            {
                auto fullCachePath = write_base_path_ + "/" + ipfs_cache_dir_;
                bitswap_->setCacheDir( fullCachePath );
            }
            FileManager::GetInstance().InitializeSingletons();
            FileManager::GetInstance().setBitswap( bitswap_ );

            graphsyncnetwork_ = std::make_shared<ipfs_lite::ipfs::graphsync::Network>( pubsub_->GetHost(), scheduler_ );

            // Initialize DHT early so peer discovery works during database migration
            if ( autodht_ )
            {
                DHTInit();
            }
        } while ( 0 );
        return ret;
    }

    bool GeniusNode::InitUPNP()
    {
        auto upnp = std::make_shared<upnp::UPNP>();
        if ( !upnp->GetIGD() )
        {
            return true;
        }

        bool ret = false;
        do
        {
            std::string wanip = upnp->GetWanIP();
            std::string lanip = upnp->GetLocalIP();
            node_logger_->info( "Wan IP: {}", wanip );
            node_logger_->info( "Lan IP: {}", lanip );

            std::string owner;

            constexpr uint16_t MAX_ATTEMPTS = 10;
            for ( uint16_t i = 0; i < MAX_ATTEMPTS; ++i )
            {
                uint16_t candidate_port = pubsubport_ + i;
                if ( upnp->CheckIfPortInUse( candidate_port, "TCP", owner ) )
                {
                    if ( owner == lanip )
                    {
                        node_logger_->info( "Port {} is already mapped by this device. Try using it.", candidate_port );
                        if ( upnp->OpenPort( candidate_port, candidate_port, "TCP", 3600 ) )
                        {
                            ret         = true;
                            pubsubport_ = candidate_port;
                            break;
                        }

                        node_logger_->error(
                            "Port {} is already mapped by this device. We tried using it, but could not. Will try other ports.",
                            candidate_port );
                        continue;
                    }
                    node_logger_->error( "Port {} already in use by {}", candidate_port, owner );
                    continue;
                }

                if ( upnp->OpenPort( candidate_port, candidate_port, "TCP", 3600 ) )
                {
                    node_logger_->info( "Successfully opened port {}", candidate_port );
                    ret         = true;
                    pubsubport_ = candidate_port;
                    break;
                }
                node_logger_->warn( "Failed to open port {}", candidate_port );
            }
            if ( !ret )
            {
                node_logger_->error( "Unable to open a usable UPnP port after {} attempts", MAX_ATTEMPTS );
                break;
            }

        } while ( 0 );

        return ret;
    }

    bool GeniusNode::InitDatabase()
    {
        bool ret = false;
        do
        {
            auto global_db_ret = crdt::GlobalDB::New( io_,
                                                      write_base_path_ + gnus_network_full_path_,
                                                      pubsub_,
                                                      crdt::CrdtOptions::DefaultOptions(),
                                                      graphsyncnetwork_,
                                                      scheduler_,
                                                      generator_,
                                                      nullptr,
                                                      crdt_backup_config_ );
            if ( global_db_ret.has_error() )
            {
                node_logger_->error( "Error creating GlobalDB: {}", global_db_ret.error().message() );
                break;
            }
            tx_globaldb_ = std::move( global_db_ret.value() );

            tx_globaldb_->Start();

            ret = true;
        } while ( 0 );
        return ret;
    }

    bool GeniusNode::InitProcessingModules()
    {
        bool ret = true;

        task_queue_      = processing::TaskQueueImpl::New( tx_globaldb_, processing_channel_topic_ );
        processing_core_ = processing::ProcessingCoreImpl::New( task_queue_, 1, dev_config_.TokenID );

        task_result_storage_ = std::make_shared<processing::SubTaskResultStorageImpl>( tx_globaldb_,
                                                                                       processing_channel_topic_ );

        // Restore previously-submitted task IDs from local file
        LoadMyTaskIds();

        return ret;
    }

    void GeniusNode::MigrateDatabase( std::function<void( outcome::result<void> )> callback )
    {
        auto mgr = sgns::MigrationManager::New( io_,               // ioContext
                                                pubsub_,           // pubSub
                                                graphsyncnetwork_, // graphsync
                                                scheduler_,        // scheduler
                                                generator_,        // generator
                                                write_base_path_,  // writeBasePath
                                                base58key_,        // base58key
                                                account_,
                                                is_full_node_ );

        // We store it to query migration progress later.
        {
            std::lock_guard<std::mutex> lock( migration_mutex_ );
            migration_manager_ = mgr;
        }

        std::thread migration_thread(
            [manager = std::move( mgr ), cb = std::move( callback )]
            {
                auto migrationResult = manager->Migrate();
                if ( cb )
                {
                    cb( migrationResult );
                }
            } );
        migration_thread.detach();
    }

    void GeniusNode::ScheduleMigrationRetry()
    {
        std::thread(
            [weak_self = weak_from_this()]
            {
                std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
                if ( auto strong = weak_self.lock() )
                {
                    strong->StateTransition( NodeState::MIGRATING_DATABASE );
                }
            } )
            .detach();
    }

    void GeniusNode::ScheduleBlockchainRetry( std::chrono::seconds delay )
    {
        std::thread(
            [weak_self = weak_from_this(), delay]
            {
                std::this_thread::sleep_for( delay );
                if ( auto strong = weak_self.lock() )
                {
                    auto current_state = strong->state_.load();
                    if ( current_state != NodeState::INITIALIZING_BLOCKCHAIN )
                    {
                        strong->node_logger_->debug( "Skipping blockchain retry, unexpected state: {}",
                                                     NodeStateToString( current_state ) );
                        return;
                    }
                    strong->StateTransition( NodeState::INITIALIZING_BLOCKCHAIN );
                }
            } )
            .detach();
    }

    base::Logger GeniusNode::ConfigureLogger( const std::string        &tag,
                                              const std::string        &logdir,
                                              spdlog::level::level_enum level )
    {
        auto logger = base::createLogger( tag, logdir );
        logger->set_level( level );
        if ( level != spdlog::level::off )
        {
            logger->flush_on( level );
        }
        return logger;
    }

    outcome::result<void> GeniusNode::ShutdownAccountBoundServices( bool deconfigure_account,
                                                                    bool release_members )
    {
        if ( processing_service_ )
        {
            processing_service_->StopProcessing();
        }

        // Invalidate any in-flight async bridge init and drop its observer
        // registrations BEFORE bridge_relayer_ is destroyed. The posted init
        // job captures this generation token and aborts if stale; resetting the
        // provider here also releases its raw bridge_relayer_ observer so a
        // late Initialize() cannot notify a freed relayer.
        ++bridge_init_generation_;
        rpc_endpoint_provider_.reset();

        // Stop consensus and drain registry persistence while TransactionManager
        // and GlobalDB are still alive. Consensus owns callbacks into both.
        if ( blockchain_ )
        {
            BOOST_OUTCOME_TRY( blockchain_->Stop() );
        }

        if ( transaction_manager_ )
        {
            transaction_manager_->Stop();
        }

        if ( release_members )
        {
            ResetProcessingMembers();
            transaction_manager_.reset();
            bridge_relayer_.reset();
            eth_watch_service_.reset();
            blockchain_.reset();
        }

        if ( deconfigure_account && account_ )
        {
            account_->DeconfigureDatabaseDependencies();
        }

        return outcome::success();
    }

    void GeniusNode::ReleaseRuntimeMembersAfterIoStopped()
    {
        // The timer's completion handler captures a scheduling closure associated
        // with this node. Destroy it while the io_context is still alive.
        if ( gc_timer_ )
        {
            boost::system::error_code ignored;
            gc_timer_->cancel( ignored );
            gc_timer_.reset();
        }

        // Account-bound services depend on GlobalDB, which in turn depends on
        // GraphSync, the scheduler, PubSub, and the io_context.
        ResetProcessingMembers();
        transaction_manager_.reset();
        bridge_relayer_.reset();
        eth_watch_service_.reset();
        blockchain_.reset();

        {
            std::lock_guard<std::mutex> lock( migration_mutex_ );
            migration_manager_.reset();
        }

        if ( job_globaldb_ )
        {
            job_globaldb_->ShutdownNow();
        }
        job_globaldb_.reset();
        tx_globaldb_.reset();

        // Bitswap borrows the PubSub host and event bus; GraphSync borrows the
        // PubSub host and scheduler. Release dependents before their providers.
        FileManager::GetInstance().clearBitswap( bitswap_ );
        bitswap_.reset();
        bitswap_event_bus_.reset();
        graphsyncnetwork_.reset();
        generator_.reset();
        scheduler_.reset();

        // GeniusAccount owns AccountMessenger, which owns PubSub subscriptions.
        // account_ is declared before io_, so relying on implicit destruction
        // would otherwise destroy its messenger after the io_context.
        account_.reset();
        pubsub_.reset();
    }

    void GeniusNode::ShutdownForDestruction()
    {
        bool expected = false;
        if ( !shutdown_started_.compare_exchange_strong( expected, true ) )
        {
            return;
        }

        node_logger_->info( "GeniusNode shutdown start" );

        // Stop the catch-up watcher before tearing down account-bound services
        if ( catchup_watcher_ )
        {
            catchup_watcher_->stopWatching();
            catchup_watcher_.reset();
        }

        // Cancel bootstrap health check timer
        if ( health_check_handle_ )
        {
            health_check_handle_->cancel();
            health_check_handle_.reset();
        }

        if ( gc_timer_ )
        {
            boost::system::error_code ignored;
            gc_timer_->cancel( ignored );
        }

        // Unsubscribe from bootstrap disconnect events
        if ( bootstrap_disconnect_subscription_ )
        {
            bootstrap_disconnect_subscription_->unsubscribe();
            bootstrap_disconnect_subscription_.reset();
        }

        // Stop and unregister account-bound work, but retain the owning objects
        // until the io_context has been stopped and all handlers have drained.
        auto services_shutdown = ShutdownAccountBoundServices( true, false );
        if ( services_shutdown.has_error() )
        {
            node_logger_->error( "GeniusNode shutdown account-bound services failed: {}",
                                 services_shutdown.error().message() );
        }

        if ( tx_globaldb_ )
        {
            tx_globaldb_->ShutdownNow();
        }

        node_logger_->info( "GeniusNode shutdown phase CRDT/GlobalDB complete" );
    }

    GeniusNode::~GeniusNode()
    {
        node_logger_->debug( "~GeniusNode CALLED" );

        ShutdownForDestruction();

        // Signal PubSub to stop, but do not destroy it yet: the io_context threads
        // below may still be running in-flight asio/libp2p completion handlers that
        // reference pubsub_/bitswap_. Resetting those objects before the io_context
        // is stopped and joined is a use-after-free race.
        if ( pubsub_ )
        {
            pubsub_->Stop();
        }
        io_work_guard_.reset();
        if ( io_ )
        {
            io_->stop(); // Stop our io_context
        }
        const auto caller_thread_id = std::this_thread::get_id();
        for ( auto &t : io_threads_ )
        {
            if ( t.joinable() )
            {
                if ( t.get_id() == caller_thread_id )
                {
                    node_logger_->error(
                        "~GeniusNode called from io_context thread; detaching current thread to avoid self-join" );
                    t.detach();
                    continue;
                }
                t.join();
            }
        }
        io_threads_.clear();
        stop_upnp = true;
        if ( upnp_thread.joinable() )
        {
            if ( upnp_thread.get_id() == caller_thread_id )
            {
                node_logger_->error(
                    "~GeniusNode called from UPNP thread; detaching current thread to avoid self-join" );
                upnp_thread.detach();
            }
            else
            {
                upnp_thread.join();
            }
        }

        // Destroy the complete runtime graph in dependency order while io_ is
        // still alive. This also tears down AccountMessenger subscriptions before
        // the io_context is implicitly destroyed with the remaining members.
        ReleaseRuntimeMembersAfterIoStopped();

        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        node_logger_->debug( "~GeniusNode FINISHED" );
    }

    void GeniusNode::RefreshUPNP( uint16_t pubsubport )
    {
        if ( upnp_thread.joinable() )
        {
            stop_upnp = true;   // Signal the existing thread to stop
            upnp_thread.join(); // Wait for it to finish
        }

        stop_upnp = false; // Reset the stop flag for the new thread

        upnp_thread = std::thread(
            [this, pubsubport]()
            {
                auto next_refresh_time = std::chrono::steady_clock::now() + std::chrono::minutes( 60 );
                auto upnp_shared       = std::make_shared<upnp::UPNP>();

                while ( !stop_upnp )
                {
                    if ( std::chrono::steady_clock::now() >= next_refresh_time )
                    {
                        std::weak_ptr<upnp::UPNP> upnp_weak = upnp_shared;

                        if ( auto upnp = upnp_weak.lock() )
                        {
                            if ( upnp->GetIGD() )
                            {
                                auto openedPort = upnp->OpenPort( pubsubport, pubsubport, "TCP", 3600 );
                                if ( !openedPort )
                                {
                                    GeniusNodeLogger()->error( "Failed to open port" );
                                }
                                else
                                {
                                    GeniusNodeLogger()->info( "Open Ports Success pubsub: {} ", pubsubport );
                                }
                            }
                            else
                            {
                                GeniusNodeLogger()->info( "No IGD" );
                            }
                        }
                        else
                        {
                            GeniusNodeLogger()->info( "UPNP weak_ptr expired" );
                            stop_upnp = true; // Signal thread to stop gracefully
                        }

                        next_refresh_time = std::chrono::steady_clock::now() + std::chrono::minutes( 60 );
                    }

                    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
                }
            } );
    }

    void GeniusNode::AddPeer( const std::string &peer )
    {
        auto peer_info = ParsePeerInfoFromString( peer );
        if ( !peer_info )
        {
            node_logger_->warn( "Cannot add invalid peer multiaddress: {}", peer );
            return;
        }

        ConnectPeer( peer, std::move( peer_info.value() ), 0 );
    }

    void GeniusNode::ConnectPeer( std::string peer, libp2p::peer::PeerInfo peer_info, unsigned attempt )
    {
        if ( shutdown_started_.load() )
        {
            return;
        }

        auto weak_self = weak_from_this();
        pubsub_->GetAsioContext()->post(
            [weak_self, peer = std::move( peer ), peer_info = std::move( peer_info ), attempt]()
            {
                auto strong = weak_self.lock();
                if ( !strong || strong->shutdown_started_.load() )
                {
                    return;
                }

                strong->pubsub_->GetHost()->connect(
                    peer_info,
                    [weak_self, peer, peer_info, attempt]( auto result ) mutable
                    {
                        auto strong = weak_self.lock();
                        if ( !strong || strong->shutdown_started_.load() )
                        {
                            return;
                        }

                        if ( result )
                        {
                            strong->node_logger_->debug( "Connected to peer {}", peer_info.id.toBase58() );
                            // Register only after the transport is connected. Otherwise
                            // GossipSub joins the failed dial and bans the peer for a minute.
                            strong->pubsub_->AddPeers( { peer } );
                            return;
                        }

                        const auto retry_attempt = std::min( attempt, 10u );
                        auto       delay_sec =
                            strong->reconnect_config_.base_delay.count() * ( 1ull << retry_attempt );
                        delay_sec = std::min<uint64_t>(
                            delay_sec,
                            static_cast<uint64_t>( strong->reconnect_config_.max_delay.count() ) );
                        const auto delay = std::chrono::seconds( delay_sec );

                        strong->node_logger_->warn( "Failed to connect to peer {}: {}; retrying in {}s",
                                                    peer_info.id.toBase58(),
                                                    result.error().message(),
                                                    delay.count() );
                        strong->scheduler_->schedule(
                            [weak_self,
                             peer = std::move( peer ),
                             peer_info = std::move( peer_info ),
                             attempt = retry_attempt + 1]() mutable
                            {
                                if ( auto strong = weak_self.lock() )
                                {
                                    strong->ConnectPeer( std::move( peer ), std::move( peer_info ), attempt );
                                }
                            },
                            delay );
                    },
                    std::chrono::seconds( 15 ) );
            } );
    }

    void GeniusNode::AddPeers( const std::vector<std::string> &peers )
    {
        for ( const auto &peer : peers )
        {
            AddPeer( peer );
        }
    }

    void GeniusNode::DHTInit()
    {
        // Encode the string to UTF-8 bytes
        std::string                temp = processing_grid_chanel_topic_ + sgns::version::GetNetAndVersionAppendix();
        std::vector<unsigned char> inputBytes( temp.begin(), temp.end() );

        // Compute the SHA-256 hash of the input bytes
        std::vector<unsigned char> hash( SHA256_DIGEST_LENGTH );
        SHA256( inputBytes.data(), inputBytes.size(), hash.data() );

        // Provide CID
        auto key = libp2p::multi::ContentIdentifierCodec::encodeCIDV0( hash.data(), hash.size() );
        pubsub_->GetDHT()->Start();
        pubsub_->ProvideCID( key );

        auto cidtest = libp2p::multi::ContentIdentifierCodec::decode( key );

        auto cidstring = libp2p::multi::ContentIdentifierCodec::toString( cidtest.value() );
        node_logger_->info( "CID Test:: {}", cidstring.value() );

        // Also Find providers
        pubsub_->StartFindingPeers( key );
    }

    std::string generate_uuid_with_ipfs_id( const std::string &ipfs_id )
    {
        // Hash the IPFS ID
        std::hash<std::string> hasher;
        uint64_t               id_hash = hasher( ipfs_id );

        // Get a high-resolution timestamp
        auto now       = std::chrono::high_resolution_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>( now.time_since_epoch() ).count();

        // Combine the IPFS ID hash and timestamp to create a seed
        uint64_t seed = id_hash ^ static_cast<uint64_t>( timestamp );

        // Seed the PRNG
        std::mt19937                                       gen( seed );
        boost::uuids::basic_random_generator<std::mt19937> uuid_gen( gen );

        // Generate UUID
        boost::uuids::uuid uuid = uuid_gen();
        return boost::uuids::to_string( uuid );
    }

    std::vector<std::string> GeniusNode::GetAvailableAccounts()
    {
        return GeniusAccount::GetAvailableAccounts( write_base_path_ );
    }

    outcome::result<void> GeniusNode::AddAccountWithKey( const char *private_key ) const
    {
        auto new_account = GeniusAccount::NewFromPrivateKey( this->GetTokenID(),
                                                             private_key,
                                                             write_base_path_,
                                                             is_full_node_ );
        if ( new_account == nullptr )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return outcome::success();
    }

    outcome::result<void> GeniusNode::AddAccountWithMnemonic( const std::string &mnemonic ) const
    {
        auto new_account = GeniusAccount::NewFromMnemonic( this->GetTokenID(),
                                                           mnemonic,
                                                           write_base_path_,
                                                           is_full_node_ );
        if ( new_account == nullptr )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return outcome::success();
    }

    outcome::result<std::string> GeniusNode::AddAccountWithRandomMnemonic() const
    {
        auto new_account = GeniusAccount::NewFromRandomMnemonic( this->GetTokenID(), write_base_path_, is_full_node_ );
        if ( new_account.first == nullptr )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return new_account.second;
    }

    outcome::result<void> GeniusNode::SelectAccount( std::string_view public_address )
    {
        public_address = GeniusAccount::NormalizeAddress( public_address );

        if ( public_address == GetAddress() )
        {
            node_logger_->warn( "Address already active" );
            return std::errc::address_in_use;
        }
        auto addresses = GeniusAccount::GetAvailableAccounts( write_base_path_ );

        if ( std::find( addresses.cbegin(), addresses.cend(), public_address ) == addresses.cend() )
        {
            node_logger_->error( "Could not find requested address" );
            return std::errc::address_not_available;
        }

        auto account = GeniusAccount::NewFromPublicKey( GetTokenID(), public_address, is_full_node_ );

        if ( account == nullptr )
        {
            node_logger_->error( "Account not created" );
            return std::errc::address_not_available;
        }

        BOOST_OUTCOME_TRY( ShutdownAccountBoundServices( true ) );

        if ( account_ )
        {
            account_.swap( account );
        }
        else
        {
            account_ = account;
        }
        account.reset();

        if ( this->tx_globaldb_ )
        {
            // Database is already initialized (keyed by node ID, not account).
            // Keep it alive, configure it for the new account, and restart the
            // account-dependent layers. We must replicate what MIGRATING_DATABASE
            // and INITIALIZING_DATABASE do for a new account, without recreating
            // the database itself.
            this->account_->InitMessenger( this->pubsub_ );
            this->account_->ConfigureDatabaseDependencies( this->tx_globaldb_ );
            this->tx_globaldb_->AddListenTopic( processing_channel_topic_ );
            StateTransition( NodeState::INITIALIZING_BLOCKCHAIN );
        }

        return outcome::success();
    }

    void GeniusNode::ResetProcessingMembers()
    {
        if ( processing_service_ )
        {
            processing_service_->StopProcessing();
        }
        processing_service_.reset();
        task_result_storage_.reset();
        processing_core_.reset();
        task_queue_.reset();
    }

    outcome::result<void> GeniusNode::TransferAccount( std::string_view public_address )
    {
        const std::string destination_address( GeniusAccount::NormalizeAddress( public_address ) );

        if ( destination_address == GetAddress() )
        {
            node_logger_->warn( "Address already active" );
            return std::errc::address_in_use;
        }

        auto addresses = GeniusAccount::GetAvailableAccounts( write_base_path_ );

        if ( std::find( addresses.cbegin(), addresses.cend(), destination_address ) == addresses.cend() )
        {
            node_logger_->error( "Tried to transfer to account that was not added to GeniusNode" );
            return std::errc::address_not_available;
        }

        const auto token_id = GetTokenID();
        auto       balance  = account_->GetUTXOManager().GetBalance( token_id );
        if ( balance > 0 )
        {
            BOOST_OUTCOME_TRY( auto transfer_result,
                               TransferFunds( balance, destination_address, token_id, TIMEOUT_TRANSFER ) );
            const auto &[tx_id, duration_ms] = transfer_result;
            node_logger_->debug( "Transferred account balance {} to {:.8} in transaction {} after {} ms",
                                 balance,
                                 destination_address,
                                 tx_id,
                                 duration_ms );
        }

        return SelectAccount( destination_address );
    }

    outcome::result<void> GeniusNode::DeleteAccount( std::string_view public_address )
    {
        if ( public_address == GetAddress() )
        {
            node_logger_->error( "Can't delete active account" );
            return std::errc::address_not_available;
        }

        return GeniusAccount::DeleteAccount( public_address, write_base_path_ );
    }

    outcome::result<void> GeniusNode::MergeAccount( std::string_view public_address )
    {
        const auto previous_address = GetAddress();
        BOOST_OUTCOME_TRY( TransferAccount( public_address ) );
        return DeleteAccount( previous_address );
    }

    outcome::result<void> GeniusNode::SetPayoutAddress( std::string_view payout_address )
    {
        if ( !GeniusAccount::IsValidPublicKey( payout_address ) )
        {
            return outcome::failure( std::errc::bad_address );
        }

        BOOST_OUTCOME_TRY( account_->SaveInSecureStorage( "payout_address", std::string( payout_address ) ) );

        this->StateTransition( NodeState::INITIALIZING_PROCESSING );

        return outcome::success();
    }

    outcome::result<std::string> GeniusNode::ProcessImage( const std::string &jsondata )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }
        BOOST_OUTCOME_TRY( auto procmgr, sgns::sgprocessing::ProcessingManager::Create( jsondata ) );

        auto funds = GetProcessCost( procmgr );
        if ( funds <= 0 )
        {
            return outcome::failure( Error::PROCESS_COST_ERROR );
        }

        if ( account_->GetUTXOManager().GetBalance() < funds )
        {
            return outcome::failure( Error::INSUFFICIENT_FUNDS );
        }

        SGProcessing::Task task;
        auto               uuidstring = generate_uuid_with_ipfs_id( pubsub_->GetHost()->getId().toBase58() );

        //Make a small json to insert without extra indentation and spacing.
        json smalljson;
        sgns::to_json( smalljson, procmgr->GetProcessingData() );
        task.set_ipfs_block_id( uuidstring );
        task.set_json_data( smalljson.dump( -1 ) );
        task.set_random_seed( 0 );
        task.set_results_channel( ( boost::format( "RESULT_CHANNEL_ID_%1%" ) % ( 1 ) ).str() );
        //Get Processing Data
        auto procdata = procmgr->GetProcessingData();

        //Split into subtasks
        processing::ProcessTaskSplitter  taskSplitter;
        std::list<SGProcessing::SubTask> subTasks;
        //Make Copies, trying to use references for passes/input nodes may cause problems.
        auto passes = procdata.get_passes();
        for ( const auto &pass : passes )
        {
            auto input_nodes = pass.get_model().value().get_input_nodes();
            for ( auto &model : input_nodes )
            {
                json modeljson;
                sgns::to_json( modeljson, model );
                auto   index = procmgr->GetInputIndex( model.get_source().value() );
                size_t nChunks =
                    procdata.get_inputs()[index.value()].get_dimensions().value().get_chunk_count().value();
                rapidjson::StringBuffer                    buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer( buffer );

                taskSplitter.SplitTask( task,
                                        subTasks,
                                        modeljson.dump( -1 ),
                                        nChunks,
                                        false,
                                        pubsub_->GetHost()->getId().toBase58() );
            }
        }
        if ( subTasks.size() <= 0 )
        {
            return outcome::failure( Error::INVALID_JSON );
        }
        auto cut = sgns::TokenAmount::ParseMinions( dev_config_.Cut );
        if ( !cut )
        {
            return outcome::failure( cut.error() );
        }

        BOOST_OUTCOME_TRY( auto manager, GetTransactionManager() );
        BOOST_OUTCOME_TRY( auto result_pair,
                           manager->HoldEscrow( funds, std::string( dev_config_.Addr ), cut.value(), uuidstring ) );

        //TODO - Make it async to post the job data in case the transaction gets confirmed.
        auto [tx_id, escrow_data_pair] = result_pair;

        auto [escrow_path, escrow_data] = escrow_data_pair;

        task.set_escrow_path( escrow_path );

        BOOST_OUTCOME_TRY( auto crdt_transaction,
                           CreateEscrowInfoCRDTTransaction( escrow_path, std::move( escrow_data ) ) );

        auto enqueue_task_return = task_queue_->EnqueueTask( task, subTasks, crdt_transaction );
        if ( enqueue_task_return.has_failure() )
        {
            return outcome::failure( Error::DATABASE_WRITE_ERROR );
        }

        // Track this task locally so it can be polled later via GetMyTaskIds()
        my_task_ids_.push_back( uuidstring );
        if ( my_task_ids_.size() > kMyTasksMemoryLimit )
        {
            my_task_ids_.erase( my_task_ids_.begin() ); // Evict oldest
        }
        PersistMyTaskIds();

        return tx_id;
    }

    std::vector<std::string> GeniusNode::GetMyTaskIds( size_t limit, size_t offset ) const
    {
        if ( limit == 0 || my_task_ids_.empty() )
        {
            return {};
        }

        // Work from the end (newest entries) backward
        const size_t total     = my_task_ids_.size();
        const size_t start     = ( offset >= total ) ? 0 : ( total - offset );
        const size_t available = ( start >= limit ) ? ( start - limit ) : 0;
        const size_t count     = start - available;

        std::vector<std::string> result;
        result.reserve( count );
        for ( size_t i = available; i < start; ++i )
        {
            result.push_back( my_task_ids_[i] );
        }
        return result;
    }

    outcome::result<SGProcessing::TaskResult> GeniusNode::GetTaskResult( const std::string &taskId )
    {
        if ( !task_queue_ )
        {
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }

        return task_queue_->GetTaskResult( taskId );
    }

    uint64_t GeniusNode::GetProcessCost( std::shared_ptr<sgns::sgprocessing::ProcessingManager> &procmgr )
    {
        auto blockLen = procmgr->ParseBlockSize();
        if ( !blockLen )
        {
            node_logger_->error( "ParseBlockSize failed" );
            return 0;
        }
        node_logger_->trace( "Parsed totalBytes: {}", blockLen.value() );

        auto maybeGnusPrice = GetGNUSPrice();
        if ( !maybeGnusPrice )
        {
            node_logger_->error( "GetGNUSPrice failed: {}", maybeGnusPrice.error().message() );
            return 0;
        }
        double gnusPrice = maybeGnusPrice.value();
        node_logger_->trace( "Retrieved GNUS price (USD/genius): {}", gnusPrice );

        auto rawMinionsRes = TokenAmount::CalculateCostMinions( blockLen.value(), gnusPrice );
        if ( !rawMinionsRes )
        {
            node_logger_->error( "TokenAmount::CalculateCostMinions failed" );
            return 0;
        }
        uint64_t rawMinions = rawMinionsRes.value();
        node_logger_->trace( "Raw cost in minions: {}", rawMinions );

        return rawMinions;
    }

    outcome::result<double> GeniusNode::GetGNUSPrice()
    {
        auto price = GetCoinprice( { "genius-ai" } );
        if ( !price )
        {
            node_logger_->error( "GNUS price request failed: {}", price.error().message() );
            return price.error();
        }
        auto price_it = price.value().find( "genius-ai" );
        if ( price_it == price.value().end() || !std::isfinite( price_it->second ) || price_it->second <= 0.0 )
        {
            node_logger_->error( "GNUS price response did not contain a finite positive price" );
            return outcome::failure( Error::NO_PRICE );
        }
        return price_it->second;
    }

    std::string GeniusNode::GetVersion()
    {
        return sgns::version::SuperGeniusVersionFullString();
    }

    outcome::result<std::string> GeniusNode::MintTokens( uint64_t           amount,
                                                         const std::string &transaction_hash,
                                                         const std::string &chainid,
                                                         TokenID            tokenid,
                                                         std::string        destination )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction manager not ready", __func__ );
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }
        if ( destination.empty() )
        {
            destination = account_->GetAddress();
        }

        BOOST_OUTCOME_TRY( auto manager, GetTransactionManager() );
        BOOST_OUTCOME_TRY( auto tx_id, manager->MintFunds( amount, transaction_hash, chainid, tokenid, destination ) );

        node_logger_->debug( "{}: Mint transaction {} sent ", __func__, tx_id );
        return tx_id;
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::MintTokens( uint64_t           amount,
                                                                              const std::string &transaction_hash,
                                                                              const std::string &chainid,
                                                                              TokenID            tokenid,
                                                                              std::string        destination,
                                                                              std::chrono::milliseconds timeout )
    {
        BOOST_OUTCOME_TRY( auto tx_id,
                           MintTokens( amount, transaction_hash, chainid, tokenid, std::move( destination ) ) );

        BOOST_OUTCOME_TRY( auto finalized_result, WaitForFinalized( tx_id, timeout ) );

        auto [tx_status, duration] = finalized_result;

        if ( tx_status != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "{}: transaction {} failed after {} ms", __func__, tx_id, duration );
            return outcome::failure( Error::TRANSACTION_FAILED );
        }

        node_logger_->debug( "{}: transaction {} sent in {} ms", __func__, tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    std::optional<std::string> GeniusNode::GetMnemonicOfActiveAccount() const
    {
        auto res = this->account_->LoadFromSecureStorage( "mnemonic" );
        if ( res.has_error() )
        {
            return std::nullopt;
        }
        return res.value();
    }

    [[nodiscard]] std::pair<float, std::string> GeniusNode::GetInitializationStatus() const
    {
        auto node_state = state_.load();

        // Note: these weights are arbitrary and may be changed if some stage is taking too long
        switch ( node_state )
        {
            case NodeState::CREATING:
                return { 0.0f, "Creating node and initializing services" };

            case NodeState::MIGRATING_DATABASE:
            {
                std::lock_guard<std::mutex> lock( migration_mutex_ );
                if ( migration_manager_ )
                {
                    auto total   = migration_manager_->GetTotalSteps();
                    auto current = migration_manager_->GetCurrentStepIndex();
                    if ( total > 0 && current > 0 )
                    {
                        // Subdivide the 0.05 -- 0.30 range across migration steps
                        float pct = 0.05f + 0.25f * ( static_cast<float>( current ) / static_cast<float>( total ) );
                        return { pct, migration_manager_->GetCurrentStepDescription() };
                    }
                    return { 0.05f, "Preparing migration steps" };
                }
                return { 0.30f, "Migrating database" };
            }

            case NodeState::INITIALIZING_DATABASE:
                return { 0.40f, "Initializing CRDT database" };

            case NodeState::INITIALIZING_BLOCKCHAIN:
                return { 0.525f, "Initializing blockchain service" };

            case NodeState::INITIALIZING_TRANSACTIONS:
            {
                // 0.60 -- 0.90 range with sub-progress from TransactionManager state
                switch ( GetTransactionManagerState() )
                {
                    case TransactionManager::State::CREATING:
                        return { 0.60f, "Creating transaction manager" };
                    case TransactionManager::State::INITIALIZING:
                        return { 0.70f, "Initializing transaction manager" };
                    case TransactionManager::State::SYNCING:
                        return { 0.80f, "Syncing transactions" };
                    case TransactionManager::State::READY:
                        return { 0.90f, "Finalizing transaction manager" };
                }
                return { 0.60f, "Initializing transactions" };
            }

            case NodeState::INITIALIZING_PROCESSING:
                return { 0.945f, "Initializing processing modules" };

            case NodeState::READY:
                return { 1.0f, "Ready" };
        }

        return { 0.0f, "Unknown state" };
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::TransferFunds( uint64_t                  amount,
                                                                                 const std::string        &destination,
                                                                                 TokenID                   token_id,
                                                                                 std::chrono::milliseconds timeout )
    {
        BOOST_OUTCOME_TRY( auto &&tx_id, TransferFunds( amount, destination, token_id ) );

        BOOST_OUTCOME_TRY( auto finalized_result, WaitForFinalized( tx_id, timeout ) );

        auto [tx_status, duration] = finalized_result;

        if ( tx_status != TransactionManager::TransactionStatus::CONFIRMED )
        {
            node_logger_->error( "{}: transaction {} failed after {} ms", __func__, tx_id, duration );
            return outcome::failure( Error::TRANSACTION_FAILED );
        }

        node_logger_->debug( "{}: transaction {} sent in {} ms", __func__, tx_id, duration );
        return std::make_pair( tx_id, duration );
    }

    outcome::result<std::string> GeniusNode::TransferFunds( uint64_t           amount,
                                                            const std::string &destination,
                                                            TokenID            token_id )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction Manager is not ready", __func__ );
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }

        auto available_balance = account_->GetUTXOManager().GetBalance( token_id );
        if ( available_balance < amount )
        {
            node_logger_->error( "{}: insufficient local funds: requested={}, available={}",
                                 __func__,
                                 amount,
                                 available_balance );
            return outcome::failure( Error::INSUFFICIENT_FUNDS );
        }

        BOOST_OUTCOME_TRY( auto manager, GetTransactionManager() );
        BOOST_OUTCOME_TRY( auto tx_id, manager->TransferFunds( amount, destination, token_id ) );

        node_logger_->debug( "{}: transaction {} sent", __func__, tx_id );
        return tx_id;
    }

    outcome::result<std::string> GeniusNode::PayDev( uint64_t amount, TokenID token_id )
    {
        return TransferFunds( amount, dev_config_.Addr, token_id );
    }

    outcome::result<std::pair<std::string, uint64_t>> GeniusNode::PayDev( uint64_t                  amount,
                                                                          TokenID                   token_id,
                                                                          std::chrono::milliseconds timeout )
    {
        return TransferFunds( amount, dev_config_.Addr, token_id, timeout );
    }

    outcome::result<std::pair<TransactionManager::TransactionStatus, uint64_t>> GeniusNode::WaitForFinalized(
        const std::string        &tx_id,
        std::chrono::milliseconds timeout )
    {
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction Manager is not ready", __func__ );

            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }
        auto start_time = std::chrono::steady_clock::now();

        do
        {
            auto finalized_result = IsFinalized( tx_id );
            if ( finalized_result.has_value() )
            {
                auto end_time = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( end_time - start_time ).count();
                node_logger_->debug( "{}: Transaction finalized with status {} and duration of {} ms",
                                     __func__,
                                     static_cast<int>( finalized_result.value() ),
                                     duration );

                return std::make_pair( finalized_result.value(), duration );
            }

            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        } while ( std::chrono::steady_clock::now() - start_time < timeout );

        node_logger_->error( "{}: Transaction not finalized within timeout of {} ms", __func__, timeout.count() );

        return outcome::failure( Error::TRANSACTION_NOT_FINALIZED );
    }

    std::optional<TransactionManager::TransactionStatus> GeniusNode::IsFinalized( const std::string &tx_id )
    {
        auto manager_result = GetTransactionManager();

        if ( manager_result.has_failure() )
        {
            node_logger_->error( "{}: Failed to get Transaction Manager: {}",
                                 __func__,
                                 manager_result.error().message() );
            return std::nullopt;
        }
        auto manager   = manager_result.value();
        auto tx_status = manager->GetOutgoingStatusByTxId( tx_id );
        if ( tx_status == TransactionManager::TransactionStatus::CONFIRMED ||
             tx_status == TransactionManager::TransactionStatus::FAILED ||
             tx_status == TransactionManager::TransactionStatus::INVALID )
        {
            return tx_status;
        }
        return std::nullopt;
    }

    uint64_t GeniusNode::GetBalance()
    {
        return account_->GetUTXOManager().GetBalance();
    }

    uint64_t GeniusNode::GetBalance( const TokenID token_id )
    {
        return account_->GetUTXOManager().GetBalance( token_id );
    }

    uint64_t GeniusNode::GetBalance( const std::string &address )
    {
        return account_->GetUTXOManager().GetBalance( address );
    }

    uint64_t GeniusNode::GetBalance( const TokenID token_id, const std::string &address )
    {
        return account_->GetUTXOManager().GetBalance( token_id, address );
    }

    void GeniusNode::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
    {
        static constexpr std::string_view FUNC        = __func__;
        const auto                        account_tag = account_->GetAddress().substr( 0, 8 );
        node_logger_->info( "[{}]{}: SUCCESS PROCESSING TASK {}", account_tag, FUNC, task_id );

        if ( task_queue_->IsTaskCompleted( task_id ) )
        {
            node_logger_->info( "[{}]{}: Task Already completed!", account_tag, FUNC );
            return;
        }
        if ( GetTransactionManagerState() != TransactionManager::State::READY )
        {
            node_logger_->info( "[{}]{}: Transactions are not ready", account_tag, FUNC );
            return;
        }

        auto maybe_task = task_queue_->GetTask( task_id );
        if ( maybe_task.has_failure() )
        {
            node_logger_->info( "[{}]{}: Task id {} not found in DB", account_tag, FUNC, task_id );
            return;
        }

        auto complete_task_result = task_queue_->CompleteTask( task_id, taskresult );
        if ( complete_task_result.has_failure() )
        {
            node_logger_->error( "[{}]{}: Unable to complete task: {} ", account_tag, FUNC, task_id );
            return;
        }

        auto manager_result = GetTransactionManager();
        if ( manager_result.has_failure() )
        {
            node_logger_->error( "[{}]{}: Unable to access transaction manager for task: {}",
                                 account_tag,
                                 FUNC,
                                 task_id );
            return;
        }

        node_logger_->info( "[{}]{}: Creating the payout transaction", account_tag, FUNC );
        manager_result.value()->AsyncPayEscrow(
            maybe_task.value().escrow_path(),
            taskresult,
            std::move( complete_task_result.value() ),
            TIMEOUT_ESCROW_PAY,
            [logger = node_logger_, account_tag, task_id]( TransactionManager::TransactionCompletion completion )
            {
                if ( completion.error == boost::asio::error::operation_aborted )
                {
                    logger->debug( "[{}]ProcessingDone: Escrow payout cancelled during shutdown for task {}",
                                   account_tag,
                                   task_id );
                    return;
                }
                if ( completion.error )
                {
                    logger->error( "[{}]ProcessingDone: Escrow payout failed for task {} after {} ms: {}",
                                   account_tag,
                                   task_id,
                                   completion.elapsed.count(),
                                   completion.error.message() );
                    return;
                }
                if ( completion.status != TransactionManager::TransactionStatus::CONFIRMED )
                {
                    logger->error( "[{}]ProcessingDone: Escrow payout transaction {} ended in state {} for task {}",
                                   account_tag,
                                   completion.transaction_id,
                                   static_cast<int>( completion.status ),
                                   task_id );
                    return;
                }

                logger->info( "[{}]ProcessingDone: Paid for task {} with transaction {} in {} ms",
                              account_tag,
                              task_id,
                              completion.transaction_id,
                              completion.elapsed.count() );
            } );
    }

    void GeniusNode::ProcessingError( const std::string &task_id )
    {
        boost::asio::post( boost::asio::system_executor{},
                           [weak_self( weak_from_this() ), task_id]()
                           {
                               if ( auto strong = weak_self.lock() )
                               {
                                   strong->node_logger_->error( "[ {} ] ERROR PROCESSING SUBTASK ",
                                                                strong->account_->GetAddress().substr( 0, 8 ),
                                                                task_id );
                               }
                           } );
    }

    void GeniusNode::PrintDataStore() const
    {
        if ( tx_globaldb_ )
        {
            tx_globaldb_->PrintDataStore();
        }
        else
        {
            node_logger_->error( "{}: GlobalDB is not initialized", __func__ );
        }
    }

    void GeniusNode::StopProcessing()
    {
        if ( processing_service_ )
        {
            processing_service_->StopProcessing();
        }
        else
        {
            node_logger_->error( "{}: Processing service is not initialized", __func__ );
        }
    }

    void GeniusNode::StartProcessing()
    {
        if ( processing_service_ )
        {
            processing_service_->StartProcessing( processing_grid_chanel_topic_ );
        }
        else
        {
            node_logger_->error( "{}: Processing service is not initialized", __func__ );
        }
    }

    outcome::result<std::map<std::string, double>> GeniusNode::GetCoinprice( const std::vector<std::string> &tokenIds )
    {
        auto                          currentTime = std::chrono::system_clock::now();
        std::map<std::string, double> result;
        std::vector<std::string>      tokensToFetch;
        // Determine which tokens need to be fetched
        for ( const auto &tokenId : tokenIds )
        {
            auto it = m_tokenPriceCache.find( tokenId );

            if ( it != m_tokenPriceCache.end() && ( currentTime - it->second.lastUpdate ) < m_cacheValidityDuration )
            {
                // Use cached price if it's still valid
                result[tokenId] = it->second.price;
            }
            else
            {
                // Add to the list of tokens that need fresh data
                tokensToFetch.push_back( tokenId );
            }
        }

        // If we have tokens to fetch and we're not rate limited
        if ( !tokensToFetch.empty() && ( currentTime - m_lastApiCall ) >= MIN_API_CALL_INTERVAL )
        {
            sgns::CoinGeckoPriceRetriever retriever;
            auto                          newPricesResult = retriever.getCurrentPrices( tokensToFetch );

            if ( newPricesResult )
            {
                auto &newPrices = newPricesResult.value();
                m_lastApiCall   = currentTime;

                // Update the cache and result with new prices
                for ( const auto &[token, price] : newPrices )
                {
                    m_tokenPriceCache[token] = { price, currentTime };
                    result[token]            = price;
                }
            }
            else
            {
                // Handle the error case
                // If we have some cached data, continue with what we have
                if ( result.empty() )
                {
                    // Only return error if we have no data at all
                    return newPricesResult.error();
                }
                // Otherwise, continue with partial data and log the error
                // log("Failed to fetch prices for some tokens: " + newPricesResult.error().message());
            }
        }

        return result;
    }

    outcome::result<std::map<std::string, std::map<int64_t, double>>> GeniusNode::GetCoinPriceByDate(
        const std::vector<std::string> &tokenIds,
        const std::vector<int64_t>     &timestamps )
    {
        sgns::CoinGeckoPriceRetriever retriever;
        return retriever.getHistoricalPrices( tokenIds, timestamps );
    }

    outcome::result<std::map<std::string, std::map<int64_t, double>>> GeniusNode::GetCoinPricesByDateRange(
        const std::vector<std::string> &tokenIds,
        int64_t                         from,
        int64_t                         to )
    {
        sgns::CoinGeckoPriceRetriever retriever;
        return retriever.getHistoricalPriceRange( tokenIds, from, to );
    }

    outcome::result<std::string> GeniusNode::FormatTokens( uint64_t amount, TokenID tokenId )
    {
        if ( tokenId.IsGNUS() )
        {
            return TokenAmount::FormatMinions( amount );
        }
        if ( tokenId.Equals( dev_config_.TokenID ) )
        {
            auto child = TokenAmount::ConvertToChildToken( amount, dev_config_.TokenValueInGNUS );
            if ( !child )
            {
                return outcome::failure( child.error() );
            }
            return child.value();
        }
        return outcome::failure( make_error_code( GeniusNode::Error::TOKEN_ID_MISMATCH ) );
    }

    outcome::result<uint64_t> GeniusNode::ParseTokens( const std::string &str, TokenID tokenId )
    {
        if ( tokenId.IsGNUS() )
        {
            return TokenAmount::ParseMinions( str );
        }
        if ( tokenId.Equals( dev_config_.TokenID ) )
        {
            return TokenAmount::ConvertFromChildToken( str, dev_config_.TokenValueInGNUS );
        }
        return outcome::failure( make_error_code( GeniusNode::Error::TOKEN_ID_MISMATCH ) );
    }

    std::vector<std::vector<uint8_t>> GeniusNode::GetInTransactions() const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return {};
        }
        return manager_result.value()->GetInTransactions();
    }

    std::vector<std::vector<uint8_t>> GeniusNode::GetOutTransactions() const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return {};
        }
        return manager_result.value()->GetOutTransactions();
    }

    size_t GeniusNode::CountTransactions( std::optional<TransactionManager::TransactionStatus> tx_status ) const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return 0;
        }
        return manager_result.value()->CountTransactions( tx_status );
    }

    std::string GeniusNode::GetAddress() const
    {
        std::string address = "UNVAILABLE";
        auto        account = account_;
        if ( account )
        {
            address = account->GetAddress();
        }
        return address;
    }

    // Wait for a transaction to be processed with a timeout
    TransactionManager::TransactionStatus GeniusNode::WaitForTransactionOutgoing( const std::string        &txId,
                                                                                  std::chrono::milliseconds timeout )
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::TransactionStatus::INVALID;
        }
        return manager_result.value()->WaitForTransactionOutgoing( txId, timeout );
    }

    // Wait for a transaction to be processed with a timeout
    TransactionManager::TransactionStatus GeniusNode::WaitForTransactionIncoming( const std::string        &txId,
                                                                                  std::chrono::milliseconds timeout )
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::TransactionStatus::INVALID;
        }
        return manager_result.value()->WaitForTransactionIncoming( txId, timeout );
    }

    TransactionManager::TransactionStatus GeniusNode::WaitForEscrowRelease( const std::string        &originalEscrowId,
                                                                            std::chrono::milliseconds timeout )
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::TransactionStatus::INVALID;
        }
        return manager_result.value()->WaitForEscrowRelease( originalEscrowId, timeout );
    }

    outcome::result<std::shared_ptr<TransactionManager>> GeniusNode::GetTransactionManager() const
    {
        if ( !transaction_manager_ )
        {
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }
        return transaction_manager_;
    }

    outcome::result<std::shared_ptr<crdt::AtomicTransaction>> GeniusNode::CreateEscrowInfoCRDTTransaction(
        std::string        path,
        sgns::base::Buffer value )
    {
        auto crdt_transaction = tx_globaldb_->BeginTransaction();

        sgns::crdt::HierarchicalKey key( path );

        BOOST_OUTCOME_TRY( crdt_transaction->Put( std::move( key ), std::move( value ) ) );

        return crdt_transaction;
    }

    TransactionManager::State GeniusNode::GetTransactionManagerState() const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            return TransactionManager::State::CREATING;
        }
        return manager_result.value()->GetState();
    }

    void GeniusNode::SendTransactionAndProof( std::shared_ptr<GeniusTransaction> tx, std::vector<uint8_t> proof )
    {
        auto manager_result = GetTransactionManager();
        if ( manager_result.has_value() )
        {
            manager_result.value()->EnqueueTransaction( std::make_pair( tx, proof ) );
        }
        else
        {
            node_logger_->error( "{}: Transactions not ready", __func__ );
        }
    }

    void GeniusNode::RotateLogFiles( const std::string &base_path )
    {
        std::filesystem::path basePath( base_path );

        // Define log file paths
        std::filesystem::path sgnslog_path      = basePath / "sgnslog.log";
        std::filesystem::path sgnslog2_path     = basePath / "sgnslog2.log";
        std::filesystem::path sgnslog_old_path  = basePath / "sgnslog.old.log";
        std::filesystem::path sgnslog2_old_path = basePath / "sgnslog2.old.log";

        try
        {
            // Handle sgnslog.log rotation
            if ( std::filesystem::exists( sgnslog_path ) )
            {
                // Delete old backup if it exists
                if ( std::filesystem::exists( sgnslog_old_path ) )
                {
                    std::filesystem::remove( sgnslog_old_path );
                    std::cout << "Deleted old backup: " << sgnslog_old_path << std::endl;
                }

                // Rename current log to backup
                std::filesystem::rename( sgnslog_path, sgnslog_old_path );
                std::cout << "Rotated log: " << sgnslog_path << " -> " << sgnslog_old_path << std::endl;
            }

            // Handle sgnslog2.log rotation
            if ( std::filesystem::exists( sgnslog2_path ) )
            {
                // Delete old backup if it exists
                if ( std::filesystem::exists( sgnslog2_old_path ) )
                {
                    std::filesystem::remove( sgnslog2_old_path );
                    std::cout << "Deleted old backup: " << sgnslog2_old_path << std::endl;
                }

                // Rename current log to backup
                std::filesystem::rename( sgnslog2_path, sgnslog2_old_path );
                std::cout << "Rotated log: " << sgnslog2_path << " -> " << sgnslog2_old_path << std::endl;
            }
        }
        catch ( const std::filesystem::filesystem_error &e )
        {
            std::cerr << "Log rotation error: " << e.what() << std::endl;
            // Continue execution - don't let log rotation failure stop the application
        }
    }

    void GeniusNode::SetChainlistFetcher( std::function<std::optional<std::string>()> fetcher )
    {
        chainlist_fetcher_ = std::move( fetcher );
    }

    bool GeniusNode::ConfigureRpcEndpoint( const std::string &chain_id, std::vector<WeightedRpcEndpoint> endpoints )
    {
        auto transaction_manager = transaction_manager_;
        if ( !transaction_manager || transaction_manager->GetState() != TransactionManager::State::READY )
        {
            node_logger_->warn( "ConfigureRpcEndpoint called before transaction manager is ready" );
            return false;
        }
        const size_t endpoint_count = endpoints.size();
        transaction_manager->GetPublicChainInputValidator().SetRpcEndpoints( chain_id, std::move( endpoints ) );
        node_logger_->info( "Configured {} RPC endpoint(s) for chain {}", endpoint_count, chain_id );
        return true;
    }

    std::filesystem::path GeniusNode::ResolveBridgeChainsConfigPath() const
    {
        std::filesystem::path bridge_chains_path;

        // Primary: use GeniusNodeConfig BaseWritePath (writable on all platforms including Android)
        if ( !dev_config_.BaseWritePath.empty() )
        {
            bridge_chains_path = std::filesystem::path( dev_config_.BaseWritePath ) / "bridge_chains_config.json";
        }

        // Fallback: binary directory (finds CMake-installed or copied default)
        if ( bridge_chains_path.empty() || !std::filesystem::exists( bridge_chains_path ) )
        {
            try
            {
                auto bin_dir   = boost::dll::program_location().parent_path();
                auto candidate = std::filesystem::path( bin_dir.string() ) / "bridge_chains_config.json";
                if ( std::filesystem::exists( candidate ) )
                {
                    bridge_chains_path = std::move( candidate );
                }
            }
            catch ( const std::exception &e )
            {
                node_logger_->warn( "ResolveBridgeChainsConfigPath: cannot determine binary location ({}), "
                                    "falling back to CWD",
                                    e.what() );
            }
        }

        // Final fallback: current working directory
        if ( bridge_chains_path.empty() || !std::filesystem::exists( bridge_chains_path ) )
        {
            bridge_chains_path = std::filesystem::current_path() / "bridge_chains_config.json";
        }

        return bridge_chains_path;
    }

    void GeniusNode::OnRpcEndpointsReady( std::vector<ChainContractPair> chains )
    {
        std::lock_guard lock( catchup_mutex_ );
        catchup_chains_ = std::move( chains );
        node_logger_->info( "GeniusNode: received {} chain(s) from provider — stored for catch-up watcher",
                            catchup_chains_.size() );
    }

    void GeniusNode::InitializeAndStartBridge()
    {
        node_logger_->info( "InitializeAndStartBridge: thin orchestrator (D-01, D-03)" );

        // 1. Resolve config path (stays in GeniusNode per D-01)
        auto config_path = ResolveBridgeChainsConfigPath();
        node_logger_->info( "InitializeAndStartBridge: loading bridge chain config from {}", config_path.string() );

        // 2. Construct provider
        rpc_endpoint_provider_ = std::make_shared<ChainRpcEndpointProvider>();

        // 2a. Inject custom chainlist fetcher if provided (test injection point via SetChainlistFetcher)
        if ( chainlist_fetcher_ )
        {
            rpc_endpoint_provider_->SetChainlistFetcher( chainlist_fetcher_ );
        }

        // 3. Subscribe observers BEFORE post (D-03 ordering)
        rpc_endpoint_provider_->AddObserverCallback(
            [weak_self = weak_from_this()]( std::vector<ChainContractPair> chains )
            {
                if ( auto strong = weak_self.lock() )
                {
                    strong->OnRpcEndpointsReady( std::move( chains ) );
                }
            } );
        if ( bridge_relayer_ )
        {
            rpc_endpoint_provider_->AddObserver( *bridge_relayer_ );
        }

        // 3a. Create and start the catch-up polling watcher (replaces the old
        //     INITIALIZING_RPC_CATCH_UP state machine + PerformStartupCatchupScan).
        //     The watcher owns its own thread and polls eth_getLogs independently
        //     of the node lifecycle — no state machine coupling.
        if ( rpc_catchup_ )
        {
            evmwatcher::BridgeCatchupWatcher::Config catchup_config;
            catchup_config.poll_interval = std::chrono::seconds( 15 );

            auto chains_provider = [weak_self = weak_from_this()]() -> std::vector<ChainContractPair>
            {
                auto strong = weak_self.lock();
                if ( !strong )
                {
                    return {};
                }
                std::lock_guard lock( strong->catchup_mutex_ );
                return strong->catchup_chains_;
            };

            auto rpc_resolver =
                [weak_self = weak_from_this()]( const std::string &chain_id_str ) -> std::optional<std::string>
            {
                auto strong = weak_self.lock();
                if ( !strong || !strong->transaction_manager_ )
                {
                    return std::nullopt;
                }
                auto &validator = strong->transaction_manager_->GetPublicChainInputValidator();
                return validator.GetFirstRpcUrl( chain_id_str );
            };

            auto burn_processor = [weak_self = weak_from_this()]( const std::vector<eth::abi::AbiValue> &decoded_values,
                                                                  const std::string                     &tx_hash_hex,
                                                                  const std::string &chain_id_str ) -> bool
            {
                // Parse the ABI-decoded values into a BurnEventParams
                auto burn = BridgeRelayer::ParseBurnEventValues( decoded_values );
                if ( !burn )
                {
                    GeniusNodeLogger()->debug( "CatchUpWatcher: failed to parse burn event for tx {} — skipping",
                                               tx_hash_hex );
                    return false;
                }

                // UTXO state checks (same guards as the old scan)
                base::Hash256 burn_tx_hash;
                if ( !rlp::base::parse::hex_array( tx_hash_hex, burn_tx_hash ) )
                {
                    GeniusNodeLogger()->error( "CatchUpWatcher: failed to parse tx_hash to hex {} — skipping",
                                               tx_hash_hex );
                    return false;
                }

                auto strong = weak_self.lock();
                if ( !strong || !strong->account_ )
                {
                    return false;
                }
                auto &utxo_mgr = strong->account_->GetUTXOManager();
                if ( utxo_mgr.IsOutPointConsumed( burn_tx_hash, 0 ) )
                {
                    strong->node_logger_->debug( "CatchUpWatcher: burn tx {} already CONSUMED — skipping",
                                                 tx_hash_hex );
                    return false;
                }
                if ( utxo_mgr.IsOutPointReserved( burn_tx_hash, 0 ) )
                {
                    strong->node_logger_->debug( "CatchUpWatcher: burn tx {} already RESERVED — skipping",
                                                 tx_hash_hex );
                    return false;
                }

                try
                {
                    auto result = strong->MintTokens( burn.value().amount,
                                                      tx_hash_hex,
                                                      chain_id_str,
                                                      burn.value().token_id,
                                                      burn.value().destination );
                    return result.has_value();
                }
                catch ( const std::exception &e )
                {
                    strong->node_logger_->debug( "CatchUpWatcher: MintTokens threw for tx {}: {} — skipping",
                                                 tx_hash_hex,
                                                 e.what() );
                    return false;
                }
            };

            catchup_watcher_ = std::make_shared<evmwatcher::BridgeCatchupWatcher>(
                catchup_config,
                nullptr, // no raw message callback needed
                std::move( chains_provider ),
                std::move( rpc_resolver ),
                std::move( burn_processor ) );

            catchup_watcher_->startWatching();
            node_logger_->info( "InitializeAndStartBridge: catchup watcher started (poll_interval={}s)",
                                catchup_config.poll_interval.count() );
        }
        else
        {
            node_logger_->info( "InitializeAndStartBridge: catchup watcher disabled (rpc_catchup_=false)" );
        }

        // 4. Post Initialize() to io_context — non-blocking. Capture a
        //    generation token + shared ownership of the transaction manager,
        //    provider, and relayer. Initialize() can block ~15s on the chainlist
        //    fetch; SelectAccount() may run on another io_ thread during that
        //    window and reset these members. The shared_ptr captures keep the
        //    objects alive for the duration of the call (no mid-execution free of
        //    the provider's chainlist_fetcher_/observers_, and the raw
        //    bridge_relayer_ observer stays valid). The generation check still
        //    discards work posted before a completed switch.
        const auto generation = bridge_init_generation_.load();
        auto       tx_mgr     = transaction_manager_;   // shared_ptr copy: stable lifetime
        auto       provider   = rpc_endpoint_provider_; // shared_ptr copy: keeps provider alive mid-Initialize()
        auto       relayer    = bridge_relayer_; // shared_ptr copy: keeps the raw observer valid during notification
        boost::asio::post( *io_,
                           [weak_self   = weak_from_this(),
                            config_path = std::move( config_path ),
                            generation,
                            tx_mgr   = std::move( tx_mgr ),
                            provider = std::move( provider ),
                            relayer  = std::move( relayer )]() mutable
                           {
                               auto strong = weak_self.lock();
                               if ( !strong || strong->bridge_init_generation_.load() != generation )
                               {
                                   return; // account switched — stale init, abort
                               }
                               strong.reset();
                               if ( !tx_mgr || !provider )
                               {
                                   return;
                               }
                               // Stale check consulted INSIDE Initialize() after the blocking
                               // fetch, before it publishes validator pointers / notifies
                               // observers — so a switch during the fetch aborts publishing.
                               auto is_cancelled = [weak_self, generation]() -> bool
                               {
                                   auto s = weak_self.lock();
                                   return !s || s->bridge_init_generation_.load() != generation;
                               };
                               auto &validator = tx_mgr->GetPublicChainInputValidator();
                               provider->Initialize( config_path, validator, is_cancelled );
                           } );
    }

    TransactionManager::TransactionStatus GeniusNode::GetTransactionStatus( const std::string &txId ) const
    {
        auto manager_result = GetTransactionManager();
        if ( !manager_result.has_value() )
        {
            node_logger_->error( "{}: Transactions not ready", __func__ );
            return TransactionManager::TransactionStatus::INVALID;
        }
        return manager_result.value()->GetTransactionStatusByTxId( txId );
    }

    void GeniusNode::TransactionStateChanged( TransactionManager::State old_state, TransactionManager::State new_state )
    {
        node_logger_->info( "Transaction Manager state changed from {} to {}", old_state, new_state );

        switch ( new_state )
        {
            case TransactionManager::State::READY:
            {
                if ( processing_service_ == nullptr )
                {
                    StateTransition( NodeState::INITIALIZING_PROCESSING );
                }
                else if ( isprocessor_ )
                {
                    StartProcessing();
                }
                break;
            }
            case TransactionManager::State::INITIALIZING:
            case TransactionManager::State::SYNCING:
                if ( isprocessor_ )
                {
                    StopProcessing();
                }
                break;
            case TransactionManager::State::CREATING:
            default:
                break;
        }
    }

    void GeniusNode::SetAuthorizedFullNodeAddress( const std::string &pub_address )
    {
        Blockchain::SetAuthorizedFullNodeAddress( pub_address );
        if ( blockchain_ )
        {
            blockchain_->Start();
        }
    }

    std::string GeniusNode::GetAuthorizedFullNodeAddress() const
    {
        return Blockchain::GetAuthorizedFullNodeAddress();
    }

    // ── Result Cache GC ──

    void GeniusNode::StartResultGC()
    {
        if ( result_retention_hours_ == 0 )
        {
            node_logger_->info( "Result retention disabled (retention_hours=0), skipping GC" );
            return;
        }

        auto intervalHours = std::max( 1, result_retention_hours_ / 10 );
        node_logger_->info( "Starting result GC timer: every {} hour(s), retention {} hours, max {} MB",
                            intervalHours,
                            result_retention_hours_,
                            result_retention_max_mb_ );

        gc_timer_                          = std::make_shared<boost::asio::steady_timer>( *io_ );
        std::weak_ptr<GeniusNode> weakSelf = shared_from_this();

        auto schedule = [this, weakSelf, intervalHours]()
        {
            gc_timer_->expires_from_now( std::chrono::hours( intervalHours ) );
            gc_timer_->async_wait(
                [weakSelf]( const boost::system::error_code &ec )
                {
                    if ( ec )
                    {
                        return;
                    }
                    if ( auto self = weakSelf.lock() )
                    {
                        self->RunResultGC();
                    }
                } );
        };

        schedule();
        // Run one initial pass after a short delay
        gc_timer_->cancel();
        gc_timer_->expires_from_now( std::chrono::seconds( 30 ) );
        gc_timer_->async_wait(
            [weakSelf, schedule]( const boost::system::error_code &ec )
            {
                if ( ec )
                {
                    return;
                }
                if ( auto self = weakSelf.lock() )
                {
                    self->RunResultGC();
                    schedule();
                }
            } );
    }

    void GeniusNode::RunResultGC()
    {
        if ( result_retention_hours_ == 0 || !bitswap_ )
        {
            return;
        }

        auto            resultsDir = write_base_path_ + "/" + ipfs_cache_dir_ + "/results";
        std::error_code ec;
        if ( !std::filesystem::exists( resultsDir, ec ) )
        {
            return;
        }

        size_t    deletedCount = 0;
        uintmax_t deletedBytes = 0;

        // Collect all result files sorted by age (oldest first)
        struct FileEntry
        {
            std::string                     path;
            std::filesystem::file_time_type mtime;
        };

        std::vector<FileEntry> files;
        for ( const auto &entry : std::filesystem::recursive_directory_iterator( resultsDir, ec ) )
        {
            if ( ec || !entry.is_regular_file() )
            {
                continue;
            }
            FileEntry fe;
            fe.path  = entry.path().string();
            fe.mtime = entry.last_write_time();
            files.push_back( fe );
        }

        std::sort( files.begin(), files.end(), []( const auto &a, const auto &b ) { return a.mtime < b.mtime; } );

        // Compute cutoff using the clock backing file_time_type
        using FT    = std::filesystem::file_time_type;
        auto now    = FT::clock::now();
        auto cutoff = now - std::chrono::hours( result_retention_hours_ );

        uintmax_t totalBytes = 0;
        for ( const auto &f : files )
        {
            totalBytes += std::filesystem::file_size( f.path, ec );
        }

        // Evict expired files
        for ( auto it = files.begin(); it != files.end() && totalBytes > 0; ++it )
        {
            const auto &path    = it->path;
            const auto &mtime   = it->mtime;
            bool        expired = mtime < cutoff;
            bool        overCap = ( result_retention_max_mb_ > 0 ) &&
                                  ( totalBytes > static_cast<uintmax_t>( result_retention_max_mb_ ) * 1024 * 1024 );
            if ( !expired && !overCap )
            {
                break;
            }

            auto fileSize = std::filesystem::file_size( path, ec );
            std::filesystem::remove( path, ec );
            if ( !ec )
            {
                auto cidStr = std::filesystem::path( path ).filename().string();
                auto cid    = libp2p::multi::ContentIdentifierCodec::fromString( cidStr );
                if ( cid )
                {
                    bitswap_->unpersistBlock( cid.value() );
                }
                deletedCount++;
                deletedBytes += fileSize;
                totalBytes   -= fileSize;
            }
        }

        if ( deletedCount > 0 )
        {
            node_logger_->info( "GC: removed {} result files ({} bytes), {} files remaining ({} bytes)",
                                deletedCount,
                                deletedBytes,
                                files.size() - deletedCount,
                                totalBytes );
        }
    }

    // ── Bootstrap Fullnode Reconnection ──

    boost::optional<libp2p::peer::PeerInfo> GeniusNode::ParsePeerInfoFromString( const std::string &multiaddr_str )
    {
        if ( multiaddr_str.empty() )
        {
            return boost::none;
        }

        auto ma_res = libp2p::multi::Multiaddress::create( multiaddr_str );
        if ( !ma_res )
        {
            return boost::none;
        }

        auto ma = std::move( ma_res.value() );

        auto peer_id_str = ma.getPeerId();
        if ( !peer_id_str )
        {
            return boost::none;
        }

        auto peer_id_res = libp2p::peer::PeerId::fromBase58( *peer_id_str );
        if ( !peer_id_res )
        {
            return boost::none;
        }

        std::vector<libp2p::multi::Multiaddress> multiaddresses;
        multiaddresses.push_back( std::move( ma ) );

        return libp2p::peer::PeerInfo{ peer_id_res.value(), std::move( multiaddresses ) };
    }

    void GeniusNode::InitBootstrapReconnect()
    {
        if ( bootstrap_fullnode_ids_.empty() && bootstrap_peer_ids_.empty() )
        {
            node_logger_->debug( "No bootstrap peers configured, skipping reconnect subscription" );
            return;
        }

        auto host = pubsub_->GetHost();
        bootstrap_disconnect_subscription_.emplace(
            host->getBus().getChannel<libp2p::event::network::OnPeerDisconnectedChannel>().subscribe(
                [weak_self = weak_from_this()]( const libp2p::peer::PeerId &peer_id )
                {
                    if ( auto strong = weak_self.lock() )
                    {
                        if ( strong->shutdown_started_.load() )
                        {
                            return;
                        }
                        bool is_bootstrap = strong->bootstrap_fullnode_ids_.count( peer_id ) ||
                                            strong->bootstrap_peer_ids_.count( peer_id );
                        if ( is_bootstrap )
                        {
                            const char *kind = strong->bootstrap_fullnode_ids_.count( peer_id ) ? "fullnode" : "peer";
                            strong->node_logger_->info( "Bootstrap {} {} disconnected, scheduling reconnect",
                                                        kind,
                                                        peer_id.toBase58() );
                            unsigned attempt = 0;
                            {
                                std::lock_guard<std::mutex> lock( strong->reconnect_mutex_ );
                                auto                        it = strong->reconnect_attempts_.find( peer_id );
                                if ( it != strong->reconnect_attempts_.end() )
                                {
                                    attempt = it->second;
                                }
                            }
                            strong->ScheduleBootstrapReconnect( peer_id, attempt );
                        }
                    }
                } ) );

        node_logger_->info( "Subscribed to disconnect events for {} bootstrap fullnode(s) + {} peer(s)",
                            bootstrap_fullnode_ids_.size(),
                            bootstrap_peer_ids_.size() );
    }

    void GeniusNode::StartBootstrapHealthCheck()
    {
        if ( bootstrap_fullnode_infos_.empty() && bootstrap_peer_infos_.empty() )
        {
            node_logger_->debug( "No bootstrap peers to health-check" );
            return;
        }
        ScheduleNextHealthCheck();
        node_logger_->info( "Bootstrap health check started (interval: {}s, tracking {} fullnodes + {} peers)",
                            reconnect_config_.health_check_interval.count(),
                            bootstrap_fullnode_infos_.size(),
                            bootstrap_peer_infos_.size() );
    }

    void GeniusNode::ScheduleNextHealthCheck()
    {
        if ( shutdown_started_.load() )
        {
            return;
        }

        auto interval = reconnect_config_.health_check_interval;
        {
            std::lock_guard<std::mutex> lock( reconnect_mutex_ );
            if ( !reconnect_attempts_.empty() )
            {
                interval = reconnect_config_.health_check_disconnected_interval;
            }
        }

        auto weak_self = weak_from_this();
        health_check_handle_.emplace( scheduler_->scheduleWithHandle(
            [weak_self]()
            {
                if ( auto strong = weak_self.lock() )
                {
                    strong->PerformHealthCheck();
                }
            },
            interval ) );
    }

    void GeniusNode::PerformHealthCheck()
    {
        if ( shutdown_started_.load() )
        {
            return;
        }

        auto host = pubsub_->GetHost();

        // Check both fullnodes and peers
        for ( const auto &infos : { &bootstrap_fullnode_infos_, &bootstrap_peer_infos_ } )
        {
            for ( const auto &peer_info : *infos )
            {
                auto connectedness = host->connectedness( peer_info );
                if ( connectedness == libp2p::Host::Connectedness::NOT_CONNECTED ||
                     connectedness == libp2p::Host::Connectedness::CAN_NOT_CONNECT )
                {
                    node_logger_->debug( "Health check: bootstrap peer {} is {}",
                                         peer_info.id.toBase58(),
                                         connectedness == libp2p::Host::Connectedness::NOT_CONNECTED
                                             ? "NOT_CONNECTED"
                                             : "CAN_NOT_CONNECT" );

                    unsigned attempt = 0;
                    {
                        std::lock_guard<std::mutex> lock( reconnect_mutex_ );
                        auto                        it = reconnect_attempts_.find( peer_info.id );
                        if ( it != reconnect_attempts_.end() )
                        {
                            attempt = it->second;
                        }
                    }
                    ScheduleBootstrapReconnect( peer_info.id, attempt );
                }
            }
        }

        ScheduleNextHealthCheck();
    }

    void GeniusNode::ScheduleBootstrapReconnect( const libp2p::peer::PeerId &peer_id, unsigned attempt )
    {
        if ( shutdown_started_.load() )
        {
            return;
        }

        // Calculate exponential backoff: base_delay * 2^attempt, capped at max_delay
        auto delay_sec = reconnect_config_.base_delay.count() * ( 1ull << std::min( attempt, 10u ) );
        if ( delay_sec > static_cast<uint64_t>( reconnect_config_.max_delay.count() ) )
        {
            delay_sec = reconnect_config_.max_delay.count();
        }
        auto delay = std::chrono::seconds( delay_sec );

        // Update attempt counter
        {
            std::lock_guard<std::mutex> lock( reconnect_mutex_ );
            reconnect_attempts_[peer_id] = attempt + 1;
        }

        node_logger_->info( "Scheduling reconnect to bootstrap fullnode {} in {}s (attempt {})",
                            peer_id.toBase58(),
                            delay.count(),
                            attempt + 1 );

        auto weak_self = weak_from_this();
        scheduler_->schedule(
            [weak_self, peer_id]()
            {
                if ( auto strong = weak_self.lock() )
                {
                    // DialerImpl is shared with GossipSub and is not thread-safe.
                    strong->pubsub_->GetAsioContext()->post(
                        [weak_self, peer_id]()
                        {
                            if ( auto strong = weak_self.lock() )
                            {
                                strong->DoReconnectToBootstrapPeer( peer_id );
                            }
                        } );
                }
            },
            delay );
    }

    void GeniusNode::DoReconnectToBootstrapPeer( const libp2p::peer::PeerId &peer_id )
    {
        if ( shutdown_started_.load() )
        {
            return;
        }

        // Find the PeerInfo for this peer_id (search fullnodes then peers)
        const libp2p::peer::PeerInfo *peer_info_ptr = nullptr;
        for ( const auto &infos : { &bootstrap_fullnode_infos_, &bootstrap_peer_infos_ } )
        {
            for ( const auto &info : *infos )
            {
                if ( info.id == peer_id )
                {
                    peer_info_ptr = &info;
                    break;
                }
            }
            if ( peer_info_ptr )
            {
                break;
            }
        }

        if ( !peer_info_ptr )
        {
            node_logger_->error( "Cannot reconnect: PeerInfo not found for {}", peer_id.toBase58() );
            return;
        }

        auto connectedness = pubsub_->GetHost()->connectedness( *peer_info_ptr );
        if ( connectedness == libp2p::Host::Connectedness::CONNECTED )
        {
            node_logger_->info( "Bootstrap fullnode {} already connected, resetting attempt counter",
                                peer_id.toBase58() );
            std::lock_guard<std::mutex> lock( reconnect_mutex_ );
            reconnect_attempts_.erase( peer_id );
            return;
        }

        node_logger_->info( "Attempting reconnect to bootstrap fullnode {}...", peer_id.toBase58() );

        auto weak_self = weak_from_this();
        auto ipv4_source = libp2p::multi::Multiaddress::create( "/ip4/0.0.0.0/tcp/0" ).value();
        auto ipv6_source = libp2p::multi::Multiaddress::create( "/ip6/::/tcp/0" ).value();
        libp2p::network::RouteHelper::SourceAddresses source_addresses{
            std::move( ipv4_source ), std::move( ipv6_source ), true, true };

        pubsub_->GetHost()->getNetwork().getDialer().dial(
            *peer_info_ptr,
            [weak_self, peer_id]( auto result )
            {
                if ( auto strong = weak_self.lock() )
                {
                    if ( result.has_value() )
                    {
                        strong->node_logger_->info( "Successfully reconnected to bootstrap fullnode {}",
                                                    peer_id.toBase58() );
                        std::lock_guard<std::mutex> lock( strong->reconnect_mutex_ );
                        strong->reconnect_attempts_.erase( peer_id );
                    }
                    else
                    {
                        strong->node_logger_->warn( "Reconnect to bootstrap fullnode {} failed: {}",
                                                    peer_id.toBase58(),
                                                    result.error().message() );
                        unsigned attempt = 0;
                        {
                            std::lock_guard<std::mutex> lock( strong->reconnect_mutex_ );
                            auto                        it = strong->reconnect_attempts_.find( peer_id );
                            if ( it != strong->reconnect_attempts_.end() )
                            {
                                attempt = it->second;
                            }
                        }
                        strong->ScheduleBootstrapReconnect( peer_id, attempt );
                    }
                }
            },
            std::chrono::seconds( 15 ),
            source_addresses );
    }

    std::string GeniusNode::MyTasksFilePath() const
    {
        return write_base_path_ + "/my_tasks.json";
    }

    void GeniusNode::LoadMyTaskIds()
    {
        my_task_ids_.clear();

        std::ifstream file( MyTasksFilePath() );
        if ( !file.is_open() )
        {
            return; // No existing file — first run or clean state
        }

        try
        {
            auto j = nlohmann::json::parse( file );
            if ( j.is_array() )
            {
                std::vector<std::string> all_ids;
                for ( const auto &item : j )
                {
                    if ( item.is_string() )
                    {
                        all_ids.push_back( item.get<std::string>() );
                    }
                }

                // Only keep the most recent entries in memory
                const size_t total = all_ids.size();
                const size_t keep  = std::min( total, kMyTasksMemoryLimit );
                for ( size_t i = total - keep; i < total; ++i )
                {
                    my_task_ids_.push_back( std::move( all_ids[i] ) );
                }

                node_logger_->info( "Loaded {} of {} task IDs from {}", my_task_ids_.size(), total, MyTasksFilePath() );
            }
        }
        catch ( const std::exception &e )
        {
            node_logger_->warn( "Failed to parse {}: {}", MyTasksFilePath(), e.what() );
        }
    }

    void GeniusNode::PersistMyTaskIds()
    {
        try
        {
            // Append the newest entry so the on-disk file retains full history
            const std::string &newest = my_task_ids_.back();

            // Read existing array, append, rewrite
            nlohmann::json j = nlohmann::json::array();
            {
                std::ifstream in( MyTasksFilePath() );
                if ( in.is_open() )
                {
                    try
                    {
                        auto existing = nlohmann::json::parse( in );
                        if ( existing.is_array() )
                        {
                            j = std::move( existing );
                        }
                    }
                    catch ( ... )
                    {
                        // Corrupt or empty — start fresh
                    }
                }
            }

            // Avoid duplicates (shouldn't happen, but be safe)
            bool already_present = false;
            for ( const auto &item : j )
            {
                if ( item.is_string() && item.get<std::string>() == newest )
                {
                    already_present = true;
                    break;
                }
            }
            if ( !already_present )
            {
                j.push_back( newest );
            }

            std::ofstream file( MyTasksFilePath() );
            if ( file.is_open() )
            {
                file << j.dump( 2 );
            }
        }
        catch ( const std::exception &e )
        {
            node_logger_->warn( "Failed to persist task IDs to {}: {}", MyTasksFilePath(), e.what() );
        }
    }
}
