#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <ostream>
#include <random>
#include <string_view>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/dll.hpp>

#include "account/GeniusNode.hpp"
#include "account/GeniusAccount.hpp"
#include "account/TokenID.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/genius_node_test_access.hpp"
#include "blockchain/Blockchain.hpp"
#include "testutil/wait_condition.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

using namespace sgns;
using namespace sgns::test;

namespace sgns
{
    class MultiAccountTestAccess
    {
    public:
        static std::shared_ptr<Blockchain> GetBlockchain( const std::shared_ptr<GeniusNode> &node )
        {
            return node ? node->blockchain_ : nullptr;
        }

        static std::shared_ptr<ConsensusManager> GetConsensusManager( const std::shared_ptr<Blockchain> &blockchain )
        {
            return blockchain ? blockchain->consensus_manager_ : nullptr;
        }

        static void SetGNUSPrice( const std::shared_ptr<GeniusNode> &node, double price )
        {
            node->m_tokenPriceCache["genius-ai"] = { price, std::chrono::system_clock::now() };
        }
    };
} // namespace sgns

namespace
{
    /**
     * @brief Helper to create a GeniusNode with its own directory and cleanup.
     * @param tokenValue TokenValueInGNUS to initialize GeniusGeniusNodeConfig.
     * @param tokenId TokenID to initialize GeniusGeniusNodeConfig.
     * @return shared_ptr to the initialized GeniusNode.
     */
    std::shared_ptr<sgns::GeniusNode> CreateNode( const std::string &self_address,
                                                  const std::string &tokenValue,
                                                  sgns::TokenID      tokenId,
                                                  bool               isFullNode      = false,
                                                  bool               setAsAuthorized = false,
                                                  bool               isProcessor     = false )
    {
        // Inject in-memory secure storage to avoid OS keychain prompts during tests
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );
        static std::atomic<int> nodeCounter{ 0 };
        int                     id = nodeCounter.fetch_add( 1 );

        std::string binaryPath = boost::dll::program_location().parent_path().string();
        auto        outPath    = binaryPath + "/child_tokens_node_" + std::to_string( id ) + "/";

        GeniusNodeConfig devConfig = { self_address, 0.35, tokenValue, tokenId, outPath };

        removeAllWithRetry( devConfig.BaseWritePath );
        std::filesystem::create_directories( devConfig.BaseWritePath );

        std::string key;
        key.reserve( 64 );

        std::mt19937 rng( static_cast<uint32_t>( std::time( nullptr ) ) + static_cast<uint32_t>( id ) );
        std::uniform_int_distribution<> dist( 0, 15 );
        std::generate_n( std::back_inserter( key ),
                         64,
                         [&]()
                         {
                             static constexpr std::string_view HEX_CHARS = "0123456789abcdef";
                             return HEX_CHARS[dist( rng )];
                         } );

        sgns::GeniusNode::WriteNetworkConfig( devConfig.BaseWritePath, /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( devConfig.BaseWritePath, isFullNode ? "Full" : "Light", /*is_processor=*/isProcessor, /*rpc_catchup=*/false );
        auto node = sgns::GeniusNode::New( devConfig, sgns::FromPrivateKey{ key } );
        sgns::GeniusNodeTestAccess::CacheGnusPrice( node, 1.0 );

        if ( setAsAuthorized )
        {
            sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
        }

        return node;
    }

    void ConfigureTestConsensus( const std::shared_ptr<GeniusNode> &node, const std::string &description )
    {
        test::assertWaitForCondition(
            [&]()
            {
                auto blockchain = MultiAccountTestAccess::GetBlockchain( node );
                return node->GetState() == GeniusNode::NodeState::READY && blockchain &&
                       MultiAccountTestAccess::GetConsensusManager( blockchain );
            },
            std::chrono::milliseconds( 50000 ),
            description + " not synced" );

        MultiAccountTestAccess::GetConsensusManager( MultiAccountTestAccess::GetBlockchain( node ) )
            ->ConfigureCertificateDelay( std::chrono::seconds( 1 ) );
    }

} // namespace

