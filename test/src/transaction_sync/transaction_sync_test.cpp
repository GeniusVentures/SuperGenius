#include <filesystem>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <iostream>
#include <cstdio>

#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include "account/TransferTransaction.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "proof/TransferProof.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns;

namespace sgns
{
    class TransactionSyncTest : public ::testing::Test
    {
    protected:
        static inline std::shared_ptr<sgns::GeniusNode> node_proc1;
        static inline std::shared_ptr<sgns::GeniusNode> node_proc2;
        static inline std::shared_ptr<sgns::GeniusNode> full_node;

        static inline GeniusNodeConfig DEV_CONFIG  = { "0xcafe",
                                                       "0.65",
                                                       "1.0",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       "./transaction_sync_node1" };
        static inline GeniusNodeConfig DEV_CONFIG2 = { "0xcafe",
                                                       "0.65",
                                                       "1.0",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       "./transaction_sync_node2" };
        static inline GeniusNodeConfig DEV_CONFIG3 = { "0xcafe",
                                                       "0.65",
                                                       "1.0",
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       "./transaction_sync_full_node" };

        static inline std::string binary_path = "";

        static void SetUpTestSuite()
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );

            std::string binary_path   = boost::dll::program_location().parent_path().string();
            DEV_CONFIG.BaseWritePath  = binary_path + "/transaction_sync_node1/";
            DEV_CONFIG2.BaseWritePath = binary_path + "/transaction_sync_node2/";
            DEV_CONFIG3.BaseWritePath = binary_path + "/transaction_sync_full_node/";

            try
            {
                test::removeAllWithRetry( DEV_CONFIG.BaseWritePath );
                test::removeAllWithRetry( DEV_CONFIG2.BaseWritePath );
                test::removeAllWithRetry( DEV_CONFIG3.BaseWritePath );
            }
            catch ( ... )
            {
            }

            // All nodes in this test are non-processors (is_processor=false). Config-driven (Phase 3).
            std::filesystem::create_directories( DEV_CONFIG3.BaseWritePath );
            sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG3.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
            sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG3.BaseWritePath,
                                               /*node_type=*/"Full",
                                               /*is_processor=*/false,
                                               /*rpc_catchup=*/false );
            std::filesystem::create_directories( DEV_CONFIG.BaseWritePath );
            sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
            sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG.BaseWritePath,
                                               /*node_type=*/"Light",
                                               /*is_processor=*/false,
                                               /*rpc_catchup=*/false );
            std::filesystem::create_directories( DEV_CONFIG2.BaseWritePath );
            sgns::GeniusNode::WriteNetworkConfig( DEV_CONFIG2.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
            sgns::GeniusNode::WriteSgnsConfig( DEV_CONFIG2.BaseWritePath,
                                               /*node_type=*/"Light",
                                               /*is_processor=*/false,
                                               /*rpc_catchup=*/false );

            full_node = sgns::GeniusNode::New(
                DEV_CONFIG3,
                sgns::FromPrivateKey{ "9389e5f08c01e791dc436abab7a61a502515ddc7f91cb09f10289e147c651780" } );
            Blockchain::SetAuthorizedFullNodeAddress( full_node->GetAddress() );
            node_proc1 = sgns::GeniusNode::New(
                DEV_CONFIG,
                sgns::FromPrivateKey{ "1f06d98b1d1613ad98279f8d57ce30580e8a7a0385dc85da713333f53a928395" } );
            node_proc2 = sgns::GeniusNode::New(
                DEV_CONFIG2,
                sgns::FromPrivateKey{ "19c2f2db8e7cb27e5438093cf377d27888ddd4b257827baddd0418eefacedd02" } );

            node_proc1->AddPeers(
                { node_proc2->GetPubSub()->GetInterfaceAddress(), full_node->GetPubSub()->GetInterfaceAddress() } );
            node_proc2->AddPeers( { full_node->GetPubSub()->GetInterfaceAddress() } );

            test::assertWaitForCondition( [&]() { return full_node->GetState() == GeniusNode::NodeState::READY; },
                                          std::chrono::milliseconds( 50000 ),
                                          "full_node not ready" );
            test::assertWaitForCondition( [&]() { return node_proc1->GetState() == GeniusNode::NodeState::READY; },
                                          std::chrono::milliseconds( 50000 ),
                                          "node_proc1 not ready" );
            test::assertWaitForCondition( [&]() { return node_proc2->GetState() == GeniusNode::NodeState::READY; },
                                          std::chrono::milliseconds( 50000 ),
                                          "node_proc2 not ready" );
        }

        static void TearDownTestSuite()
        {
            node_proc1.reset();
            node_proc2.reset();
            full_node.reset();
        }

        outcome::result<sgns::TransactionManager::TransactionPair> CreateTransfer(
            sgns::GeniusAccount &account,
            uint64_t             amount,
            const std::string   &destination,
            const std::string   &previous_hash = "" )
        {
            BOOST_OUTCOME_TRY( auto params,
                               account.GetUTXOManager().CreateTxParameter( amount,
                                                                           destination,
                                                                           sgns::TokenID::FromBytes( { 0x00 } ) ) );

            auto timestamp = std::chrono::system_clock::now();

            SGTransaction::DAGStruct dag;
            dag.set_previous_hash( previous_hash );
            dag.set_nonce( account.ReserveNextNonce() );
            dag.set_source_addr( account.GetAddress() );
            dag.set_timestamp(
                std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );
            dag.set_uncle_hash( "" );
            dag.set_data_hash( "" ); //filled by transaction class

            auto transfer_transaction = std::make_shared<sgns::TransferTransaction>(
                sgns::TransferTransaction::New( params.first, params.second, dag ) );
            transfer_transaction->MakeSignature( account );
            std::optional<std::vector<uint8_t>> maybe_proof;

            TransferProof prover( account.GetUTXOManager().GetBalance(), amount );
            BOOST_OUTCOME_TRY( auto proof_result, prover.GenerateFullProof() );

            maybe_proof = std::move( proof_result );

            account.GetUTXOManager().ReserveUTXOs( params.first, transfer_transaction->GetHash() );
            return std::make_pair( transfer_transaction, maybe_proof );
        }

        std::shared_ptr<sgns::GeniusAccount> GetAccountFromNode( sgns::GeniusNode &node )
        {
            return node.account_;
        }

        UTXOManager *GetUTXOManagerFromNode( sgns::GeniusNode &node )
        {
            return &node.account_->GetUTXOManager();
        }

        void SendPair( sgns::GeniusNode &node, std::shared_ptr<GeniusTransaction> tx, std::vector<uint8_t> proof )
        {
            node.SendTransactionAndProof( std::move( tx ), std::move( proof ) );
        }
    };
}

