#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>

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
#include <boost/multiprecision/cpp_int.hpp>
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "account/TransferTransaction.hpp"
#include "testutil/outcome.hpp"
#include "proof/TransferProof.hpp"
#include "testutil/wait_condition.hpp"

namespace sgns
{
    class TransactionSyncTest : public ::testing::Test
    {
    protected:
        static inline std::shared_ptr<sgns::GeniusNode> node_proc1;
        static inline std::shared_ptr<sgns::GeniusNode> node_proc2;
        static inline std::shared_ptr<sgns::GeniusNode> full_node;

        static inline DevConfig_st DEV_CONFIG  = { "0xcafe",
                                                   "0.65",
                                                   "1.0",
                                                   sgns::TokenID::FromBytes( { 0x00 } ),
                                                   "./node10" };
        static inline DevConfig_st DEV_CONFIG2 = { "0xcafe",
                                                   "0.65",
                                                   "1.0",
                                                   sgns::TokenID::FromBytes( { 0x00 } ),
                                                   "./node20" };
        static inline DevConfig_st DEV_CONFIG3 = { "0xcafe",
                                                   "0.65",
                                                   "1.0",
                                                   sgns::TokenID::FromBytes( { 0x00 } ),
                                                   "./node_full" };

        static inline std::string binary_path = "";

        static void SetUpTestSuite()
        {
            std::string binary_path = boost::dll::program_location().parent_path().string();
            std::strncpy( DEV_CONFIG.BaseWritePath,
                          ( binary_path + "/node10/" ).c_str(),
                          sizeof( DEV_CONFIG.BaseWritePath ) );
            std::strncpy( DEV_CONFIG2.BaseWritePath,
                          ( binary_path + "/node20/" ).c_str(),
                          sizeof( DEV_CONFIG2.BaseWritePath ) );
            std::strncpy( DEV_CONFIG3.BaseWritePath,
                          ( binary_path + "/node_full/" ).c_str(),
                          sizeof( DEV_CONFIG3.BaseWritePath ) );

            // Ensure null termination in case the string is too long
            DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1]   = '\0';
            DEV_CONFIG2.BaseWritePath[sizeof( DEV_CONFIG2.BaseWritePath ) - 1] = '\0';
            DEV_CONFIG3.BaseWritePath[sizeof( DEV_CONFIG3.BaseWritePath ) - 1] = '\0';

            std::string full_node_pub_address =
                "8b095989e76c1fef19451abc6837c8da086b9196a65bb7335f92a8aad48226319ab3f85c54d932c914c49f39c679314bc2bb6fad905d66d96969834e9c9f12b3";
            Blockchain::SetAuthorizedFullNodeAddress( full_node_pub_address );

            full_node = sgns::GeniusNode::New( DEV_CONFIG3,
                                               "feedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                               false,
                                               false,
                                               40001,
                                               true );
            test::assertWaitForCondition(
                [&]() { return full_node->GetTransactionManagerState() == TransactionManager::State::READY; },
                std::chrono::milliseconds( 40000 ),
                "full_node not ready" );

            node_proc1 = sgns::GeniusNode::New( DEV_CONFIG,
                                                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                                false,
                                                false );

            node_proc2 = sgns::GeniusNode::New( DEV_CONFIG2,
                                                "cafebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                                false,
                                                false );

            node_proc1->GetPubSub()->AddPeers(
                { node_proc2->GetPubSub()->GetInterfaceAddress(), full_node->GetPubSub()->GetInterfaceAddress() } );
            node_proc2->GetPubSub()->AddPeers( { full_node->GetPubSub()->GetInterfaceAddress() } );

            test::assertWaitForCondition(
                [&]() { return node_proc1->GetTransactionManagerState() == TransactionManager::State::READY; },
                std::chrono::milliseconds( 40000 ),
                "node_proc1 not ready" );
            test::assertWaitForCondition(
                [&]() { return node_proc2->GetTransactionManagerState() == TransactionManager::State::READY; },
                std::chrono::milliseconds( 40000 ),
                "node_proc2 not ready" );
        }

        static void TearDownTestSuite()
        {
            node_proc1.reset();
            node_proc2.reset();
            full_node.reset();
        }

