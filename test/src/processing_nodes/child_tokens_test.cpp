#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <thread>
#include <cstring>
#include <ostream>

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
        std::string binaryPath = boost::dll::program_location().parent_path().string();
        std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        outPath                = binaryPath + "/" + testName;

        boost::filesystem::remove_all( outPath );
        boost::filesystem::create_directories( outPath );

        DevConfig_st devConfig = { "0xcafe", "0.65", tokenValue, 0, "" };
        std::strncpy( devConfig.BaseWritePath, outPath.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
        devConfig.BaseWritePath[sizeof( devConfig.BaseWritePath ) - 1] = '\0';

        const char *key  = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
        auto        node = std::make_unique<sgns::GeniusNode>( devConfig, key, false, false );
        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        return node;
    }
} // namespace

// ------------------ Suite 1: Mint Main Tokens ------------------
/// @brief Parameters for mint-main tests
struct MintMainCase_s
{
    std::string tokenValue;    ///< TokenValueInGNUS
    uint64_t    mintMain;      ///< Amount to mint in main tokens
    uint64_t    expectedMain;  ///< Expected delta of main balance
    uint64_t    expectedChild; ///< Expected delta of child balance
};

inline std::ostream &operator<<( std::ostream &os, MintMainCase_s const &c )
{
    return os << "MintMainCase_s{tokenValue='" << c.tokenValue << "', mintMain=" << c.mintMain
              << ", expectedMain=" << c.expectedMain << ", expectedChild=" << c.expectedChild << "}";
}

class GeniusNodeMintMainTest : public ::testing::TestWithParam<MintMainCase_s>
{
};

TEST_P( GeniusNodeMintMainTest, MintMainBalance )
{
    auto        p = GetParam();
    std::string basePath;
    auto        node = CreateNode( p.tokenValue, basePath );

    uint64_t initialMain  = node->GetBalance();
    uint64_t initialChild = node->GetChildBalance().value();

    auto res = node->MintTokens( p.mintMain, "", "", "" );
    ASSERT_TRUE( res.has_value() ) << "MintTokens failed for " << p;

    assertWaitForCondition( [&]() { return node->GetBalance() - initialMain == p.expectedMain; },
                            std::chrono::milliseconds( 20000 ),
                            "Main mint timeout for " + p.tokenValue );

    uint64_t finalMain  = node->GetBalance();
    uint64_t finalChild = node->GetChildBalance().value();
    EXPECT_EQ( finalMain - initialMain, p.expectedMain ) << p;
    EXPECT_EQ( finalChild - initialChild, p.expectedChild ) << p;

    boost::filesystem::remove_all( basePath );
}

INSTANTIATE_TEST_SUITE_P( MintMainVariations,
                          GeniusNodeMintMainTest,
                          ::testing::Values( MintMainCase_s{ "1.0", 1000, 1000, 1000 },
                                             MintMainCase_s{ "0.5", 1000, 1000, 2000 },
                                             MintMainCase_s{ "1.25", 1000, 1000, 800 } ) );

// ------------------ Suite 2: Mint Child Tokens ------------------
/// @brief Parameters for mint-child tests
struct MintChildCase_s
{
    std::string tokenValue;    ///< TokenValueInGNUS
    uint64_t    mintChild;     ///< Amount to mint in child tokens
    uint64_t    expectedChild; ///< Expected delta of child balance
    uint64_t    expectedMain;  ///< Expected delta of main balance
};

inline std::ostream &operator<<( std::ostream &os, MintChildCase_s const &c )
{
    return os << "MintChildCase_s{tokenValue='" << c.tokenValue << "', mintChild=" << c.mintChild
              << ", expectedChild=" << c.expectedChild << ", expectedMain=" << c.expectedMain << "}";
}

class GeniusNodeMintChildTest : public ::testing::TestWithParam<MintChildCase_s>
{
};

TEST_P( GeniusNodeMintChildTest, MintChildBalance )
{
    auto        p = GetParam();
    std::string basePath;
    auto        node = CreateNode( p.tokenValue, basePath );

    uint64_t initialMain  = node->GetBalance();
    uint64_t initialChild = node->GetChildBalance().value();

    auto res = node->MintChildTokens( p.mintChild, "", "", "" );
    ASSERT_TRUE( res.has_value() ) << "MintChildTokens failed for " << p;

    assertWaitForCondition( [&]() { return node->GetChildBalance().value() - initialChild == p.expectedChild; },
                            std::chrono::milliseconds( 20000 ),
                            "Child mint timeout for " + p.tokenValue );

    EXPECT_EQ( node->GetChildBalance().value() - initialChild, p.expectedChild ) << p;
    EXPECT_EQ( node->GetBalance() - initialMain, p.expectedMain ) << p;

    boost::filesystem::remove_all( basePath );
}

INSTANTIATE_TEST_SUITE_P( MintChildVariations,
                          GeniusNodeMintChildTest,
                          ::testing::Values( MintChildCase_s{ "1.0", 500, 500, 500 },
                                             MintChildCase_s{ "0.5", 1000, 1000, 500 },
                                             MintChildCase_s{ "1.5", 2000, 2000, 3000 } ) );

// ------------------ Suite 3: Transfer Main Tokens ------------------
/// @brief Parameters for transfer-main tests
struct TransferMainCase_s
{
    std::string tokenValue;   ///< TokenValueInGNUS
    uint64_t    mintMain;     ///< Mint on nodeA
    uint64_t    transferMain; ///< Transfer from A to B
    uint64_t    expA_main;    ///< Expected delta A main
    uint64_t    expA_child;   ///< Expected delta A child
    uint64_t    expB_main;    ///< Expected delta B main
    uint64_t    expB_child;   ///< Expected delta B child
};

