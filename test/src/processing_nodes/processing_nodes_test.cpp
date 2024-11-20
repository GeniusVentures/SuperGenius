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

#include <functional>
#include <thread>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
class ProcessingNodesTest : public ::testing::Test
{
protected:
    static sgns::GeniusNode* node_main;
    static sgns::GeniusNode* node_proc1;
    static sgns::GeniusNode* node_proc2;

    static DevConfig_st DEV_CONFIG;
    static DevConfig_st DEV_CONFIG2;
    static DevConfig_st DEV_CONFIG3;

    static void SetUpTestSuite()
    {

        node_main = new sgns::GeniusNode(DEV_CONFIG, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef", false);
        node_proc1 = new sgns::GeniusNode(DEV_CONFIG2, "livebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef", false);
        node_proc2 = new sgns::GeniusNode(DEV_CONFIG3, "zzombeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef", false);
        std::cout << "nodes made" << std::endl;
    }

    static void TearDownTestSuite()
    {
        std::cout << "Tear Down" << std::endl;
        delete node_main;
        std::cout << "Tear Down2" << std::endl;
        delete node_proc1;
        std::cout << "Tear Down3" << std::endl;
        delete node_proc2;
        std::cout << "Tear Down4" << std::endl;
    }
};

// Static member initialization
sgns::GeniusNode* ProcessingNodesTest::node_main = nullptr;
sgns::GeniusNode* ProcessingNodesTest::node_proc1 = nullptr;
sgns::GeniusNode* ProcessingNodesTest::node_proc2 = nullptr;

DevConfig_st ProcessingNodesTest::DEV_CONFIG = { "0xcafe", 0.65, 1.0, 0, "./node1" };
DevConfig_st ProcessingNodesTest::DEV_CONFIG2 = { "0xcafe", 0.65, 1.0, 0, "./node2" };
DevConfig_st ProcessingNodesTest::DEV_CONFIG3 = { "0xcafe", 0.65, 1.0, 0, "./node3" };

TEST_F(ProcessingNodesTest, ProcessNodesAddress)
{
    std::string address_main = node_main->GetAddress();
    std::string address_proc1 = node_proc1->GetAddress();
    std::string address_proc2 = node_proc2->GetAddress();
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
    std::string address_main = node_main->GetPubSub()->GetLocalAddress();
    std::string address_proc1 = node_proc1->GetPubSub()->GetLocalAddress();
    std::string address_proc2 = node_proc2->GetPubSub()->GetLocalAddress();
    // std::cout << "Addresses " << std::endl;
    // std::cout << "Main Node: " << address_main << std::endl;
    // std::cout << "Proc Node 1: " << address_proc1 << std::endl;
    // std::cout << "Proc Node 2: " << address_proc2 << std::endl;
    EXPECT_NE(address_main, address_proc1) << "node_main and node_proc1 have the same address!";
    EXPECT_NE(address_main, address_proc2) << "node_main and node_proc2 have the same address!";
    EXPECT_NE(address_proc1, address_proc2) << "node_proc1 and node_proc2 have the same address!";
}

TEST_F(ProcessingNodesTest, ProcessNodesTransactionsCount)
{
    //Bootstrap everyone to connect to each other
    std::vector<std::string> bootstrappers;
    bootstrappers.push_back(node_proc1->GetPubSub()->GetLocalAddress());
    bootstrappers.push_back(node_proc2->GetPubSub()->GetLocalAddress());
    node_main->GetPubSub()->AddPeers(bootstrappers);
    // bootstrappers.clear();
    // bootstrappers.push_back(node_main.GetPubSub()->GetLocalAddress());
    // bootstrappers.push_back(node_proc2.GetPubSub()->GetLocalAddress());   
    // node_proc1.GetPubSub()->AddPeers(bootstrappers);
    // bootstrappers.clear();
    // bootstrappers.push_back(node_main.GetPubSub()->GetLocalAddress());
    // bootstrappers.push_back(node_proc1.GetPubSub()->GetLocalAddress());   
    // node_proc2.GetPubSub()->AddPeers(bootstrappers);    
    node_main->MintTokens(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    int transcount_main = node_main->GetTransactions().size();
    int transcount_node1 = node_proc1->GetTransactions().size();
    int transcount_node2 = node_proc2->GetTransactions().size();
    std::cout << "Count 1" << transcount_main << std::endl;
    std::cout << "Count 2" << transcount_node1 << std::endl;
    std::cout << "Count 3" << transcount_node2 << std::endl;

    //ASSERT_EQ(1, transcount_main);
    ASSERT_EQ(transcount_main, transcount_node1);
    ASSERT_EQ(transcount_main, transcount_node2);
}