        outcome::result<sgns::TransactionManager::TransactionPair> CreateTransfer(
            std::shared_ptr<sgns::GeniusAccount> account,
            UTXOManager                         &utxo_manager,
            uint64_t                             amount,
            const std::string                   &destination )
        {
            OUTCOME_TRY( auto &&params,
                         utxo_manager.CreateTxParameter( amount, destination, sgns::TokenID::FromBytes( { 0x00 } ) ) );

            auto timestamp = std::chrono::system_clock::now();

            SGTransaction::DAGStruct dag;
            dag.set_previous_hash( "" );
            dag.set_nonce( account->ReserveNextNonce() );
            dag.set_source_addr( account->GetAddress() );
            dag.set_timestamp( timestamp.time_since_epoch().count() );
            dag.set_uncle_hash( "" );
            dag.set_data_hash( "" ); //filled by transaction class

            auto transfer_transaction = std::make_shared<sgns::TransferTransaction>(
                sgns::TransferTransaction::New( params.first, params.second, dag ) );
            std::optional<std::vector<uint8_t>> maybe_proof;

            TransferProof prover( static_cast<uint64_t>( utxo_manager.GetBalance() ), static_cast<uint64_t>( amount ) );
            OUTCOME_TRY( ( auto &&, proof_result ), prover.GenerateFullProof() );

            maybe_proof = std::move( proof_result );

            utxo_manager.ReserveUTXOs( params.first );
            return std::make_pair( transfer_transaction, maybe_proof );
        }

        std::shared_ptr<sgns::GeniusAccount> GetAccountFromNode( sgns::GeniusNode &node )
        {
            return node.account_;
        }

        UTXOManager *GetUTXOManagerFromNode( sgns::GeniusNode &node )
        {
            return &node.utxo_manager_;
        }

