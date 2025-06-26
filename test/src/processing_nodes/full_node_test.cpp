#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <thread>
#include <cstring>
#include <atomic>
#include <iostream>
#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns;

/**
 * @brief Helper to create a GeniusNode with explicit full-node flag, custom folder, and fixed private key.
 * @param self_address Address for this node
 * @param tokenValue   TokenValueInGNUS to initialize DevConfig.
 * @param tokenId      TokenID to initialize DevConfig.
 * @param isProcessor  Whether this node is a processor node.
 * @param isFullNode   Whether this node should run as a full node.
 * @param folderName   Subfolder name under the binary path for storage.
 * @param privKey      Hex string private key (64 chars) for deterministic identity.
 * @return unique_ptr to the initialized GeniusNode.
 */
static std::unique_ptr<GeniusNode> CreateNodeWithMode( const std::string &self_address,
                                                       const std::string &tokenValue,
                                                       TokenID            tokenId,
                                                       bool               isProcessor,
                                                       bool               isFullNode,
                                                       const std::string &folderName,
                                                       const std::string &privKey )
{
    static std::atomic<int> nodeCounter{ 0 };
    int                     id         = nodeCounter.fetch_add( 1 );
    std::string             binaryPath = boost::dll::program_location().parent_path().string();
    std::string             outPath    = binaryPath + "/" + folderName + "/";

    DevConfig_st devConfig = { "", "1.0", tokenValue, tokenId, "" };
    std::strncpy( devConfig.Addr, self_address.c_str(), sizeof( devConfig.Addr ) - 1 );
    std::strncpy( devConfig.BaseWritePath, outPath.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
    devConfig.Addr[sizeof( devConfig.Addr ) - 1]                   = '\0';
    devConfig.BaseWritePath[sizeof( devConfig.BaseWritePath ) - 1] = '\0';

    uint16_t port = static_cast<uint16_t>( 40001 + id );
    auto     node = std::make_unique<GeniusNode>( devConfig, privKey.c_str(), false, isProcessor, port, isFullNode );

    // allow startup
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    return node;
}

TEST( NodeBalancePersistenceTest, BalancePersistsAfterRecreation )
{
    std::cout << "****** Removing old node_recovery folder ****" << std::endl;
    {
        std::string binaryPath   = boost::dll::program_location().parent_path().string();
        std::string recoveryPath = binaryPath + "/node_recovery";
        boost::filesystem::remove_all( recoveryPath );
    }
    const std::string fullKey   = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    const std::string sharedKey = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeea";

    std::cout << "****** Full node creation ****" << std::endl;
    auto fullNode = CreateNodeWithMode( "0xffff",
                                        "1.0",
                                        TokenID::FromBytes( { 0x01 } ),
                                        /*isProcessor=*/false,
                                        /*isFullNode=*/true,
                                        /*folderName=*/"node_full",
                                        fullKey );

    std::cout << "****** Original node creation ****" << std::endl;
    auto originalNode = CreateNodeWithMode( "0xabcd",
                                            "1.0",
                                            TokenID::FromBytes( { 0x00 } ),
                                            /*isProcessor=*/false,
                                            /*isFullNode=*/false,
                                            /*folderName=*/"node_original",
                                            sharedKey );

    originalNode->GetPubSub()->AddPeers( { fullNode->GetPubSub()->GetLocalAddress() } );

    std::cout << "****** Minting tokens on original node ****" << std::endl;
    uint64_t beforeMint = originalNode->GetBalance();
    auto     mintRes    = originalNode->MintTokens(
        /*amount=*/500000,
        /*txHash=*/"",
        /*chainId=*/"",
        TokenID::FromBytes( { 0x00 } ) );
    ASSERT_TRUE( mintRes.has_value() ) << "MintTokens failed on original node";
    uint64_t afterMint = originalNode->GetBalance();
    ASSERT_GT( afterMint, beforeMint );

    std::cout << "****** Destroying original node after 10 seconds ****" << std::endl;
    std::this_thread::sleep_for( std::chrono::seconds( 15 ) );
    originalNode.reset();
    std::this_thread::sleep_for( std::chrono::seconds( 10 ) );

    std::cout << "****** Recovery node creation ****" << std::endl;
    auto recoveryNode = CreateNodeWithMode( "0xabcd",
                                            "1.0",
                                            TokenID::FromBytes( { 0x01 } ),
                                            /*isProcessor=*/false,
                                            /*isFullNode=*/false,
                                            /*folderName=*/"node_recovery",
                                            sharedKey );
    recoveryNode->GetPubSub()->AddPeers( { fullNode->GetPubSub()->GetLocalAddress() } );

    std::cout << "****** Verifying recovery node balance ****" << std::endl;
    test::assertWaitForCondition( [&]() { return recoveryNode->GetBalance() == afterMint; },
                                  std::chrono::milliseconds( 20000 ),
                                  "Recovery node balance not updated in time" );
}
