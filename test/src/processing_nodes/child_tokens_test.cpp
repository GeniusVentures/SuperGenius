#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <thread>
#include <cstring>
#include <ostream>
#include <atomic>
#include "account/GeniusNode.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns::test;

namespace
{
    /**
     * @brief Helper to create a GeniusNode with its own directory and cleanup.
     * @param tokenValue TokenValueInGNUS to initialize DevConfig.
     * @param outPath Receives the created base path for cleanup.
     * @return unique_ptr to the initialized GeniusNode.
     */
    std::unique_ptr<sgns::GeniusNode> CreateNode( const std::string &tokenValue, std::string &outPath )
    {
        static std::atomic<int> node_counter{ 0 };
        int                     id         = node_counter.fetch_add( 1 );
        std::string             binaryPath = boost::dll::program_location().parent_path().string();
        std::string             testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        outPath                            = binaryPath + "/" + testName + "_" + std::to_string( id );

        DevConfig_st devConfig = { "0xcafe", "0.65", tokenValue, 0, "" };
        std::strncpy( devConfig.BaseWritePath, outPath.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
        devConfig.BaseWritePath[sizeof( devConfig.BaseWritePath ) - 1] = '\0';

        const char *key  = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
        auto        node = std::make_unique<sgns::GeniusNode>( devConfig, key, false, false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        return node;
    }
} // namespace

/// Suite: Simple Three-Node Transfers

TEST( SimpleThreeNodeTransferTest, Node51AndNode52ToNode50 )
{
    const std::string binary_path = boost::dll::program_location().parent_path().string();

    DevConfig_st cfg50 = { "0xcafe", "0.65", "1.0", 0, {} };
    DevConfig_st cfg51 = { "0xcafe", "0.65", "0.5", 0, {} };
    DevConfig_st cfg52 = { "0xcafe", "0.65", "2.0", 0, {} };

    std::strncpy( cfg50.BaseWritePath, ( binary_path + "/node50/" ).c_str(), sizeof( cfg50.BaseWritePath ) - 1 );
    std::strncpy( cfg51.BaseWritePath, ( binary_path + "/node51/" ).c_str(), sizeof( cfg51.BaseWritePath ) - 1 );
    std::strncpy( cfg52.BaseWritePath, ( binary_path + "/node52/" ).c_str(), sizeof( cfg52.BaseWritePath ) - 1 );

    cfg50.BaseWritePath[sizeof( cfg50.BaseWritePath ) - 1] = '\0';
    cfg51.BaseWritePath[sizeof( cfg51.BaseWritePath ) - 1] = '\0';
    cfg52.BaseWritePath[sizeof( cfg52.BaseWritePath ) - 1] = '\0';

    auto node50 = std::make_unique<sgns::GeniusNode>(
        cfg50,
        "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        false,
        false );
    auto node51 = std::make_unique<sgns::GeniusNode>(
        cfg51,
        "eeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        false,
        false );
    auto node52 = std::make_unique<sgns::GeniusNode>(
        cfg52,
        "feadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        false,
        false );

    node51->GetPubSub()->AddPeers( { node50->GetPubSub()->GetLocalAddress() } );
    node52->GetPubSub()->AddPeers( { node50->GetPubSub()->GetLocalAddress() } );
    node50->GetPubSub()->AddPeers( { node51->GetPubSub()->GetLocalAddress(), node52->GetPubSub()->GetLocalAddress() } );

    auto init50 = node50->GetBalance();
    auto init51 = node51->GetBalance();
    auto init52 = node52->GetBalance();

    // ---------- Ensure enought balance ----------
    uint64_t mintAmount51 = 1000000;
    auto     mintRes51    = node51->MintTokens( 10000000,
                                         "",
                                         "",
                                         "",
                                         std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mintRes51.has_value() ) << "Mint failed on node51";

    uint64_t mintAmount52 = 1000000;
    auto     mintRes52    = node52->MintTokens( 10000000,
                                         "",
                                         "",
                                         "",
                                         std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mintRes52.has_value() ) << "Mint failed on node52";

    // ---------- Node51 to Node50 ----------
    std::string childTransfer51 = "1.0";
    auto        parsed51        = node51->ParseChildTokens( childTransfer51 );
    ASSERT_TRUE( parsed51.has_value() ) << "Failed to parse child transfer string (node51)";

    auto transferRes51 = node51->TransferFunds( parsed51.value(),
                                                node50->GetAddress(),
                                                std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transferRes51.has_value() ) << "Transfer from node51 failed";
    auto [tx51, dur51] = transferRes51.value();

    ASSERT_TRUE(
        node50->WaitForTransactionIncoming( tx51, std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ) )
        << "Node50 did not receive transaction from node51";

    // ---------- Node52 to Node50 ----------
    std::string childTransfer52 = "1.0";
    auto        parsed52        = node52->ParseChildTokens( childTransfer52 );
    ASSERT_TRUE( parsed52.has_value() ) << "Failed to parse child transfer string (node52)";

    auto transferRes52 = node52->TransferFunds( parsed52.value(),
                                                node50->GetAddress(),
                                                std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( transferRes52.has_value() ) << "Transfer from node52 failed";
    auto [tx52, dur52] = transferRes52.value();

    ASSERT_TRUE(
        node50->WaitForTransactionIncoming( tx52, std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ) )
        << "Node50 did not receive transaction from node52";

    // ---------- Final balance checks ----------
    auto final50 = node50->GetBalance();
    auto final51 = node51->GetBalance();
    auto final52 = node52->GetBalance();

    EXPECT_EQ( final50 - init50, 2000000 + 500000 );
    EXPECT_EQ( final51 - init51, 10000000 - 500000 );
    EXPECT_EQ( final52 - init52, 10000000 - 2000000 );
}

// ------------------ Suite 1: Mint Main Tokens ------------------

/// Parameters for minting main tokens
struct MintMainCase_s
{
    std::string tokenValue;    // TokenValueInGNUS
    uint64_t    mintMain;      // Amount to mint in main tokens
    std::string expectedChild; // Expected delta of child balance (as string)
};

inline std::ostream &operator<<( std::ostream &os, MintMainCase_s const &c )
{
    return os << "MintMainCase_s{tokenValue='" << c.tokenValue << "', mintMain=" << c.mintMain << ", expectedChild='"
              << c.expectedChild << "'}";
}

class GeniusNodeMintMainTest : public ::testing::TestWithParam<MintMainCase_s>
{
};

TEST_P( GeniusNodeMintMainTest, DISABLED_MintMainBalance )
{
    auto        p = GetParam();
    std::string basePath;
    auto        node = CreateNode( p.tokenValue, basePath );

    uint64_t    initialMain        = node->GetBalance();
    std::string initialChildStr    = node->FormatChildTokens( initialMain );
    auto        parsedInitialChild = node->ParseChildTokens( initialChildStr );
    ASSERT_TRUE( parsedInitialChild.has_value() );

    auto res = node->MintTokens( p.mintMain, "", "", "", std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( res.has_value() );

    uint64_t    finalMain        = node->GetBalance();
    std::string finalChildStr    = node->FormatChildTokens( finalMain );
    auto        parsedFinalChild = node->ParseChildTokens( finalChildStr );
    ASSERT_TRUE( parsedFinalChild.has_value() );

    auto parsedExpectedDelta = node->ParseChildTokens( p.expectedChild );
    ASSERT_TRUE( parsedExpectedDelta.has_value() );

    EXPECT_EQ( finalMain - initialMain, p.mintMain );
    EXPECT_EQ( parsedFinalChild.value() - parsedInitialChild.value(), parsedExpectedDelta.value() );
}

INSTANTIATE_TEST_SUITE_P(
    MintMainVariations,
    GeniusNodeMintMainTest,
    ::testing::Values( MintMainCase_s{ .tokenValue = "1", .mintMain = 1000000, .expectedChild = "1" },
                       MintMainCase_s{ .tokenValue = "0.5", .mintMain = 1000000, .expectedChild = "2.0" },
                       MintMainCase_s{ .tokenValue = "2", .mintMain = 1000000, .expectedChild = "0.5" },
                       MintMainCase_s{ .tokenValue = "0.5", .mintMain = 2000000, .expectedChild = "4.0" } ) );

// ------------------ Suite 2: Mint Child Tokens ------------------

/// Parameters for minting child tokens
struct MintChildCase_s
{
    std::string tokenValue;   // TokenValueInGNUS
    std::string mintChild;    // Amount to mint in child tokens (as string)
    uint64_t    expectedMain; // Expected delta of main balance
};

inline std::ostream &operator<<( std::ostream &os, MintChildCase_s const &c )
{
    return os << "MintChildCase_s{tokenValue='" << c.tokenValue << "', mintChild='" << c.mintChild
              << "', expectedMain=" << c.expectedMain << "}";
}

class GeniusNodeMintChildTest : public ::testing::TestWithParam<MintChildCase_s>
{
protected:
    static void SetUpTestSuite()
    {
        // CleanTestSuiteDirectories( "GeniusNodeMintChildTest" );
    }
};

TEST_P( GeniusNodeMintChildTest, DISABLED_MintChildBalance )
{
    auto        p = GetParam();
    std::string basePath;
    auto        node = CreateNode( p.tokenValue, basePath );

    uint64_t    initialMain     = node->GetBalance();
    std::string initialChildStr = node->FormatChildTokens( initialMain );
    auto        parsedInitial   = node->ParseChildTokens( initialChildStr );
    ASSERT_TRUE( parsedInitial.has_value() );

    auto parsedMint = node->ParseChildTokens( p.mintChild );
    ASSERT_TRUE( parsedMint.has_value() );

    auto res = node->MintTokens( parsedMint.value(),
                                 "",
                                 "",
                                 "",
                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( res.has_value() );

    uint64_t    finalMain     = node->GetBalance();
    std::string finalChildStr = node->FormatChildTokens( finalMain );
    auto        parsedFinal   = node->ParseChildTokens( finalChildStr );
    ASSERT_TRUE( parsedFinal.has_value() );

    auto parsedExpectedDelta = node->ParseChildTokens( p.mintChild );
    ASSERT_TRUE( parsedExpectedDelta.has_value() );

    EXPECT_EQ( finalMain - initialMain, p.expectedMain );
    EXPECT_EQ( parsedFinal.value() - parsedInitial.value(), parsedExpectedDelta.value() );
}

INSTANTIATE_TEST_SUITE_P(
    MintChildVariations,
    GeniusNodeMintChildTest,
    ::testing::Values( MintChildCase_s{ .tokenValue = "1.0", .mintChild = "1.0", .expectedMain = 1000000 },
                       MintChildCase_s{ .tokenValue = "0.5", .mintChild = "1.0", .expectedMain = 500000 },
                       MintChildCase_s{ .tokenValue = "2.0", .mintChild = "1.0", .expectedMain = 2000000 } ) );

/// Suite 3: Transfer Main and Child Tokens with Base Node

/// Parameters for transfer-main tests
typedef struct
{
    std::string tokenValue;     ///< TokenValueInGNUS (used to mint child tokens)
    uint64_t    mintValue;      ///< Amount to mint on child node
    uint64_t    transferMain;   ///< Amount to transfer from child to base
    std::string expBase_child;  ///< Expected delta on base child tokens
    std::string expChild_child; ///< Expected delta on child child tokens
} TransferMainCase_s;

inline std::ostream &operator<<( std::ostream &os, TransferMainCase_s const &c )
{
    return os << "TransferMainCase_s{tokenValue='" << c.tokenValue << "', mintValue=" << c.mintValue
              << ", transferMain=" << c.transferMain << ", expBase_child='" << c.expBase_child << "', expChild_child='"
              << c.expChild_child << "'}";
}

class GeniusNodeTransferMainTest : public ::testing::TestWithParam<TransferMainCase_s>
{
protected:
    // Shared binary path & counter
    static inline std::string      binary_path;
    static inline std::atomic<int> dir_counter{ 30 };

    // Base node lives for entire suite
    static inline sgns::GeniusNode *base_node = nullptr;
    // All created nodes (base + children)
    static inline std::vector<sgns::GeniusNode *> all_nodes;

    // Child node for each test
    sgns::GeniusNode *child_node = nullptr;
    DevConfig_st      dev_cfg_child{};

    // Called once before any tests in this suite
    static void SetUpTestSuite()
    {
        // Initialize binary path
        binary_path = boost::dll::program_location().parent_path().string();

        // Configure base node
        DevConfig_st cfg_base  = { "0xcafe", "0.65", "1.0", 0, {} };
        std::string  base_path = binary_path + "/node00/";
        std::strncpy( cfg_base.BaseWritePath, base_path.c_str(), sizeof( cfg_base.BaseWritePath ) - 1 );

        // Instantiate base node
        base_node = new sgns::GeniusNode( cfg_base,
                                          "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                          false,
                                          false );
        all_nodes.push_back( base_node );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    }

    static void TearDownTestSuite()
    {
        for ( auto node : all_nodes )
        {
            delete node;
        }
        all_nodes.clear();
    }

    void SetUp() override
    {
        int         id         = dir_counter.fetch_add( 1 );
        std::string child_path = binary_path + "/node" + std::to_string( id ) + "/";

        dev_cfg_child = { "0xcafe", "0.65", "1.0", 0, {} };
        std::strncpy( dev_cfg_child.BaseWritePath, child_path.c_str(), sizeof( dev_cfg_child.BaseWritePath ) - 1 );

        child_node = new sgns::GeniusNode( dev_cfg_child,
                                           "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                           false,
                                           false );
        all_nodes.push_back( child_node );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

        auto base_addr  = base_node->GetPubSub()->GetLocalAddress();
        auto child_addr = child_node->GetPubSub()->GetLocalAddress();
        base_node->GetPubSub()->AddPeers( { child_addr } );
        child_node->GetPubSub()->AddPeers( { base_addr } );
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    }

    // No per-test cleanup; nodes live until TearDownTestSuite
    void TearDown() override {}
};

TEST_P( GeniusNodeTransferMainTest, DISABLED_TransferMainBalance )
{
    const auto c = GetParam();

    // Record initial balances
    auto initBase_main  = base_node->GetBalance();
    auto parsedInitBase = base_node->ParseChildTokens( base_node->FormatChildTokens( initBase_main ) );
    ASSERT_TRUE( parsedInitBase.has_value() );

    auto initChild_main  = child_node->GetBalance();
    auto parsedInitChild = child_node->ParseChildTokens( child_node->FormatChildTokens( initChild_main ) );
    ASSERT_TRUE( parsedInitChild.has_value() );

    // Mint on child and transfer to base
    ASSERT_TRUE(
        child_node->MintTokens( c.mintValue, "", "", "", std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) )
            .has_value() );
    auto [txId, _] = child_node
                         ->TransferFunds( c.transferMain,
                                          base_node->GetAddress(),
                                          std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) )
                         .value();
    ASSERT_TRUE(
        base_node->WaitForTransactionIncoming( txId, std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ) );

    // Verify final balances and child deltas
    auto finalBase_main  = base_node->GetBalance();
    auto parsedFinalBase = base_node->ParseChildTokens( base_node->FormatChildTokens( finalBase_main ) );
    ASSERT_TRUE( parsedFinalBase.has_value() );

    auto finalChild_main  = child_node->GetBalance();
    auto parsedFinalChild = child_node->ParseChildTokens( child_node->FormatChildTokens( finalChild_main ) );
    ASSERT_TRUE( parsedFinalChild.has_value() );

    auto expBase_val  = base_node->ParseChildTokens( c.expBase_child );
    auto expChild_val = child_node->ParseChildTokens( c.expChild_child );
    ASSERT_TRUE( expBase_val.has_value() && expChild_val.has_value() );

    // Child balance decreased by transfer; base balance increased by transfer
    EXPECT_EQ( finalChild_main - initChild_main, c.mintValue - c.transferMain );
    EXPECT_EQ( finalBase_main - initBase_main, c.transferMain );
    // Child and base child-token deltas
    EXPECT_EQ( parsedFinalChild.value() - parsedInitChild.value(), expChild_val.value() );
    EXPECT_EQ( parsedFinalBase.value() - parsedInitBase.value(), expBase_val.value() );
}

INSTANTIATE_TEST_SUITE_P( TransferMainVariations,
                          GeniusNodeTransferMainTest,
                          ::testing::Values( TransferMainCase_s{ "1.0", 1000000, 400000, "0.4", "0.6" },
                                             TransferMainCase_s{ "1.0", 1000000, 400000, "0.4", "0.6" },
                                             TransferMainCase_s{ "0.5", 2000000, 500000, "0.25", "0.75" } ) );

// ------------------ Suite 4: Transfer Child via Main Methods ------------------

/// @brief Parameters for transfer-child tests (converted via main units)
struct TransferChildCase_s
{
    std::string tokenValue;
    std::string transferChild; ///< Amount to transfer in child tokens (as string)
    std::string expA_child;    ///< Expected delta A child (as string)
    std::string expB_child;    ///< Expected delta B child (as string)
};

inline std::ostream &operator<<( std::ostream &os, TransferChildCase_s const &c )
{
    return os << "TransferChildCase_s{tokenValue='" << c.tokenValue << "', transferChild='" << c.transferChild
              << "', expA_child='" << c.expA_child << "', expB_child='" << c.expB_child << "'}";
}

class GeniusNodeTransferChildTest : public ::testing::TestWithParam<TransferChildCase_s>
{
};

TEST_P( GeniusNodeTransferChildTest, DISABLED_TransferChildBalance )
{
    auto        c = GetParam();
    std::string baseA, baseB;
    auto        nodeA = CreateNode( c.tokenValue, baseA );
    auto        nodeB = CreateNode( c.tokenValue, baseB );

    uint64_t initA_main     = nodeA->GetBalance();
    auto     initA_childStr = nodeA->FormatChildTokens( initA_main );
    auto     parsedInitA    = nodeA->ParseChildTokens( initA_childStr );
    ASSERT_TRUE( parsedInitA.has_value() );
    uint64_t initB_main     = nodeB->GetBalance();
    auto     initB_childStr = nodeB->FormatChildTokens( initB_main );
    auto     parsedInitB    = nodeB->ParseChildTokens( initB_childStr );
    ASSERT_TRUE( parsedInitB.has_value() );

    auto parsedChild = nodeA->ParseChildTokens( c.transferChild );
    ASSERT_TRUE( parsedChild.has_value() );
    std::string mainStr      = nodeA->FormatTokens( parsedChild.value() );
    auto        transferMain = std::stoull( mainStr );

    auto t = nodeA->TransferFunds( transferMain,
                                   nodeB->GetAddress(),
                                   std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( t.has_value() );

    auto finalA = nodeA->ParseChildTokens( nodeA->FormatChildTokens( nodeA->GetBalance() ) );
    auto finalB = nodeB->ParseChildTokens( nodeB->FormatChildTokens( nodeB->GetBalance() ) );
    ASSERT_TRUE( finalA.has_value() && finalB.has_value() );

    auto expA_childVal = nodeA->ParseChildTokens( c.expA_child );
    ASSERT_TRUE( expA_childVal.has_value() );
    auto expB_childVal = nodeB->ParseChildTokens( c.expB_child );
    ASSERT_TRUE( expB_childVal.has_value() );

    EXPECT_EQ( finalA.value() - parsedInitA.value(), expA_childVal.value() );
    EXPECT_EQ( finalB.value() - parsedInitB.value(), expB_childVal.value() );
}

INSTANTIATE_TEST_SUITE_P( DISABLED_TransferChildVariations,
                          GeniusNodeTransferChildTest,
                          ::testing::Values( TransferChildCase_s{ "1.0", "0.2", "-0.2", "0.2" },
                                             TransferChildCase_s{ "0.5", "0.25", "-0.25", "0.25" } ) );
