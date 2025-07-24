/**
 * @file       NodeExample.cpp
 * @brief      
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <math.h>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <atomic>
#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <thread>
#include <chrono>

std::mutex              keyboard_mutex;
std::condition_variable cv;
std::queue<std::string> events;
std::string             current_input;
std::atomic<bool>       finished( false );

#ifdef _WIN32
// Add a global variable to store the original console mode
DWORD original_console_mode;
#else
// Store the original terminal attributes
termios original_term;
#endif

void enable_raw_mode()
{
#ifdef _WIN32
    HANDLE hInput = GetStdHandle( STD_INPUT_HANDLE );
    // Save the original console mode to the global variable
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
    // Restore the original console mode from the global variable
    SetConsoleMode( hInput, original_console_mode );
#else
    // Restore the original terminal attributes
    tcsetattr( STDIN_FILENO, TCSANOW, &original_term );
#endif
}

void clear_line()
{
    std::cout << "\r\033[K"; // Clear the current line
}

void redraw_prompt()
{
    clear_line();
    std::cout << "> " << current_input << std::flush; // Redraw the input prompt
}

void keyboard_input_thread()
{
    enable_raw_mode();

    while ( !finished )
    {
        char ch;
        std::cin.get( ch );

        {
            std::lock_guard<std::mutex> lock( keyboard_mutex );
            if ( ch == '\n' || ch == '\r' )
            {
                // Check for both newline and carriage return
                if ( !current_input.empty() )
                {
                    events.push( current_input );
                    current_input.clear();
                    cv.notify_one(); // Notify the event processor
                }
                std::cout << std::endl;
            }
            else if ( ch == 127 || ch == '\b' )
            { // Handle backspace
                if ( !current_input.empty() )
                {
                    current_input.pop_back();
                }
            }
            else if ( std::isprint( ch ) || std::isspace( ch ) )
            {
                current_input += ch;
            }
        }

        redraw_prompt();
    }

    disable_raw_mode();
}

void PrintAccountInfo( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() != 1 )
    {
        std::cerr << "Invalid info command format.\n";
        return;
    }
    std::cout << "Balance: " << genius_node.GetBalance() << std::endl;
}

void PrintDataStore( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() != 1 )
    {
        std::cerr << "Invalid info command format.\n";
        return;
    }
    genius_node.PrintDataStore();
}

void MintTokens( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid mint command format.\n";
        return;
    }
    genius_node.MintTokens( std::stoull( args[1] ), "", "", sgns::TokenID::FromBytes( { 0x00 } ) );
}
void TransferTokens( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() != 3 )
    {
        std::cerr << "Invalid mint command format.\n";
        return;
    }
    genius_node.TransferFunds( std::stoull( args[1] ), args[2], sgns::TokenID::FromBytes( { 0x00 } ) );
}

void GetCoinPrice( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() < 2 ) // Check if there's at least one token ID (args[0] is "price")
    {
        std::cerr << "Invalid price command format. Usage: price <token_id1> [token_id2] [token_id3] ...\n";
        return;
    }

    // Create a vector of token IDs (skipping args[0] which is "price")
    std::vector<std::string> tokenIds( args.begin() + 1, args.end() );

    // Call the GetCoinprice function with the token IDs
    auto prices = genius_node.GetCoinprice( tokenIds );

    // Display the results
    for ( const auto &[token, price] : prices.value() )
    {
        std::cout << token << ": $" << std::fixed << std::setprecision( 4 ) << price << std::endl;
    }
}

void CreateProcessingTransaction( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    std::string json_data = R"(
{
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
        "width": 1350,
        "height": 900,
		"block_len": 4860000 ,
		"block_line_stride": 5400,
		"block_stride": 0,
		"chunk_line_stride": 1080,
		"chunk_offset": 0,
		"chunk_stride": 4320,
		"chunk_subchunk_height": 5,
		"chunk_subchunk_width": 5,
		"chunk_count": 25,
        "channels": 4
      },
      "format": "RGBA8"
    },
    {
      "name": "frisbee_image", 
	  "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/data/frisbee3.data",
      "type": "texture2D",
      "description": "Frisbee pose image input",
      "dimensions": {
        "width": 512,
        "height": 512,
		"block_len": 786432 ,
		"block_line_stride": 1536,
		"block_stride": 0,
		"chunk_line_stride": 384,
		"chunk_offset": 0,
		"chunk_stride": 1152,
		"chunk_subchunk_height": 4,
		"chunk_subchunk_width": 4,
		"chunk_count": 16,
        "channels": 3
      },
      "format": "RGB8"
    }
  ],

  "outputs": [
    {
      "name": "ballet_keypoints",
	  "source_uri_param": "dummy",
      "type": "tensor",
      "description": "Detected keypoints for ballet image",
      "dimensions": {
        "width": 17,
        "height": 3
      },
      "format": "FLOAT32"
    },
    {
      "name": "frisbee_keypoints",
	  "source_uri_param": "dummy",
      "type": "tensor", 
      "description": "Detected keypoints for frisbee image",
      "dimensions": {
        "width": 17,
        "height": 3
      },
      "format": "FLOAT32"
    }
  ],

  "passes": [
    {
      "name": "ballet_pose_inference",
      "type": "inference",
      "description": "Run PoseNet inference on ballet image",
      "model": {
        "source_uri_param": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/model.mnn",
        "format": "MNN",
        "batch_size": 1,
        "input_nodes": [
          {
            "name": "input",
            "type": "texture2D",
            "source": "input:ballet_image",
            "shape": [1, 256, 256, 4]
          }
        ],
        "output_nodes": [
          {
            "name": "output",
            "type": "tensor",
            "target": "output:ballet_keypoints",
            "shape": [1, 17, 3]
          }
        ]
      }
    },
    {
      "name": "frisbee_pose_inference",
      "type": "inference", 
      "description": "Run PoseNet inference on frisbee image",
      "model": {
        "source_uri_param": "model.mnn",
        "format": "MNN",
        "batch_size": 1,
        "input_nodes": [
          {
            "name": "input",
            "type": "texture2D", 
            "source": "input:frisbee_image",
            "shape": [1, 256, 256, 4]
          }
        ],
        "output_nodes": [
          {
            "name": "output",
            "type": "tensor",
            "target": "output:frisbee_keypoints", 
            "shape": [1, 17, 3]
          }
        ]
      }
    }
  ]
}
       )";
    auto        jobpost   = genius_node.ProcessImage( json_data /*args[1]*/
    );
    if ( !jobpost )
    {
        std::cout << "Job post error: " << jobpost.error().message() << std::endl;
    }
}