inline std::ostream &operator<<( std::ostream &os, TransferMainCase_s const &c )
{
    return os << "TransferMainCase_s{tokenValue='" << c.tokenValue << "', mintMain=" << c.mintMain
              << ", transferMain=" << c.transferMain << ", expA_main=" << c.expA_main << ", expA_child=" << c.expA_child
              << ", expB_main=" << c.expB_main << ", expB_child=" << c.expB_child << "}";
}

class GeniusNodeTransferMainTest : public ::testing::TestWithParam<TransferMainCase_s>
{
};

TEST_P( GeniusNodeTransferMainTest, DISABLED_TransferMainBalance )
{
    auto        c = GetParam();
    std::string baseA, baseB;
    auto        nodeA = CreateNode( c.tokenValue, baseA );
    auto        nodeB = CreateNode( c.tokenValue, baseB );

    uint64_t initA_main  = nodeA->GetBalance();
    uint64_t initA_child = nodeA->GetChildBalance().value();
    uint64_t initB_main  = nodeB->GetBalance();
    uint64_t initB_child = nodeB->GetChildBalance().value();

    auto r = nodeA->MintTokens( c.mintMain, "", "", "" );
    ASSERT_TRUE( r.has_value() ) << "MintTokens failed for " << c;

    auto t = nodeA->TransferFunds( c.transferMain, nodeB->GetAddress(), std::chrono::milliseconds( 20000 ) );
    ASSERT_TRUE( t.has_value() ) << "TransferFunds failed for " << c;

    assertWaitForCondition( [&]() { return nodeA->GetBalance() - initA_main == c.expA_main; },
                            std::chrono::milliseconds( 20000 ),
                            "A main timeout for " + c.tokenValue );
    assertWaitForCondition( [&]() { return nodeB->GetBalance() - initB_main == c.expB_main; },
                            std::chrono::milliseconds( 20000 ),
                            "B main timeout for " + c.tokenValue );

    EXPECT_EQ( nodeA->GetBalance() - initA_main, c.expA_main ) << c;
    EXPECT_EQ( nodeA->GetChildBalance().value() - initA_child, c.expA_child ) << c;
    EXPECT_EQ( nodeB->GetBalance() - initB_main, c.expB_main ) << c;
    EXPECT_EQ( nodeB->GetChildBalance().value() - initB_child, c.expB_child ) << c;

    boost::filesystem::remove_all( baseA );
    boost::filesystem::remove_all( baseB );
}

INSTANTIATE_TEST_SUITE_P( DISABLED_TransferMainVariations,
                          GeniusNodeTransferMainTest,
                          ::testing::Values( TransferMainCase_s{ "1.0", 1000, 400, 600, 0, 400, 0 },
                                             TransferMainCase_s{ "0.5", 2000, 500, 1500, 0, 500, 0 } ) );

// ------------------ Suite 4: Transfer Child Tokens ------------------
/// @brief Parameters for transfer-child tests
struct TransferChildCase_s
{
    std::string tokenValue;
    uint64_t    mintChild;
    uint64_t    transferChild;
    uint64_t    expA_main;
    uint64_t    expA_child;
    uint64_t    expB_main;
    uint64_t    expB_child;
};

inline std::ostream &operator<<( std::ostream &os, TransferChildCase_s const &c )
{
    return os << "TransferChildCase_s{tokenValue='" << c.tokenValue << "', mintChild=" << c.mintChild
              << ", transferChild=" << c.transferChild << ", expA_main=" << c.expA_main
              << ", expA_child=" << c.expA_child << ", expB_main=" << c.expB_main << ", expB_child=" << c.expB_child
              << "}";
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

    uint64_t initA_main  = nodeA->GetBalance();
    uint64_t initA_child = nodeA->GetChildBalance().value();
    uint64_t initB_main  = nodeB->GetBalance();
    uint64_t initB_child = nodeB->GetChildBalance().value();

    auto r = nodeA->MintChildTokens( c.mintChild, "", "", "" );
    ASSERT_TRUE( r.has_value() ) << "MintChildTokens failed for " << c;

    auto t = nodeA->TransferChildTokens( c.transferChild, nodeB->GetAddress(), std::chrono::milliseconds( 20000 ) );
    ASSERT_TRUE( t.has_value() ) << "TransferChildTokens failed for " << c;

    assertWaitForCondition( [&]() { return nodeA->GetChildBalance().value() - initA_child == c.expA_child; },
                            std::chrono::milliseconds( 20000 ),
                            "A child timeout for " + c.tokenValue );
    assertWaitForCondition( [&]() { return nodeB->GetChildBalance().value() - initB_child == c.expB_child; },
                            std::chrono::milliseconds( 20000 ),
                            "B child timeout for " + c.tokenValue );

    EXPECT_EQ( nodeA->GetChildBalance().value() - initA_child, c.expA_child ) << c;
    EXPECT_EQ( nodeA->GetBalance() - initA_main, c.expA_main ) << c;
    EXPECT_EQ( nodeB->GetChildBalance().value() - initB_child, c.expB_child ) << c;
    EXPECT_EQ( nodeB->GetBalance() - initB_main, c.expB_main ) << c;

    boost::filesystem::remove_all( baseA );
    boost::filesystem::remove_all( baseB );
}

INSTANTIATE_TEST_SUITE_P( DISABLED_TransferChildVariations,
                          GeniusNodeTransferChildTest,
                          ::testing::Values( TransferChildCase_s{ "1.0", 500, 200, 300, 300, 200, 200 },
                                             TransferChildCase_s{ "0.5", 1000, 250, 375, 750, 125, 250 },
                                             TransferChildCase_s{ "1.5", 300, 100, 150, 200, 100, 100 } ) );
