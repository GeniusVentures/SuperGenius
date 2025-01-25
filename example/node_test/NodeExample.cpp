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
std::string current_input;
std::atomic<bool> finished(false);


void enable_raw_mode() {
#ifdef _WIN32
    DWORD mode;
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hInput, &mode);
    SetConsoleMode(hInput, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
#else
    termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
#endif
}

void disable_raw_mode() {
#ifdef _WIN32
    DWORD mode;
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleMode(hInput, mode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
#else
    termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= ICANON | ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
#endif
}

void clear_line() {
    std::cout << "\r\033[K";  // Clear the current line
}

void redraw_prompt() {
    clear_line();
    std::cout << "> " << current_input << std::flush;  // Redraw the input prompt
}

void keyboard_input_thread() {
    enable_raw_mode();

    while (!finished) {
        char ch;
        std::cin.get(ch);

        {
            std::lock_guard<std::mutex> lock(keyboard_mutex);
            if (ch == '\n' || ch == '\r') {
                // Check for both newline and carriage return
                if (!current_input.empty()) {
                    events.push(current_input);
                    current_input.clear();
                    cv.notify_one();  // Notify the event processor
                }
                std::cout << std::endl;
            }
            else if (ch == 127 || ch == '\b') { // Handle backspace
                if (!current_input.empty()) {
                    current_input.pop_back();
                }
            }
            else if (std::isprint(ch) || std::isspace(ch)) {
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

void MintTokens( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid mint command format.\n";
        return;
    }
    genius_node.MintTokens( std::stoull( args[1] ), "", "", "" );
}

void CreateProcessingTransaction( const std::vector<std::string> &args, sgns::GeniusNode &genius_node )
{
    if ( args.size() != 4 )
    {
        std::cerr << "Invalid process command format.\n";
        return;
    }
    uint64_t price = std::stoull( args[2] );
    std::string procxml_loc = args[3];
    std::string json_data = "";
    if (procxml_loc.size() > 0)
    {
        libp2p::protocol::kademlia::Config kademlia_config;
        kademlia_config.randomWalk.enabled = true;
        kademlia_config.randomWalk.interval = std::chrono::seconds(300);
        kademlia_config.requestConcurency = 20;
        auto injector = libp2p::injector::makeHostInjector(
            libp2p::injector::makeKademliaInjector(libp2p::injector::useKademliaConfig(kademlia_config)));
        auto ioc = injector.create<std::shared_ptr<boost::asio::io_context>>();

        boost::asio::io_context::executor_type                                   executor = ioc->get_executor();
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard(executor);
        FileManager::GetInstance().InitializeSingletons();
        auto data = FileManager::GetInstance().LoadASync(procxml_loc, false, false, ioc, [ioc](const sgns::AsyncError::CustomResult& status) {
            if (status.has_value())
            {
                std::cout << "Success: " << status.value().message << std::endl;
            }
            else
            {
                std::cout << "Error: " << status.error() << std::endl;
            };
            }, [ioc, &json_data](std::shared_ptr<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>> buffers) {
                json_data = std::string(buffers->second[0].begin(), buffers->second[0].end());
                },"file");
            ioc->run();
            ioc->stop();
            ioc->reset();
    }

    if ( genius_node.GetBalance() >= price )
    {
        
        if (json_data.empty())
        {
            std::cerr << "No input XML obtained" << std::endl;;
            return;
        }
        // std::string json_data = R"(
        //         {
        //         "data": {
        //             "type": "https",
        //             "URL": "https://ipfs.filebase.io/ipfs/QmdHvvEXRUgmyn1q3nkQwf9yE412Vzy5gSuGAukHRLicXA/"
        //         },
        //         "model": {
        //             "name": "mnnimage",
        //             "file": "model.mnn"
        //         },
        //         "input": [
        //             {
        //                 "image": "data/ballet.data",
        //                 "block_len": 4860000 ,
        //                 "block_line_stride": 5400,
        //                 "block_stride": 0,
        //                 "chunk_line_stride": 1080,
        //                 "chunk_offset": 0,
        //                 "chunk_stride": 4320,
        //                 "chunk_subchunk_height": 5,
        //                 "chunk_subchunk_width": 5,
        //                 "chunk_count": 25,
        //                 "channels": 4
        //             },
        //             {
        //                 "image": "data/frisbee3.data",
        //                 "block_len": 786432 ,
        //                 "block_line_stride": 1536,
        //                 "block_stride": 0,
        //                 "chunk_line_stride": 384,
        //                 "chunk_offset": 0,
        //                 "chunk_stride": 1152,
        //                 "chunk_subchunk_height": 4,
        //                 "chunk_subchunk_width": 4,
        //                 "chunk_count": 16,
        //                 "channels": 3
        //             }
        //         ]
        //         }
        //        )";
        genius_node.ProcessImage( json_data /*args[1]*/
                                      );
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

void process_events(sgns::GeniusNode& genius_node) {
    while (!finished) {
        std::unique_lock<std::mutex> lock(keyboard_mutex);
        cv.wait(lock, [] { return !events.empty() || finished; });

        while (!events.empty()) {
            std::string event = std::move(events.front());
            events.pop();

            lock.unlock();  // Unlock while processing

            auto arguments = split_string(event);
            if (arguments.empty()) {
                std::cerr << "Invalid command\n";
            }
            else if (arguments[0] == "process") {
                CreateProcessingTransaction(arguments, genius_node);
            }
            else if (arguments[0] == "mint") {
                MintTokens(arguments, genius_node);
            }
            else if (arguments[0] == "info") {
                PrintAccountInfo(arguments, genius_node);
            }
            else if (arguments[0] == "peer") {
                if (arguments.size() > 1) {
                    genius_node.AddPeer(arguments[1]);
                }
                else {
                    std::cerr << "Invalid peer command\n";
                }
            }
            else {
                std::cerr << "Unknown command: " << arguments[0] << "\n";
            }

            lock.lock();  // Re-lock before checking the condition again
        }
    }
}


void periodic_processing(sgns::GeniusNode &genius_node) {
    while (!finished) {
        std::this_thread::sleep_for(std::chrono::minutes(30)); // Wait for 30 minutes
        if (finished) break;  // Exit if the application is shutting down
        
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
                genius_node.ProcessImage( json_data /*args[1]*/
                                      );
    }
}

DevConfig_st DEV_CONFIG{ "0xcafe", 650000000, 1.0, 0 , "./"};

int main( int argc, char *argv[] )
{
    std::thread input_thread(keyboard_input_thread);

    
    //Inputs

    sgns::GeniusNode node_instance( DEV_CONFIG, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef", true, false );

    std::thread processing_thread(periodic_processing, std::ref(node_instance));
    std::cout << "Insert \"process\", the image and the number of tokens to be" << std::endl;
    redraw_prompt();
    //while ( !finished )
    //{
        process_events( node_instance );
    //}
    if ( input_thread.joinable() )
    {
        input_thread.join();
    }
    if (processing_thread.joinable()) {
        processing_thread.join();
    }
    return 0;
}
