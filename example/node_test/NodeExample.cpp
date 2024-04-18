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
#include "account/AccountManager.hpp"

std::mutex              mutex;
std::condition_variable cv;
std::queue<std::string> events;

void keyboard_input_thread()
{
    std::string line;
    while ( std::getline( std::cin, line ) )
    {
        {
            std::lock_guard<std::mutex> lock( mutex );
            events.push( line );
        }
        cv.notify_one();
    }
}

void PrintAccountInfo( const std::vector<std::string> &args/*, sgns::AccountManager &account_manager*/ )
{
    if ( args.size() != 1 )
    {
        std::cerr << "Invalid info command format.\n";
        return;
    }
    std::cout << "account info" << std::endl;
    //transaction_manager.PrintAccountInfo();

    //TODO - Create processing transaction
}
void MintTokens( const std::vector<std::string> &args, sgns::AccountManager &account_manager )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid process command format.\n";
        return;
    }
    account_manager.MintFunds(std::stoull( args[1] ));
}

std::vector<std::string> split_string( const std::string &str )
{
    std::istringstream       iss( str );
    std::vector<std::string> results( ( std::istream_iterator<std::string>( iss ) ), std::istream_iterator<std::string>() );
    return results;
}

void process_events( sgns::AccountManager &account_manager )
{
    std::unique_lock<std::mutex> lock( mutex );
    cv.wait( lock, [] { return !events.empty(); } );

    while ( !events.empty() )
    {
        std::cout << "simple event" <<std::endl;
        std::string event = events.front();
        events.pop();

        auto arguments = split_string( event );
        if ( arguments.size() == 0 )
        {
            return;
        }
        else if ( arguments[0] == "process" )
        {
            account_manager.ProcessImage("image.mnn", 50);
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
        else
        {
            std::cerr << "Unknown command: " << arguments[0] << "\n";
        }
    }
}

int main( int argc, char *argv[] )
{
    std::thread input_thread( keyboard_input_thread );

    //Inputs

    std::string own_wallet_address( argv[1] );
    std::string pubs_address( argv[2] );

    sgns::AccountManager node_instance( own_wallet_address );

    std::cout << "Insert \"process\", the image and the number of tokens to be" <<std::endl;
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

