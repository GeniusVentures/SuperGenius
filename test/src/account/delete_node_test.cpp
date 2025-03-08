#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "account/GeniusNode.hpp"

class NodeDeleteTest : public ::testing::Test
{
protected:
    static inline sgns::GeniusNode *node_proc = nullptr;

    static inline DevConfig_st DEV_CONFIG = { "0xcafe", "0.65", 1.0, 0, "./nodeDeleteTest/" };

    static inline std::string PRIVATE_KEY = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    static void SetUpTestSuite() {}

    static void TearDownTestSuite()
    {
        if ( node_proc != nullptr )
        {
            delete node_proc;
            node_proc = nullptr;
        }
    }
};

TEST_F( NodeDeleteTest, RecreateNodeWithSameConfigRetainsBalance )
{
    node_proc = new sgns::GeniusNode( DEV_CONFIG, PRIVATE_KEY.c_str(), false, false, 3000 );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    uint64_t balance_before = node_proc->GetBalance();

    auto mint_result = node_proc->MintTokens( 12345ULL,
                                              "",
                                              "",
                                              "",
                                              std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );

    ASSERT_TRUE( mint_result.has_value() );

    uint64_t balance_after_mint = node_proc->GetBalance();
    ASSERT_EQ( balance_after_mint, balance_before + 12345ULL );

    delete node_proc;
    node_proc = nullptr;

    node_proc = new sgns::GeniusNode( DEV_CONFIG, PRIVATE_KEY.c_str(), false, false, 3000 );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    uint64_t balance_after_reload = node_proc->GetBalance();
    ASSERT_EQ( balance_after_reload, balance_after_mint );
}
