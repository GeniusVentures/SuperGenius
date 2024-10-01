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

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
std::mutex              keyboard_mutex;
std::condition_variable cv;
std::queue<std::string> events;

void keyboard_input_thread()
{
    std::string line;
    while ( std::getline( std::cin, line ) )
    {
        {
            std::lock_guard<std::mutex> lock( keyboard_mutex );
            events.push( line );
        }
        cv.notify_one();
    }
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

void MintTokens( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid mint command format.\n";
        return;
    }
    genius_node.MintTokens( std::stoull( args[1] ) );
}

void CreateProcessingTransaction( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() != 3 )
    {
        std::cerr << "Invalid process command format.\n";
        return;
    }
    uint64_t price = std::stoull( args[2] );

    if ( genius_node.GetBalance() >= price )
    {
        std::string json_data = R"(
                {
                  "data": {
	                "type": "https",
	                "URL": "https://ipfs.filebase.io/ipfs/QmUDMvGQXbUKMsjmTzjf4ZuMx7tHx6Z4x8YH8RbwrgyGAf/"
                  },
                  "model": {
                    "name": "posenet",
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
		                "chunk_count": 24
	                }
                  ]
                }
                )";
        genius_node.ProcessImage( json_data /*args[1]*/,
                                      std::stoull( args[2] ) );
    }
    else
    {
        std::cout << "Insufficient funds to process image " << std::endl; 
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
    std::unique_lock<std::mutex> lock( keyboard_mutex );
    cv.wait( lock, [] { return !events.empty(); } );

    while ( !events.empty() )
    {
        std::cout << "simple event" << std::endl;
        std::string event = events.front();
        events.pop();

        auto arguments = split_string( event );
        if ( arguments.size() == 0 )
        {
            return;
        }
        else if ( arguments[0] == "process" )
        {
            CreateProcessingTransaction( arguments, genius_node );
        }
        else if ( arguments[0] == "mint" )
        {
            MintTokens( arguments, genius_node );
        }
        else if ( arguments[0] == "info" )
        {
            PrintAccountInfo( arguments, genius_node );
        }
        else if ( arguments[0] == "peer" )
        {
            genius_node.AddPeer( std::string{ arguments[1] } );
        }
        else
        {
            std::cerr << "Unknown command: " << arguments[0] << "\n";
        }
    }
}

DevConfig_st DEV_CONFIG{ "0xcafe", 0.65, 1.0, 0 , "./"};

int main( int argc, char *argv[] )
{
    std::thread input_thread( keyboard_input_thread );

    //Inputs

    sgns::GeniusNode node_instance( DEV_CONFIG );

    std::cout << "Insert \"process\", the image and the number of tokens to be" << std::endl;
    while ( true )
    {
        process_events( node_instance );
    }
    if ( input_thread.joinable() )
    {
        input_thread.join();
    }
    return 0;
}
