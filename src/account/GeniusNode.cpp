/**
 * @file       GeniusNode.cpp
 * @brief
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <chrono>
#include <algorithm>
#include <future>
#include <stdexcept>
#include <thread>
#include <memory>
#include <random>
#include <cctype>
#include <filesystem>
#include <set>
#include <string_view>

#include <boost/asio/post.hpp>
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
#include "account/TrustStartupController.hpp"
#include "securecrdt/SecureCrdt.hpp"
#include "securecrdt/QuorumThresholdValidation.hpp"
#include "trustedpeer/TrustStateStore.hpp"
#include "trustedpeer/TrustedPeerRegistry.hpp"
#include "networkregistry/NetworkMembershipFilter.hpp"
#include "networkregistry/NetworkRegistry.hpp"
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
            case State::WAITING_FOR_TRUST_GENESIS:
                return "WAITING_FOR_TRUST_GENESIS";
            case State::WAITING_FOR_BURN_GENESIS:
                return "WAITING_FOR_BURN_GENESIS";
            case State::FATAL_TRUST_MISMATCH:
                return "FATAL_TRUST_MISMATCH";
            case State::READY:
                return "READY";
        }
        return "UNKNOWN";
    }
    // NodeTypeFromString lives in account/NodeType.hpp (moved there so lower layers
    // can parse node roles without including this facade).

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

    outcome::result<void> GeniusNode::WriteNetworkConfig( const std::string              &base_path,
                                                          uint16_t                        port_seed,
                                                          bool                            auto_dht,
                                                          const std::string              &network_key,
                                                          const std::string              &private_network_id,
                                                          const std::vector<std::string> &network_bootstrap_peers )
    {
        std::error_code ec;
        std::filesystem::create_directories( base_path, ec ); // ofstream can't create dirs; ensure parent exists
        std::ofstream ofs( base_path + "/network_config.json" );
        if ( !ofs.good() )
        {
            return Error::DATABASE_WRITE_ERROR;
        }
        // upnp_enabled is intentionally forced false here: this helper writes the
        // network_config.json used by tests/examples (see class doc comment), which must
        // not depend on real host-LAN UPnP/SSDP discovery. Without this, GeniusNode
        // defaults upnp_enabled to true and every node performs real, blocking, non-
        // deterministic UPnP network I/O during construction (see multi-node-crdt-
        // instability debug session for the crash this caused).
        ofs << "{ \"port_seed\": " << port_seed << ", \"auto_dht\": " << ( auto_dht ? "true" : "false" )
            << ", \"upnp_enabled\": false";
        if ( !network_key.empty() )
        {
            // Full JSON string escaping so any of the accepted PSK encodings survives a
            // round-trip through the JSON config file. The canonical go-ipfs swarm-key text
            // ("/key/swarm/psk/1.0.0/\n/base16/<64 hex>\n") contains literal newline bytes,
            // which are illegal raw inside JSON strings (CR-01): every control character is
            // escaped here so the written file is always parseable.
            std::string escaped;
            escaped.reserve( network_key.size() );
            for ( char c : network_key )
            {
                switch ( c )
                {
                    case '\\':
                        escaped += "\\\\";
                        break;
                    case '"':
                        escaped += "\\\"";
                        break;
                    case '\n':
                        escaped += "\\n";
                        break;
                    case '\r':
                        escaped += "\\r";
                        break;
                    case '\t':
                        escaped += "\\t";
                        break;
                    case '\b':
                        escaped += "\\b";
                        break;
                    case '\f':
                        escaped += "\\f";
                        break;
                    default:
                        if ( static_cast<unsigned char>( c ) < 0x20 )
                        {
                            escaped += fmt::format( "\\u{:04x}", static_cast<unsigned>( static_cast<unsigned char>( c ) ) );
                        }
                        else
                        {
                            escaped += c;
                        }
                        break;
                }
            }
            ofs << ", \"network_key\": \"" << escaped << "\"";
        }
        if ( !private_network_id.empty() )
        {
            // Plain string: the id is 0x-prefixed hex (no characters JSON would escape).
            ofs << ", \"private_network_id\": \"" << private_network_id << "\"";
        }
        if ( !network_bootstrap_peers.empty() )
        {
            ofs << ", \"network_bootstrap_peers\": [";
            bool first = true;
            for ( const auto &peer : network_bootstrap_peers )
            {
                if ( !first )
                {
                    ofs << ", ";
                }
                first = false;
                ofs << "\"" << peer << "\"";
            }
            ofs << "]";
        }
        ofs << " }";
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
    // AFTER LoadSgnsConfig() resolves node_type_ (the init-order hinge fix).
    // account_ and node_type_ are default-init here (no source/param) and assigned in the
    // body; autodht_ defaults to true (Phase-1 config layer overrides from network_config.json).
    // Throws on account-restore failure; New(dev_config, AccountSource) catches -> nullptr (D-04).
    GeniusNode::GeniusNode( const GeniusNodeConfig &dev_config, AccountSource source ) :
        write_base_path_( dev_config.BaseWritePath ),
        io_( std::make_shared<boost::asio::io_context>() ),
        io_work_guard_( boost::asio::make_work_guard( *io_ ) ),
        scheduler_( std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( io_ ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } ) ),
        generator_( std::make_shared<ipfs_lite::ipfs::graphsync::RequestIdGenerator>() ),
        autodht_( true ),
        isprocessor_( true ),
        dev_config_( dev_config ),
        processing_channel_topic_( std::string( PROCESSING_CHANNEL ) ),
        processing_grid_chanel_topic_( std::string( PROCESSING_GRID_CHANNEL ) ),
        m_lastApiCall( std::chrono::system_clock::now() - MIN_API_CALL_INTERVAL )
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

        // Create the account with node_type_ already resolved (the hinge fix).
        account_ = std::visit(
            [this]( auto &&src ) -> std::shared_ptr<GeniusAccount>
            {
                using T = std::decay_t<decltype( src )>;
                if constexpr ( std::is_same_v<T, NewAccount> )
                {
                    return GeniusAccount::New( dev_config_.TokenID, write_base_path_ );
                }
                else if constexpr ( std::is_same_v<T, FromPrivateKey> )
                {
                    return GeniusAccount::NewFromPrivateKey( dev_config_.TokenID,
                                                             src.eth_private_key.c_str(),
                                                             write_base_path_ );
                }
                else if constexpr ( std::is_same_v<T, FromMnemonic> )
                {
                    return GeniusAccount::NewFromMnemonic( dev_config_.TokenID, src.mnemonic, write_base_path_ );
                }
                else if constexpr ( std::is_same_v<T, FromPublicKey> )
                {
                    // FromPublicKey carries a public_address; GeniusAccount::NewFromPublicKey
                    // takes no base_path and consumes an address-like string_view.
                    return GeniusAccount::NewFromPublicKey( dev_config_.TokenID, src.public_address );
                }
            },
            source );
        if ( !account_ )
        {
            throw std::runtime_error( "Account creation failed" ); // D-04: New() catches -> nullptr
        }

        // Default port_seed (40001); Phase-1 config layer overrides from network_config.json when present.
        if ( !InitNetwork( 40001, node_type_ ) )
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
        // node_type read (CFG-02 / CONTEXT D-02). The resolved node_type_ is the single
        // source of truth for the node role; consumers take it directly.
        if ( config_json.HasMember( "node_type" ) && config_json["node_type"].IsString() )
        {
            const auto parsed = NodeTypeFromString( config_json["node_type"].GetString() );
            if ( parsed )
            {
                node_type_ = *parsed;
                node_logger_->info( "sgns_config.json: node_type={}", node_type_ );
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
        // An Archive is a passive replica: it stores everything and does no work. Processing is
        // resolved here rather than at StartProcessing() so the role is honoured once,
        // and so the log states plainly that the config key was overridden rather than ignored.
        if ( node_type_ == NodeType::Archive && isprocessor_ )
        {
            isprocessor_ = false;
            node_logger_->info( "Archive node: forcing is_processor=false (archives do not process)" );
        }
        // Mirroring is how an archive accumulates results it did not produce, so it is the
        // default for that role; an explicit mirror_results key still wins.
        if ( node_type_ == NodeType::Archive && !config_json.HasMember( "mirror_results" ) )
        {
            mirror_results_ = true;
            node_logger_->info( "Archive node: defaulting mirror_results=true" );
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
        const auto majority_floor = static_cast<uint64_t>( trusted_peers_genesis_.size() / 2 + 1 );
        const auto burn_floor     = static_cast<uint64_t>( trusted_peers_genesis_.size() -
                                                       trusted_peers_genesis_.size() / 3 );
        if ( trusted_peer_quorum_threshold_ == 0 )
        {
            trusted_peer_quorum_threshold_ = majority_floor;
        }
        if ( burn_config_quorum_threshold_ == 0 )
        {
            burn_config_quorum_threshold_ = burn_floor;
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
        std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
        // Shutdown gate: pending blockchain/migration retry threads must not
        // restart services once node destruction has begun.
        if ( shutdown_started_.load() )
        {
            node_logger_->debug( "Ignoring transition to {}, shutdown in progress", next_state );
            return;
        }
        if ( transition_in_progress_.has_value() && transition_in_progress_.value() == next_state )
        {
            node_logger_->debug( "Suppressing re-entrant transition to state {}",
                                 NodeStateToString( next_state ) );
            return;
        }

        const auto previous_transition = transition_in_progress_;
        transition_in_progress_        = next_state;
        ++transition_epoch_;
        state_.store( next_state );
        node_logger_->debug( "Transitioning to state {}", next_state );

        try
        {
            [&]
            {
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
                tx_globaldb_->AddListenTopic( ScopedProcessingChannel() );
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
                                        current_state );
                                    return;
                                }
                                strong->node_logger_->debug(
                                    "Blockchain started successfully, starting transaction manager" );
                                if ( ReplicatesAllAccounts( strong->node_type_ ) )
                                {
                                    strong->node_logger_->debug(
                                        "{} node: Setting blockchain to grab other account creation blocks",
                                        strong->node_type_ );
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
                                                    current_state );
                                                return;
                                            }
                                            strong->StateTransition( NodeState::INITIALIZING_TRANSACTIONS );
                                        }
                                    } );
                            }
                        },
                        node_type_,
                        // Scope this blockchain's validator consensus to the private
                        // network (empty on public nodes keeps public identifiers).
                        private_network_id_ );
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

                if ( !secure_crdt_ )
                {
                    secure_crdt_ = std::make_shared<sgns::securecrdt::SecureCrdt>( tx_globaldb_, quorum_topic );
                }

                const std::string trust_path = write_base_path_ + gnus_network_full_path_ + "/trust-state";
                if ( !trust_state_store_ )
                {
                    auto opened = sgns::trustedpeer::TrustStateStore::Open( trust_path, subnet_id_ );
                    if ( opened.has_value() )
                    {
                        trust_state_store_ = opened.value();
                    }
                }
                const bool has_configured_trust = !trusted_peers_genesis_.empty() ||
                                                  !bootstrapper_node_address_.empty();
                bool has_persisted_trust = false;
                if ( trust_state_store_ )
                {
                    auto persisted      = trust_state_store_->LoadAndVerify();
                    has_persisted_trust = persisted.has_value() ||
                                          persisted.error() != sgns::trustedpeer::TrustStateStore::Error::NOT_FOUND;
                }

                if ( ( has_configured_trust || has_persisted_trust ) && !trust_startup_controller_ )
                {
                    std::optional<sgns::trustedpeer::GenesisManifest> manifest;
                    if ( has_configured_trust )
                    {
                        sgns::trustedpeer::GenesisManifest configured;
                        configured.network_id              = subnet_id_;
                        configured.bootstrapper_public_key = bootstrapper_node_address_;
                        configured.peers                   = trusted_peers_genesis_;
                        configured.membership_threshold    = trusted_peer_quorum_threshold_;
                        configured.burn_threshold          = burn_config_quorum_threshold_;
                        manifest                           = std::move( configured );
                    }
                    if ( !trust_signer_ )
                    {
                        trust_signer_ = std::make_shared<const NodeTrustSigner>(
                            NodeTrustSigner{ account_->GetAddress(), account_ } );
                    }
                    const auto trust_signer = trust_signer_;
                    auto created = sgns::account::TrustStartupController::New(
                        secure_crdt_,
                        trust_state_store_,
                        std::move( manifest ),
                        trust_signer->address,
                        [trust_signer]( const std::vector<uint8_t> &bytes ) { return trust_signer->Sign( bytes ); },
                        [logger = node_logger_]( const sgns::account::TrustStartupController::Event &event )
                        {
                            const char *code = "TRUST_CONFIG_CONFLICT";
                            switch ( event.code )
                            {
                                case sgns::account::TrustStartupController::EventCode::TRUST_NETWORK_MISMATCH:
                                    code = "TRUST_NETWORK_MISMATCH";
                                    break;
                                case sgns::account::TrustStartupController::EventCode::TRUST_LOCAL_STATE_CORRUPT:
                                    code = "TRUST_LOCAL_STATE_CORRUPT";
                                    break;
                                case sgns::account::TrustStartupController::EventCode::TRUST_CRDT_MISSING:
                                    code = "TRUST_CRDT_MISSING";
                                    break;
                                case sgns::account::TrustStartupController::EventCode::TRUST_CRDT_ROLLBACK:
                                    code = "TRUST_CRDT_ROLLBACK";
                                    break;
                                case sgns::account::TrustStartupController::EventCode::TRUST_CRDT_FORK:
                                    code = "TRUST_CRDT_FORK";
                                    break;
                                case sgns::account::TrustStartupController::EventCode::TRUST_ACTIVATION_FAILED:
                                    code = "TRUST_ACTIVATION_FAILED";
                                    break;
                                case sgns::account::TrustStartupController::EventCode::TRUST_REFRESH_RETRY_SCHEDULED:
                                    code = "TRUST_REFRESH_RETRY_SCHEDULED";
                                    break;
                                case sgns::account::TrustStartupController::EventCode::TRUST_REFRESH_RETRY_EXHAUSTED:
                                    code = "TRUST_REFRESH_RETRY_EXHAUSTED";
                                    break;
                                case sgns::account::TrustStartupController::EventCode::TRUST_CONFIG_CONFLICT:
                                    break;
                            }
                            logger->critical( "{} fingerprint={} fields={}",
                                              code,
                                              event.persisted_fingerprint,
                                              fmt::join( event.fields, "," ) );
                        },
                        [weak_self = weak_from_this()]( sgns::account::TrustStartupController::State state )
                        {
                            auto self = weak_self.lock();
                            if ( !self )
                            {
                                return;
                            }

                            NodeState target_state = NodeState::FATAL_TRUST_MISMATCH;
                            if ( state == sgns::account::TrustStartupController::State::ConfirmedReady )
                            {
                                target_state = NodeState::INITIALIZING_TRANSACTIONS;
                            }
                            else if ( state ==
                                      sgns::account::TrustStartupController::State::WaitingForInitialBurn )
                            {
                                target_state = NodeState::WAITING_FOR_BURN_GENESIS;
                            }
                            else if ( state ==
                                      sgns::account::TrustStartupController::State::FreshWaitingForGenesis )
                            {
                                target_state = NodeState::WAITING_FOR_TRUST_GENESIS;
                            }

                            uint64_t  captured_epoch;
                            NodeState source_state;
                            {
                                std::lock_guard<std::recursive_mutex> lifecycle_lock( self->lifecycle_mutex_ );
                                source_state = self->state_.load();
                                if ( target_state == NodeState::INITIALIZING_TRANSACTIONS &&
                                     source_state != NodeState::WAITING_FOR_TRUST_GENESIS &&
                                     source_state != NodeState::WAITING_FOR_BURN_GENESIS )
                                {
                                    self->node_logger_->debug(
                                        "Ignoring trust-ready transaction initialization from state {}",
                                        NodeStateToString( source_state ) );
                                    return;
                                }
                                captured_epoch = self->transition_epoch_;
                            }

                            boost::asio::post(
                                *self->io_,
                                [weak_self, captured_epoch, source_state, target_state]
                                {
                                    auto node = weak_self.lock();
                                    if ( !node )
                                    {
                                        return;
                                    }

                                    std::lock_guard<std::recursive_mutex> lifecycle_lock( node->lifecycle_mutex_ );
                                    if ( node->transition_epoch_ != captured_epoch ||
                                         node->state_.load() != source_state )
                                    {
                                        node->node_logger_->debug(
                                            "Suppressing stale trust transition from {} to {}",
                                            NodeStateToString( source_state ),
                                            NodeStateToString( target_state ) );
                                        return;
                                    }
                                    node->StateTransition( target_state );
                                } );
                        } );
                    if ( created.has_error() )
                    {
                        node_logger_->critical( "Trust startup failed closed: {}", created.error().message() );
                        StateTransition( NodeState::FATAL_TRUST_MISMATCH );
                        return;
                    }
                    trust_startup_controller_ = created.value();
                    trusted_peer_registry_    = trust_startup_controller_->registry();
                    burn_config_              = trust_startup_controller_->burn_config();
                }

                if ( trust_startup_controller_ )
                {
                    auto refreshed = trust_startup_controller_->Refresh();
                    if ( refreshed.has_error() )
                    {
                        StateTransition( NodeState::FATAL_TRUST_MISMATCH );
                        return;
                    }
                    if ( !trust_startup_controller_->IsEconomicallyReady() )
                    {
                        StateTransition( trust_startup_controller_->GetState() ==
                                                 sgns::account::TrustStartupController::State::FreshWaitingForGenesis
                                             ? NodeState::WAITING_FOR_TRUST_GENESIS
                                             : NodeState::WAITING_FOR_BURN_GENESIS );
                        return;
                    }
                }
                else
                {
                    auto tpr_result = sgns::trustedpeer::TrustedPeerRegistry::New( secure_crdt_,
                                                                                   trusted_peers_genesis_,
                                                                                   bootstrapper_node_address_,
                                                                                   trusted_peer_quorum_threshold_ );
                    if ( tpr_result.has_error() )
                    {
                        node_logger_->error( "TrustedPeerRegistry construction failed (majority-floor violation): {}",
                                             tpr_result.error().message() );
                        secure_crdt_.reset();
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
                        ShutdownNodePolicyServices();
                        return;
                    }
                    burn_config_ = burn_config_result.value();

                    // Register only after both policy owners have populated SecureCrdtRegistry;
                    // otherwise a new node can start without filters for either runtime key.
                    if ( !secure_crdt_->RegisterFilters() )
                    {
                        node_logger_->error( "SecureCrdt filter registration failed" );
                        ShutdownNodePolicyServices();
                        return;
                    }
                }

                // D-06/D-07 (15-05): a node provisioned with a private_network_id constructs
                // its per-network membership registry once the quorum trio is live. The
                // registry (registered with SecureCrdtRegistry under "network-registry/<id>"
                // inside New) is this network's PeerRegistry; its cached membership is the
                // authorization state for the private network. Public nodes (empty
                // private_network_id_) construct nothing here. Construction is FAIL-CLOSED: a
                // private-network node whose membership authority cannot be established (for
                // example, empty network_bootstrap_peers below the strict-majority quorum
                // floor) must not start as if network enforcement were active. Only the
                // PUBLIC private_network_id_ and the membership SIZE are ever logged (D-03).
                if ( !private_network_id_.empty() && !network_registry_ )
                {
                    const auto network_quorum_floor =
                        sgns::securecrdt::StrictMajorityQuorumFloor( network_bootstrap_peers_.size() );
                    // The trailing arguments enable the registry's live cache refresh:
                    // with tx_globaldb_ passed as global_db, membership changes
                    // replicated through the network-registry CRDT key refresh the
                    // cached PeerId set without a restart (BurnConfig pattern; the
                    // 15-09-fixed refresh loop wakes per notification and never spins).
                    auto network_registry_result = sgns::networkregistry::NetworkRegistry::New(
                        secure_crdt_,
                        trusted_peer_registry_,
                        private_network_id_,
                        network_bootstrap_peers_,
                        network_quorum_floor,
                        /*initial_network_signers=*/{},
                        /*pnet_key_fingerprint=*/{},
                        tx_globaldb_ );
                    if ( network_registry_result.has_error() )
                    {
                        node_logger_->error(
                            "NetworkRegistry construction failed for private network {} with {} bootstrap "
                            "peers (quorum floor {}): {} - private-network membership is not provisioned; "
                            "failing closed",
                            private_network_id_,
                            network_bootstrap_peers_.size(),
                            network_quorum_floor,
                            network_registry_result.error().message() );
                        ShutdownNodePolicyServices();
                        return;
                    }
                    network_registry_ = network_registry_result.value();

                    // D-07 (15-12) enforcement posture: pnet proves PSK possession at
                    // the transport; THIS filter is the identity/membership decision —
                    // the registry's cached membership authorizes every inbound gossip
                    // message before it enters CRDT replication (application-layer
                    // ingest gate per the owner direction, deferred-items.md §3). The
                    // direct processing-path channels (grid/results/queue) receive the
                    // same filter in the processing-path gate plan (15-13).
                    if ( tx_globaldb_ && tx_globaldb_->GetBroadcaster() )
                    {
                        tx_globaldb_->GetBroadcaster()->SetMembershipFilter(
                            sgns::networkregistry::MakeNetworkMembershipFilter( network_registry_ ) );
                        node_logger_->info( "Gossip ingest membership filtering active for private network {}",
                                            private_network_id_ );
                    }
                    node_logger_->info( "NetworkRegistry active for private network {} (bootstrap membership: "
                                        "{} peers)",
                                        private_network_id_,
                                        network_registry_->GetCurrentPeers().size() );
                }

                // A replacement must not register the same GlobalDB/account patterns until the
                // previous owner has stopped and its destructor has removed those callbacks.
                ReleaseTransactionManagerOwnership();
                transaction_manager_ = TransactionManager::New( tx_globaldb_,
                                                                io_,
                                                                account_,
                                                                blockchain_,
                                                                node_type_,
                                                                subnet_id_,
                                                                std::chrono::milliseconds( 300000 ),
                                                                std::chrono::milliseconds( 0 ),
                                                                burn_config_->GetCachedBasisPoints(),
                                                                burn_config_->GetConfirmedValueProvider() );
                if ( !transaction_manager_ )
                {
                    node_logger_->error( "TransactionManager construction failed" );
                    return;
                }

                ++transaction_manager_construction_count_;
                if ( account_service_generation_ == 0 )
                {
                    account_service_generation_ = 1;
                }
                const auto owner_generation = account_service_generation_;
                transaction_manager_owner_generation_.store( owner_generation );
                account_transaction_callback_owner_generation_.store( owner_generation );
                catchup_callback_owner_generation_.store( owner_generation );
                auto manager = transaction_manager_;
                // The replacement is now a complete account/manager pair.  This
                // is the sole publication point for a switching generation.
                account_service_switching_ = false;

                transaction_manager_->RegisterStateChangeCallback(
                    [weak_self = weak_from_this(),
                     weak_manager = std::weak_ptr<TransactionManager>( manager ),
                     owner_generation]( TransactionManager::State old_state, TransactionManager::State new_state )
                    {
                        if ( auto strong = weak_self.lock() )
                        {
                            auto callback_manager = weak_manager.lock();
                            const auto snapshot = strong->SnapshotAccountServices();
                            if ( !callback_manager || snapshot.generation != owner_generation ||
                                 snapshot.manager.get() != callback_manager.get() )
                            {
                                return;
                            }
                            strong->TransactionStateChanged( old_state, new_state );
                        }
                    } );
                transaction_manager_->Start();
                ++transaction_manager_start_count_;
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
                     weak_self                = weak_from_this(),
                     owner_generation,
                     logger                   = node_logger_]( sgns::ConsensusVote          &vote,
                                                               const sgns::ConsensusSubject &subject )
                    {
                        auto self = weak_self.lock();
                        auto transaction_manager = weak_transaction_manager.lock();
                        const auto snapshot = self ? self->SnapshotAccountServices() : AccountServiceSnapshot{};
                        if ( !self || !transaction_manager || snapshot.generation != owner_generation ||
                             snapshot.manager.get() != transaction_manager.get() )
                        {
                            return;
                        }
                        auto &validator = transaction_manager->GetPublicChainInputValidator();

                        // #364: slots are populated ONLY from evidence recorded while
                        // verifying this exact claim. No evidence (claim not verified
                        // locally, evidence already consumed, or a different claim)
                        // means every slot abstains.
                        const auto claim_key = sgns::PublicChainInputValidator::ClaimKey( subject );
                        if ( !claim_key.has_value() )
                        {
                            logger->debug( "SlotHashPopulator: no claim key for subject; abstaining" );
                            return;
                        }

                        const auto evidence = validator.TakeEvidence( claim_key.value() );
                        if ( !evidence.has_value() )
                        {
                            logger->debug( "SlotHashPopulator: no verification evidence for claim={}; abstaining",
                                           claim_key.value().substr( 0, 8 ) );
                            return;
                        }

                        const auto slot0 = evidence->SlotHash( 0 );
                        const auto slot1 = evidence->SlotHash( 1 );
                        const auto slot2 = evidence->SlotHash( 2 );
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

                        logger->debug( "SlotHashPopulator: claim={} weight={} slot0={} slot1={} slot2={}",
                                       claim_key.value().substr( 0, 8 ),
                                       evidence->successful_weight,
                                       !slot0.empty(),
                                       !slot1.empty(),
                                       !slot2.empty() );
                    } );
                blockchain_slot_hash_owner_generation_.store( owner_generation );

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
            case NodeState::WAITING_FOR_TRUST_GENESIS:
            case NodeState::WAITING_FOR_BURN_GENESIS:
            case NodeState::FATAL_TRUST_MISMATCH:
                break;
                    case NodeState::CREATING:
                    default:
                        break;
                }
            }();
        }
        catch ( ... )
        {
            transition_in_progress_ = previous_transition;
            throw;
        }
        transition_in_progress_ = previous_transition;
    }

    bool GeniusNode::IsTrustEconomicallyReady() const
    {
        return trust_startup_controller_ ? trust_startup_controller_->IsEconomicallyReady()
                                         : burn_config_ && burn_config_->IsEconomicallyReady();
    }

    bool GeniusNode::CanApproveTrustSuccessors() const
    {
        return trust_startup_controller_ && trust_startup_controller_->CanApproveSuccessors();
    }

    std::vector<std::string> GeniusNode::GetCurrentTrustedPeers() const
    {
        return trust_startup_controller_ ? trust_startup_controller_->GetCurrentPeers()
                                         : ( trusted_peer_registry_ ? trusted_peer_registry_->GetCurrentPeers()
                                                                    : std::vector<std::string>{} );
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
        auto loggerGeniusSigner     = ConfigureLogger( "GeniusSigner", logdir, spdlog::level::err );
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
        auto loggerGeniusSigner     = ConfigureLogger( "GeniusSigner", logdir, spdlog::level::err );
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

    GeniusNode::NodeType GeniusNode::GetNodeType() const noexcept
    {
        return node_type_;
    }

    bool GeniusNode::IsProcessor() const noexcept
    {
        return isprocessor_;
    }

    GeniusNode::NetworkSettings GeniusNode::LoadNetworkConfig( uint16_t port_seed, NodeType node_type )
    {
        NetworkSettings settings;
        settings.port_seed = port_seed;
        // Replicating roles (Full, Archive) carry network-wide traffic and need the higher water marks.
        const bool replicates = ReplicatesAllAccounts( node_type );
        settings.high_water   = replicates ? 400 : 300;
        settings.low_water    = replicates ? 200 : 150;

        bootstrap_peers_.clear();

        std::ifstream config_file( write_base_path_ + "/network_config.json" );
        if ( !config_file.good() )
        {
            // Deliberate distinction (D-01): a MISSING file is the documented public-node
            // provisioning state (absent identity keys -> public defaults), while a
            // PRESENT-but-corrupt file below is a provisioning failure and is fatal.
            GeniusNodeLogger()->error( "Could not read network config file" );
            return settings;
        }
        std::stringstream buffer;
        buffer << config_file.rdbuf();

        rapidjson::Document config_json;
        config_json.Parse( buffer.str().c_str() );
        if ( config_json.HasParseError() || !config_json.IsObject() )
        {
            // Fail closed (WR-01): an unparseable existing config must abort the load, never
            // silently boot public defaults - identity validation below only runs after a
            // successful parse. Only the file and condition are named, never key values (D-03).
            GeniusNodeLogger()->error( "network_config.json is unreadable or invalid JSON - refusing to start" );
            settings.valid = false;
            return settings;
        }

        // Optional-key reader: applies the value only when the key exists with the type its
        // destination implies, so an absent or ill-typed key keeps whatever default was passed in.
        // FindMember resolves the key in one lookup, unlike HasMember followed by operator[].
        auto read = [&]( const char *key, auto &out )
        {
            using T           = std::decay_t<decltype( out )>;
            const auto member = config_json.FindMember( key );
            if ( member == config_json.MemberEnd() )
            {
                return;
            }
            if constexpr ( std::is_same_v<T, double> )
            {
                // Accept any JSON number: Is<double> maps to IsDouble(), which rejects an
                // integer-valued literal, so "multiplier": 3 would be silently ignored.
                if ( member->value.IsNumber() )
                {
                    out = member->value.GetDouble();
                }
            }
            else if ( member->value.Is<T>() )
            {
                out = member->value.Get<T>();
            }
        };
        auto read_seconds = [&]( const char *key, std::chrono::seconds &out )
        {
            auto seconds = static_cast<int>( out.count() );
            read( key, seconds );
            out = std::chrono::seconds( seconds );
        };

        read( "pubsub_bind_address", settings.bind_address );
        read( "upnp_enabled", settings.upnp_enabled );
        read( "high_water", settings.high_water );
        read( "low_water", settings.low_water );
        read( "network_key", settings.network_key );
        read( "private_network_id", settings.private_network_id );

        // Offline-provisioned initial NetworkRegistry membership (D-01): same FindMember /
        // type-check array pattern as "bootstrap_addresses" below, but the entries land in the
        // returned settings because they are consumed only when private_network_id is set.
        if ( config_json.HasMember( "network_bootstrap_peers" ) && config_json["network_bootstrap_peers"].IsArray() )
        {
            for ( auto &v : config_json["network_bootstrap_peers"].GetArray() )
            {
                if ( v.IsString() )
                {
                    settings.network_bootstrap_peers.emplace_back( v.GetString() );
                }
            }
        }

        // D-01/D-02 identity validation (Task 2 encoding decision: 0x-hex-32B). The
        // private_network_id is the PUBLIC identity - 0x-prefixed hex of exactly 32 bytes
        // (66 characters) - and is intentionally distinct from the network_key secret.
        // Malformed ids are fatal (fail closed): this mirrors the warn-on-ill-typed divergence
        // precedent above but escalates to failure, because a misidentified "private" node
        // must not start. Only key names appear in errors; the network_key value is never
        // logged (D-03).
        if ( !settings.private_network_id.empty() )
        {
            const auto &id          = settings.private_network_id;
            const bool  well_formed = id.size() == 66 && id[0] == '0' && id[1] == 'x' &&
                                     std::all_of( id.begin() + 2,
                                                  id.end(),
                                                  []( char c )
                                                  { return std::isxdigit( static_cast<unsigned char>( c ) ) != 0; } );
            const bool all_zero = id.size() == 66 &&
                                  std::all_of( id.begin() + 2, id.end(), []( char c ) { return c == '0'; } );
            if ( !well_formed )
            {
                node_logger_->error( "network_config.json: private_network_id must be 0x-prefixed hex of exactly "
                                     "32 bytes (66 characters, 0x + 64 hex digits) - refusing to start" );
                settings.valid = false;
            }
            else if ( all_zero )
            {
                node_logger_->error( "network_config.json: private_network_id is all-zero, which is reserved for "
                                     "the public network - refusing to start" );
                settings.valid = false;
            }
        }
        // D-01 provisioning pair: the public identity and the pnet secret are provisioned
        // together or not at all. "network_key" without "private_network_id" would run a
        // PSK-isolated node writing private-intent data into PUBLIC CRDT paths (D-08 misroute);
        // "private_network_id" without "network_key" claims a private namespace with no
        // transport protection. Both-set and both-absent configs load exactly as before.
        const bool has_id  = !settings.private_network_id.empty();
        const bool has_key = !settings.network_key.empty();
        if ( settings.valid && has_id != has_key )
        {
            settings.valid = false;
            node_logger_->error( "network_config.json: half-provisioned private-network identity - \"{}\" is set "
                                 "but \"{}\" is missing; provision both keys or neither - refusing to start",
                                 has_key ? "network_key" : "private_network_id",
                                 has_key ? "private_network_id" : "network_key" );
        }

        std::string port_str;
        read( "pubsub_port", port_str );
        if ( !port_str.empty() )
        {
            try
            {
                settings.config_port = static_cast<uint16_t>( std::stoi( port_str ) );
            }
            catch ( ... )
            {
                node_logger_->warn( "Invalid pubsub_port in config, using default" );
            }
        }

        if ( config_json.HasMember( "bootstrap_addresses" ) && config_json["bootstrap_addresses"].IsArray() )
        {
            for ( auto &v : config_json["bootstrap_addresses"].GetArray() )
            {
                if ( v.IsString() )
                {
                    bootstrap_peers_.emplace_back( v.GetString() );
                }
            }
        }

        // port_seed is read numerically — an intentional divergence from the legacy string-based
        // pubsub_port read above (HARD-01 / CONTEXT D-08) — and auto_dht as a bool into autodht_
        // (D-07). Both warn on an ill-typed value rather than silently falling back, because the
        // constructor param they override is not otherwise visible to the operator.
        if ( config_json.HasMember( "port_seed" ) )
        {
            if ( config_json["port_seed"].IsUint() )
            {
                settings.port_seed = static_cast<uint16_t>( config_json["port_seed"].GetUint() );
                node_logger_->info( "network_config.json: port_seed overridden to {}", settings.port_seed );
            }
            else
            {
                node_logger_->warn( "network_config.json: port_seed is not a uint, using default/param {}",
                                    settings.port_seed );
            }
        }
        if ( config_json.HasMember( "auto_dht" ) )
        {
            if ( config_json["auto_dht"].IsBool() )
            {
                autodht_ = config_json["auto_dht"].GetBool();
                node_logger_->info( "network_config.json: auto_dht overridden to {}", autodht_ );
            }
            else
            {
                node_logger_->warn( "network_config.json: auto_dht is not a bool, using default/param {}", autodht_ );
            }
        }

        read_seconds( "bootstrap_reconnect_base_delay_sec", reconnect_config_.base_delay );
        read_seconds( "bootstrap_reconnect_max_delay_sec", reconnect_config_.max_delay );
        read_seconds( "bootstrap_health_check_interval_sec", reconnect_config_.health_check_interval );
        read_seconds( "bootstrap_health_check_disconnected_interval_sec",
                      reconnect_config_.health_check_disconnected_interval );
        read( "bootstrap_background_multiplier", reconnect_config_.background_multiplier );

        return settings;
    }

    GeniusNode::BootstrapPeers GeniusNode::ParseBootstrapPeers( const std::vector<std::string> &addresses,
                                                                std::string_view                kind ) const
    {
        BootstrapPeers parsed;
        for ( const auto &addr : addresses )
        {
            auto peer_info = ParsePeerInfoFromString( addr );
            if ( !peer_info )
            {
                node_logger_->warn( "Failed to parse bootstrap {} multiaddr: {}", kind, addr );
                continue;
            }
            parsed.infos.push_back( peer_info.value() );
            parsed.ids.insert( peer_info->id );
        }
        if ( !parsed.infos.empty() )
        {
            node_logger_->info( "Parsed {} bootstrap {}(s) for reconnection tracking", parsed.infos.size(), kind );
        }
        return parsed;
    }

    bool GeniusNode::AdoptEphemeralPort( const std::string &interface_address )
    {
        auto address = libp2p::multi::Multiaddress::create( interface_address );
        if ( !address )
        {
            return false;
        }
        auto assigned_port = address.value().getFirstValueForProtocol<uint16_t>(
            libp2p::multi::Protocol::Code::TCP,
            []( const std::string &value ) { return static_cast<uint16_t>( std::stoul( value ) ); } );
        if ( !assigned_port )
        {
            return false;
        }
        pubsubport_ = assigned_port.value();
        return pubsubport_ != 0;
    }

    bool GeniusNode::StartPubSub( const NetworkSettings &settings )
    {
        // Make a base58 out of our address
        const std::string   address = account_->GetAddress();
        const base::Hash256 hash    = crypto::sha2_256( address.data(), address.size() );

        auto key          = libp2p::multi::ContentIdentifierCodec::encodeCIDV0( hash.data(), hash.size() );
        auto acc_cid      = libp2p::multi::ContentIdentifierCodec::decode( key );
        auto maybe_base58 = libp2p::multi::ContentIdentifierCodec::toString( acc_cid.value() );
        if ( !maybe_base58 )
        {
            node_logger_->error( "We couldn't convert the account {} to base58", address );
            return false;
        }
        base58key_              = maybe_base58.value();
        gnus_network_full_path_ = std::string( GNUS_NETWORK_PATH ) + version::GetNetAndVersionAppendix() + base58key_;

        //Set a pubsub config, use no signing because we can verify with proof and dag structure
        libp2p::protocol::gossip::Config config;
        config.echo_forward_mode       = false;
        config.sign_messages           = false;
        config.seen_cache_limit        = 10;
        config.heartbeat_interval_msec = std::chrono::milliseconds{ 500 };
        config.rw_timeout_msec         = std::chrono::seconds{ 30 };

        auto keypair = crdt::KeyPairFileStorage( write_base_path_ + gnus_network_full_path_ + "/pubs_processor" )
                           .GetKeyPair()
                           .value();

        // A non-empty network key puts PubSub in private-network (pnet) mode: every
        // connection passes the PSK boundary on both dial and accept paths. The pnet
        // constructor validates the key eagerly and throws PskValidationError on bad
        // key material; StartPubSub reports that as a plain init failure.
        if ( settings.network_key.empty() )
        {
            pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>( std::move( keypair ), config );
        }
        else
        {
            try
            {
                pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>( std::move( keypair ),
                                                                       config,
                                                                       settings.network_key );
            }
            catch ( const std::exception &e )
            {
                node_logger_->error( "Private-network (pnet) initialization failed: {}", e.what() );
                return false;
            }
        }

        // A half-started PubSub must not be left reachable, so every failure tears it down.
        auto fail = [this]( const std::string &message )
        {
            node_logger_->error( "{}", message );
            pubsub_->Stop();
            pubsub_.reset();
            return false;
        };

        auto pubs = pubsub_->Start( pubsubport_, bootstrap_peers_, settings.bind_address, {} );
        if ( auto pubsub_start_error = pubs.get(); pubsub_start_error )
        {
            return fail( fmt::format( "PubSub failed to start on {}:{}: {}",
                                      settings.bind_address,
                                      pubsubport_,
                                      pubsub_start_error.message() ) );
        }

        const auto interface_address = pubsub_->GetInterfaceAddress();
        if ( interface_address.empty() )
        {
            return fail( fmt::format( "PubSub started without an interface address on {}:{}",
                                      settings.bind_address,
                                      pubsubport_ ) );
        }
        if ( pubsubport_ == 0 && !AdoptEphemeralPort( interface_address ) )
        {
            return fail( fmt::format( "PubSub did not report its OS-assigned TCP port: {}", interface_address ) );
        }
        node_logger_->info( "PubSub started at address: {}", interface_address );

        pubsub_->GetHost()->getConnectionManagerConfig().high_water = settings.high_water;
        pubsub_->GetHost()->getConnectionManagerConfig().low_water  = settings.low_water;
        return true;
    }

    void GeniusNode::InitContentExchange()
    {
        // Initialize Bitswap for IPFS content-addressed data exchange
        bitswap_event_bus_ = std::make_shared<libp2p::event::Bus>();
        bitswap_ = std::make_shared<sgns::ipfs_bitswap::Bitswap>( *pubsub_->GetHost(), *bitswap_event_bus_, io_ );
        bitswap_->initialize();
        if ( !ipfs_cache_dir_.empty() )
        {
            bitswap_->setCacheDir( write_base_path_ + "/" + ipfs_cache_dir_ );
        }
        FileManager::GetInstance().InitializeSingletons();
        FileManager::GetInstance().setBitswap( bitswap_ );

        graphsyncnetwork_ = std::make_shared<ipfs_lite::ipfs::graphsync::Network>( pubsub_->GetHost(), scheduler_ );
    }

    bool GeniusNode::InitNetwork( uint16_t port_seed, NodeType node_type )
    {
        const NetworkSettings settings = LoadNetworkConfig( port_seed, node_type );

        // Fail closed on fatal identity config divergences (malformed private_network_id or a
        // half-provisioned private_network_id/network_key pair); the reason was already logged
        // at the detection site, and no network side effects may run before this check.
        if ( !settings.valid )
        {
            return false;
        }

        auto fullnodes            = ParseBootstrapPeers( bootstrap_fullnodes_, "fullnode" );
        bootstrap_fullnode_infos_ = std::move( fullnodes.infos );
        bootstrap_fullnode_ids_   = std::move( fullnodes.ids );

        auto peers            = ParseBootstrapPeers( bootstrap_peers_, "peer" );
        bootstrap_peer_infos_ = std::move( peers.infos );
        bootstrap_peer_ids_   = std::move( peers.ids );

        // Port resolution priority (Doxygen: see InitNetwork declaration):
        //   1. pubsub_port (string override from network_config.json) -> settings.config_port
        //   2. else: port_seed (constructor param, or the network_config.json "port_seed" key)
        //      derives the port via GenerateRandomPort(port_seed, address); zero uses an
        //      OS-selected port because GossipPubSub cannot reliably start on zero.
        pubsubport_ = settings.config_port != 0 ? settings.config_port
                                                : GenerateRandomPort( settings.port_seed, account_->GetAddress() );

        // Remember the pnet key (if any) so it can be reported and re-applied consistently.
        network_key_ = settings.network_key;
        if ( !network_key_.empty() )
        {
            node_logger_->info( "network_config.json: private-network (pnet) mode enabled" );
        }

        // Retain the distinct public identity (D-02) and the offline-provisioned bootstrap
        // membership. Log the public id only - never the network_key value (D-03).
        private_network_id_      = settings.private_network_id;
        network_bootstrap_peers_ = settings.network_bootstrap_peers;
        if ( !private_network_id_.empty() )
        {
            node_logger_->info( "network_config.json: private-network identity: {}", private_network_id_ );
        }

        // Never block node construction on UPnP/IGD discovery.
        // RefreshUPNP() runs on its own thread and will try immediately.
        if ( settings.upnp_enabled )
        {
            (void) InitUPNP(); // Ignore UPNP init result for now
        }

        if ( !StartPubSub( settings ) )
        {
            return false;
        }

        if ( settings.upnp_enabled )
        {
            RefreshUPNP( pubsubport_ );
        }

        InitContentExchange();

        // Initialize DHT early so peer discovery works during database migration
        if ( autodht_ )
        {
            if ( DHTInit().has_failure() )
            {
                return false;
            }
        }
        return true;
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

        task_queue_ = processing::TaskQueueImpl::New( tx_globaldb_, ScopedProcessingChannel(), private_network_id_ );
        // Thread the private-network key into the per-subtask processing host so it gets
        // the same Noise-only + pnet enforcement as the gossip host (D-11). Public nodes
        // keep the defaulted argument and today's construction semantics.
        processing_core_ = processing::ProcessingCoreImpl::New( task_queue_, 1, dev_config_.TokenID, network_key_ );

        task_result_storage_ = std::make_shared<processing::SubTaskResultStorageImpl>( tx_globaldb_,
                                                                                       ScopedProcessingChannel(),
                                                                                       private_network_id_ );

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
                                                node_type_ );

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
        ++blockchain_retry_count_;
        std::thread(
            [weak_self = weak_from_this(), delay]
            {
                std::this_thread::sleep_for( delay );
                if ( auto strong = weak_self.lock() )
                {
                    if ( strong->shutdown_started_.load() )
                    {
                        return;
                    }
                    auto current_state = strong->state_.load();
                    if ( current_state != NodeState::INITIALIZING_BLOCKCHAIN )
                    {
                        strong->node_logger_->debug( "Skipping blockchain retry, unexpected state: {}", current_state );
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

    outcome::result<void> GeniusNode::ShutdownAccountBoundServices( bool deconfigure_account, bool release_members )
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

        if ( release_members )
        {
            ResetProcessingMembers();
            ReleaseTransactionManagerOwnership();
            blockchain_.reset();
        }
        else if ( transaction_manager_ )
        {
            transaction_manager_->Stop();
        }

        if ( deconfigure_account && account_ )
        {
            account_->DeconfigureDatabaseDependencies();
        }

        return outcome::success();
    }

    void GeniusNode::ReleaseTransactionManagerOwnership()
    {
        // Invalidate posted bridge initialization before releasing its observer and
        // weak TransactionManager target.
        ++bridge_init_generation_;
        rpc_endpoint_provider_.reset();
        bridge_relayer_.reset();
        eth_watch_service_.reset();

        auto previous_manager = std::move( transaction_manager_ );
        if ( !previous_manager )
        {
            account_transaction_callback_owner_generation_.store( 0 );
            blockchain_slot_hash_owner_generation_.store( 0 );
            return;
        }

        // These callbacks can re-enter GeniusNode or borrow the manager. Remove them
        // before Stop and before the manager destructor unregisters its GlobalDB and
        // account transaction-CID callbacks.
        previous_manager->UnregisterStateChangeCallback();
        if ( blockchain_ )
        {
            blockchain_->SetSlotHashPopulator( {} );
        }
        account_transaction_callback_owner_generation_.store( 0 );
        blockchain_slot_hash_owner_generation_.store( 0 );
        previous_manager->Stop();
        previous_manager.reset();
    }

    void GeniusNode::ShutdownNodePolicyServices()
    {
        // The controller owns candidate callbacks into SecureCrdt and retains both
        // policy services. Release it before unregistering those owners.
        trust_startup_controller_.reset();

        // 15-12: clear the gossip-ingest membership filter BEFORE releasing the
        // registry it consults. The weak_ptr-backed predicate would deny-all anyway
        // (fail-closed) once the registry is gone, but the explicit clear keeps the
        // post-teardown state clean and testable; it is a no-op when no filter was
        // ever installed (public node).
        if ( tx_globaldb_ && tx_globaldb_->GetBroadcaster() )
        {
            tx_globaldb_->GetBroadcaster()->ClearMembershipFilter();
        }

        // Unregister while the policy owners and their owner tokens are still alive.
        // Their destructors repeat this defensively, so partial initialization is safe.
        // NetworkRegistry first: it retains SecureCrdt and TrustedPeerRegistry, so it
        // must drop those references before the owners below are released (15-05).
        if ( network_registry_ )
        {
            network_registry_->Unregister();
        }
        if ( burn_config_ )
        {
            burn_config_->Unregister();
        }
        if ( trusted_peer_registry_ )
        {
            trusted_peer_registry_->Unregister();
        }

        // BurnConfig retains GlobalDB, TrustedPeerRegistry, SecureCrdt, and the
        // account. Release it first so those dependencies can actually drain.
        network_registry_.reset();
        burn_config_.reset();
        trusted_peer_registry_.reset();
        secure_crdt_.reset();
        trust_state_store_.reset();
    }

    // ReleaseRuntimeMembersAfterIoStopped() (phase-13) was superseded by the
    // ownership-order teardown: members are declared provider-first and destroyed
    // implicitly in reverse declaration order by ~GeniusNode; the phase-13
    // ShutdownNodePolicyServices() hook runs from ShutdownForDestruction instead.

    void GeniusNode::ShutdownForDestruction()
    {
        bool expected = false;
        if ( !shutdown_started_.compare_exchange_strong( expected, true ) )
        {
            return;
        }

        node_logger_->info( "GeniusNode shutdown start" );

        // Stop the catch-up watcher before tearing down account-bound services.
        // Only stop it here — destruction is implicit, in declaration order.
        if ( catchup_watcher_ )
        {
            catchup_watcher_->stopWatching();
        }

        // Cancel bootstrap health check timer
        if ( health_check_handle_ )
        {
            health_check_handle_->cancel();
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
        }

        // Stop and join the messenger worker before PubSub teardown: queued
        // blockchain nonce/UTXO tasks call GossipPubSub::getPeerCount, which
        // faults once PubSub::Stop() releases the gossip object.
        if ( account_ )
        {
            account_->StopMessenger();
        }

        // Stop and unregister account-bound work, but retain the owning objects
        // until the io_context has been stopped and all handlers have drained.
        auto services_shutdown = ShutdownAccountBoundServices( true, false );
        if ( services_shutdown.has_error() )
        {
            node_logger_->error( "GeniusNode shutdown account-bound services failed: {}",
                                 services_shutdown.error().message() );
        }
        ShutdownNodePolicyServices();
        if ( tx_globaldb_ )
        {
            tx_globaldb_->ShutdownNow();
        }

        if ( graphsyncnetwork_ )
        {
            node_logger_->debug( "GeniusNode shutdown: closing GraphSync peers before PubSub" );
            graphsyncnetwork_->stop( nullptr );
            node_logger_->debug( "GeniusNode shutdown: GraphSync peers closed" );
        }

        // FileManager is a process-wide singleton holding a copy of bitswap_ (set in
        // InitNetwork). Implicit destruction cannot reach it, so drop that copy here
        // or the service outlives this node.
        FileManager::GetInstance().clearBitswap( bitswap_ );

        node_logger_->info( "GeniusNode shutdown phase CRDT/GlobalDB complete" );
    }

    GeniusNode::~GeniusNode()
    {
        node_logger_->debug( "~GeniusNode CALLED" );

        ShutdownForDestruction();

        // GraphSync retains PubSub's libp2p host, whose sockets are backed by
        // PubSub's io_context. GossipPubSub::Stop() releases its own references
        // to both objects, so keep the context alive until GraphSync releases
        // the last host reference and destroys those sockets.
        if ( pubsub_ )
        {
            pubsub_context_keepalive_ = pubsub_->GetAsioContext();
        }

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

        // The runtime graph is now destroyed implicitly, in reverse declaration
        // order, after this body returns. See the ownership-order block in the
        // header: members are declared provider-first, so reverse destruction
        // tears down borrowers before the things they borrow.
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
                        auto       delay_sec = strong->reconnect_config_.base_delay.count() * ( 1ull << retry_attempt );
                        delay_sec            = std::min<uint64_t>(
                            delay_sec,
                            static_cast<uint64_t>( strong->reconnect_config_.max_delay.count() ) );
                        const auto delay = std::chrono::seconds( delay_sec );

                        strong->node_logger_->warn( "Failed to connect to peer {}: {}; retrying in {}s",
                                                    peer_info.id.toBase58(),
                                                    result.error().message(),
                                                    delay.count() );
                        strong->scheduler_->schedule(
                            [weak_self,
                             peer      = std::move( peer ),
                             peer_info = std::move( peer_info ),
                             attempt   = retry_attempt + 1]() mutable
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

    namespace
    {
        // Parses a base58 peer-id string; nullopt when pubsub_ is down or the id is malformed.
        std::optional<libp2p::peer::PeerId> ParsePeerId( const std::shared_ptr<ipfs_pubsub::GossipPubSub> &pubsub,
                                                         const std::string                                         &peer_id,
                                                         base::Logger                                               logger )
        {
            if ( !pubsub )
            {
                logger->warn( "Cannot manage peer deny list: PubSub is not running" );
                return std::nullopt;
            }
            auto parsed = libp2p::peer::PeerId::fromBase58( peer_id );
            if ( !parsed )
            {
                logger->warn( "Invalid peer id (expected base58): {}", peer_id );
                return std::nullopt;
            }
            return parsed.value();
        }
    } // namespace

    void GeniusNode::BlockPeer( const std::string &peer_id )
    {
        auto peer = ParsePeerId( pubsub_, peer_id, node_logger_ );
        if ( peer )
        {
            pubsub_->BlockPeer( peer.value() );
        }
    }

    void GeniusNode::BlockPeers( const std::vector<std::string> &peer_ids )
    {
        if ( !pubsub_ )
        {
            node_logger_->warn( "Cannot manage peer deny list: PubSub is not running" );
            return;
        }
        std::vector<libp2p::peer::PeerId> peers;
        peers.reserve( peer_ids.size() );
        for ( const auto &id : peer_ids )
        {
            if ( auto peer = ParsePeerId( pubsub_, id, node_logger_ ) )
            {
                peers.push_back( std::move( peer.value() ) );
            }
        }
        pubsub_->BlockPeers( peers );
    }

    void GeniusNode::UnblockPeer( const std::string &peer_id )
    {
        auto peer = ParsePeerId( pubsub_, peer_id, node_logger_ );
        if ( peer )
        {
            pubsub_->UnblockPeer( peer.value() );
        }
    }

    bool GeniusNode::IsPeerBlocked( const std::string &peer_id ) const
    {
        auto peer = ParsePeerId( pubsub_, peer_id, node_logger_ );
        return peer && pubsub_->IsPeerBlocked( peer.value() );
    }

    std::vector<std::string> GeniusNode::GetBlockedPeers() const
    {
        if ( !pubsub_ )
        {
            return {};
        }
        std::vector<std::string> result;
        for ( const auto &peer : pubsub_->GetBlockedPeers() )
        {
            result.push_back( peer.toBase58() );
        }
        return result;
    }

    std::string GeniusNode::ScopedProcessingChannel() const
    {
        return processing::TaskKeys::ScopedTopic( processing_channel_topic_, private_network_id_ );
    }

    std::string GeniusNode::ScopedProcessingGridChannel() const
    {
        return processing::TaskKeys::ScopedTopic( processing_grid_chanel_topic_, private_network_id_ );
    }

    outcome::result<void> GeniusNode::DHTInit()
    {
        // Encode the string to UTF-8 bytes, then compute its SHA-256
        // Scope FIRST, net-and-version appendix LAST: an empty scope hashes today's exact
        // byte string, so public job discovery keeps its CID.
        const std::string topic = ScopedProcessingGridChannel() + sgns::version::GetNetAndVersionAppendix();
        const base::Hash256 hash  = crypto::sha2_256( topic.data(), topic.size() );

        // Provide CID
        auto key = libp2p::multi::ContentIdentifierCodec::encodeCIDV0( hash.data(), hash.size() );
        BOOST_OUTCOME_TRY( pubsub_->GetDHT()->Start() );
        pubsub_->ProvideCID( key );

        auto cidtest = libp2p::multi::ContentIdentifierCodec::decode( key );

        auto cidstring = libp2p::multi::ContentIdentifierCodec::toString( cidtest.value() );
        node_logger_->info( "CID Test:: {}", cidstring.value() );

        // Also Find providers
        return pubsub_->StartFindingPeers( key );
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
        auto new_account = GeniusAccount::NewFromPrivateKey( this->GetTokenID(), private_key, write_base_path_ );
        if ( new_account == nullptr )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return outcome::success();
    }

    outcome::result<void> GeniusNode::AddAccountWithMnemonic( const std::string &mnemonic ) const
    {
        auto new_account = GeniusAccount::NewFromMnemonic( this->GetTokenID(), mnemonic, write_base_path_ );
        if ( new_account == nullptr )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return outcome::success();
    }

    outcome::result<std::string> GeniusNode::AddAccountWithRandomMnemonic() const
    {
        auto new_account = GeniusAccount::NewFromRandomMnemonic( this->GetTokenID(), write_base_path_ );
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

        auto account = GeniusAccount::NewFromPublicKey( GetTokenID(), public_address );

        if ( account == nullptr )
        {
            node_logger_->error( "Account not created" );
            return std::errc::address_not_available;
        }

        std::shared_ptr<evmwatcher::BridgeCatchupWatcher> previous_watcher;
        {
            std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
            if ( account_service_switching_ )
            {
                return std::errc::operation_in_progress;
            }
            account_service_switching_ = true;
            ++account_service_generation_; // invalidate every captured account-service snapshot
            ++bridge_init_generation_;
            catchup_callback_owner_generation_.store( 0 );
            previous_watcher = std::move( catchup_watcher_ );
        }

        // Watcher draining and manager/blockchain Stop may block or join threads;
        // they deliberately run without lifecycle_mutex_.  Public and async
        // consumers see the switching epoch as unavailable throughout the drain.
        if ( previous_watcher )
        {
            previous_watcher->stopWatching();
            previous_watcher.reset();
        }
        auto shutdown_result = ShutdownAccountBoundServices( true );
        if ( shutdown_result.has_error() )
        {
            std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
            account_service_switching_ = false;
            return outcome::failure( shutdown_result.error() );
        }

        if ( this->tx_globaldb_ )
        {
            // Database is already initialized (keyed by node ID, not account).
            // Keep it alive, configure it for the new account, and restart the
            // account-dependent layers. We must replicate what MIGRATING_DATABASE
            // and INITIALIZING_DATABASE do for a new account, without recreating
            // the database itself.
            account->InitMessenger( this->pubsub_ );
            account->ConfigureDatabaseDependencies( this->tx_globaldb_ );
            this->tx_globaldb_->AddListenTopic( ScopedProcessingChannel() );
            {
                std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
                account_ = std::move( account );
                StateTransition( NodeState::INITIALIZING_BLOCKCHAIN );
                account_service_switching_ = false;
            }
        }
        else
        {
            std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
            account_ = std::move( account );
            account_service_switching_ = false;
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

        const auto snapshot = SnapshotAccountServices();
        if ( !snapshot.account || !snapshot.manager ) return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        const auto token_id = GetTokenID();
        auto       balance  = snapshot.account->GetUTXOManager().GetBalance( token_id );
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

        const auto snapshot = SnapshotAccountServices();
        if ( !snapshot.account ) return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        BOOST_OUTCOME_TRY( snapshot.account->SaveInSecureStorage( "payout_address", std::string( payout_address ) ) );

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

        auto funds = GetProcessCost( *procmgr );
        if ( funds <= 0 )
        {
            return outcome::failure( Error::PROCESS_COST_ERROR );
        }

        const auto snapshot = SnapshotAccountServices();
        if ( !snapshot.account || !snapshot.manager ) return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        if ( snapshot.account->GetUTXOManager().GetBalance() < funds )
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
        BOOST_OUTCOME_TRY(
            auto result_pair,
            manager->HoldEscrow(
                funds, std::string( dev_config_.Addr ), cut.value(), uuidstring, private_network_id_ ) );

        //TODO - Make it async to post the job data in case the transaction gets confirmed.
        auto [tx_id, escrow_data_pair] = result_pair;

        auto [escrow_path, escrow_data] = escrow_data_pair;

        // Scope the escrow CRDT path through the task-carried escrow_path so the write here and
        // the PayEscrow -> FetchTransaction read stay symmetric; a public node's path equals the
        // raw lock_id byte-for-byte.
        const std::string scoped_escrow_path =
            processing::TaskKeys::ScopedKeyPath( private_network_id_, escrow_path );
        task.set_escrow_path( scoped_escrow_path );

        BOOST_OUTCOME_TRY( auto crdt_transaction,
                           CreateEscrowInfoCRDTTransaction( scoped_escrow_path, std::move( escrow_data ) ) );

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

    uint64_t GeniusNode::GetProcessCost( const sgns::sgprocessing::ProcessingManager &procmgr )
    {
        auto blockLen = procmgr.ParseBlockSize();
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
        const auto snapshot = SnapshotAccountServices();
        if ( !snapshot.account || !snapshot.manager ||
             snapshot.manager->GetState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction manager not ready", __func__ );
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }
        if ( destination.empty() )
        {
            destination = snapshot.account->GetAddress();
        }

        BOOST_OUTCOME_TRY( auto tx_id,
                           snapshot.manager->MintFunds( amount, transaction_hash, chainid, tokenid, destination ) );

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
        const auto snapshot = SnapshotAccountServices();
        if ( !snapshot.account ) return std::nullopt;
        auto res = snapshot.account->LoadFromSecureStorage( "mnemonic" );
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

            case NodeState::WAITING_FOR_TRUST_GENESIS:
                return { 0.60f, "Waiting for confirmed trust genesis" };

            case NodeState::WAITING_FOR_BURN_GENESIS:
                return { 0.65f, "Waiting for confirmed burn genesis" };

            case NodeState::FATAL_TRUST_MISMATCH:
                return { 0.60f, "Trust startup failed closed" };

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
        const auto snapshot = SnapshotAccountServices();
        if ( !snapshot.account || !snapshot.manager ||
             snapshot.manager->GetState() != TransactionManager::State::READY )
        {
            node_logger_->error( "{}: Transaction Manager is not ready", __func__ );
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }

        auto available_balance = snapshot.account->GetUTXOManager().GetBalance( token_id );
        if ( available_balance < amount )
        {
            node_logger_->error( "{}: insufficient local funds: requested={}, available={}",
                                 __func__,
                                 amount,
                                 available_balance );
            return outcome::failure( Error::INSUFFICIENT_FUNDS );
        }

        BOOST_OUTCOME_TRY( auto tx_id, snapshot.manager->TransferFunds( amount, destination, token_id ) );

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
        const auto snapshot = SnapshotAccountServices();
        return snapshot.account ? snapshot.account->GetUTXOManager().GetBalance() : 0;
    }

    uint64_t GeniusNode::GetBalance( const TokenID token_id )
    {
        const auto snapshot = SnapshotAccountServices();
        return snapshot.account ? snapshot.account->GetUTXOManager().GetBalance( token_id ) : 0;
    }

    uint64_t GeniusNode::GetBalance( const std::string &address )
    {
        const auto snapshot = SnapshotAccountServices();
        return snapshot.account ? snapshot.account->GetUTXOManager().GetBalance( address ) : 0;
    }

    uint64_t GeniusNode::GetBalance( const TokenID token_id, const std::string &address )
    {
        const auto snapshot = SnapshotAccountServices();
        return snapshot.account ? snapshot.account->GetUTXOManager().GetBalance( token_id, address ) : 0;
    }

    void GeniusNode::ProcessingDone( const std::string &task_id, const SGProcessing::TaskResult &taskresult )
    {
        static constexpr std::string_view FUNC        = __func__;
        const auto snapshot = SnapshotAccountServices();
        if ( !snapshot.account || !snapshot.manager ) return;
        const auto account_tag = snapshot.account->GetAddress().substr( 0, 8 );
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
                                   const auto snapshot = strong->SnapshotAccountServices();
                                   if ( !snapshot.account ) return;
                                   strong->node_logger_->error( "[ {} ] ERROR PROCESSING SUBTASK ",
                                                                snapshot.account->GetAddress().substr( 0, 8 ),
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
            processing_service_->StartProcessing( ScopedProcessingGridChannel() );
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

    GeniusNode::AccountServiceSnapshot GeniusNode::SnapshotAccountServices() const
    {
        std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
        if ( account_service_switching_ )
        {
            return {};
        }
        return { account_, transaction_manager_, account_service_generation_ };
    }

    bool GeniusNode::ApplyIfCurrentAccountServices( const AccountServiceSnapshot &snapshot,
                                                    const std::function<void()>  &side_effect )
    {
        std::lock_guard<std::recursive_mutex> lifecycle_lock( lifecycle_mutex_ );
        if ( account_service_switching_ || snapshot.generation != account_service_generation_ ||
             snapshot.account.get() != account_.get() || snapshot.manager.get() != transaction_manager_.get() )
        {
            return false;
        }
        side_effect();
        return true;
    }

    std::string GeniusNode::GetAddress() const
    {
        std::string address = "UNVAILABLE";
        auto        snapshot = SnapshotAccountServices();
        if ( snapshot.account )
        {
            address = snapshot.account->GetAddress();
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
        auto snapshot = SnapshotAccountServices();
        if ( !snapshot.manager )
        {
            return outcome::failure( Error::TRANSACTIONS_NOT_READY );
        }
        return snapshot.manager;
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
        auto transaction_manager = SnapshotAccountServices().manager;
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
        const auto account_services = SnapshotAccountServices();
        if ( !account_services.account || !account_services.manager )
        {
            node_logger_->warn( "InitializeAndStartBridge: account services are not published" );
            return;
        }

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

            auto rpc_resolver = [weak_self = weak_from_this(), account_services](
                                    const std::string &chain_id_str ) -> std::optional<std::string>
            {
                auto strong = weak_self.lock();
                if ( !strong )
                {
                    return std::nullopt;
                }
                std::optional<std::string> url;
                strong->ApplyIfCurrentAccountServices(
                    account_services,
                    [&]
                    {
                        auto &validator = account_services.manager->GetPublicChainInputValidator();
                        url             = validator.GetFirstRpcUrl( chain_id_str );
                    } );
                return url;
            };

            auto burn_processor = [weak_self = weak_from_this(), account_services](
                                      const std::vector<eth::abi::AbiValue> &decoded_values,
                                      const std::string                     &tx_hash_hex,
                                      const std::string                     &chain_id_str ) -> bool
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
                if ( !strong )
                {
                    return false;
                }
                bool processed = false;
                strong->ApplyIfCurrentAccountServices(
                    account_services,
                    [&]
                    {
                        auto &utxo_mgr = account_services.account->GetUTXOManager();
                        if ( utxo_mgr.IsOutPointConsumed( burn_tx_hash, 0 ) ||
                             utxo_mgr.IsOutPointReserved( burn_tx_hash, 0 ) )
                        {
                            return;
                        }
                        try
                        {
                            auto result = strong->MintTokens( burn.value().amount,
                                                              tx_hash_hex,
                                                              chain_id_str,
                                                              burn.value().token_id,
                                                              burn.value().destination );
                            processed = result.has_value();
                        }
                        catch ( const std::exception &e )
                        {
                            strong->node_logger_->debug( "CatchUpWatcher: MintTokens threw for tx {}: {} — skipping",
                                                         tx_hash_hex,
                                                         e.what() );
                        }
                    } );
                return processed;
            };

            catchup_watcher_ = std::make_unique<evmwatcher::BridgeCatchupWatcher>(
                catchup_config,
                nullptr, // no raw message callback needed
                std::move( chains_provider ),
                std::move( rpc_resolver ),
                std::move( burn_processor ) );

            catchup_watcher_->startWatching();
            catchup_callback_owner_generation_.store( account_services.generation );
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
        const auto generation = account_services.generation;
        auto       tx_mgr     = account_services.manager;
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
                               if ( !strong || strong->SnapshotAccountServices().generation != generation )
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
                                   return !s || s->SnapshotAccountServices().generation != generation;
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

        gc_timer_                          = std::make_unique<boost::asio::steady_timer>( *io_ );
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

    libp2p::Host::Connectedness GeniusNode::HostConnectedness( const libp2p::peer::PeerInfo &peer ) const
    {
        if ( !pubsub_ )
        {
            return libp2p::Host::Connectedness::NOT_CONNECTED;
        }

        auto host = pubsub_->GetHost();
        if ( !host )
        {
            return libp2p::Host::Connectedness::NOT_CONNECTED;
        }

        auto context = pubsub_->GetAsioContext();

        // No context, a stopped context, or a call already made from the pubsub
        // thread: post-and-wait would never complete, so read inline. Reading
        // inline from the owning thread is exactly what libp2p expects.
        if ( !context || context->stopped() || context->get_executor().running_in_this_thread() )
        {
            return host->connectedness( peer );
        }

        auto promise = std::make_shared<std::promise<libp2p::Host::Connectedness>>();
        auto future  = promise->get_future();

        boost::asio::post( *context,
                           [host, peer, promise]()
                           {
                               promise->set_value( host->connectedness( peer ) );
                           } );

        // Bounded wait: during shutdown the context can stop between the
        // stopped() check above and the post, leaving the task unrun. Report
        // NOT_CONNECTED rather than blocking a scheduler thread forever.
        if ( future.wait_for( std::chrono::seconds( 5 ) ) != std::future_status::ready )
        {
            node_logger_->warn( "HostConnectedness: timed out querying connectedness for {}",
                                peer.id.toBase58() );
            return libp2p::Host::Connectedness::NOT_CONNECTED;
        }
        return future.get();
    }

    void GeniusNode::PerformHealthCheck()
    {
        if ( shutdown_started_.load() )
        {
            return;
        }

        // Check both fullnodes and peers
        for ( const auto &infos : { &bootstrap_fullnode_infos_, &bootstrap_peer_infos_ } )
        {
            for ( const auto &peer_info : *infos )
            {
                auto connectedness = HostConnectedness( peer_info );
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

        auto connectedness = HostConnectedness( *peer_info_ptr );
        if ( connectedness == libp2p::Host::Connectedness::CONNECTED )
        {
            node_logger_->info( "Bootstrap fullnode {} already connected, resetting attempt counter",
                                peer_id.toBase58() );
            std::lock_guard<std::mutex> lock( reconnect_mutex_ );
            reconnect_attempts_.erase( peer_id );
            return;
        }

        node_logger_->info( "Attempting reconnect to bootstrap fullnode {}...", peer_id.toBase58() );

        auto weak_self   = weak_from_this();
        auto ipv4_source = libp2p::multi::Multiaddress::create( "/ip4/0.0.0.0/tcp/0" ).value();
        auto ipv6_source = libp2p::multi::Multiaddress::create( "/ip6/::/tcp/0" ).value();
        libp2p::network::RouteHelper::SourceAddresses source_addresses{ std::move( ipv4_source ),
                                                                        std::move( ipv6_source ),
                                                                        true,
                                                                        true };

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

fmt::format_context::iterator fmt::formatter<sgns::GeniusNode::NodeState>::format( sgns::GeniusNode::NodeState state,
                                                                                   format_context &ctx ) const
{
    using State = sgns::GeniusNode::NodeState;

    string_view name = "UNKNOWN";

    switch ( state )
    {
        case State::CREATING:
            name = "CREATING";
            break;
        case State::MIGRATING_DATABASE:
            name = "MIGRATING_DATABASE";
            break;
        case State::INITIALIZING_DATABASE:
            name = "INITIALIZING_DATABASE";
            break;
        case State::INITIALIZING_PROCESSING:
            name = "INITIALIZING_PROCESSING";
            break;
        case State::INITIALIZING_BLOCKCHAIN:
            name = "INITIALIZING_BLOCKCHAIN";
            break;
        case State::INITIALIZING_TRANSACTIONS:
            name = "INITIALIZING_TRANSACTIONS";
            break;
        case State::READY:
            name = "READY";
            break;
    }

    return formatter<string_view>::format( name, ctx );
}