// Suite: Enhanced Three-Node Transfers with Grouped Minting and Change
TEST( TransferTokenValue, ThreeNodeTransferTest )
{
    // Create nodes
    auto node50 = CreateNode( "0xcafe", "1.0", sgns::TokenID::FromBytes( { 0x50 } ), true, true );
    auto node51 = CreateNode( "0xcade", "0.5", sgns::TokenID::FromBytes( { 0x51 } ) );
    auto node52 = CreateNode( "0xdafe", "2.0", sgns::TokenID::FromBytes( { 0x52 } ) );

    // Configure peer connections
    node51->AddPeers(
        { node50->GetPubSub()->GetInterfaceAddress(), node52->GetPubSub()->GetInterfaceAddress() } );
    node52->AddPeers( { node50->GetPubSub()->GetInterfaceAddress() } );
    ConfigureTestConsensus( node50, "node50" );
    ConfigureTestConsensus( node51, "node51" );
    ConfigureTestConsensus( node52, "node52" );

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
                                         sgns::test::NextMintSourceHash(),
                                         "test",
                                         sgns::TokenID::FromBytes( { 0x51 } ),
                                         "",
                                         std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mintRes51.has_value() ) << "Grouped mint failed on token51";
    std::cout << "Minted total " << ( totalMint51 + 1 ) << " of token51 on node51\n";

    auto mintRes52 = node52->MintTokens( totalMint52 + 1,
                                         sgns::test::NextMintSourceHash(),
                                         "test",
                                         sgns::TokenID::FromBytes( { 0x52 } ),
                                         "",
                                         std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mintRes52.has_value() ) << "Grouped mint failed on token52";
    std::cout << "Minted total " << ( totalMint52 + 1 ) << " of token52 on node52\n";

    // Execute each transfer in sequence
    for ( const auto &t : transfers )
    {
        auto transferRes = t.src->TransferFunds( t.amount,
                                                 node50->GetAddress(),
                                                 t.tokenId,
                                                 std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
        ASSERT_TRUE( transferRes.has_value() ); // << "Transfer failed for " << t.tokenId;
        auto [txHash, duration] = transferRes.value();
        std::cout << "Transferred " << t.amount << " of " << t.tokenId << " in " << duration << " ms\n";

        EXPECT_EQ(
            node50->WaitForTransactionIncoming( txHash, std::chrono::milliseconds( INCOMING_TIMEOUT_MILLISECONDS ) ),
            TransactionManager::TransactionStatus::CONFIRMED )
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

// Suite: one live node check that child-token conversion is wired into minting.
TEST( GeniusNodeChildTokenMintTest, MintMainAndChildBalance )
{
    auto tokenId  = sgns::TokenID::FromBytes( { 0x05 } );
    auto nodefull = CreateNode( "0xaffb", "0.5", tokenId, true, true );
    auto node = CreateNode( "0xfadb", "0.5", tokenId );
    nodefull->AddPeers( { node->GetPubSub()->GetInterfaceAddress() } );

    ConfigureTestConsensus( nodefull, "nodefull" );
    ConfigureTestConsensus( node, "node" );

    auto initialMain  = node->GetBalance();
    auto initialToken = node->GetBalance( tokenId );

    auto parsedChildMint = node->ParseTokens( "1.0", tokenId );
    ASSERT_TRUE( parsedChildMint.has_value() );
    EXPECT_EQ( parsedChildMint.value(), 500000 );

    auto childMintRes = node->MintTokens( parsedChildMint.value(),
                                          sgns::test::NextMintSourceHash(),
                                          "test",
                                          tokenId,
                                          "",
                                          std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( childMintRes.has_value() );

    auto finalFmtRes = node->FormatTokens( node->GetBalance( tokenId ) - initialToken, tokenId );
    ASSERT_TRUE( finalFmtRes.has_value() );
    EXPECT_EQ( finalFmtRes.value(), "1.000000" );
    EXPECT_EQ( node->GetBalance() - initialMain, parsedChildMint.value() );
    EXPECT_EQ( node->GetBalance( tokenId ) - initialToken, parsedChildMint.value() );
}

// Suite 3: Mint multiple token IDs on same node
TEST( GeniusNodeMultiTokenMintTest, MintMultipleTokenIds )
{
    auto nodefull = CreateNode( "0xaffd", "1.0", sgns::TokenID::FromBytes( { 0x0a } ), true, true );
    auto node = CreateNode( "0xfafe", "1.0", sgns::TokenID::FromBytes( { 0x0a } ) );
    nodefull->AddPeers( { node->GetPubSub()->GetInterfaceAddress() } );

    ConfigureTestConsensus( nodefull, "nodefull" );
    ConfigureTestConsensus( node, "node" );

    struct TokenMint
    {
        sgns::TokenID tokenId;
        uint64_t      amount;
    };

    std::vector<TokenMint> mints = { { sgns::TokenID::FromBytes( { 0x0a } ), 1000 },
                                     { sgns::TokenID::FromBytes( { 0x0a } ), 2000 },
                                     { sgns::TokenID::FromBytes( { 0x0b } ), 500 },
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
                                     sgns::test::NextMintSourceHash(),
                                     "test",
                                     tm.tokenId,
                                     "",
                                     std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
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
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

/// Scale of SubTaskResult::developer_cut, mirroring SGProcessing.proto.
static constexpr uint64_t DEVELOPER_CUT_SCALE = 1000000;
/// Developer fraction every node created by CreateNode is configured with (0.35).
static constexpr uint64_t DEVELOPER_CUT = 350000;

TEST_F( ProcessingNodesModuleTest, SinglePostProcessing )
{
    auto node_proc1 = CreateNode( "0xadfe", "0.65", sgns::TokenID::FromBytes( { 0x01 } ), true, true, true );
    auto node_main  = CreateNode( "0xacfe", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), false );
    auto node_proc2 = CreateNode( "0xaffa", "0.65", sgns::TokenID::FromBytes( { 0x02 } ), false, false, true );

    node_main->AddPeers(
        { node_proc1->GetPubSub()->GetInterfaceAddress(), node_proc2->GetPubSub()->GetInterfaceAddress() } );
    node_proc1->AddPeers( { node_proc2->GetPubSub()->GetInterfaceAddress() } );

    ConfigureTestConsensus( node_proc1, "node_proc1" );
    ConfigureTestConsensus( node_main, "node_main" );
    ConfigureTestConsensus( node_proc2, "node_proc2" );

    auto mintResMain = node_main->MintTokens( 1000,
                                              sgns::test::NextMintSourceHash(),
                                              "test",
                                              sgns::TokenID::FromBytes( { 0x00 } ),
                                              "",
                                              std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mintResMain.has_value() ) << "Mint failed on node_main";

    std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
    std::string json_data = R"(
{
  "name": "posenet-inference",
  "version": "1.0.0",
  "gnus_spec_version": 1.0,
  "author": "AI Assistant",
  "description": "PoseNet inference on multiple image inputs using MNN model",
  "tags": ["pose-estimation", "computer-vision", "inference"],

  "inputs": [
    {
      "name": "ballet_image",
	  "source_uri_param": "file://[basepath]./child_tokens/data/ballet.data",
      "type": "texture2D",
      "description": "Ballet pose image input",
      "dimensions": {
        "width": 1350,
        "height": 900,
		"block_len": 4860000 ,
		"block_line_stride": 5400,
		"block_stride": 0,
		"chunk_line_stride": 1080,
		"chunk_offset": 0,
		"chunk_stride": 4320,
		"chunk_subchunk_height": 5,
		"chunk_subchunk_width": 5,
		"chunk_count": 25
      },
      "format": "RGBA8"
    },
    {
      "name": "frisbee_image",
	  "source_uri_param": "file://[basepath]./child_tokens/data/frisbee3.data",
      "type": "texture2D",
      "description": "Frisbee pose image input",
      "dimensions": {
        "width": 512,
        "height": 512,
		"block_len": 786432 ,
		"block_line_stride": 1536,
		"block_stride": 0,
		"chunk_line_stride": 384,
		"chunk_offset": 0,
		"chunk_stride": 1152,
		"chunk_subchunk_height": 4,
		"chunk_subchunk_width": 4,
		"chunk_count": 16
      },
      "format": "RGB8"
    }
  ],

  "outputs": [
    {
      "name": "ballet_keypoints",
	  "source_uri_param": "dummy",
      "type": "tensor",
      "description": "Detected keypoints for ballet image",
      "dimensions": {
        "width": 17,
        "height": 3
      },
      "format": "FLOAT32"
    },
    {
      "name": "frisbee_keypoints",
	  "source_uri_param": "dummy",
      "type": "tensor",
      "description": "Detected keypoints for frisbee image",
      "dimensions": {
        "width": 17,
        "height": 3
      },
      "format": "FLOAT32"
    }
  ],

  "passes": [
    {
      "name": "ballet_pose_inference",
      "type": "inference",
      "description": "Run PoseNet inference on ballet image",
      "model": {
        "source_uri_param": "file://[basepath]./child_tokens/model.mnn",
        "format": "MNN",
        "batch_size": 1,
        "input_nodes": [
          {
            "name": "input",
            "type": "texture2D",
            "source": "input:ballet_image",
            "shape": [1, 256, 256, 4]
          }
        ],
        "output_nodes": [
          {
            "name": "output",
            "type": "tensor",
            "target": "output:ballet_keypoints",
            "shape": [1, 17, 3]
          }
        ]
      }
    },
    {
      "name": "frisbee_pose_inference",
      "type": "inference",
      "description": "Run PoseNet inference on frisbee image",
      "model": {
        "source_uri_param": "file://[basepath]./child_tokens/model.mnn",
        "format": "MNN",
        "batch_size": 1,
        "input_nodes": [
          {
            "name": "input",
            "type": "texture2D",
            "source": "input:frisbee_image",
            "shape": [1, 256, 256, 4]
          }
        ],
        "output_nodes": [
          {
            "name": "output",
            "type": "tensor",
            "target": "output:frisbee_keypoints",
            "shape": [1, 17, 3]
          }
        ]
      }
    }
  ]
}
       )";
    std::replace( bin_path.begin(), bin_path.end(), '\\', '/' );
    boost::replace_all( json_data, "[basepath]", bin_path );
    MultiAccountTestAccess::SetGNUSPrice( node_main, 1.0 );
    auto procmgr       = sgns::sgprocessing::ProcessingManager::Create( json_data );
    auto cost          = node_main->GetProcessCost( *procmgr.value() );
    auto bal_main_init = node_main->GetBalance();
    auto bal_p1_init   = node_proc1->GetBalance();
    auto bal_p2_init   = node_proc2->GetBalance();
    auto tok_main_init = node_main->GetBalance( sgns::TokenID::FromBytes( { 0x00 } ) );
    auto tok_p1_init   = node_proc1->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) );
    auto tok_p2_init   = node_proc2->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) );

    std::cout << "Process cost: " << cost << "\n";
    auto postjob = node_main->ProcessImage( json_data );
    ASSERT_TRUE( postjob ) << "ProcessImage failed: " << postjob.error().message();
    EXPECT_EQ( node_main->WaitForEscrowRelease( postjob.value(), std::chrono::milliseconds( 300000 ) ),
               TransactionManager::TransactionStatus::CONFIRMED );

    assertWaitForCondition( [&]() { return node_main->GetBalance() == bal_main_init - cost; },
                            std::chrono::milliseconds( 20000 ),
                            "Main general balance not updated in time" );

    ASSERT_EQ( bal_main_init - cost, node_main->GetBalance() );
    ASSERT_EQ( tok_main_init - cost, node_main->GetBalance( sgns::TokenID::FromBytes( { 0x00 } ) ) );

    uint64_t burn_amount = ( cost * sgns::GeniusNode::GetBurnBasisPoints() ) / sgns::GeniusNode::GetBasisPointsTotal();
    uint64_t available   = cost - burn_amount;

    // node_proc1 and node_proc2 run apps from *different* developers (0xadfe and 0xaffa), each
    // taking the same 0.35 cut. This is precisely what issue #148 got wrong: the release used to
    // name a single developer on the escrow hold, so one developer collected everything. It now
    // credits each developer out of its own peer's share, in that peer's child token.
    //
    // Each peer is entitled to (1 - 0.35) of its own half of the available amount. Payouts are
    // apportioned by largest remainder, so the sub-minion dust adds at most one minion to any one
    // credit -- there are four credits here, two peers and two developers.
    const uint64_t peer_entitlement = ( available * ( DEVELOPER_CUT_SCALE - DEVELOPER_CUT ) ) /
                                      ( 2 * DEVELOPER_CUT_SCALE );

    assertWaitForCondition(
        [&]()
        {
            auto gain = ( node_proc1->GetBalance() + node_proc2->GetBalance() ) - ( bal_p1_init + bal_p2_init );
            return ( gain >= 2 * peer_entitlement ) && ( gain <= 2 * peer_entitlement + 2 );
        },
        std::chrono::milliseconds( 40000 ),
        "Other nodes balance not updated in time" );

    const auto p1_gain = node_proc1->GetBalance() - bal_p1_init;
    const auto p2_gain = node_proc2->GetBalance() - bal_p2_init;
    ASSERT_GE( p1_gain, peer_entitlement );
    ASSERT_LE( p1_gain, peer_entitlement + 1 );
    ASSERT_GE( p2_gain, peer_entitlement );
    ASSERT_LE( p2_gain, peer_entitlement + 1 );

    // Each peer is paid in its own child token, and nothing leaks into the other's.
    ASSERT_EQ( tok_p1_init + p1_gain, node_proc1->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ) );
    ASSERT_EQ( tok_p2_init + p2_gain, node_proc2->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ) );

    // Whatever the peers did not take went to the two developers: the outputs sum to the escrow
    // exactly, so this closes the books on the whole release.
    const uint64_t dev_payment = available - p1_gain - p2_gain;
    ASSERT_EQ( bal_main_init + bal_p1_init + bal_p2_init,
               node_main->GetBalance() + node_proc1->GetBalance() + node_proc2->GetBalance() + dev_payment +
                   burn_amount );
}
