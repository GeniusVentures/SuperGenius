/**
 * @file       child_registration.cpp
 * @brief      Multi-node integration test for child-wallet registration through
 *             CRDT persistence, pubsub broadcast, and main-wallet discovery.
 * @date       2026-07-16
 * @author     (Phase 05-03)
 */

#include <gtest/gtest.h>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>
#include <cstdio>
#include <system_error>
#include <functional>
#include <thread>
#include <atomic>
#include <random>
#include <ctime>
#include <tuple>
#include <optional>

#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/RegistrationTransaction.hpp"
#include "account/TransactionManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include "crdt/proto/crdt.pb.h"
#include "FileManager.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "SGTransaction.pb.h"
#include "testutil/mint_source_hash.hpp"
#include "testutil/outcome.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns;

namespace sgns
{
    /**
     * @brief Friend accessor for FilterRegistration, used by negative-test injection
     *        in TEST-04 (InvalidRegistrationRejected).
     */
    class RegTestAccess
    {
    public:
        static std::optional<std::vector<crdt::pb::Element>> FilterRegistration(
            TransactionManager      &tm,
            const crdt::pb::Element &element )
        {
            return tm.FilterRegistration( element );
        }
    };
} // namespace sgns

/**
 * @brief Multi-node integration test fixture for child-wallet registration.
 *
 * Boots a 3-node network (genesis-authorized + main node A + child node B)
 * once in SetUpTestSuite and shares it across all TEST_F cases per D-51/D-52.
 */
class ChildRegistrationIntegrationTest : public ::testing::Test
{
protected:
    static constexpr std::string_view FILE_PREFIX = "cri_";

    static std::shared_ptr<sgns::GeniusNode> genesis_node_;
    static std::shared_ptr<sgns::GeniusNode> main_node_;
    static std::shared_ptr<sgns::GeniusNode> child_node_;

    static void SetUpTestSuite()
    {
        GeniusAccount::SetSecureStorageFactory(
            []( const std::string &id ) { return std::make_shared<MemorySecureStorage>( id ); } );

        // D-51: genesis-authorized node (full node + processor + authorized)
        genesis_node_ = CreateNode( "regtest_genesis", "0xcafe", "1.0",
            sgns::TokenID::FromBytes( { 0x00 } ), true, true, true );
        sgns::test::assertWaitForCondition(
            [&]() { return genesis_node_->GetState() == GeniusNode::NodeState::READY; },
            std::chrono::milliseconds( 180000 ), "genesis_node_ not synced" );

        // Main wallet node A
        main_node_ = CreateNode( "regtest_main", "0xcafe", "1.0",
            sgns::TokenID::FromBytes( { 0x00 } ) );
        main_node_->GetPubSub()->AddPeers( { genesis_node_->GetPubSub()->GetInterfaceAddress() } );
        sgns::test::assertWaitForCondition(
            [&]() { return main_node_->GetState() == GeniusNode::NodeState::READY; },
            std::chrono::milliseconds( 180000 ), "main_node_ not synced" );

        // Child wallet node B
        child_node_ = CreateNode( "regtest_child", "0xcafe", "1.0",
            sgns::TokenID::FromBytes( { 0x00 } ) );
        child_node_->GetPubSub()->AddPeers( { genesis_node_->GetPubSub()->GetInterfaceAddress() } );
        sgns::test::assertWaitForCondition(
            [&]() { return child_node_->GetState() == GeniusNode::NodeState::READY; },
            std::chrono::milliseconds( 180000 ), "child_node_ not synced" );
    }

    static void TearDownTestSuite()
    {
        child_node_.reset();
        main_node_.reset();
        genesis_node_.reset();
        GeniusAccount::SetSecureStorageFactory( nullptr );
    }

    /**
     * @brief Creates a SuperGenius node with deterministic key, isolated file storage,
     *        and a unique libp2p port.
     *
     * Copied from multi_account_sync.cpp with FILE_PREFIX changed to "cri_".
     */
    static std::shared_ptr<sgns::GeniusNode> CreateNode(
        const std::string &self_address,
        const std::string &dev_addr,
        const std::string &tokenValue,
        sgns::TokenID      tokenId,
        bool               isFullNode          = false,
        bool               isProcessor         = false,
        bool               isGenesisAuthorized = false )
    {
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );

