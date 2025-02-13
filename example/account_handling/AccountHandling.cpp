/**
 * @file       AccountHandling.cpp
 * @brief      
 * @date       2024-03-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <cstdint>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "account/TransactionManager.hpp"
#include "AccountHelper.hpp"

using namespace boost::multiprecision;

std::vector<std::string> wallet_addr{ "0x4E8794BE4831C45D0699865028C8BE23D608C19C1E24371E3089614A50514262",
                                      "0x06DDC80283462181C02917CC3E99C7BC4BDB2856E19A392300A62DBA6262212C" };

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;

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

void CreateTransferTransaction( const std::vector<std::string> &args, sgns::TransactionManager &transaction_manager )
{
    if ( args.size() != 3 )
    {
        std::cerr << "Invalid transfer command format.\n";
        return;
    }
    uint64_t amount = std::stoull( args[1] );
    if ( !transaction_manager.TransferFunds( amount, { args[2] } ) )
    {
        std::cout << "Insufficient funds.\n";
    }
}

void CreateProcessingTransaction( const std::vector<std::string> &args, sgns::TransactionManager &transaction_manager )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid process command format.\n";
        return;
    }

    //TODO - Create processing transaction
}

void MintTokens( const std::vector<std::string> &args, sgns::TransactionManager &transaction_manager )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid process command format.\n";
        return;
    }
    transaction_manager.MintFunds( std::stoull( args[1] ), "", "", "" );
}

void PrintAccountInfo( const std::vector<std::string> &args, sgns::TransactionManager &transaction_manager )
{
    if ( args.size() != 1 )
    {
        std::cerr << "Invalid info command format.\n";
        return;
    }
    transaction_manager.PrintAccountInfo();

    //TODO - Create processing transaction
}

std::vector<std::string> split_string( const std::string &str )
{
    std::istringstream       iss( str );
    std::vector<std::string> results( ( std::istream_iterator<std::string>( iss ) ),
                                      std::istream_iterator<std::string>() );
    return results;
}

void process_events( sgns::TransactionManager &transaction_manager )
{
    std::unique_lock<std::mutex> lock( keyboard_mutex );
    cv.wait( lock, [] { return !events.empty(); } );

    while ( !events.empty() )
    {
        std::string event = events.front();
        events.pop();

        auto arguments = split_string( event );
        if ( arguments.size() == 0 )
        {
            return;
        }
        if ( arguments[0] == "transfer" )
        {
            CreateTransferTransaction( arguments, transaction_manager );
        }
        else if ( arguments[0] == "process" )
        {
            CreateProcessingTransaction( arguments, transaction_manager );
        }
        else if ( arguments[0] == "info" )
        {
            PrintAccountInfo( arguments, transaction_manager );
        }
        else if ( arguments[0] == "mint" )
        {
            MintTokens( arguments, transaction_manager );
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

    size_t      serviceindex = std::strtoul( argv[1], nullptr, 10 );
    std::string own_wallet_address( argv[2] );

    AccountKey2   key;
    DevConfig_st2 local_config{ "0xbeefbeef", "0.65" };

    strncpy( key, argv[2], sizeof( key ) );

    sgns::AccountHelper helper( key, local_config, "deadbeef" );

    while ( true )
    {
        process_events( *( helper.GetManager() ) );
    }

    if ( input_thread.joinable() )
    {
        input_thread.join();
    }
    return 0;
}
