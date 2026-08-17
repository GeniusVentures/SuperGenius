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
#include "crdt/proto/delta.pb.h"
#include "FileManager.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "account/proto/SGTransaction.pb.h"
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

    /**
     * @brief Friend accessor for GeniusNode protected members, used by
     *        the integration test to sign transactions with the child node's
     *        account and access the transaction manager.
     */
    class ChildRegTestAccess
    {
    public:
        static std::shared_ptr<GeniusAccount> GetAccount(
            const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->account_ : nullptr;
        }

        static outcome::result<std::shared_ptr<TransactionManager>> GetTransactionManager(
            const std::shared_ptr<GeniusNode> &node )
        {
            if ( !node ) return outcome::failure( GeniusNode::Error::TRANSACTIONS_NOT_READY );
            return node->GetTransactionManager();
        }
    };
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
        ASSERT_WAIT_FOR_CONDITION(
            ([&]() { return genesis_node_->GetState() == GeniusNode::NodeState::READY; }),
            std::chrono::milliseconds( 180000 ), "genesis_node_ not synced", nullptr );

        // Main wallet node A
        main_node_ = CreateNode( "regtest_main", "0xcafe", "1.0",
            sgns::TokenID::FromBytes( { 0x00 } ) );
        main_node_->GetPubSub()->AddPeers( { genesis_node_->GetPubSub()->GetInterfaceAddress() } );
        ASSERT_WAIT_FOR_CONDITION(
            ([&]() { return main_node_->GetState() == GeniusNode::NodeState::READY; }),
            std::chrono::milliseconds( 180000 ), "main_node_ not synced", nullptr );

        // Child wallet node B
        child_node_ = CreateNode( "regtest_child", "0xcafe", "1.0",
            sgns::TokenID::FromBytes( { 0x00 } ) );
        child_node_->GetPubSub()->AddPeers( { genesis_node_->GetPubSub()->GetInterfaceAddress() } );
        ASSERT_WAIT_FOR_CONDITION(
            ([&]() { return child_node_->GetState() == GeniusNode::NodeState::READY; }),
            std::chrono::milliseconds( 180000 ), "child_node_ not synced", nullptr );
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

        GeniusNodeConfig devConfig = { dev_addr, "0.65", tokenValue, tokenId, outPathStr };

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
    ASSERT_WAIT_FOR_CONDITION(
        ([&]() -> bool
        {
            auto entries_result = main_node_->GetRegistrationsForMain( main_address );
            if ( !entries_result.has_value() ) return false;
            auto &entries = entries_result.value();
            if ( entries.size() != 1 ) return false;
            return entries[0].child_addr == child_address
                && entries[0].main_addr == main_address
                && entries[0].sequence == 2;
        }),
        std::chrono::milliseconds( 60000 ),
        "Main node did not discover child registration", nullptr );

    // Verify discovery entry fields
    ASSERT_OUTCOME_SUCCESS( entries, main_node_->GetRegistrationsForMain( main_address ) );
    ASSERT_EQ( entries.size(), 1U );
    EXPECT_EQ( entries[0].child_addr, child_address );
    EXPECT_EQ( entries[0].main_addr, main_address );
    EXPECT_EQ( entries[0].sequence, 2U );
}