        void SendPair( sgns::GeniusNode &node, std::shared_ptr<IGeniusTransactions> tx, std::vector<uint8_t> proof )
        {
            node.SendTransactionAndProof( tx, proof );
        }
    };

    TEST_F( TransactionSyncTest, TransactionSimpleTransfer )
    {
        auto balance_1_before = node_proc1->GetBalance();
        auto balance_2_before = node_proc2->GetBalance();

        // Mint tokens with timeout
        auto mint_result = node_proc1->MintTokens( 10000000000,
                                                   "",
                                                   "",
                                                   sgns::TokenID::FromBytes( { 0x00 } ),
                                                   std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

        auto [mint_tx_id, mint_duration] = mint_result.value();
        std::cout << "Mint transaction completed in " << mint_duration << " ms" << std::endl;

        // Verify balance after minting
        EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 10000000000 )
            << "Correct Balance of outgoing transactions";

        // Transfer funds with timeout
        auto transfer_result = node_proc1->TransferFunds( 10000000000,
                                                          node_proc2->GetAddress(),
                                                          sgns::TokenID::FromBytes( { 0x00 } ),
                                                          std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( transfer_result.has_value() ) << "Transfer transaction failed or timed out";
        auto [transfer_tx_id, transfer_duration] = transfer_result.value();
        std::cout << "Transfer transaction completed in " << transfer_duration << " ms" << std::endl;

        auto start_time        = std::chrono::steady_clock::now();
        auto transfer_received = node_proc2->WaitForTransactionIncoming(
            transfer_tx_id,
            std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
        EXPECT_EQ( transfer_received, TransactionManager::TransactionStatus::CONFIRMED );
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                               start_time )
                            .count();
        std::cout << "Transfer Received transaction completed in " << duration << " ms" << std::endl;

        start_time                       = std::chrono::steady_clock::now();
        auto full_node_transfer_received = full_node->WaitForTransactionIncoming(
            transfer_tx_id,
            std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
        EXPECT_EQ( full_node_transfer_received, TransactionManager::TransactionStatus::CONFIRMED );
        duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                          start_time )
                       .count();
        std::cout << "Full Node received transaction completed in " << duration << " ms" << std::endl;

        // Verify node_proc1's balance decreased
        EXPECT_EQ( node_proc1->GetBalance(), balance_1_before ) << "Transfer should decrease node_proc1's balance";

        // Verify node_proc2's balance increased
        EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 10000000000 )
            << "Transfer should increase node_proc2's balance";
    }

    TEST_F( TransactionSyncTest, TransactionMintSync )
    {
        auto balance_1_before = node_proc1->GetBalance();
        auto balance_2_before = node_proc2->GetBalance();

        // Connect the nodes
        node_proc1->GetPubSub()->AddPeers(
            { node_proc2->GetPubSub()->GetInterfaceAddress(), full_node->GetPubSub()->GetInterfaceAddress() } );
        node_proc2->GetPubSub()->AddPeers( { full_node->GetPubSub()->GetInterfaceAddress() } );

        test::assertWaitForCondition(
            [&]() { return node_proc1->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "node_proc1 not synched" );
        test::assertWaitForCondition(
            [&]() { return node_proc2->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "node_proc2 not synched" );
        test::assertWaitForCondition(
            [&]() { return full_node->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "full_node not synched" );

        // Mint tokens on node_proc1
        std::vector<uint64_t> mint_amounts =
            { 10000000000, 20000000000, 30000000000, 40000000000, 50000000000, 60000000000 };

        for ( auto amount : mint_amounts )
        {
            auto mint_result = node_proc1->MintTokens( amount,
                                                       "",
                                                       "",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
            ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction of " << amount << " failed or timed out";

            auto [tx_id, duration] = mint_result.value();
            std::cout << "Mint transaction of " << amount << " completed in " << duration << " ms" << std::endl;
        }

        // Mint tokens on node_proc2
        auto mint_result1 = node_proc2->MintTokens( 10000000000,
                                                    "",
                                                    "",
                                                    sgns::TokenID::FromBytes( { 0x00 } ),
                                                    std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( mint_result1.has_value() ) << "Mint transaction failed or timed out";

        auto mint_result2 = node_proc2->MintTokens( 20000000000,
                                                    "",
                                                    "",
                                                    sgns::TokenID::FromBytes( { 0x00 } ),
                                                    std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( mint_result2.has_value() ) << "Mint transaction failed or timed out";

        // Verify balances after minting
        EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 210000000000 )
            << "Correct Balance of outgoing transactions";
        EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 30000000000 )
            << "Correct Balance of outgoing transactions";

        // Transfer funds
        auto transfer_result1 = node_proc1->TransferFunds( 10000000000,
                                                           node_proc2->GetAddress(),
                                                           sgns::TokenID::FromBytes( { 0x00 } ),
                                                           std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( transfer_result1.has_value() ) << "Transfer transaction failed or timed out";
        auto [transfer_tx_id1, transfer_duration1] = transfer_result1.value();

        auto transfer_result2 = node_proc1->TransferFunds( 20000000000,
                                                           node_proc2->GetAddress(),
                                                           sgns::TokenID::FromBytes( { 0x00 } ),
                                                           std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( transfer_result2.has_value() ) << "Transfer transaction failed or timed out";
        auto [transfer_tx_id2, transfer_duration2] = transfer_result2.value();

        // wait for both transfers to happen or timeout.
        auto start_time = std::chrono::steady_clock::now();

        auto transfer_received = node_proc2->WaitForTransactionIncoming(
            transfer_tx_id1,
            std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
        EXPECT_EQ( transfer_received, TransactionManager::TransactionStatus::CONFIRMED );
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                               start_time )
                            .count();
        std::cout << "node2 Transfer Received transaction completed in " << duration << " ms" << std::endl;

        // wait for both transfers to happen or timeout.
        start_time        = std::chrono::steady_clock::now();
        transfer_received = node_proc2->WaitForTransactionIncoming(
            transfer_tx_id2,
            std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
        EXPECT_EQ( transfer_received, TransactionManager::TransactionStatus::CONFIRMED );
        duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                          start_time )
                       .count();
        std::cout << "node1 Transfer Received transaction completed in " << duration << " ms" << std::endl;

        // Verify balances after transfers
        EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 180000000000 )
            << "Correct Balance of outgoing transactions";
        EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 60000000000 )
            << "Correct Balance of outgoing transactions";
    }

    TEST_F( TransactionSyncTest, TransactionTransferSync )
    {
        auto balance_1_before = node_proc1->GetBalance();
        auto balance_2_before = node_proc2->GetBalance();

        // Connect the nodes
        node_proc1->GetPubSub()->AddPeers(
            { node_proc2->GetPubSub()->GetInterfaceAddress(), full_node->GetPubSub()->GetInterfaceAddress() } );
        node_proc2->GetPubSub()->AddPeers( { full_node->GetPubSub()->GetInterfaceAddress() } );

        test::assertWaitForCondition(
            [&]() { return node_proc1->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "node_proc1 not synched" );
        test::assertWaitForCondition(
            [&]() { return node_proc2->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "node_proc2 not synched" );
        test::assertWaitForCondition(
            [&]() { return full_node->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "full_node not synched" );

        // Mint tokens on node_proc1
        std::vector<uint64_t> xfer_amounts[2] = {
            { 10000000000, 20000000000, 30000000000, 30000000000, 20000000000, 10000000000 },
            { 10000000000, 20000000000, 20000000000, 3000000000, 3000000000, 3000000000 },
        };

        std::vector<std::string> txIDs[2];

        uint64_t xfer_amount_1 = 0;
        uint64_t xfer_amount_2 = 0;

        for ( size_t index = 0; index < xfer_amounts[0].size(); index++ )
        {
            auto xfer_amount       = xfer_amounts[0][index];
            xfer_amount_1         += xfer_amount;
            auto transfer_result1  = node_proc1->TransferFunds(
                xfer_amount,
                node_proc2->GetAddress(),
                sgns::TokenID::FromBytes( { 0x00 } ),
                std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
            ASSERT_TRUE( transfer_result1.has_value() ) << "Transfer transaction failed or timed out";
            auto [transfer_tx_id1, transfer_duration1] = transfer_result1.value();
            std::cout << "node 1 Transfer transaction completed in " << transfer_duration1 << " ms" << std::endl;

            txIDs[0].push_back( transfer_tx_id1 );

            xfer_amount            = xfer_amounts[1][index];
            xfer_amount_2         += xfer_amount;
            auto transfer_result2  = node_proc2->TransferFunds(
                xfer_amount,
                node_proc1->GetAddress(),
                sgns::TokenID::FromBytes( { 0x00 } ),
                std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
            ASSERT_TRUE( transfer_result2.has_value() ) << "Transfer transaction failed or timed out";
            auto [transfer_tx_id2, transfer_duration2] = transfer_result2.value();
            std::cout << "node 2 Transfer transaction completed in " << transfer_duration2 << " ms" << std::endl;

            txIDs[1].push_back( transfer_tx_id2 );
        }

        for ( size_t index = 0; index < txIDs[0].size(); index++ )
        {
            // wait for both transfers to happen or timeout.
            auto start_time1        = std::chrono::steady_clock::now();
            auto transfer_tx_id2    = txIDs[1][index];
            auto transfer_received1 = node_proc1->WaitForTransactionIncoming(
                transfer_tx_id2,
                std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
            EXPECT_EQ( transfer_received1, TransactionManager::TransactionStatus::CONFIRMED );
            auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                                    start_time1 )
                                 .count();
            std::cout << "node2 Transfer Received transaction completed in " << duration1 << " ms" << std::endl;

            // wait for both transfers to happen or timeout.
            auto start_time2        = std::chrono::steady_clock::now();
            auto transfer_tx_id1    = txIDs[0][index];
            auto transfer_received2 = node_proc2->WaitForTransactionIncoming(
                transfer_tx_id1,
                std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
            EXPECT_EQ( transfer_received2, TransactionManager::TransactionStatus::CONFIRMED );
            auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                                    start_time2 )
                                 .count();
            std::cout << "node2 Transfer Received transaction completed in " << duration2 << " ms" << std::endl;
        }

        // Verify balances after transfers
        EXPECT_EQ( node_proc1->GetBalance(), balance_1_before - xfer_amount_1 + xfer_amount_2 )
            << "Correct Balance of outgoing transactions";
        EXPECT_EQ( node_proc2->GetBalance(), balance_2_before - xfer_amount_2 + xfer_amount_1 )
            << "Correct Balance of outgoing transactions";
    }

    TEST_F( TransactionSyncTest, InvalidTransactionTest )
    {
        auto balance_1_before = node_proc1->GetBalance();
        auto balance_2_before = node_proc2->GetBalance();

        // Connect the nodes
        node_proc1->GetPubSub()->AddPeers(
            { node_proc2->GetPubSub()->GetInterfaceAddress(), full_node->GetPubSub()->GetInterfaceAddress() } );
        node_proc2->GetPubSub()->AddPeers( { full_node->GetPubSub()->GetInterfaceAddress() } );

        test::assertWaitForCondition(
            [&]() { return node_proc1->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "node_proc1 not synched" );
        test::assertWaitForCondition(
            [&]() { return node_proc2->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "node_proc2 not synched" );
        test::assertWaitForCondition(
            [&]() { return full_node->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "full_node not synched" );

        // Mint tokens with timeout
        auto mint_result = node_proc1->MintTokens( 10000000000,
                                                   "",
                                                   "",
                                                   TokenID::FromBytes( { 0x00 } ),
                                                   std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";
        mint_result = node_proc1->MintTokens( 10000000000,
                                              "",
                                              "",
                                              TokenID::FromBytes( { 0x00 } ),
                                              std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

        auto [mint_tx_id, mint_duration] = mint_result.value();
        std::cout << "Mint transaction completed in " << mint_duration << " ms" << std::endl;

        auto balance_1_before_invalid = balance_1_before + 20000000000;

        // Verify balance after minting
        EXPECT_EQ( node_proc1->GetBalance(), balance_1_before_invalid ) << "Correct Balance of outgoing transactions";

        auto tx_pair = CreateTransfer( GetAccountFromNode( *node_proc1 ),
                                       *GetUTXOManagerFromNode( *node_proc1 ),
                                       10000000000,
                                       node_proc2->GetAddress() );
        if ( !tx_pair.has_value() )
        {
        }

        auto [tx, proof] = tx_pair.value();

        std::vector<uint8_t> proof_vect;
        if ( proof.has_value() )
        {
            proof_vect = proof.value();
        }

        tx->dag_st.clear_signature();

        auto              invalid_tx_id = tx->dag_st.data_hash();
        sgns::GeniusNode &node          = *node_proc1;
        SendPair( *node_proc1, tx, proof_vect );

        test::assertWaitForCondition(
            [&]()
            {
                return node_proc1->GetTransactionStatus( invalid_tx_id ) ==
                       TransactionManager::TransactionStatus::VERIFYING;
            },
            std::chrono::milliseconds( 20000 ),
            "Invalid transaction didn't get sent" );

        EXPECT_EQ( node_proc1->GetBalance(), balance_1_before_invalid - 10000000000 )
            << "Correct Balance of outgoing transactions";

        std::cout << "Invalid tx confirmed " << std::endl;

        // Transfer funds with timeout
        auto transfer_result = node_proc1->TransferFunds( 10000000000,
                                                          node_proc2->GetAddress(),
                                                          sgns::TokenID::FromBytes( { 0x00 } ),
                                                          std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_FALSE( transfer_result.has_value() ) << "Transfer transaction succeeded when it should fail";

        std::cout << "subsequent tx failed" << std::endl;

        test::assertWaitForCondition(
            [&]() { return node_proc1->GetTransactionManagerState() == TransactionManager::State::SYNCING; },
            std::chrono::milliseconds( 20000 ),
            "Node didn't went into synching" );

        EXPECT_EQ( node_proc1->GetTransactionManagerState(),
                   TransactionManager::State::SYNCING ); //confirms it's invalid

        auto invalid_tx_result_sent = node_proc1->WaitForTransactionOutgoing(
            invalid_tx_id,
            std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );

        std::cout << "waited again for the invalid tx" << std::endl;

        EXPECT_EQ( invalid_tx_result_sent, TransactionManager::TransactionStatus::FAILED ); //confirms it's invalid

        std::cout << "now it's invalid" << std::endl;

        test::assertWaitForCondition(
            [&]() { return node_proc1->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 20000 ),
            "Node didn't recover from wrong transaction" );

        std::cout << "wait until its ready" << std::endl;

        EXPECT_EQ( node_proc1->GetBalance(), balance_1_before_invalid ) << "Correct Balance of outgoing transactions";

        transfer_result = node_proc1->TransferFunds( 10000000000,
                                                     node_proc2->GetAddress(),
                                                     sgns::TokenID::FromBytes( { 0x00 } ),
                                                     std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( transfer_result.has_value() ) << "Transfer transaction failed when it should succeed";

        auto [transfer_tx_id, transfer_duration] = transfer_result.value();
        std::cout << "Transfer transaction completed in " << transfer_duration << " ms" << std::endl;

        auto start_time        = std::chrono::steady_clock::now();
        auto transfer_received = node_proc2->WaitForTransactionIncoming(
            transfer_tx_id,
            std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
        EXPECT_EQ( transfer_received, TransactionManager::TransactionStatus::CONFIRMED );
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() -
                                                                               start_time )
                            .count();
        std::cout << "Transfer Received transaction completed in " << duration << " ms" << std::endl;

        auto start_time_invalid = std::chrono::steady_clock::now();

        // Verify node_proc2's balance increased
        EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 10000000000 )
            << "Transfer should increase node_proc2's balance";
    }
}
