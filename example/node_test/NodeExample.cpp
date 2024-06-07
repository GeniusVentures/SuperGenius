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
#include "account/GeniusNode.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
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
        genius_node.ProcessImage( "QmUDMvGQXbUKMsjmTzjf4ZuMx7tHx6Z4x8YH8RbwrgyGAf" /*args[1]*/,
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

//This is not used at the moment. Static initialization order fiasco issues on node
//DevConfig_st DEV_CONFIG{ "0xcafe", 0.65, 1.0, 0 };

int main( int argc, char *argv[] )
{
    std::thread input_thread( keyboard_input_thread );

    //Inputs
    DevConfig_st local_config{ "0xbeef", 0.7, 1.0, 0 };

    sgns::GeniusNode node_instance( local_config );

    std::cout << "Insert \"process\", the image and the number of tokens to be" << std::endl;
    while ( true )
    {
        process_events( node_instance );
        //process_events( sgns::GeniusNode::GetInstance() );
    }
    if ( input_thread.joinable() )
    {
        input_thread.join();
    }
    return 0;
}
