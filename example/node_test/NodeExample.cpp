/**
 * @file       NodeExample.cpp
 * @brief
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <random>
#include <map>
#include <functional>
#include <cctype>
#include <iomanip>
#include "base/logger.hpp"
#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "account/GeniusNode.hpp"
#include <thread>
#include <chrono>

static sgns::base::Logger logger = sgns::base::createLogger( "NodeExample" );

std::mutex              keyboard_mutex;
std::condition_variable cv;
std::queue<std::string> events;
std::string             current_input;
std::atomic<bool>       finished( false );

#ifdef _WIN32
DWORD original_console_mode;
#else
termios original_term;
#endif

void enable_raw_mode()
{
#ifdef _WIN32
    HANDLE hInput = GetStdHandle( STD_INPUT_HANDLE );
    GetConsoleMode( hInput, &original_console_mode );
    SetConsoleMode( hInput, original_console_mode & ~( ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT ) );
#else
    tcgetattr( STDIN_FILENO, &original_term );
    termios term  = original_term;
    term.c_lflag &= ~( ICANON | ECHO );
    tcsetattr( STDIN_FILENO, TCSANOW, &term );
#endif
}

void disable_raw_mode()
{
#ifdef _WIN32
    HANDLE hInput = GetStdHandle( STD_INPUT_HANDLE );
    SetConsoleMode( hInput, original_console_mode );
#else
    tcsetattr( STDIN_FILENO, TCSANOW, &original_term );
#endif
}

void clear_line()
{
    std::cout << "\r\033[K";
}

void redraw_prompt()
{
    clear_line();
    std::cout << "> " << current_input << std::flush;
}

static std::string trim( std::string_view s )
{
    auto start = s.find_first_not_of( " \t\r\n" );
    if ( start == std::string::npos )
    {
        return {};
    }
    auto end = s.find_last_not_of( " \t\r\n" );
    return std::string( s.substr( start, end - start + 1 ) );
}

static std::string to_lower( std::string s )
{
    for ( auto &c : s )
    {
        c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    }
    return s;
}

void keyboard_input_thread()
{
    enable_raw_mode();

    while ( !finished )
    {
        char ch;
        if ( !std::cin.get( ch ) )
        {
            // EOF (Ctrl+D) — signal quit
            std::lock_guard<std::mutex> lock( keyboard_mutex );
            events.emplace( "quit" );
            cv.notify_one();
            break;
        }

        {
            std::lock_guard<std::mutex> lock( keyboard_mutex );
            if ( ch == '\n' || ch == '\r' )
            {
                if ( !current_input.empty() )
                {
                    events.push( trim( current_input ) );
                    current_input.clear();
                    cv.notify_one();
                }
                std::cout << std::endl;
            }
            else if ( ch == 127 || ch == '\b' )
            {
                if ( !current_input.empty() )
                {
                    current_input.pop_back();
                }
            }
            else if ( std::isprint( static_cast<unsigned char>( ch ) ) ||
                      std::isspace( static_cast<unsigned char>( ch ) ) )
            {
                current_input += ch;
            }
        }

        redraw_prompt();
    }

    disable_raw_mode();
}

static bool check_arg_count( const std::vector<std::string> &args, size_t expected, const std::string &usage )
{
    if ( args.size() != expected )
    {
        logger->error( "Usage: {}", usage );
        return false;
    }
    return true;
}

static bool check_arg_count_min( const std::vector<std::string> &args, size_t min, const std::string &usage )
{
    if ( args.size() < min )
    {
        logger->error( "Usage: {}", usage );
        return false;
    }
    return true;
}

static constexpr const char *POSENET_JSON = R"({
  "name": "posenet-inference",
  "version": "1.0.0",
  "gnus_spec_version": 1.0,
  "author": "AI Assistant",
  "description": "PoseNet inference on multiple image inputs using MNN model",
  "tags": ["pose-estimation", "computer-vision", "inference"],

  "inputs": [
    {
      "name": "ballet_image",
      "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/data/ballet.data",
      "type": "texture2D",
      "description": "Ballet pose image input",
      "dimensions": {
        "width": 1350, "height": 900,
        "block_len": 4860000, "block_line_stride": 5400, "block_stride": 0,
        "chunk_line_stride": 1080, "chunk_offset": 0, "chunk_stride": 4320,
        "chunk_subchunk_height": 5, "chunk_subchunk_width": 5, "chunk_count": 25
      },
      "format": "RGBA8"
    },
    {
      "name": "frisbee_image",
      "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/data/frisbee3.data",
      "type": "texture2D",
      "description": "Frisbee pose image input",
      "dimensions": {
        "width": 512, "height": 512,
        "block_len": 786432, "block_line_stride": 1536, "block_stride": 0,
        "chunk_line_stride": 384, "chunk_offset": 0, "chunk_stride": 1152,
        "chunk_subchunk_height": 4, "chunk_subchunk_width": 4, "chunk_count": 16
      },
      "format": "RGB8"
    }
  ],

  "outputs": [
    {
      "name": "ballet_keypoints", "source_uri_param": "dummy",
      "type": "tensor", "description": "Detected keypoints for ballet image",
      "dimensions": { "width": 17, "height": 3 }, "format": "FLOAT32"
    },
    {
      "name": "frisbee_keypoints", "source_uri_param": "dummy",
      "type": "tensor", "description": "Detected keypoints for frisbee image",
      "dimensions": { "width": 17, "height": 3 }, "format": "FLOAT32"
    }
  ],

  "passes": [
    {
      "name": "ballet_pose_inference", "type": "inference",
      "description": "Run PoseNet inference on ballet image",
      "model": {
        "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/model.mnn",
        "format": "MNN", "batch_size": 1,
        "input_nodes": [
          { "name": "input", "type": "texture2D", "source": "input:ballet_image", "shape": [1, 256, 256, 4] }
        ],
        "output_nodes": [
          { "name": "output", "type": "tensor", "target": "output:ballet_keypoints", "shape": [1, 17, 3] }
        ]
      }
    },
    {
      "name": "frisbee_pose_inference", "type": "inference",
      "description": "Run PoseNet inference on frisbee image",
      "model": {
        "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/model.mnn",
        "format": "MNN", "batch_size": 1,
        "input_nodes": [
          { "name": "input", "type": "texture2D", "source": "input:frisbee_image", "shape": [1, 256, 256, 4] }
        ],
        "output_nodes": [
          { "name": "output", "type": "tensor", "target": "output:frisbee_keypoints", "shape": [1, 17, 3] }
        ]
      }
    }
  ]
})";

static void cmd_info( const std::vector<std::string> &args, std::shared_ptr<sgns::GeniusNode> node )
{
    if ( !check_arg_count( args, 1, "info" ) )
    {
        return;
    }
    logger->info( "Balance: {}", node->GetBalance() );
}

static void cmd_balance( const std::vector<std::string> &args, std::shared_ptr<sgns::GeniusNode> node )
{
    if ( !check_arg_count( args, 2, "balance <token_id>" ) )
    {
        return;
    }
    logger->info( "Balance: {}", node->GetBalance( args[1] ) );
}

static void cmd_ds( const std::vector<std::string> &args, std::shared_ptr<sgns::GeniusNode> node )
{
    if ( !check_arg_count( args, 1, "ds" ) )
    {
        return;
    }
    node->PrintDataStore();
}

static void cmd_mint( const std::vector<std::string> &args, std::shared_ptr<sgns::GeniusNode> node )
{
    if ( !check_arg_count( args, 2, "mint <amount>" ) )
    {
        return;
    }
    try
    {
        node->MintTokens( std::stoull( args[1] ), "supergenius", "", 0u, sgns::TokenID::FromBytes( { 0x00 } ) );
    }
    catch ( const std::exception &e )
    {
        logger->error( "Invalid amount: '{}' — must be a non-negative integer.", args[1] );
    }
}

static void cmd_transfer( const std::vector<std::string> &args, std::shared_ptr<sgns::GeniusNode> node )
{
    if ( !check_arg_count( args, 3, "transfer <amount> <recipient_address>" ) )
    {
        return;
    }
    try
    {
        node->TransferFunds( std::stoull( args[1] ),
                             args[2],
                             sgns::TokenID::FromBytes( { 0x00 } ),
                             std::chrono::milliseconds( sgns::GeniusNode::TIMEOUT_TRANSFER ) );
    }
    catch ( const std::exception &e )
    {
        logger->error( "Invalid amount: '{}' — must be a non-negative integer.", args[1] );
    }
}

static void cmd_price( const std::vector<std::string> &args, std::shared_ptr<sgns::GeniusNode> node )
{
    if ( !check_arg_count_min( args, 2, "price <token_id1> [token_id2 ...]" ) )
    {
        return;
    }
    std::vector<std::string> tokenIds( args.begin() + 1, args.end() );
    auto                     prices = node->GetCoinprice( tokenIds );
    for ( const auto &[token, price] : prices.value() )
    {
        logger->info( "{}: ${:.4f}", token, price );
    }
}

static void cmd_process( const std::vector<std::string> & /*args*/, std::shared_ptr<sgns::GeniusNode> node )
{
    auto jobpost = node->ProcessImage( POSENET_JSON );
    if ( !jobpost )
    {
        logger->error( "Job post error: {}", jobpost.error().message() );
    }
}

