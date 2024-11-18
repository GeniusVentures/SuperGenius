#include <gtest/gtest.h>

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
class ProcessingNodesTest : public ::testing::Test
{
public:
    virtual void SetUp() override
    {
        
    }
};
DevConfig_st DEV_CONFIG{ "0xcafe", 0.65, 1.0, 0 , "./node1"};
DevConfig_st DEV_CONFIG2{ "0xcafe", 0.65, 1.0, 0 , "./node2"};
DevConfig_st DEV_CONFIG3{ "0xcafe", 0.65, 1.0, 0 , "./node3"};

TEST_F(ProcessingNodesTest, ProcessNodesAddress)
{
    sgns::GeniusNode node_main( DEV_CONFIG, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
    sgns::GeniusNode node_proc1( DEV_CONFIG2, "livebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
    sgns::GeniusNode node_proc2( DEV_CONFIG3, "zzombeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
    std::string address_main = node_main.GetAddress();
    std::string address_proc1 = node_proc1.GetAddress();
    std::string address_proc2 = node_proc2.GetAddress();
    // std::cout << "Addresses " << std::endl;
    // std::cout << "Main Node: " << address_main << std::endl;
    // std::cout << "Proc Node 1: " << address_proc1 << std::endl;
    // std::cout << "Proc Node 2: " << address_proc2 << std::endl;

    EXPECT_NE(address_main, address_proc1) << "node_main and node_proc1 have the same address!";
    EXPECT_NE(address_main, address_proc2) << "node_main and node_proc2 have the same address!";
    EXPECT_NE(address_proc1, address_proc2) << "node_proc1 and node_proc2 have the same address!";
}

TEST_F(ProcessingNodesTest, ProcessNodesPubsubs)
{
    sgns::GeniusNode node_main( DEV_CONFIG, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
    sgns::GeniusNode node_proc1( DEV_CONFIG2, "livebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
    sgns::GeniusNode node_proc2( DEV_CONFIG3, "zzombeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
    std::string address_main = node_main.GetPubSub()->GetLocalAddress();
    std::string address_proc1 = node_proc1.GetPubSub()->GetLocalAddress();
    std::string address_proc2 = node_proc2.GetPubSub()->GetLocalAddress();
    // std::cout << "Addresses " << std::endl;
    // std::cout << "Main Node: " << address_main << std::endl;
    // std::cout << "Proc Node 1: " << address_proc1 << std::endl;
    // std::cout << "Proc Node 2: " << address_proc2 << std::endl;
    EXPECT_NE(address_main, address_proc1) << "node_main and node_proc1 have the same address!";
    EXPECT_NE(address_main, address_proc2) << "node_main and node_proc2 have the same address!";
    EXPECT_NE(address_proc1, address_proc2) << "node_proc1 and node_proc2 have the same address!";

}