std::vector<std::string> split_string( const std::string &str )
{
    std::istringstream       iss( str );
    std::vector<std::string> results( ( std::istream_iterator<std::string>( iss ) ),
                                      std::istream_iterator<std::string>() );
    return results;
}

void process_events( sgns::GeniusNode &genius_node )
{
    while ( !finished )
    {
        std::unique_lock<std::mutex> lock( keyboard_mutex );
        cv.wait( lock, [] { return !events.empty() || finished; } );

        while ( !events.empty() )
        {
            std::string event = std::move( events.front() );
            events.pop();

            lock.unlock(); // Unlock while processing

            auto arguments = split_string( event );
            if ( arguments.empty() )
            {
                std::cerr << "Invalid command\n";
            }
            else if ( arguments[0] == "process" )
            {
                CreateProcessingTransaction( arguments, genius_node );
            }
            else if ( arguments[0] == "mint" )
            {
                MintTokens( arguments, genius_node );
            }
            else if ( arguments[0] == "transfer" )
            {
                TransferTokens( arguments, genius_node );
            }
            else if ( arguments[0] == "info" )
            {
                PrintAccountInfo( arguments, genius_node );
            }
            else if ( arguments[0] == "ds" )
            {
                PrintDataStore( arguments, genius_node );
            }
            else if ( arguments[0] == "price" )
            {
                GetCoinPrice( arguments, genius_node );
            }
            else if ( arguments[0] == "peer" )
            {
                if ( arguments.size() > 1 )
                {
                    genius_node.AddPeer( arguments[1] );
                }
                else
                {
                    std::cerr << "Invalid peer command\n";
                }
            }
            else if ( arguments[0] == "stopprocessing" )
            {
                genius_node.StopProcessing();
                std::cout << "Stopping processing" << std::endl;
            }
            else if ( arguments[0] == "quit" )
            {
                finished = true;
            }
            else
            {
                std::cerr << "Unknown command: " << arguments[0] << "\n";
            }

            lock.lock(); // Re-lock before checking the condition again
        }
    }
}

