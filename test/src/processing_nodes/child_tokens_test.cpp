#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <thread>
#include <cstring>
#include <ostream>

#include "account/GeniusNode.hpp"
#include "testutil/wait_condition.hpp"

using namespace sgns::test;

/**
 * @brief Common fixture for GeniusNode tests.
 */
template <typename ParamType>
class GeniusNodeCommonTest : public ::testing::TestWithParam<ParamType>
{
protected:
    DevConfig_st                      devConfig;
    std::string                       baseA, baseB;
    std::unique_ptr<sgns::GeniusNode> nodeA, nodeB;

    void SetUp() override
    {
        auto        p          = this->GetParam();
        std::string binaryPath = boost::dll::program_location().parent_path().string();
        std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        baseA                  = binaryPath + "/" + testName + "_A";
        baseB                  = binaryPath + "/" + testName + "_B";

        boost::filesystem::remove_all( baseA );
        boost::filesystem::remove_all( baseB );
        boost::filesystem::create_directories( baseA );
        boost::filesystem::create_directories( baseB );

        devConfig = { "0xcafe", "0.65", p.tokenValue, 0, "" };
        std::strncpy( devConfig.BaseWritePath, baseA.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
        devConfig.BaseWritePath[sizeof( devConfig.BaseWritePath ) - 1] = '\0';

        const char *key = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
        nodeA           = std::make_unique<sgns::GeniusNode>( devConfig, key, false, false );

        std::strncpy( devConfig.BaseWritePath, baseB.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
        devConfig.BaseWritePath[sizeof( devConfig.BaseWritePath ) - 1] = '\0';
        nodeB = std::make_unique<sgns::GeniusNode>( devConfig, key, false, false );

        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    }

    void TearDown() override
    {
        nodeA.reset();
        nodeB.reset();
        boost::filesystem::remove_all( baseA );
        boost::filesystem::remove_all( baseB );
    }
};

// ------------------ Suite 1: Mint Main Tokens ------------------
struct MintMainCase_s
{
    std::string tokenValue;
    uint64_t    mintMain;
    uint64_t    expectedMain;
    uint64_t    expectedChild;
};

inline std::ostream &operator<<( std::ostream &os, MintMainCase_s const &c )
{
    return os << "MintMainCase_s{tokenValue='" << c.tokenValue << "', mintMain=" << c.mintMain
              << ", expectedMain=" << c.expectedMain << ", expectedChild=" << c.expectedChild << "}";
}

class GeniusNodeMintMainTest : public GeniusNodeCommonTest<MintMainCase_s>
{
};

TEST_P( GeniusNodeMintMainTest, MintMainBalance )
{
    auto p   = this->GetParam();
    auto res = this->nodeA->MintTokens( p.mintMain, "", "", "" );
    ASSERT_TRUE( res.has_value() ) << "MintTokens failed for " << p;
    assertWaitForCondition( [&]() { return this->nodeA->GetBalance() == p.expectedMain; },
                            std::chrono::milliseconds( 20000 ),
                            "Main mint timeout for " + p.tokenValue );
    EXPECT_EQ( this->nodeA->GetBalance(), p.expectedMain ) << p;
    EXPECT_EQ( this->nodeA->GetChildBalance().value(), p.expectedChild ) << p;
}

INSTANTIATE_TEST_SUITE_P(
    MintMainVariations,
    GeniusNodeMintMainTest,
    ::testing::Values(
        MintMainCase_s{ .tokenValue = "1.0", .mintMain = 1000, .expectedMain = 1000, .expectedChild = 1000 },
        MintMainCase_s{ .tokenValue = "0.5", .mintMain = 1000, .expectedMain = 1000, .expectedChild = 2000 },
        MintMainCase_s{ .tokenValue = "1.25", .mintMain = 1000, .expectedMain = 1000, .expectedChild = 800 } ) );

// ------------------ Suite 2: Mint Child Tokens ------------------
struct MintChildCase_s
{
    std::string tokenValue;
    uint64_t    mintChild;
    uint64_t    expectedChild;
    uint64_t    expectedMain;
};

inline std::ostream &operator<<( std::ostream &os, MintChildCase_s const &c )
{
    return os << "MintChildCase_s{tokenValue='" << c.tokenValue << "', mintChild=" << c.mintChild
              << ", expectedChild=" << c.expectedChild << ", expectedMain=" << c.expectedMain << "}";
}

class GeniusNodeMintChildTest : public GeniusNodeCommonTest<MintChildCase_s>
{
};

TEST_P( GeniusNodeMintChildTest, DISABLED_MintChildBalance )
{
    auto p   = this->GetParam();
    auto res = this->nodeA->MintChildTokens( p.mintChild, "", "", "" );
    ASSERT_TRUE( res.has_value() ) << "MintChildTokens failed for " << p;
    assertWaitForCondition( [&]() { return this->nodeA->GetChildBalance().value() == p.expectedChild; },
                            std::chrono::milliseconds( 20000 ),
                            "Child mint timeout for " + p.tokenValue );
    EXPECT_EQ( this->nodeA->GetChildBalance().value(), p.expectedChild ) << p;
    EXPECT_EQ( this->nodeA->GetBalance(), p.expectedMain ) << p;
}

INSTANTIATE_TEST_SUITE_P(
    DISABLED_MintChildVariations,
    GeniusNodeMintChildTest,
    ::testing::Values(
        MintChildCase_s{ .tokenValue = "1.0", .mintChild = 500, .expectedChild = 500, .expectedMain = 500 },
        MintChildCase_s{ .tokenValue = "0.5", .mintChild = 1000, .expectedChild = 1000, .expectedMain = 500 },
        MintChildCase_s{ .tokenValue = "1.5", .mintChild = 2000, .expectedChild = 2000, .expectedMain = 3000 } ) );

// ------------------ Suite 3: Transfer Main Tokens ------------------
struct TransferMainCase_s
{
    std::string tokenValue;
    uint64_t    mintMain;
    uint64_t    transferMain;
    uint64_t    expA_main;
    uint64_t    expA_child;
    uint64_t    expB_main;
    uint64_t    expB_child;
};

inline std::ostream &operator<<( std::ostream &os, TransferMainCase_s const &c )
{
    return os << "TransferMainCase_s{tokenValue='" << c.tokenValue << "', mintMain=" << c.mintMain
              << ", transferMain=" << c.transferMain << ", expA_main=" << c.expA_main << ", expA_child=" << c.expA_child
              << ", expB_main=" << c.expB_main << ", expB_child=" << c.expB_child << "}";
}

class GeniusNodeTransferMainTest : public GeniusNodeCommonTest<TransferMainCase_s>
{
};

TEST_P( GeniusNodeTransferMainTest, DISABLED_TransferMainBalance )
{
    auto c = this->GetParam();
    auto r = this->nodeA->MintTokens( c.mintMain, "", "", "" );
    ASSERT_TRUE( r.has_value() ) << "MintTokens failed for " << c;
    assertWaitForCondition( [&]() { return this->nodeA->GetBalance() == c.mintMain; },
                            std::chrono::milliseconds( 20000 ) );
    auto t = this->nodeA->TransferFunds( c.transferMain,
                                         this->nodeB->GetAddress(),
                                         std::chrono::milliseconds( 20000 ) );
    ASSERT_TRUE( t.has_value() ) << "TransferFunds failed for " << c;
    assertWaitForCondition( [&]() { return this->nodeA->GetBalance() == c.expA_main; },
                            std::chrono::milliseconds( 20000 ) );
    EXPECT_EQ( this->nodeA->GetBalance(), c.expA_main ) << c;
    EXPECT_EQ( this->nodeA->GetChildBalance().value(), c.expA_child ) << c;
    assertWaitForCondition( [&]() { return this->nodeB->GetBalance() == c.expB_main; },
                            std::chrono::milliseconds( 20000 ) );
    EXPECT_EQ( this->nodeB->GetBalance(), c.expB_main ) << c;
    EXPECT_EQ( this->nodeB->GetChildBalance().value(), c.expB_child ) << c;
}

INSTANTIATE_TEST_SUITE_P( DISABLED_TransferMainVariations,
                          GeniusNodeTransferMainTest,
                          ::testing::Values( TransferMainCase_s{ .tokenValue   = "1.0",
                                                                 .mintMain     = 1000,
                                                                 .transferMain = 400,
                                                                 .expA_main    = 600,
                                                                 .expA_child   = 0,
                                                                 .expB_main    = 400,
                                                                 .expB_child   = 0 },
                                             TransferMainCase_s{ .tokenValue   = "0.5",
                                                                 .mintMain     = 2000,
                                                                 .transferMain = 500,
                                                                 .expA_main    = 1500,
                                                                 .expA_child   = 0,
                                                                 .expB_main    = 500,
                                                                 .expB_child   = 0 } ) );

// ------------------ Suite 4: Transfer Child Tokens ------------------
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

class GeniusNodeTransferChildTest : public GeniusNodeCommonTest<TransferChildCase_s>
{
};

TEST_P( GeniusNodeTransferChildTest, DISABLED_TransferChildBalance )
{
    auto c = this->GetParam();
    auto r = this->nodeA->MintChildTokens( c.mintChild, "", "", "" );
    ASSERT_TRUE( r.has_value() ) << "MintChildTokens failed for " << c;
    assertWaitForCondition( [&]()
                            { return this->nodeA->GetChildBalance().value() == ( c.mintChild - c.transferChild ); },
                            std::chrono::milliseconds( 20000 ) );
    auto t = this->nodeA->TransferChildTokens( c.transferChild,
                                               this->nodeB->GetAddress(),
                                               std::chrono::milliseconds( 20000 ) );
    ASSERT_TRUE( t.has_value() ) << "TransferChildTokens failed for " << c;
    assertWaitForCondition( [&]() { return this->nodeA->GetChildBalance().value() == c.expA_child; },
                            std::chrono::milliseconds( 20000 ) );
    EXPECT_EQ( this->nodeA->GetChildBalance().value(), c.expA_child ) << c;
    EXPECT_EQ( this->nodeA->GetBalance(), c.expA_main ) << c;
    assertWaitForCondition( [&]() { return this->nodeB->GetChildBalance().value() == c.expB_child; },
                            std::chrono::milliseconds( 20000 ) );
    EXPECT_EQ( this->nodeB->GetChildBalance().value(), c.expB_child ) << c;
    EXPECT_EQ( this->nodeB->GetBalance(), c.expB_main ) << c;
}

INSTANTIATE_TEST_SUITE_P( DISABLED_TransferChildVariations,
                          GeniusNodeTransferChildTest,
                          ::testing::Values( TransferChildCase_s{ .tokenValue    = "1.0",
                                                                  .mintChild     = 500,
                                                                  .transferChild = 200,
                                                                  .expA_main     = 300,
                                                                  .expA_child    = 300,
                                                                  .expB_main     = 200,
                                                                  .expB_child    = 200 },
                                             TransferChildCase_s{ .tokenValue    = "0.5",
                                                                  .mintChild     = 1000,
                                                                  .transferChild = 250,
                                                                  .expA_main     = 375,
                                                                  .expA_child    = 750,
                                                                  .expB_main     = 125,
                                                                  .expB_child    = 250 },
                                             TransferChildCase_s{ .tokenValue    = "1.5",
                                                                  .mintChild     = 300,
                                                                  .transferChild = 100,
                                                                  .expA_main     = 150,
                                                                  .expA_child    = 200,
                                                                  .expB_main     = 100,
                                                                  .expB_child    = 100 } ) );