TEST_F( TransactionSyncTest, TransactionSimpleTransfer )
{
    auto balance_1_before = node_proc1->GetBalance();
    auto balance_2_before = node_proc2->GetBalance();
    auto mint_result      = node_proc1->MintTokens( 10000000000,
                                                    sgns::test::NextMintSourceHash(),
                                                    "test", 0u,
                                                    sgns::TokenID::FromBytes( { 0x00 } ),
                                                    "",
                                                    std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto [mint_tx_id, mint_duration] = mint_result.value();
    std::cout << "Mint transaction completed in " << mint_duration << " ms" << std::endl;

    // Verify balance after minting
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 10000000000 ) << "Correct Balance of outgoing transactions";

    // Transfer funds with timeout
    auto transfer_result = node_proc1->TransferFunds( 10000000000,
                                                      node_proc2->GetAddress(),
                                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                                      std::chrono::milliseconds( GeniusNode::TIMEOUT_TRANSFER ) );
    ASSERT_TRUE( transfer_result.has_value() ) << "Transfer transaction failed or timed out";
    auto [transfer_tx_id, transfer_duration] = transfer_result.value();
    std::cout << "Transfer transaction completed in " << transfer_duration << " ms" << std::endl;

    auto start_time        = std::chrono::steady_clock::now();
    auto transfer_received = node_proc2->WaitForTransactionIncoming(
        transfer_tx_id,
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( transfer_received, TransactionManager::TransactionStatus::CONFIRMED );
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start_time ).count();
    std::cout << "Transfer Received transaction completed in " << duration << " ms" << std::endl;

    start_time                       = std::chrono::steady_clock::now();
    auto full_node_transfer_received = full_node->WaitForTransactionIncoming(
        transfer_tx_id,
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( full_node_transfer_received, TransactionManager::TransactionStatus::CONFIRMED );
    duration = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start_time )
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

    // Mint tokens on node_proc1
    std::vector<uint64_t> mint_amounts = { 10000000000, 20000000000 };

    for ( auto amount : mint_amounts )
    {
        auto mint_result = node_proc1->MintTokens( amount,
                                                   sgns::test::NextMintSourceHash(),
                                                   "test", 0u,
                                                   sgns::TokenID::FromBytes( { 0x00 } ),
                                                   "",
                                                   std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
        ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction of " << amount << " failed or timed out";

        auto [tx_id, duration] = mint_result.value();
        std::cout << "Mint transaction of " << amount << " completed in " << duration << " ms" << std::endl;
    }

    // Mint tokens on node_proc2
    auto mint_result1 = node_proc2->MintTokens( 10000000000,
                                                sgns::test::NextMintSourceHash(),
                                                "test", 0u,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                "",
                                                std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result1.has_value() ) << "Mint transaction failed or timed out";

    auto mint_result2 = node_proc2->MintTokens( 20000000000,
                                                sgns::test::NextMintSourceHash(),
                                                "test", 0u,
                                                sgns::TokenID::FromBytes( { 0x00 } ),
                                                "",
                                                std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result2.has_value() ) << "Mint transaction failed or timed out";

    // Verify balances after minting
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 30000000000 ) << "Correct Balance of outgoing transactions";
    EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 10000000000 ) << "Correct Balance of outgoing transactions";

    // Transfer funds
    auto transfer_result1 = node_proc1->TransferFunds( 10000000000,
                                                       node_proc2->GetAddress(),
                                                       sgns::TokenID::FromBytes( { 0x00 } ),
                                                       std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer_result1.has_value() ) << "Transfer transaction failed or timed out";
    auto [transfer_tx_id1, transfer_duration1] = transfer_result1.value();

    // Wait for the transfer to happen or timeout.
    auto start_time = std::chrono::steady_clock::now();

    auto transfer_received = node_proc2->WaitForTransactionIncoming(
        transfer_tx_id1,
        std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( transfer_received, TransactionManager::TransactionStatus::CONFIRMED );
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start_time ).count();
    std::cout << "node2 Transfer Received transaction completed in " << duration << " ms" << std::endl;

    // Verify balances after transfers
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before + 20000000000 ) << "Correct Balance of outgoing transactions";
    EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 20000000000 ) << "Correct Balance of outgoing transactions";
}

