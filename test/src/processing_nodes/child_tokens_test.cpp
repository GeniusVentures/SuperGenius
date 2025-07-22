#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <thread>
#include <cstring>
#include <ostream>
#include <atomic>
#include "account/GeniusNode.hpp"
<<<<<<< HEAD
=======
#include "account/TokenID.hpp"
>>>>>>> origin/dev_logfixes
#include "testutil/wait_condition.hpp"
#include <boost/multiprecision/cpp_dec_float.hpp>

using namespace sgns::test;
using boost::multiprecision::cpp_dec_float_50;

namespace
{
    /**
     * @brief Helper to create a GeniusNode with its own directory and cleanup.
     * @param tokenValue TokenValueInGNUS to initialize DevConfig.
<<<<<<< HEAD
     * @return unique_ptr to the initialized GeniusNode.
     */
    std::unique_ptr<sgns::GeniusNode> CreateNode( const std::string &tokenValue )
    {
        static std::atomic<int> node_counter{ 0 };
        int                     id = node_counter.fetch_add( 1 );
=======
     * @param tokenId TokenID to initialize DevConfig.
     * @return unique_ptr to the initialized GeniusNode.
     */
    std::unique_ptr<sgns::GeniusNode> CreateNode( const std::string &self_address,
                                                  const std::string &tokenValue,
                                                  sgns::TokenID      tokenId,
                                                  bool               isProcessor = false )
    {
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );
>>>>>>> origin/dev_logfixes

        std::string binaryPath = boost::dll::program_location().parent_path().string();
        const char *filePath   = ::testing::UnitTest::GetInstance()->current_test_info()->file();
        std::string fileStem   = std::filesystem::path( filePath ).stem().string();
<<<<<<< HEAD
        auto        outPath    = binaryPath + /*"/" + fileStem +*/ "/node_" + std::to_string( id ) + "/";

        DevConfig_st devConfig = { "0xcafe", "0.65", tokenValue, "0", "" };
        std::strncpy( devConfig.BaseWritePath, outPath.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
=======
        auto        outPath    = binaryPath + "/node_" + std::to_string( id ) + "/";

        DevConfig_st devConfig = { "", "0.65", tokenValue, tokenId, "" };
        std::strncpy( devConfig.Addr, self_address.c_str(), sizeof( devConfig.Addr ) - 1 );
        std::strncpy( devConfig.BaseWritePath, outPath.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
        devConfig.Addr[sizeof( devConfig.Addr ) - 1]                   = '\0';
>>>>>>> origin/dev_logfixes
        devConfig.BaseWritePath[sizeof( devConfig.BaseWritePath ) - 1] = '\0';

        std::string key;
        key.reserve( 64 );

        std::mt19937 rng( static_cast<uint32_t>( std::time( nullptr ) ) + static_cast<uint32_t>( id ) );
        std::uniform_int_distribution<> dist( 0, 15 );
<<<<<<< HEAD

=======
>>>>>>> origin/dev_logfixes
        std::generate_n( std::back_inserter( key ),
                         64,
                         [&]()
                         {
<<<<<<< HEAD
                             static constexpr std::string_view hex_chars = "0123456789abcdef";
                             return hex_chars[dist( rng )];
                         } );

        auto node = std::make_unique<sgns::GeniusNode>( devConfig, key.c_str(), false, false );
=======
                             static constexpr std::string_view hexChars = "0123456789abcdef";
                             return hexChars[dist( rng )];
                         } );

        uint16_t uniquePort = static_cast<uint16_t>( 40001 + id );
        auto     node = std::make_unique<sgns::GeniusNode>( devConfig, key.c_str(), false, isProcessor, uniquePort );

>>>>>>> origin/dev_logfixes
        std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
        return node;
    }

    /**
     * Adds two decimal numbers represented as strings.
     * @param a Decimal string, e.g. "-0.4"
     * @param b Decimal string, e.g. "0.6"
     * @return std::string Result of the addition without unnecessary trailing zeros, e.g. "0.2"
     */
    std::string addDecimalStrings( const std::string &a, const std::string &b )
    {
        cpp_dec_float_50 da( a );
        cpp_dec_float_50 db( b );
        cpp_dec_float_50 sum = da + db;
        std::string      s   = sum.convert_to<std::string>();
        if ( s.find( '.' ) != std::string::npos )
        {
            while ( !s.empty() && s.back() == '0' )
            {
                s.pop_back();
            }
            if ( !s.empty() && s.back() == '.' )
            {
                s.pop_back();
            }
        }
        return s;
    }

} // namespace