static void cmd_peer( const std::vector<std::string> &args, std::shared_ptr<sgns::GeniusNode> node )
{
    if ( !check_arg_count( args, 2, "peer <multiaddr>" ) )
    {
        return;
    }
    node->AddPeer( args[1] );
}

static void cmd_stopprocessing( const std::vector<std::string> & /*args*/, std::shared_ptr<sgns::GeniusNode> node )
{
    node->StopProcessing();
    logger->info( "Stopping processing" );
}

static void cmd_quit( const std::vector<std::string> & /*args*/, std::shared_ptr<sgns::GeniusNode> /*node*/ )
{
    finished = true;
}

static void cmd_help( const std::vector<std::string> & /*args*/, std::shared_ptr<sgns::GeniusNode> /*node*/ );

using CmdFunc = std::function<void( const std::vector<std::string> &, std::shared_ptr<sgns::GeniusNode> )>;
static const std::map<std::string, CmdFunc> COMMANDS = {
    { "help", cmd_help },
    { "?", cmd_help },
    { "info", cmd_info },
    { "balance", cmd_balance },
    { "ds", cmd_ds },
    { "mint", cmd_mint },
    { "transfer", cmd_transfer },
    { "price", cmd_price },
    { "process", cmd_process },
    { "peer", cmd_peer },
    { "stopprocessing", cmd_stopprocessing },
    { "quit", cmd_quit },
};