void periodic_processing( sgns::GeniusNode &genius_node )
{
    while ( !finished )
    {
        std::this_thread::sleep_for( std::chrono::minutes( 30 ) ); // Wait for 1 minute
        if ( finished )
        {
            break; // Exit if the application is shutting down
        }

        std::string json_data = R"(
                {
                "data": {
                    "type": "https",
                    "URL": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/"
                },
                "model": {
                    "name": "mnnimage",
                    "file": "model.mnn"
                },
                "input": [
                    {
                        "image": "data/ballet.data",
                        "block_len": 4860000 ,
                        "block_line_stride": 5400,
                        "block_stride": 0,
                        "chunk_line_stride": 1080,
                        "chunk_offset": 0,
                        "chunk_stride": 4320,
                        "chunk_subchunk_height": 5,
                        "chunk_subchunk_width": 5,
                        "chunk_count": 25,
                        "channels": 4
                    },
                    {
                        "image": "data/frisbee3.data",
                        "block_len": 786432 ,
                        "block_line_stride": 1536,
                        "block_stride": 0,
                        "chunk_line_stride": 384,
                        "chunk_offset": 0,
                        "chunk_stride": 1152,
                        "chunk_subchunk_height": 4,
                        "chunk_subchunk_width": 4,
                        "chunk_count": 16,
                        "channels": 3
                    }
                ]
                }
               )";
        auto        jobpost   = genius_node.ProcessImage( json_data /*args[1]*/
        );
        if ( !jobpost )
        {
            std::cout << "Job post error: " << jobpost.error().message() << std::endl;
        }
    }
}

std::string generate_eth_private_key()
{
    std::random_device                      rd;
    std::mt19937                            gen( rd() );
    std::uniform_int_distribution<uint16_t> dist( 0, 255 );

    std::ostringstream oss;
    for ( int i = 0; i < 32; ++i )
    {
        oss << std::hex << std::setw( 2 ) << std::setfill( '0' )
            << ( dist( gen ) & 0xFF ); // Mask to ensure only lower 8 bits are used
    }
    return oss.str();
}

DevConfig_st DEV_CONFIG{ "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./" };

int main( int argc, char *argv[] )
{
    bool start_processing = false; // Default behavior for "process"
    bool last_param       = true;  // Default value for the last parameter

    // Parse command-line arguments
    if ( argc > 1 )
    {
        std::string arg = argv[1];
        if ( arg == "server" )
        {
            start_processing = true;
            last_param       = false;
        }
    }

    // Generate a random Ethereum-compatible private key
    std::string eth_private_key = generate_eth_private_key();
    std::cout << "Generated Ethereum Private Key: " << eth_private_key << std::endl;

    std::thread input_thread( keyboard_input_thread );

    sgns::GeniusNode node_instance( DEV_CONFIG, eth_private_key.c_str(), false, last_param, 400101, true );

    std::cout << "Insert \"process\", the image and the number of tokens to be" << std::endl;
    redraw_prompt();

    if ( start_processing )
    {
        std::thread processing_thread( periodic_processing, std::ref( node_instance ) );

        process_events( node_instance );

        if ( processing_thread.joinable() )
        {
            processing_thread.join();
        }
    }
    else
    {
        process_events( node_instance );
    }

    if ( input_thread.joinable() )
    {
        input_thread.join();
    }

    return 0;
}
