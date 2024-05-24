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

void PrintAccountInfo( const std::vector<std::string> &args, sgns::GeniusNode &account_manager )
{
    if ( args.size() != 1 )
    {
        std::cerr << "Invalid info command format.\n";
        return;
    }
    std::cout << "Balance: " << account_manager.GetBalance() << std::endl;
}

void MintTokens( const std::vector<std::string> &args, sgns::GeniusNode &account_manager )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid process command format.\n";
        return;
    }
    account_manager.MintTokens( std::stoull( args[1] ) );
}

std::vector<std::string> split_string( const std::string &str )
{
    std::istringstream       iss( str );
    std::vector<std::string> results( ( std::istream_iterator<std::string>( iss ) ),
                                      std::istream_iterator<std::string>() );
    return results;
}

void process_events( sgns::GeniusNode &account_manager )
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
            account_manager.ProcessImage( arguments[1], 100 );
            //CreateProcessingTransaction( arguments, transaction_manager );
        }
        else if ( arguments[0] == "mint" )
        {
            MintTokens( arguments, account_manager );

            //CreateProcessingTransaction( arguments, transaction_manager );
        }
        else if ( arguments[0] == "info" )
        {
            PrintAccountInfo( arguments, account_manager );
        }
        else if ( arguments[0] == "peer" )
        {
            account_manager.AddPeer( std::string{ arguments[1] } );
        }
        else
        {
            std::cerr << "Unknown command: " << arguments[0] << "\n";
        }
    }
}

//This is not used at the moment. Static initialization order fiasco issues on node
//AccountKey   ACCOUNT_KEY = "1";
//DevConfig_st DEV_CONFIG{ "0xcafe", 0.65f };

int main( int argc, char *argv[] )
{
    std::thread input_thread( keyboard_input_thread );

    //Inputs
    AccountKey   key;
    DevConfig_st local_config{ "0xbeef", 0.7f };

    strncpy( key, argv[1], sizeof( key ) );

    sgns::GeniusNode node_instance( key, local_config );

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