static void cmd_help( const std::vector<std::string> & /*args*/, std::shared_ptr<sgns::GeniusNode> /*node*/ )
{
    std::cout << "Available commands:\n"
              << "  help, ?                  Show this help\n"
              << "  info                     Display account balance\n"
              << "  balance <token_id>       Display balance for a specific token\n"
              << "  ds                       Print the data store\n"
              << "  mint <amount>            Mint tokens\n"
              << "  transfer <amt> <addr>    Transfer tokens to an address\n"
              << "  price <token1> [tokens]  Get coin prices\n"
              << "  process                  Submit a processing job\n"
              << "  peer <multiaddr>         Add a peer\n"
              << "  stopprocessing           Stop processing\n"
              << "  quit                     Exit the application\n";
}

static std::vector<std::string> split_string( const std::string &str )
{
    std::istringstream iss( str );
    return { std::istream_iterator<std::string>( iss ), std::istream_iterator<std::string>() };
}

static void process_events( std::shared_ptr<sgns::GeniusNode> genius_node )
{
    while ( !finished )
    {
        std::unique_lock<std::mutex> lock( keyboard_mutex );
        cv.wait( lock, [] { return !events.empty() || finished; } );

        while ( !events.empty() )
        {
            std::string event = std::move( events.front() );
            events.pop();
            lock.unlock();

            auto arguments = split_string( event );
            if ( arguments.empty() )
            {
                lock.lock();
                continue;
            }

            std::string cmd = to_lower( arguments[0] );
            auto        it  = COMMANDS.find( cmd );

            if ( it != COMMANDS.end() )
            {
                it->second( arguments, genius_node );
            }
            else
            {
                logger->warn( "Unknown command: '{}' — type 'help' for available commands.", cmd );
            }

            lock.lock();
        }
    }
}