<<<<<<< HEAD
/// Suite: Simple Three-Node Transfers

TEST( TransferTokenValue, ThreeNodeTransferTest )
{
    auto node50 = CreateNode( "1.0" );
    auto node51 = CreateNode( "0.5" );
    auto node52 = CreateNode( "2.0" );

=======
// Suite: Enhanced Three-Node Transfers with Grouped Minting and Change
TEST( TransferTokenValue, ThreeNodeTransferTest )
{
    // Create nodes
    auto node50 = CreateNode( "0xcafe", "1.0", sgns::TokenID::FromBytes( { 0x50 } ) );
    auto node51 = CreateNode( "0xcade", "0.5", sgns::TokenID::FromBytes( { 0x51 } ) );
    auto node52 = CreateNode( "0xdafe", "2.0", sgns::TokenID::FromBytes( { 0x52 } ) );

    // Configure peer connections
>>>>>>> origin/dev_logfixes
    node51->GetPubSub()->AddPeers( { node50->GetPubSub()->GetLocalAddress() } );
    node52->GetPubSub()->AddPeers( { node50->GetPubSub()->GetLocalAddress() } );
    node50->GetPubSub()->AddPeers( { node51->GetPubSub()->GetLocalAddress(), node52->GetPubSub()->GetLocalAddress() } );

<<<<<<< HEAD
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
=======
    // Record initial balances
    uint64_t init50_full = node50->GetBalance();
    uint64_t init50_t50  = node50->GetBalance( sgns::TokenID::FromBytes( { 0x50 } ) );
    uint64_t init50_t51  = node50->GetBalance( sgns::TokenID::FromBytes( { 0x51 } ) );
    uint64_t init50_t52  = node50->GetBalance( sgns::TokenID::FromBytes( { 0x52 } ) );
    uint64_t init51_t51  = node51->GetBalance( sgns::TokenID::FromBytes( { 0x51 } ) );
    uint64_t init52_t52  = node52->GetBalance( sgns::TokenID::FromBytes( { 0x52 } ) );

    std::cout << "Initial balances:\n";
    std::cout << "node50 total: " << init50_full << ", token50: " << init50_t50 << ", token51: " << init50_t51
              << ", token52: " << init50_t52 << '\n';
    std::cout << "node51 token51: " << init51_t51 << '\n';
    std::cout << "node52 token52: " << init52_t52 << '\n';

    // Define a struct for vectorized transfers
    struct Transfer
    {
        sgns::GeniusNode *src;
        sgns::TokenID     tokenId;
        uint64_t          amount;
    };

    // List of transfers: each entry is (source node, token ID, amount)
    std::vector<Transfer> transfers = { { node51.get(), sgns::TokenID::FromBytes( { 0x51 } ), 2000000 },
                                        { node52.get(), sgns::TokenID::FromBytes( { 0x52 } ), 500000 },
                                        { node51.get(), sgns::TokenID::FromBytes( { 0x51 } ), 100000 },
                                        { node52.get(), sgns::TokenID::FromBytes( { 0x52 } ), 250000 } };

    // Sum total amounts per source node
    uint64_t totalMint51 = 0;
    uint64_t totalMint52 = 0;
    for ( const auto &t : transfers )
    {
        if ( t.src == node51.get() )
        {
            totalMint51 += t.amount;
        }
        else if ( t.src == node52.get() )
        {
            totalMint52 += t.amount;
        }
    }

    // Ensure enough balance with +1 change
    auto mintRes51 = node51->MintTokens( totalMint51 + 1,
                                         "",
                                         "",
                                         sgns::TokenID::FromBytes( { 0x51 } ),
                                         std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mintRes51.has_value() ) << "Grouped mint failed on token51";
    std::cout << "Minted total " << ( totalMint51 + 1 ) << " of token51 on node51\n";

    auto mintRes52 = node52->MintTokens( totalMint52 + 1,
                                         "",
                                         "",
                                         sgns::TokenID::FromBytes( { 0x52 } ),
                                         std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mintRes52.has_value() ) << "Grouped mint failed on token52";
    std::cout << "Minted total " << ( totalMint52 + 1 ) << " of token52 on node52\n";

    // Execute each transfer in sequence
    for ( const auto &t : transfers )
    {
        auto transferRes = t.src->TransferFunds( t.amount,
                                                 node50->GetAddress(),
                                                 t.tokenId,
                                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( transferRes.has_value() ); // << "Transfer failed for " << t.tokenId;
        auto [txHash, duration] = transferRes.value();
        std::cout << "Transferred " << t.amount << " of " << t.tokenId << " in " << duration << " ms\n";

        ASSERT_TRUE(
            node50->WaitForTransactionIncoming( txHash, std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ) )
            << "node50 did not receive transaction " << txHash;
    }

    // Record final balances
    uint64_t final50_full = node50->GetBalance();
    uint64_t final50_t50  = node50->GetBalance( sgns::TokenID::FromBytes( { 0x50 } ) );
    uint64_t final50_t51  = node50->GetBalance( sgns::TokenID::FromBytes( { 0x51 } ) );
    uint64_t final50_t52  = node50->GetBalance( sgns::TokenID::FromBytes( { 0x52 } ) );
    uint64_t final51_t51  = node51->GetBalance( sgns::TokenID::FromBytes( { 0x51 } ) );
    uint64_t final52_t52  = node52->GetBalance( sgns::TokenID::FromBytes( { 0x52 } ) );

    std::cout << "Final balances:\n";
    std::cout << "node50 total: " << final50_full << ", token50: " << final50_t50 << ", token51: " << final50_t51
              << ", token52: " << final50_t52 << '\n';
    std::cout << "node51 total" << node51->GetBalance() << " token51: " << final51_t51 << '\n';
    std::cout << "node52 total" << node52->GetBalance() << " token52: " << final52_t52 << '\n';

    // Validate expected deltas and leftover
    uint64_t expected50 = totalMint51 + totalMint52;
    EXPECT_EQ( final50_t51 - init50_t51, totalMint51 );
    EXPECT_EQ( final50_t52 - init50_t52, totalMint52 );
    EXPECT_EQ( final50_full - init50_full, expected50 );

    // Node51 and node52 should have 1 leftover token each
    EXPECT_EQ( final51_t51 - init51_t51, 1 );
    EXPECT_EQ( final52_t52 - init52_t52, 1 );
>>>>>>> origin/dev_logfixes
}

// ------------------ Suite 1: Mint Main Tokens ------------------

/// Parameters for minting main tokens
struct MintMainCase_s
{
<<<<<<< HEAD
    std::string tokenValue;    // TokenValueInGNUS
    uint64_t    mintMain;      // Amount to mint in main tokens
    std::string expectedChild; // Expected delta of child balance (as string)
=======
    std::string   tokenValue;    // TokenValueInGNUS
    sgns::TokenID TokenID;       // TokenID
    uint64_t      mintMain;      // Amount to mint in main tokens
    std::string   expectedChild; // Expected delta of child balance (as string)
>>>>>>> origin/dev_logfixes
};

inline std::ostream &operator<<( std::ostream &os, MintMainCase_s const &c )
{
    return os << "MintMainCase_s{tokenValue='" << c.tokenValue << "', mintMain=" << c.mintMain << ", expectedChild='"
              << c.expectedChild << "'}";
}

class GeniusNodeMintMainTest : public ::testing::TestWithParam<MintMainCase_s>
{
};

TEST_P( GeniusNodeMintMainTest, MintMainBalance )
{
    auto p    = GetParam();
<<<<<<< HEAD
    auto node = CreateNode( p.tokenValue );

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

INSTANTIATE_TEST_SUITE_P( MintMainVariations,
                          GeniusNodeMintMainTest,
                          ::testing::Values( MintMainCase_s{ "1", 1000000, "1" },
                                             MintMainCase_s{ "0.5", 1000000, "2.0" },
                                             MintMainCase_s{ "2", 1000000, "0.5" },
                                             MintMainCase_s{ "0.5", 2000000, "4.0" } ) );
=======
    auto node = CreateNode( "0xdade", p.tokenValue, p.TokenID );

    uint64_t initialMain = node->GetBalance();
    auto     initFmtRes  = node->FormatTokens( initialMain, p.TokenID );
    ASSERT_TRUE( initFmtRes.has_value() );
    std::string initialChildStr    = initFmtRes.value();
    auto        parsedInitialChild = node->ParseTokens( initialChildStr, p.TokenID );
    ASSERT_TRUE( parsedInitialChild.has_value() );

    auto res = node->MintTokens( p.mintMain,
                                 "",
                                 "",
                                 p.TokenID,
                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( res.has_value() );

    auto finalFmtRes = node->FormatTokens( node->GetBalance(), p.TokenID );
    ASSERT_TRUE( finalFmtRes.has_value() );
    std::string finalChildStr    = finalFmtRes.value();
    auto        parsedFinalChild = node->ParseTokens( finalChildStr, p.TokenID );
    ASSERT_TRUE( parsedFinalChild.has_value() );

    auto parsedExpectedDelta = node->ParseTokens( p.expectedChild, p.TokenID );
    ASSERT_TRUE( parsedExpectedDelta.has_value() );

    EXPECT_EQ( node->GetBalance() - initialMain, p.mintMain );
    EXPECT_EQ( node->GetBalance( p.TokenID ) - initialMain, p.mintMain );
    EXPECT_EQ( parsedFinalChild.value() - parsedInitialChild.value(), parsedExpectedDelta.value() );
}

INSTANTIATE_TEST_SUITE_P(
    MintMainVariations,
    GeniusNodeMintMainTest,
    ::testing::Values( MintMainCase_s{ "1", sgns::TokenID::FromBytes( { 0x01 } ), 1000000, "1" },
                       MintMainCase_s{ "0.5", sgns::TokenID::FromBytes( { 0x05 } ), 1000000, "2.0" },
                       MintMainCase_s{ "2", sgns::TokenID::FromBytes( { 0x02 } ), 1000000, "0.5" },
                       MintMainCase_s{ "0.5", sgns::TokenID::FromBytes( { 0x05 } ), 2000000, "4.0" } ) );
>>>>>>> origin/dev_logfixes

// ------------------ Suite 2: Mint Child Tokens ------------------

/// Parameters for minting child tokens
struct MintChildCase_s
{
<<<<<<< HEAD
    std::string tokenValue;   // TokenValueInGNUS
    std::string mintChild;    // Amount to mint in child tokens (as string)
    uint64_t    expectedMain; // Expected delta of main balance
=======
    std::string   tokenValue;   // TokenValueInGNUS
    sgns::TokenID TokenID;      // TokenID
    std::string   mintChild;    // Amount to mint in child tokens (as string)
    uint64_t      expectedMain; // Expected delta of main balance
>>>>>>> origin/dev_logfixes
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

TEST_P( GeniusNodeMintChildTest, MintChildBalance )
{
    auto p    = GetParam();
<<<<<<< HEAD
    auto node = CreateNode( p.tokenValue );

    uint64_t    initialMain     = node->GetBalance();
    std::string initialChildStr = node->FormatChildTokens( initialMain );
    auto        parsedInitial   = node->ParseChildTokens( initialChildStr );
    ASSERT_TRUE( parsedInitial.has_value() );

    auto parsedMint = node->ParseChildTokens( p.mintChild );
=======
    auto node = CreateNode( "0xfade", p.tokenValue, p.TokenID );

    uint64_t initialMain = node->GetBalance();
    auto     initFmtRes  = node->FormatTokens( initialMain, p.TokenID );
    ASSERT_TRUE( initFmtRes.has_value() );
    std::string initialChildStr = initFmtRes.value();
    auto        parsedInitial   = node->ParseTokens( initialChildStr, p.TokenID );
    ASSERT_TRUE( parsedInitial.has_value() );

    auto parsedMint = node->ParseTokens( p.mintChild, p.TokenID );
>>>>>>> origin/dev_logfixes
    ASSERT_TRUE( parsedMint.has_value() );

    auto res = node->MintTokens( parsedMint.value(),
                                 "",
                                 "",
<<<<<<< HEAD
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

INSTANTIATE_TEST_SUITE_P( MintChildVariations,
                          GeniusNodeMintChildTest,
                          ::testing::Values( MintChildCase_s{ "1.0", "1.0", 1000000 },
                                             MintChildCase_s{ "0.5", "1.0", 500000 },
                                             MintChildCase_s{ "2.0", "1.0", 2000000 } ) );

/// Suite 3: Transfer Main and Child Tokens with Base Node

/// Parameters for transfer-main tests
typedef struct
{
    std::string tokenValue;    ///< TokenValueInGNUS of child node
    uint64_t    transferValue; ///< Amount to transfer from main to child
    std::string deltaMain;     ///< Expected delta on main tokens
    std::string deltaChild;    ///< Expected delta on child tokens
} TransferMainCase_s;

inline std::ostream &operator<<( std::ostream &os, TransferMainCase_s const &c )
{
    return os << "TransferMainCase_s{tokenValue='" << c.tokenValue << ", transferValue=" << c.transferValue
              << ", deltaMain='" << c.deltaMain << "', deltaChild='" << c.deltaChild << "'}";
}

class GeniusNodeTransferMainTest : public ::testing::TestWithParam<TransferMainCase_s>
{
protected:
    static void SetUpTestSuite() {}

    static void TearDownTestSuite() {}

=======
                                 p.TokenID,
                                 std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( res.has_value() );

    auto finalFmtRes = node->FormatTokens( node->GetBalance(), p.TokenID );
    ASSERT_TRUE( finalFmtRes.has_value() );
    std::string finalChildStr = finalFmtRes.value();
    auto        parsedFinal   = node->ParseTokens( finalChildStr, p.TokenID );
    ASSERT_TRUE( parsedFinal.has_value() );

    auto parsedExpectedDelta = node->ParseTokens( p.mintChild, p.TokenID );
    ASSERT_TRUE( parsedExpectedDelta.has_value() );

    EXPECT_EQ( node->GetBalance() - initialMain, p.expectedMain );
    EXPECT_EQ( node->GetBalance( p.TokenID ) - initialMain, p.expectedMain );

    auto actualChildDelta = parsedFinal.value() - parsedInitial.value();
    EXPECT_EQ( actualChildDelta, parsedExpectedDelta.value() );
}

INSTANTIATE_TEST_SUITE_P(
    MintChildVariations,
    GeniusNodeMintChildTest,
    ::testing::Values( MintChildCase_s{ "1.0", sgns::TokenID::FromBytes( { 0x01 } ), "1.0", 1000000 },
                       MintChildCase_s{ "0.5", sgns::TokenID::FromBytes( { 0x05 } ), "1.0", 500000 },
                       MintChildCase_s{ "2.0", sgns::TokenID::FromBytes( { 0x02 } ), "1.0", 2000000 },
                       MintChildCase_s{ "1.0", sgns::TokenID::FromBytes( { 0x01 } ), "0.0001001", 100 },
                       MintChildCase_s{ "1.0", sgns::TokenID::FromBytes( { 0x01 } ), "0.0001009", 100 },
                       MintChildCase_s{ "0.5", sgns::TokenID::FromBytes( { 0x50 } ), "0.3333333", 166666 },
                       //   MintChildCase_s{ "2.5", "token2_5_frac", "1.2345678", 3086419 },
                       MintChildCase_s{ "0.1", sgns::TokenID::FromBytes( { 0x10 } ), "0.9999999", 99999 } ) );

// Suite 3: Mint multiple token IDs on same node
TEST( GeniusNodeMultiTokenMintTest, MintMultipleTokenIds )
{
    auto node = CreateNode( "0xfafe", "1.0", sgns::TokenID::FromBytes( { 0x0a } ) );

    struct TokenMint
    {
        sgns::TokenID tokenId;
        uint64_t      amount;
    };

    std::vector<TokenMint> mints = { { sgns::TokenID::FromBytes( { 0x0a } ), 1000 },
                                     { sgns::TokenID::FromBytes( { 0x0a } ), 2000 },
                                     { sgns::TokenID::FromBytes( { 0x0b } ), 500 },
                                     { sgns::TokenID::FromBytes( { 0x0b } ), 1500 },
                                     { sgns::TokenID::FromBytes( { 0x0b } ), 2500 },
                                     { sgns::TokenID::FromBytes( { 0x0c } ), 3000 } };

    std::vector<sgns::TokenID> tokenIds;
    for ( const auto &tm : mints )
    {
        tokenIds.push_back( tm.tokenId );
    }

    std::map<TokenID, uint64_t> initialBalances;
    for ( const auto &id : tokenIds )
    {
        initialBalances[id] = node->GetBalance( id );
    }

    uint64_t initialMainBalance = node->GetBalance();

    std::map<sgns::TokenID, uint64_t> expectedTotals;
    uint64_t                          totalMinted = 0;

    for ( const auto &tm : mints )
    {
        auto res = node->MintTokens( tm.amount,
                                     "",
                                     "",
                                     tm.tokenId,
                                     std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
        ASSERT_TRUE( res.has_value() ); // << "MintTokens failed for token=" << tm.tokenId << " amount=" << tm.amount;

        expectedTotals[tm.tokenId] += tm.amount;
        totalMinted                += tm.amount;
    }

    for ( const auto &entry : expectedTotals )
    {
        const auto &id       = entry.first;
        uint64_t    expected = initialBalances[id] + entry.second;
        uint64_t    balance  = node->GetBalance( id );
        EXPECT_EQ( balance, expected ); // << "Balance mismatch for " << id;
    }

    uint64_t mainBalance = node->GetBalance();
    EXPECT_EQ( mainBalance, initialMainBalance + totalMinted )
        << "Main balance did not reflect total minted (" << totalMinted << ")";
}

// ------------------ Suite 4: Processing Nodes test with child tokens ------------------

class ProcessingNodesModuleTest : public ::testing::Test
{
protected:
>>>>>>> origin/dev_logfixes
    void SetUp() override {}

    void TearDown() override {}
};

<<<<<<< HEAD
TEST_P( GeniusNodeTransferMainTest, TransferMainBalance )
{
    const auto c = GetParam();

    auto nodeMain  = CreateNode( "1.0" );
    auto nodeChild = CreateNode( c.tokenValue );

    nodeMain->GetPubSub()->AddPeers( { nodeChild->GetPubSub()->GetLocalAddress() } );
    nodeChild->GetPubSub()->AddPeers( { nodeMain->GetPubSub()->GetLocalAddress() } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    ASSERT_TRUE(
        nodeMain->MintTokens( c.transferValue, "", "", "", std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) )
            .has_value() );

    // Record initial balances
    auto initMain     = nodeMain->GetBalance();
    auto initMainStr  = nodeMain->FormatChildTokens( initMain );
    auto initChild    = nodeChild->GetBalance();
    auto initChildStr = nodeChild->FormatChildTokens( initMain );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    auto [txId, _] = nodeMain
                         ->TransferFunds( c.transferValue,
                                          nodeChild->GetAddress(),
                                          std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) )
                         .value();
    ASSERT_TRUE(
        nodeMain->WaitForTransactionIncoming( txId, std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ) );

    // Verify final balances and child deltas
    auto finalMain  = nodeMain->GetBalance();
    auto finalChild = nodeChild->GetBalance();

    // Child balance decreased by transfer; base balance increased by transfer
    EXPECT_EQ( finalMain - initMain, c.transferValue );
    EXPECT_EQ( finalChild - initChild, c.transferValue );

    // Child and base child-token deltas
    const auto finalMainString      = nodeMain->FormatChildTokens( finalMain );
    const auto expectedFinalMainStr = addDecimalStrings( initMainStr, c.deltaMain );
    EXPECT_EQ( finalMainString, expectedFinalMainStr );

    auto       finalChildString      = nodeChild->FormatChildTokens( finalChild );
    const auto expectedFinalChildStr = addDecimalStrings( initChildStr, c.deltaChild );
    EXPECT_EQ( finalChildString, expectedFinalChildStr );
}

INSTANTIATE_TEST_SUITE_P( TransferMainVariations,
                          GeniusNodeTransferMainTest,
                          ::testing::Values( TransferMainCase_s{ "1.0", 400000, "-0.4", "0.6" },
                                             TransferMainCase_s{ "0.5", 500000, "-0.5", "1.0" } ) );

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

TEST_P( GeniusNodeTransferChildTest, TransferChildBalance )
{
    auto c     = GetParam();
    auto nodeA = CreateNode( c.tokenValue );
    auto nodeB = CreateNode( c.tokenValue );

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

INSTANTIATE_TEST_SUITE_P( TransferChildVariations,
                          GeniusNodeTransferChildTest,
                          ::testing::Values( TransferChildCase_s{ "1.0", "0.2", "-0.2", "0.2" },
                                             TransferChildCase_s{ "0.5", "0.25", "-0.25", "0.25" } ) );
=======
TEST_F( ProcessingNodesModuleTest, SinglePostProcessing )
{
    auto node_main  = CreateNode( "0xacfe", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), false );
    auto node_proc1 = CreateNode( "0xadfe", "0.65", sgns::TokenID::FromBytes( { 0x01 } ), true );
    auto node_proc2 = CreateNode( "0xaffe", "0.65", sgns::TokenID::FromBytes( { 0x02 } ), true );

    auto mintResMain = node_main->MintTokens( 1000,
                                              "",
                                              "",
                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                              std::chrono::milliseconds( OUTGOING_TIMEOUT_MILLISECONDS ) );
    ASSERT_TRUE( mintResMain.has_value() ) << "Mint failed on node_main";

    node_main->GetPubSub()->AddPeers(
        { node_proc1->GetPubSub()->GetLocalAddress(), node_proc2->GetPubSub()->GetLocalAddress() } );
    node_proc1->GetPubSub()->AddPeers( { node_main->GetPubSub()->GetLocalAddress() } );
    node_proc2->GetPubSub()->AddPeers( { node_main->GetPubSub()->GetLocalAddress() } );

    std::string bin_path = boost::dll::program_location().parent_path().string() + "/";
#ifdef _WIN32
    bin_path += "../";
#endif
    std::string json_data = R"(
    {
      "data": {
        "type": "file",
        "URL": "file://[basepath]../../../../test/src/processing_nodes/"
      },
      "model": {
        "name": "mnnimage",
        "file": "model.mnn"
      },
      "input": [
        {
          "image": "data/ballet.data",
          "block_len": 4860000,
          "block_line_stride": 5400,
          "block_stride": 0,
          "chunk_line_stride": 1080,
          "chunk_offset": 0,
          "chunk_stride": 4320,
          "chunk_subchunk_height": 5,
          "chunk_subchunk_width": 5,
          "chunk_count": 25,
          "channels": 4
        },
        {
          "image": "data/frisbee3.data",
          "block_len": 786432,
          "block_line_stride": 1536,
          "block_stride": 0,
          "chunk_line_stride": 384,
          "chunk_offset": 0,
          "chunk_stride": 1152,
          "chunk_subchunk_height": 4,
          "chunk_subchunk_width": 4,
          "chunk_count": 16,
          "channels": 3
        }
      ]
    }
    )";
    std::replace( bin_path.begin(), bin_path.end(), '\\', '/' );
    boost::replace_all( json_data, "[basepath]", bin_path );

    auto cost          = node_main->GetProcessCost( json_data );
    auto bal_main_init = node_main->GetBalance();
    auto bal_p1_init   = node_proc1->GetBalance();
    auto bal_p2_init   = node_proc2->GetBalance();
    auto tok_main_init = node_main->GetBalance( sgns::TokenID::FromBytes( { 0x00 } ) );
    auto tok_p1_init   = node_proc1->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) );
    auto tok_p2_init   = node_proc2->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) );

    auto postjob = node_main->ProcessImage( json_data );
    ASSERT_TRUE( postjob ) << "ProcessImage failed: " << postjob.error().message();
    ASSERT_TRUE( node_main->WaitForEscrowRelease( postjob.value(), std::chrono::milliseconds( 300000 ) ) );

    assertWaitForCondition( [&]() { return node_main->GetBalance() == bal_main_init - cost; },
                            std::chrono::milliseconds( 20000 ),
                            "Main general balance not updated in time" );

    ASSERT_EQ( bal_main_init - cost, node_main->GetBalance() );
    ASSERT_EQ( tok_main_init - cost, node_main->GetBalance( sgns::TokenID::FromBytes( { 0x00 } ) ) );

    uint64_t expected_peer_gain = ( ( cost * 65 ) / 100 ) / 2;

    assertWaitForCondition(
        [&]()
        {
            return ( node_proc1->GetBalance() + node_proc2->GetBalance() ) ==
                   ( bal_p1_init + bal_p2_init + 2 * expected_peer_gain );
        },
        std::chrono::milliseconds( 20000 ),
        "Other nodes balance not updated in time" );
    ASSERT_EQ( bal_p1_init + bal_p2_init + 2 * expected_peer_gain,
               node_proc1->GetBalance() + node_proc2->GetBalance() );
    ASSERT_EQ( bal_p1_init + bal_p2_init + 2 * expected_peer_gain,
               node_proc1->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ) +
                   node_proc2->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ) );

    uint64_t dev_payment = cost - 2 * expected_peer_gain;
    ASSERT_EQ( bal_main_init + bal_p1_init + bal_p2_init,
               node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + dev_payment );
}
>>>>>>> origin/dev_logfixes
