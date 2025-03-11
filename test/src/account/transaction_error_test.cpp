#include <gtest/gtest.h>
#include <memory>
#include <iostream>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>

/**
 * @class TestTransactionManagerCallbacks
 * @brief Controls transaction execution by pausing and resuming.
 */
class TestTransactionManagerCallbacks : public sgns::TransactionManagerCallbacks
{
public:
    /**
     * @brief Blocks transaction execution until resumed.
     */
    void onEnterSendTransaction() override
    {
        std::unique_lock<std::mutex> lock( mutex_ );
        cv_.wait( lock, [this] { return resumed_.load(); } );
    }

    /**
     * @brief Pauses transaction execution.
     */
    void pause()
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        resumed_.store( false );
        std::cout << "Pausing transaction sending." << std::endl;
    }

    /**
     * @brief Resumes transaction execution.
     */
    void resume()
    {
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            resumed_.store( true );
        }
        cv_.notify_all();
        std::cout << "Resuming transaction sending." << std::endl;
    }

private:
    std::mutex              mutex_;           ///< Protects condition variable.
    std::condition_variable cv_;              ///< Blocks transactions when paused.
    std::atomic<bool>       resumed_{ true }; ///< Controls transaction state.
};

class TransactionRecoveryTest : public ::testing::Test
{
protected:
    static inline sgns::GeniusNode                                *node_proc1 = nullptr;
    static inline sgns::GeniusNode                                *node_proc2 = nullptr;
    static inline std::shared_ptr<TestTransactionManagerCallbacks> node1Callbacks;
    static inline std::shared_ptr<TestTransactionManagerCallbacks> node2Callbacks;
    static inline DevConfig_st DEV_CONFIG  = { "0xcafe", "0.65", 1.0, 0, "./node21" };
    static inline DevConfig_st DEV_CONFIG2 = { "0xcafe", "0.65", 1.0, 0, "./node22" };
    static inline std::string  binary_path = "";