void status_polling_thread( std::shared_ptr<sgns::GeniusNode> genius_node )
{
    static const char *STATUS_NAMES[] = { "DISABLED", "IDLE", "PROCESSING" };

    while ( !finished )
    {
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
        if ( finished )
        {
            break;
        }

        auto status = genius_node->GetProcessingStatus();
        logger->info( "[Status: {} | Progress: {:.2f}%]",
                      STATUS_NAMES[static_cast<int>( status.status )],
                      status.percentage );
    }
}

static void periodic_processing( std::shared_ptr<sgns::GeniusNode> genius_node )
{
    while ( !finished )
    {
        std::this_thread::sleep_for( std::chrono::minutes( 7 ) );
        if ( finished )
        {
            break;
        }

        auto jobpost = genius_node->ProcessImage( POSENET_JSON );
        if ( !jobpost )
        {
            logger->error( "Job post error: {}", jobpost.error().message() );
        }
    }
}

static std::string generate_eth_private_key()
{
    std::random_device                      rd;
    std::mt19937                            gen( 42 );
    std::uniform_int_distribution<uint16_t> dist( 0, 255 );

    std::ostringstream oss;
    for ( int i = 0; i < 32; ++i )
    {
        oss << std::hex << std::setw( 2 ) << std::setfill( '0' ) << ( dist( gen ) & 0xFF );
    }
    return oss.str();
}

GeniusNodeConfig DEV_CONFIG{ "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./" };

int main( int argc, char *argv[] )
{
    bool        start_processing = false; // Default behavior for "process"
    bool        terminal_mode    = false;

    //full node flag is now defined in the sgns_config.json file, so we don't need to pass it as a command line argument anymore
    std::string path_override;

    for ( int i = 1; i < argc; ++i )
    {
        std::string arg = argv[i];

        if ( arg == "server" )
        {
            start_processing = true;
        }
        else if ( arg == "jobposter" )
        {
            start_processing = true;
        }
        else if ( arg == "--terminal" )
        {
            terminal_mode = true;
        }
        else if ( arg.rfind( "--path=", 0 ) == 0 )
        {
            path_override = arg.substr( 7 );
        }
        else if ( ( arg == "-p" || arg == "--path" ) && i + 1 < argc )
        {
            path_override = argv[++i];
        }
    }

    if ( !path_override.empty() )
    {
        DEV_CONFIG.BaseWritePath = path_override;
        logger->info( "Using custom path: {}", path_override );
    }

    std::string eth_private_key = generate_eth_private_key();
    logger->info( "Generated Ethereum Private Key: {}", eth_private_key );

    // node_type/is_processor come from the shipped sgns_config.json; port_seed/auto_dht come from
    // the shipped network_config.json (both operator-managed, like a real deployment).
    auto node_instance =
        sgns::GeniusNode::New( DEV_CONFIG, sgns::FromPrivateKey{ eth_private_key } );

    std::thread status_thread;

    while ( node_instance->GetState() != sgns::GeniusNode::NodeState::READY )
    {
        std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    }

    std::thread input_thread;
    if ( terminal_mode )
    {
        input_thread = std::thread( keyboard_input_thread );
        logger->info( "Interactive mode. Type 'help' for available commands." );
        redraw_prompt();
    }

    auto idle_loop = []
    {
        while ( !finished )
        {
            std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
        }
    };

    if ( start_processing )
    {
        std::thread processing_thread( periodic_processing, std::ref( node_instance ) );

        if ( terminal_mode )
        {
            process_events( node_instance );
        }
        else
        {
            idle_loop();
        }

        if ( processing_thread.joinable() )
        {
            processing_thread.join();
        }
    }
    else
    {
        if ( terminal_mode )
        {
            process_events( node_instance );
        }
        else
        {
            idle_loop();
        }
    }

    if ( input_thread.joinable() )
    {
        input_thread.join();
    }

    return 0;
}