TEST_F( TransactionSyncTest, TransactionTransferSync )
{
    auto mint_result = node_proc1->MintTokens( 67000000000,
                                               sgns::test::NextMintSourceHash(),
                                               "test", 0u,
                                               sgns::TokenID::FromBytes( { 0x00 } ),
                                               "",
                                               std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto balance_1_before = node_proc1->GetBalance();
    auto balance_2_before = node_proc2->GetBalance();

    // Mint tokens on node_proc1
    std::vector<uint64_t> xfer_amounts[2] = {
        { 10000000000, 20000000000 },
        { 5000000000, 10000000000 },
    };

    std::vector<std::string> txIDs[2];

    uint64_t xfer_amount_1 = 0;
    uint64_t xfer_amount_2 = 0;

    for ( size_t index = 0; index < xfer_amounts[0].size(); index++ )
    {
        auto xfer_amount  = xfer_amounts[0][index];
        xfer_amount_1    += xfer_amount;
        auto transfer_result1 = node_proc1->TransferFunds( xfer_amount,
                                                           node_proc2->GetAddress(),
                                                           sgns::TokenID::FromBytes( { 0x00 } ),
                                                           std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( transfer_result1.has_value() ) << "Transfer transaction failed or timed out";
        auto [transfer_tx_id1, transfer_duration1] = transfer_result1.value();
        std::cout << "node 1 Transfer transaction completed in " << transfer_duration1 << " ms" << std::endl;

        txIDs[0].push_back( transfer_tx_id1 );

        xfer_amount    = xfer_amounts[1][index];
        xfer_amount_2 += xfer_amount;
        auto transfer_result2 = node_proc2->TransferFunds( xfer_amount,
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

    // Mint tokens with timeout
    auto mint_result = node_proc1->MintTokens( 10000000000,
                                               sgns::test::NextMintSourceHash(),
                                               "test", 0u,
                                               sgns::TokenID::FromBytes( { 0x00 } ),
                                               "",
                                               std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";
    mint_result = node_proc1->MintTokens( 10000000000,
                                          sgns::test::NextMintSourceHash(),
                                          "test", 0u,
                                          sgns::TokenID::FromBytes( { 0x00 } ),
                                          "",
                                          std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto [mint_tx_id, mint_duration] = mint_result.value();
    std::cout << "Mint transaction completed in " << mint_duration << " ms" << std::endl;

    auto balance_1_before_invalid = balance_1_before + 20000000000;

    // Verify balance after minting
    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before_invalid ) << "Correct Balance of outgoing transactions";

    auto tx_pair = CreateTransfer( *GetAccountFromNode( *node_proc1 ),
                                   10000000000,
                                   node_proc2->GetAddress(),
                                   mint_tx_id );
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

    auto invalid_tx_id = tx->dag_st.data_hash();
    SendPair( *node_proc1, tx, proof_vect );

    auto invalid_tx_result_sent = node_proc1->WaitForTransactionOutgoing(
        invalid_tx_id,
        std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( invalid_tx_result_sent, TransactionManager::TransactionStatus::FAILED );

    EXPECT_EQ( node_proc1->GetBalance(), balance_1_before_invalid ) << "Correct Balance of outgoing transactions";

    std::cout << "Invalid tx failed" << std::endl;

    test::assertWaitForCondition( [&]() { return node_proc1->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "Node didn't recover from wrong transaction" );

    std::cout << "wait until its ready" << std::endl;

    auto transfer_result = node_proc1->TransferFunds( 10000000000,
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
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start_time ).count();
    std::cout << "Transfer Received transaction completed in " << duration << " ms" << std::endl;

    // Verify node_proc2's balance increased
    EXPECT_EQ( node_proc2->GetBalance(), balance_2_before + 10000000000 )
        << "Transfer should increase node_proc2's balance";
}

TEST_F( TransactionSyncTest, InvalidPreviousHashTest )
{
    // Mint tokens to ensure sufficient balance
    auto mint_result = node_proc1->MintTokens( 20000000000,
                                               sgns::test::NextMintSourceHash(),
                                               "test", 0u,
                                               TokenID::FromBytes( { 0x00 } ),
                                               "",
                                               std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    // Create and send a valid first transfer using the normal flow
    auto transfer_result = node_proc1->TransferFunds( 10000000000,
                                                      node_proc2->GetAddress(),
                                                      sgns::TokenID::FromBytes( { 0x00 } ),
                                                      std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer_result.has_value() ) << "Transfer transaction failed or timed out";
    auto [tx1_id, transfer_duration] = transfer_result.value();
    std::cout << "Transfer transaction completed in " << transfer_duration << " ms" << std::endl;

    auto tx1_status = node_proc1->WaitForTransactionOutgoing(
        tx1_id,
        std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    EXPECT_EQ( tx1_status, TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_EQ(
        node_proc2->WaitForTransactionIncoming( tx1_id, std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ),
        TransactionManager::TransactionStatus::CONFIRMED );

    // Create a second transfer with an invalid previous hash
    auto tx_pair2 = CreateTransfer( *GetAccountFromNode( *node_proc1 ), 10000000000, node_proc2->GetAddress(), tx1_id );
    ASSERT_TRUE( tx_pair2.has_value() );

    auto [tx2, proof2]   = tx_pair2.value();
    std::string bad_prev = tx1_id;
    if ( !bad_prev.empty() )
    {
        bad_prev[0] = ( bad_prev[0] == 'a' ) ? 'b' : 'a';
    }
    tx2->dag_st.set_previous_hash( bad_prev );
    tx2->FillHash();
    tx2->MakeSignature( *GetAccountFromNode( *node_proc1 ) );

    std::vector<uint8_t> proof_vect2;
    if ( proof2.has_value() )
    {
        proof_vect2 = proof2.value();
    }
    SendPair( *node_proc1, tx2, proof_vect2 );

    auto tx2_status = node_proc1->WaitForTransactionOutgoing( tx2->GetHash(), std::chrono::seconds( 10 ) );
    EXPECT_EQ( tx2_status, TransactionManager::TransactionStatus::FAILED );
}

TEST_F( TransactionSyncTest, MissedCrdtHeadIsRecoveredAfterReconnect )
{
    constexpr uint64_t amount         = 100;
    const auto         destination    = node_proc2->GetAddress();
    const auto         balance_before = node_proc2->GetBalance();

    // Keep node_proc2's datastore, but take the node offline while the CRDT head is published.
    node_proc2.reset();

    auto mint_result = node_proc1->MintTokens( amount,
                                               sgns::test::NextMintSourceHash(),
                                               "test", 0u,
                                               TokenID::FromBytes( { 0x00 } ),
                                               "",
                                               std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() );

    auto transfer_result = node_proc1->TransferFunds( amount,
                                                      destination,
                                                      TokenID::FromBytes( { 0x00 } ),
                                                      std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transfer_result.has_value() );
    const auto &transaction_id = transfer_result.value().first;

    node_proc2 = GeniusNode::New(
        DEV_CONFIG2,
        FromPrivateKey{ "19c2f2db8e7cb27e5438093cf377d27888ddd4b257827baddd0418eefacedd02" } );
    ASSERT_TRUE( node_proc2 );
    node_proc2->AddPeers( { full_node->GetPubSub()->GetInterfaceAddress() } );

    test::assertWaitForCondition( [&]() { return node_proc2->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "reconnected node did not finish recovery" );

    EXPECT_EQ( node_proc2->WaitForTransactionIncoming( transaction_id,
                                                       std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ),
               TransactionManager::TransactionStatus::CONFIRMED );
    EXPECT_EQ( node_proc2->GetBalance(), balance_before + amount );
}
