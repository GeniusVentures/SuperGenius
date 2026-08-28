#include <gtest/gtest.h>
#include <boost/dll.hpp>
#include <thread>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns;

/**
 * @brief Helper to create a GeniusNode with explicit full-node flag, custom folder, and fixed private key.
 * @param self_address Address for this node
 * @param tokenValue   TokenValueInGNUS to initialize GeniusGeniusNodeConfig.
 * @param tokenId      TokenID to initialize GeniusGeniusNodeConfig.
 * @param isFullNode   Whether this node should run as a full node.
 * @param folderName   Subfolder name under the binary path for storage.
 * @param privKey      Hex string private key (64 chars) for deterministic identity.
 * @return unique_ptr to the initialized GeniusNode.
 */
static std::shared_ptr<GeniusNode> CreateNodeWithMode( const std::string &self_address,
                                                       const std::string &tokenValue,
                                                       TokenID            tokenId,
                                                       bool               isFullNode,
                                                       const std::string &folderName,
                                                       const std::string &privKey )
{
    std::string             binaryPath = boost::dll::program_location().parent_path().string();
    std::string             outPath    = binaryPath + "/" + folderName + "/";

    GeniusNodeConfig devConfig = { self_address, "0.0", tokenValue, tokenId, outPath };

    // All nodes in this test are non-processors.
    // is_processor is now read exclusively from sgns_config.json (defaults to true).
    sgns::test::removeAllWithRetry( devConfig.BaseWritePath );
    std::filesystem::create_directories( devConfig.BaseWritePath );
    {
        std::ofstream bridgeConfigFile( devConfig.BaseWritePath + "bridge_chains_config.json" );
        bridgeConfigFile << "{}";
    }

    GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
    GeniusNode::WriteSgnsConfig( devConfig.BaseWritePath, isFullNode ? "Full" : "Light", /*is_processor=*/false, /*rpc_catchup=*/false );

    auto node = GeniusNode::New( devConfig, FromPrivateKey{ privKey } );
    if ( isFullNode )
    {
        sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
    }

    return node;
}

TEST( NodeBalancePersistenceTest, BalancePersistsAfterRecreation )
{
    GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                            { return std::make_shared<MemorySecureStorage>( identifier ); } );

    const std::string fullKey   = "1111111111111111111111111111111111111111111111111111111111111111";
    const std::string sharedKey = "2222222222222222222222222222222222222222222222222222222222222222";

    auto fullNode     = CreateNodeWithMode( "0xffff",
                                            "1.0",
                                            TokenID::FromBytes( { 0x01 } ),
                                            true,
                                            "fnt_full_node",
                                            fullKey );
    auto originalNode = CreateNodeWithMode( "0xabcd",
                                            "1.0",
                                            TokenID::FromBytes( { 0x00 } ),
                                            false,
                                            "fnt_original",
                                            sharedKey );
    originalNode->AddPeers( { fullNode->GetPubSub()->GetInterfaceAddress() } );

    test::assertWaitForCondition( [&]() { return fullNode->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "fullnode not synced" );
    test::assertWaitForCondition( [&]() { return originalNode->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "Recovery node initial balance not updated in time" );

    std::cout << "****** Minting tokens on original node ****" << std::endl;
    uint64_t beforeMint = originalNode->GetBalance();
    uint64_t afterMint;

    constexpr size_t mintAmount = 10;
    for ( size_t i = 0; i < mintAmount; ++i )
    {
        auto mintRes = originalNode->MintTokens( 500000,
                                                 sgns::test::NextMintSourceHash(),
                                                 "test",
                                                 TokenID::FromBytes( { 0x00 } ),
                                                 "",
                                                 std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
        ASSERT_TRUE( mintRes.has_value() ) << "MintTokens failed on original node";
        afterMint = originalNode->GetBalance();
        ASSERT_GT( afterMint, beforeMint );
    }

    std::cout << "****** Recovery node creation ****" << std::endl;
    auto recoveryNode = CreateNodeWithMode( "0xabcd",
                                            "1.0",
                                            TokenID::FromBytes( { 0x01 } ),
                                            false,
                                            "fnt_recovery",
                                            sharedKey );
    recoveryNode->AddPeers( { fullNode->GetPubSub()->GetInterfaceAddress() } );

    std::cout << "****** Verifying recovery node balance ****" << std::endl;
    test::assertWaitForCondition( [&]() { return recoveryNode->GetBalance() == afterMint; },
                                  std::chrono::milliseconds( 150000 ),
                                  "Recovery node balance not updated in time" );
}