// ---------------------------------------------------------------------------
// TEST-04: InvalidRegistrationRejected — invalid registrations (tampered
//           signature, malformed main_address, non-monotonic sequence) are
//           rejected by FilterRegistration and never appear in discovery.
// ---------------------------------------------------------------------------
TEST_F( ChildRegistrationIntegrationTest, InvalidRegistrationRejected )
{
    // --- Get TransactionManager from main node for filter injection ---
    auto tm_result = ChildRegTestAccess::GetTransactionManager( main_node_ );
    ASSERT_TRUE( tm_result.has_value() ) << "Could not get TransactionManager from main node";
    auto &tm = *tm_result.value();

    std::string main_address = main_node_->GetAddress();
    std::string child_address = child_node_->GetAddress();
    std::string reg_key = "/bc/0/reg/" + child_address;

    // Helper to build a DAG struct for the child
    auto makeDAG = [&]()
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "registration" );
        dag.set_source_addr( child_address );
        dag.set_nonce( 0 );
        return dag;
    };

    // -----------------------------------------------------------------------
    // Sub-case A: Tampered child signature → FilterRegistration returns tombstone
    // -----------------------------------------------------------------------
    {
        SGTransaction::RegistrationMetadata metadata;
        metadata.set_game_id( "neg_sig" );

        auto tx = RegistrationTransaction::New( main_address, 3, metadata, makeDAG() );
        tx.MakeSignature( *ChildRegTestAccess::GetAccount( child_node_ ) );

        auto serialized = tx.SerializeByteVector();

        // Tamper the DAG signature at proto level (deterministic — always lands in sig field)
        SGTransaction::RegistrationTx tx_struct;
        ASSERT_TRUE( tx_struct.ParseFromArray( serialized.data(), serialized.size() ) )
            << "Should parse valid serialized RegistrationTx";

        auto *dag_mutable = tx_struct.mutable_dag_struct();
        std::string sig = dag_mutable->signature();
        ASSERT_FALSE( sig.empty() ) << "Signature should be non-empty after signing";
        sig[sig.size() - 1] ^= 0xFF;
        dag_mutable->set_signature( sig );

        size_t size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> tampered_bytes( size );
        ASSERT_TRUE( tx_struct.SerializeToArray( tampered_bytes.data(), tampered_bytes.size() ) );

        crdt::pb::Element element;
        element.set_key( reg_key );
        element.set_value( std::string( tampered_bytes.begin(), tampered_bytes.end() ) );

        auto filter_result = RegTestAccess::FilterRegistration( tm, element );
        EXPECT_TRUE( filter_result.has_value() )
            << "Tampered signature registration should produce tombstone";
    }

    // -----------------------------------------------------------------------
    // Sub-case B: Malformed main_address (not 128 hex chars) → tombstone
    // -----------------------------------------------------------------------
    {
        SGTransaction::RegistrationMetadata metadata;
        metadata.set_game_id( "neg_addr" );

        // main_address is only 64 chars — not a valid 128-hex public key
        std::string bad_main_address( 64, 'b' );

        auto tx = RegistrationTransaction::New( bad_main_address, 4, metadata, makeDAG() );
        tx.MakeSignature( *ChildRegTestAccess::GetAccount( child_node_ ) );

        auto serialized = tx.SerializeByteVector();

        crdt::pb::Element element;
        element.set_key( reg_key );
        element.set_value( std::string( serialized.begin(), serialized.end() ) );

        auto filter_result = RegTestAccess::FilterRegistration( tm, element );
        EXPECT_TRUE( filter_result.has_value() )
            << "Malformed main_address registration should produce tombstone";
    }

    // -----------------------------------------------------------------------
    // Sub-case C: Non-monotonic sequence (replay) → tombstone
    //
    // Strategy: submit a registration through the pipeline to establish CRDT
    // state with a known sequence, then inject a lower-sequence element into
    // the filter and assert rejection per gate (d).
    // -----------------------------------------------------------------------
    {
        // Step 1: Submit a registration through the pipeline with sequence=50
        SGTransaction::RegistrationMetadata meta_high;
        meta_high.set_game_id( "neg_seq_high" );
        auto reg_result = child_node_->RegisterChild( main_address, meta_high, 50 );
        ASSERT_TRUE( reg_result.has_value() ) << "Baseline registration (seq=50) should succeed";

        // Step 2: Poll for the registration to reach the main node's CRDT
        //         via pubsub propagation (up to 30s)
        ASSERT_WAIT_FOR_CONDITION(
            ([&]() -> bool
            {
                auto entries_result = main_node_->GetRegistrationsForMain( main_address );
                if ( !entries_result.has_value() ) return false;
                for ( const auto &entry : entries_result.value() )
                {
                    if ( entry.child_addr == child_address && entry.sequence == 50 )
                        return true;
                }
                return false;
            }),
            std::chrono::milliseconds( 30000 ),
            "Main node did not receive baseline registration (seq=50)", nullptr );

        // Step 3: Inject a CRDT element with sequence=49 (lower than stored 50)
        SGTransaction::RegistrationMetadata meta_low;
        meta_low.set_game_id( "neg_seq_low" );

        auto tx_low = RegistrationTransaction::New( main_address, 49, meta_low, makeDAG() );
        tx_low.MakeSignature( *ChildRegTestAccess::GetAccount( child_node_ ) );

        auto serialized_low = tx_low.SerializeByteVector();

        crdt::pb::Element element;
        element.set_key( reg_key );
        element.set_value( std::string( serialized_low.begin(), serialized_low.end() ) );

        auto filter_result = RegTestAccess::FilterRegistration( tm, element );
        EXPECT_TRUE( filter_result.has_value() )
            << "Non-monotonic sequence (49 <= stored 50) registration should produce tombstone";
    }

    // -----------------------------------------------------------------------
    // End-to-end assertion: none of the rejected registrations appear in discovery
    // -----------------------------------------------------------------------
    ASSERT_OUTCOME_SUCCESS( entries, main_node_->GetRegistrationsForMain( main_address ) );
    for ( const auto &entry : entries )
    {
        if ( entry.child_addr == child_address )
        {
            // Entries from this negative test use game_ids starting with "neg_"
            EXPECT_NE( entry.metadata.game_id(), "neg_sig" );
            EXPECT_NE( entry.metadata.game_id(), "neg_addr" );
            EXPECT_NE( entry.metadata.game_id(), "neg_seq_low" );
        }
    }
}

// ---------------------------------------------------------------------------
// TEST-05: MainQueriesChildBalance — child node B mints tokens using its own
//           DevConfig token; after CRDT sync converges, main node A reads the
//           child's balance via GetChildBalance without any direct child query.
// ---------------------------------------------------------------------------
TEST_F( ChildRegistrationIntegrationTest, MainQueriesChildBalance )
{
    std::string      child_address = child_node_->GetAddress();
    sgns::TokenID     child_token  = child_node_->GetTokenID();
    constexpr uint64_t kMintAmount = 500;

    // Note: chainid must match a registered test-only IInputValidator (see
    // testutil/TestMintInputValidator.hpp, which registers "test") — using an
    // unregistered chainid falls back to the public-chain validator, which
    // requires real RPC burn verification and rejects the mint immediately.
    auto mint_result = child_node_->MintTokens( kMintAmount,
                                                 sgns::test::NextMintSourceHash(),
                                                 "test",
                                                 child_token,
                                                 "",
                                                 std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out on child_node_";

    // D-65: poll child's own view first to confirm mint landed, then poll main's
    // synced view until the CRDT propagation converges (avoids flakiness).
    ASSERT_WAIT_FOR_CONDITION(
        ([&]() { return child_node_->GetBalance( child_token ) > 0; }),
        std::chrono::milliseconds( 60000 ),
        "child_node_ balance did not become non-zero after mint", nullptr );

    ASSERT_WAIT_FOR_CONDITION(
        ([&]() { return main_node_->GetChildBalance( child_address, child_token ) > 0; }),
        std::chrono::milliseconds( 60000 ),
        "main_node_ did not observe child balance after CRDT sync", nullptr );

    EXPECT_EQ( main_node_->GetChildBalance( child_address, child_token ), kMintAmount );
}

} // namespace sgns