        auto binaryPath = boost::dll::program_location().parent_path();
        auto outPath    = binaryPath / ( std::string( FILE_PREFIX ) + std::to_string( id ) );
        auto outPathStr = outPath.generic_string() + '/';

        DevConfig_st devConfig = { dev_addr, "0.65", tokenValue, tokenId, outPathStr };

        std::filesystem::remove_all( devConfig.BaseWritePath );
        std::filesystem::create_directories( devConfig.BaseWritePath );
        {
            std::ofstream bridgeConfigFile( devConfig.BaseWritePath + "bridge_chains_config.json" );
            bridgeConfigFile << "{}";
        }

        // Generate deterministic key from self_address
        std::string key;
        key.reserve( 64 );

        std::hash<std::string> hasher;
        size_t                 address_hash = hasher( self_address );

        std::mt19937                    rng( static_cast<uint32_t>( address_hash ) );
        std::uniform_int_distribution<> dist( 0, 15 );
        std::generate_n( std::back_inserter( key ),
                         64,
                         [&]()
                         {
                             static constexpr std::string_view hexChars = "0123456789abcdef";
                             return hexChars[dist( rng )];
                         } );

        uint16_t uniquePort = static_cast<uint16_t>( 40001 + id );
        sgns::GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, uniquePort, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( devConfig.BaseWritePath,
                                           isFullNode ? "Full" : "Light",
                                           /*is_processor=*/isProcessor );
        auto node = sgns::GeniusNode::New( devConfig, sgns::FromPrivateKey{ key } );
        if ( isGenesisAuthorized )
        {
            sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
        }

        return node;
    }
};

// Static member definitions
std::shared_ptr<sgns::GeniusNode> ChildRegistrationIntegrationTest::genesis_node_;
std::shared_ptr<sgns::GeniusNode> ChildRegistrationIntegrationTest::main_node_;
std::shared_ptr<sgns::GeniusNode> ChildRegistrationIntegrationTest::child_node_;

// ---------------------------------------------------------------------------
// TEST-02: ChildRegistersWithMain — child node B submits a RegistrationTx
//           naming main node A, receiving a valid transaction hash.
// ---------------------------------------------------------------------------
TEST_F( ChildRegistrationIntegrationTest, ChildRegistersWithMain )
{
    std::string main_address = main_node_->GetAddress();
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "test_02_register" );

    auto result = child_node_->RegisterChild( main_address, metadata, 1 );
    ASSERT_TRUE( result.has_value() ) << "RegisterChild returned error";
    std::string tx_hash = result.value();
    EXPECT_FALSE( tx_hash.empty() ) << "Transaction hash should not be empty";
    EXPECT_EQ( tx_hash.size(), 64U ) << "Transaction hash should be 64 hex chars (SHA-256)";
}

// ---------------------------------------------------------------------------
// TEST-03: MainDiscoversChild — after CRDT/pubsub propagation, main node A
//           discovers child node B's registration with correct addresses and sequence.
// ---------------------------------------------------------------------------
TEST_F( ChildRegistrationIntegrationTest, MainDiscoversChild )
{
    std::string main_address = main_node_->GetAddress();
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "test_03_discovery" );

    // Use a different sequence (2) to avoid collision with TEST-02's CRDT state per D-52
    auto result = child_node_->RegisterChild( main_address, metadata, 2 );
    ASSERT_TRUE( result.has_value() ) << "RegisterChild returned error";

    std::string child_address = child_node_->GetAddress();

    // Poll for discovery — CRDT/pubsub propagation takes time
    sgns::test::assertWaitForCondition(
        [&]() -> bool
        {
            auto entries_result = main_node_->GetRegistrationsForMain( main_address );
            if ( !entries_result.has_value() ) return false;
            auto &entries = entries_result.value();
            if ( entries.size() != 1 ) return false;
            return entries[0].child_addr == child_address
                && entries[0].main_addr == main_address
                && entries[0].sequence == 2;
        },
        std::chrono::milliseconds( 60000 ),
        "Main node did not discover child registration" );

    // Verify discovery entry fields
    ASSERT_OUTCOME_SUCCESS( auto entries, main_node_->GetRegistrationsForMain( main_address ) );
    ASSERT_EQ( entries.size(), 1U );
    EXPECT_EQ( entries[0].child_addr, child_address );
    EXPECT_EQ( entries[0].main_addr, main_address );
    EXPECT_EQ( entries[0].sequence, 2U );
}