    static void SetUpTestSuite()
    {
        binary_path = boost::dll::program_location().parent_path().string();
        std::strncpy( DEV_CONFIG.BaseWritePath,
                      ( binary_path + "/node21/" ).c_str(),
                      sizeof( DEV_CONFIG.BaseWritePath ) );
        std::strncpy( DEV_CONFIG2.BaseWritePath,
                      ( binary_path + "/node22/" ).c_str(),
                      sizeof( DEV_CONFIG2.BaseWritePath ) );
        DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1]   = '\0';
        DEV_CONFIG2.BaseWritePath[sizeof( DEV_CONFIG2.BaseWritePath ) - 1] = '\0';
        node_proc1                                                         = new sgns::GeniusNode( DEV_CONFIG,
                                           "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           false );
        node_proc2                                                         = new sgns::GeniusNode( DEV_CONFIG2,
                                           "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        node_proc1->GetPubSub()->AddPeers( { node_proc2->GetPubSub()->GetLocalAddress() } );
        node_proc2->GetPubSub()->AddPeers( { node_proc1->GetPubSub()->GetLocalAddress() } );
        node1Callbacks = std::make_shared<TestTransactionManagerCallbacks>();
        node_proc1->setTransactionCallbacks( node1Callbacks );
        node2Callbacks = std::make_shared<TestTransactionManagerCallbacks>();
        node_proc2->setTransactionCallbacks( node2Callbacks );
    }

    static void TearDownTestSuite()
    {
        if ( node_proc1 != nullptr )
        {
            delete node_proc1;
            node_proc1 = nullptr;
        }
        if ( node_proc2 != nullptr )
        {
            delete node_proc2;
            node_proc2 = nullptr;
        }
    }

    void TearDown() override
    {
        if ( node1Callbacks )
        {
            node1Callbacks->resume();
        }
        if ( node2Callbacks )
        {
            node2Callbacks->resume();
        }
    }
};

TEST_F( TransactionRecoveryTest, MintTransactionLowTimeout )
{
    node1Callbacks->pause();
    auto balance_before       = node_proc1->GetBalance();
    auto out_txs_before       = node_proc1->GetOutTransactions();
    auto out_txs_count_before = out_txs_before.size();
    auto mint_result          = node_proc1->MintTokens( 100000000000, "", "", "", std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( mint_result.has_value() );
    std::this_thread::sleep_for( std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( node_proc1->GetOutTransactions().size(), out_txs_count_before );
    EXPECT_EQ( node_proc1->GetBalance(), balance_before );
}

TEST_F( TransactionRecoveryTest, MintAfterTimeoutFailure )
{
    node1Callbacks->pause();
    auto balance_before       = node_proc1->GetBalance();
    auto out_txs_before       = node_proc1->GetOutTransactions();
    auto out_txs_count_before = out_txs_before.size();
    auto mint_result          = node_proc1->MintTokens( 100000000000, "", "", "", std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( mint_result.has_value() );
    node1Callbacks->resume();
    mint_result = node_proc1->MintTokens( 1, "", "", "", std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() );
    EXPECT_EQ( node_proc1->GetOutTransactions().size(), out_txs_count_before + 1 );
    EXPECT_EQ( node_proc1->GetBalance(), balance_before + 1 );
}

TEST_F( TransactionRecoveryTest, TransferTransactionLowTimeout )
{
    auto mint_result = node_proc1->MintTokens( 1500000000,
                                               "",
                                               "",
                                               "",
                                               std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() );
    node1Callbacks->pause();
    auto node1_balance_before = node_proc1->GetBalance();
    auto node2_balance_before = node_proc2->GetBalance();
    auto node1_out_txs_before = node_proc1->GetOutTransactions().size();
    auto transfer_result      = node_proc1->TransferFunds( 1000000000,
                                                      node_proc2->GetAddress(),
                                                      std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( transfer_result.has_value() );
    std::this_thread::sleep_for( std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( node_proc2->GetBalance(), node2_balance_before );
    EXPECT_EQ( node_proc1->GetBalance(), node1_balance_before );
    EXPECT_EQ( node_proc1->GetOutTransactions().size(), node1_out_txs_before );
}

TEST_F( TransactionRecoveryTest, TransferAfterTimeoutFailure )
{
    auto mint_result = node_proc1->MintTokens( 1500000000,
                                               "",
                                               "",
                                               "",
                                               std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() );
    node1Callbacks->pause();
    auto node1_balance_before = node_proc1->GetBalance();
    auto node2_balance_before = node_proc2->GetBalance();
    auto node1_out_txs_before = node_proc1->GetOutTransactions().size();
    auto node2_in_txs_before  = node_proc2->GetInTransactions().size();
    auto transfer_result      = node_proc1->TransferFunds( 1000000000,
                                                      node_proc2->GetAddress(),
                                                      std::chrono::milliseconds( 1 ) );
    ASSERT_FALSE( transfer_result.has_value() );
    node1Callbacks->resume();
    transfer_result = node_proc1->TransferFunds( 1,
                                                 node_proc2->GetAddress(),
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer_result.has_value() );
    auto [transfer_tx_id, transfer_duration] = transfer_result.value();
    auto transfer_received                   = node_proc2->WaitForTransactionIncoming(
        transfer_tx_id,
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer_received );
    EXPECT_EQ( node_proc1->GetBalance(), node1_balance_before - 1 );
    EXPECT_EQ( node_proc2->GetBalance(), node2_balance_before + 1 );
    EXPECT_EQ( node_proc1->GetOutTransactions().size(), node1_out_txs_before + 1 );
    EXPECT_EQ( node_proc2->GetInTransactions().size(), node2_in_txs_before + 1 );
}

TEST_F( TransactionRecoveryTest, TransferWithReceiverStopped )
{
    node2Callbacks->pause();

    auto mint_result = node_proc1->MintTokens( 1500000000,
                                               "",
                                               "",
                                               "",
                                               std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mint_result.has_value() );

    auto node1_balance_before = node_proc1->GetBalance();
    auto node2_balance_before = node_proc2->GetBalance();
    auto node1_out_txs_before = node_proc1->GetOutTransactions().size();
    auto node2_in_txs_before  = node_proc2->GetInTransactions().size();

    auto transfer_result = node_proc1->TransferFunds( 1,
                                                      node_proc2->GetAddress(),
                                                      std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer_result.has_value() );
    auto [transfer_tx_id, transfer_duration] = transfer_result.value();

    bool transfer_received = node_proc2->WaitForTransactionIncoming(
        transfer_tx_id,
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    ASSERT_FALSE( transfer_received );
    EXPECT_EQ( node_proc1->GetBalance(), node1_balance_before - 1 );
    EXPECT_EQ( node_proc2->GetBalance(), node2_balance_before );
    EXPECT_EQ( node_proc1->GetOutTransactions().size(), node1_out_txs_before + 1 );
    EXPECT_EQ( node_proc2->GetInTransactions().size(), node2_in_txs_before );

    node2Callbacks->resume();

    transfer_received = node_proc2->WaitForTransactionIncoming(
        transfer_tx_id,
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer_received );

    EXPECT_EQ( node_proc1->GetBalance(), node1_balance_before - 1 );
    EXPECT_EQ( node_proc2->GetBalance(), node2_balance_before + 1 );
    EXPECT_EQ( node_proc1->GetOutTransactions().size(), node1_out_txs_before + 1 );
    EXPECT_EQ( node_proc2->GetInTransactions().size(), node2_in_txs_before + 1 );
}
