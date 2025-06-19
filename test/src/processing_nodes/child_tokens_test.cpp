#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <thread>
#include <cstring>
#include <ostream>
#include <atomic>
#include "account/GeniusNode.hpp"
#include "account/TokenID.hpp"
#include "testutil/wait_condition.hpp"
#include <boost/multiprecision/cpp_dec_float.hpp>

using namespace sgns::test;
using boost::multiprecision::cpp_dec_float_50;

namespace
{
    /**
     * @brief Helper to create a GeniusNode with its own directory and cleanup.
     * @param tokenValue TokenValueInGNUS to initialize DevConfig.
     * @param tokenId TokenValueInGNUS to initialize TokenID.
     * @return unique_ptr to the initialized GeniusNode.
     */
    std::unique_ptr<sgns::GeniusNode> CreateNode( const std::string &tokenValue,
                                                  sgns::TokenID      tokenId,
                                                  bool               isProcessor = false )
    {
        static std::atomic<int> node_counter{ 0 };
        int                     id = node_counter.fetch_add( 1 );

        std::string binaryPath = boost::dll::program_location().parent_path().string();
        const char *filePath   = ::testing::UnitTest::GetInstance()->current_test_info()->file();
        std::string fileStem   = std::filesystem::path( filePath ).stem().string();
        auto        outPath    = binaryPath + /*"/" + fileStem +*/ "/node_" + std::to_string( id ) + "/";

        DevConfig_st devConfig = { "0xcafe", "0.65", tokenValue, tokenId, "" };
        std::strncpy( devConfig.BaseWritePath, outPath.c_str(), sizeof( devConfig.BaseWritePath ) - 1 );
        devConfig.BaseWritePath[sizeof( devConfig.BaseWritePath ) - 1] = '\0';

        std::string key;
        key.reserve( 64 );

        std::mt19937 rng( static_cast<uint32_t>( std::time( nullptr ) ) + static_cast<uint32_t>( id ) );
        std::uniform_int_distribution<> dist( 0, 15 );

        std::generate_n( std::back_inserter( key ),
                         64,
                         [&]()
                         {
                             static constexpr std::string_view hex_chars = "0123456789abcdef";
                             return hex_chars[dist( rng )];
                         } );

        auto node = std::make_unique<sgns::GeniusNode>( devConfig, key.c_str(), false, isProcessor );
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

// Suite: Enhanced Three-Node Transfers with Grouped Minting and Change
TEST( TransferTokenValue, ThreeNodeTransferTest )
{
    // Create nodes
    auto node50 = CreateNode( "1.0", sgns::TokenID::FromBytes( { 0x50 } ) );
    auto node51 = CreateNode( "0.5", sgns::TokenID::FromBytes( { 0x51 } ) );
    auto node52 = CreateNode( "2.0", sgns::TokenID::FromBytes( { 0x52 } ) );

    // Configure peer connections
    node51->GetPubSub()->AddPeers( { node50->GetPubSub()->GetLocalAddress() } );
    node52->GetPubSub()->AddPeers( { node50->GetPubSub()->GetLocalAddress() } );
    node50->GetPubSub()->AddPeers( { node51->GetPubSub()->GetLocalAddress(), node52->GetPubSub()->GetLocalAddress() } );

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
        ASSERT_TRUE( transferRes.has_value() );// << "Transfer failed for " << t.tokenId;
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
}

// ------------------ Suite 1: Mint Main Tokens ------------------

/// Parameters for minting main tokens
struct MintMainCase_s
{
    std::string   tokenValue;    // TokenValueInGNUS
    sgns::TokenID TokenID;       // TokenID
    uint64_t      mintMain;      // Amount to mint in main tokens
    std::string   expectedChild; // Expected delta of child balance (as string)
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
    auto node = CreateNode( p.tokenValue, p.TokenID );

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

// ------------------ Suite 2: Mint Child Tokens ------------------

/// Parameters for minting child tokens
struct MintChildCase_s
{
    std::string   tokenValue;   // TokenValueInGNUS
    sgns::TokenID TokenID;      // TokenID
    std::string   mintChild;    // Amount to mint in child tokens (as string)
    uint64_t      expectedMain; // Expected delta of main balance
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
    auto node = CreateNode( p.tokenValue, p.TokenID );

    uint64_t initialMain = node->GetBalance();
    auto     initFmtRes  = node->FormatTokens( initialMain, p.TokenID );
    ASSERT_TRUE( initFmtRes.has_value() );
    std::string initialChildStr = initFmtRes.value();
    auto        parsedInitial   = node->ParseTokens( initialChildStr, p.TokenID );
    ASSERT_TRUE( parsedInitial.has_value() );

    auto parsedMint = node->ParseTokens( p.mintChild, p.TokenID );
    ASSERT_TRUE( parsedMint.has_value() );

    auto res = node->MintTokens( parsedMint.value(),
                                 "",
                                 "",
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
    auto node = CreateNode( "1.0", sgns::TokenID::FromBytes( { 0x0a } ) );

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
        ASSERT_TRUE( res.has_value() );// << "MintTokens failed for token=" << tm.tokenId << " amount=" << tm.amount;

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
    void SetUp() override {}

    void TearDown() override {}
};

TEST_F( ProcessingNodesModuleTest, SinglePostProcessing )
{
    auto node_main  = CreateNode( "1.0", sgns::TokenID::FromBytes( { 0x00 } ), false );
    auto node_proc1 = CreateNode( "0.65", sgns::TokenID::FromBytes( { 0x01 } ), true );
    auto node_proc2 = CreateNode( "0.65", sgns::TokenID::FromBytes( { 0x02 } ), true );

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

    ASSERT_EQ( bal_p1_init + bal_p2_init + 2 * expected_peer_gain,
               node_proc1->GetBalance() + node_proc2->GetBalance() );
    ASSERT_EQ( bal_p1_init + bal_p2_init + 2 * expected_peer_gain,
               node_proc1->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ) +
                   node_proc2->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ) );

    uint64_t dev_payment = cost - 2 * expected_peer_gain;
    ASSERT_EQ( bal_main_init + bal_p1_init + bal_p2_init,
               node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + dev_payment );
}
