#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>

#include <boost/dll/runtime_symbol_info.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "account/TokenAmount.hpp"
#include "account/EscrowTransaction.hpp"
#include "transaction/TransactionManager.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/local_trust_setup.hpp"
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/offline_chainlist.hpp"
#include "HttpStubServer.hpp"
#include "testutil/scoped_env.hpp"

using namespace sgns::test;
using namespace sgns;

static sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

namespace sgns
{
    /**
     * @brief Friend accessor for private GeniusNode state needed by account
     *        management tests. Mirrors MultiAccountTestAccess in
     *        child_tokens_test.cpp.
     */
    class AccountManagementTestAccess
    {
    public:
        /// @brief WIRE-02 test support: drop the lazily-constructed price
        ///        manager so the next price read re-reads the tier env vars
        ///        at construction (D-03) — used to point a live node at a
        ///        per-test stub after construction.
        static void ResetPriceManager( const std::shared_ptr<GeniusNode> &node )
        {
            if ( node ) node->ResetPriceManagerForTest();
        }

        /// @brief WIRE-02 observation: the Task this node posted, as stored
        ///        in its processing task queue (claim/escrow checks). The
        ///        node keeps the poster's queue, so GetTask returns the task
        ///        even before any peer picks it up.
        static outcome::result<SGProcessing::Task> GetPostedTask( const std::shared_ptr<GeniusNode> &node,
                                                                 const std::string                  &taskId )
        {
            if ( !node || !node->task_queue_ ) return outcome::failure( boost::system::error_code{} );
            return node->task_queue_->GetTask( taskId );
        }
    };

    /**
     * @brief TransactionManager-side observation for the WIRE-02 escrow check.
     *        TransactionManager.hpp already declares
     *        `friend class MultiAccountTestAccess;`; each test binary
     *        declares its own TU-local copy carrying the accessors it needs
     *        (same idiom as child_tokens_test.cpp).
     */
    class MultiAccountTestAccess
    {
    public:
        /// @brief Look up an escrow hold by transaction hash (private API).
        static std::shared_ptr<GeniusTransaction> GetEscrowTransaction( const std::shared_ptr<TransactionManager> &manager,
                                                                       const std::string                         &txHash )
        {
            return manager ? manager->GetTransactionByHash( txHash ) : nullptr;
        }
    };
} // namespace sgns

namespace
{
    std::shared_ptr<GeniusAccount> WriteTrustedNodeConfig( const boost::filesystem::path &path,
                                                            const char                    *private_key,
                                                            const char                    *node_type,
                                                            bool                           is_processor )
    {
        auto account = GeniusAccount::NewFromPrivateKey( TOKEN_ID, private_key, path );
        if ( !account )
        {
            return nullptr;
        }
        const auto address = account->GetAddress();
        WriteTrustedSgnsConfig( path, node_type, is_processor, false, { address }, address, 1, 1, 144 );
        return account;
    }

    void ConfirmConfiguredTrust( const std::shared_ptr<GeniusNode> &node )
    {
        ASSERT_NO_FATAL_FAILURE( test::MakeNodeReadyWithLocalTrust( node ) );
    }
} // namespace

class AccountManagement : public ::testing::Test
{
public:
    // Each test gets its own directory. The previous test's node can finish
    // its async destruction (RocksDB close on a detached io thread) after this
    // constructor runs; reusing one path made the new node's DB open race the
    // old instance's lock and a directory wiped under still-open files, which
    // surfaced as spurious "lock hold by current process" then MANIFEST/.sst
    // corruption. No state is shared between tests — the old fixture wiped the
    // directory here anyway.
    static inline std::atomic<uint64_t> next_test_index{ 0 };
    boost::filesystem::path             path = boost::dll::program_location().parent_path() /
                   ( "am_full_node_" + std::to_string( next_test_index.fetch_add( 1 ) ) );

    AccountManagement()
    {
        // Hermetic price source (Phase 4, D-12/TEST-04): redirect both price
        // tiers to a loopback stub BEFORE any node is constructed, so
        // SetPayoutAddress's GetProcessCost call resolves against the
        // scripted genius-ai price instead of live CoinGecko. Ordering is
        // strict: stub Start (OS-assigned port) -> env guards -> node New.
        stub_.OnPath( "/api/v3/simple/price",
                      { 200, "application/json", R"({"genius-ai":{"usd":0.19}})" } );
        stub_.Start();
        const auto base = "http://127.0.0.1:" + std::to_string( stub_.Port() );
        envCoinGecko_ = std::make_unique<sgns::testutil::ScopedEnvVar>( "SGNS_COINGECKO_URL", base );
        envFallback_  = std::make_unique<sgns::testutil::ScopedEnvVar>( "SGNS_PRICE_FALLBACK_URL", base );

        test::removeAllWithRetry( path.string() );
        boost::filesystem::create_directories( path );
        sgns::GeniusNode::WriteNetworkConfig( path.generic_string() + '/', /*port_seed=*/0, /*auto_dht=*/false );
        // Inject in-memory secure storage to avoid OS keychain prompts during tests
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        const auto bootstrapper = WriteTrustedNodeConfig(
            path, "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa", "Full", true );
        assert( bootstrapper );
        Blockchain::SetAuthorizedFullNodeAddress( bootstrapper->GetAddress() );

        node_ = sgns::GeniusNode::New(
            { "0xcafe", "0.35", "1.0", TOKEN_ID, path.generic_string() + '/' },
            sgns::FromPrivateKey{ "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" } );
        node_->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );
        assert( node_ != nullptr );
        ConfirmConfiguredTrust( node_ );
        assert( node_->GetState() == GeniusNode::NodeState::READY );
    }

    ~AccountManagement() override
    {
        // Env guards die before the stub (members destroyed in reverse
        // declaration order — guards declared after stub_); reset them
        // explicitly anyway so the restore is visibly first.
        envCoinGecko_.reset();
        envFallback_.reset();
        stub_.Shutdown();
    }

    std::shared_ptr<sgns::GeniusNode> node_;

    sgns::testutil::HttpStubServer                stub_;
    std::unique_ptr<sgns::testutil::ScopedEnvVar> envCoinGecko_;
    std::unique_ptr<sgns::testutil::ScopedEnvVar> envFallback_;

    /// @brief Re-points both price tiers at a second loopback stub. The
    ///        fixture's stub_ stays alive, but both env vars select THIS
    ///        server; a freshly-constructed LocalPriceManager reads the env
    ///        at construction (D-03), so after resetting priceManager_ the
    ///        node's next GetQuotes fetch walks this stub instead. Used by
    ///        tests that must observe more than one network fetch
    ///        (ClaimedPriceMatchesEscrowQuote).
    sgns::testutil::HttpStubServer sequencingStub_;

    /// @brief Gnus-envelope body with an old fetchedAt (400s > 300s
    ///        stale-but-usable band, PriceFreshness.hpp): every quote the
    ///        sequencing stub serves is born non-fresh, so consecutive
    ///        fetches always hit the network tier and each gets the price
    ///        scripted for it (no L1 shortcut, no 60s wait).
    static std::string NonFreshGnusEnvelopeBody( double price )
    {
        std::ostringstream body;
        body << R"({"currency":"usd","source":"coingecko","stale":false,"fetchedAt":)"
             << ( std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now()
                                                                    .time_since_epoch() )
                      .count()
                  - 400 )
             << R"(,"prices":{"genius-ai":)" << price << "}}";
        return body.str();
    }

    /// @brief Resets the node's lazily-constructed price manager so the next
    ///        price read re-reads the tier env vars (see sequencingStub_).
    ///        Env writes use the CRT family directly (the ScopedEnvVar guard
    ///        semantics — MSVC std::getenv reads the CRT _environ copy).
    void RepointPriceTiersAtSequencingStub()
    {
        sequencingStub_.Start();
        const auto base = "http://127.0.0.1:" + std::to_string( sequencingStub_.Port() );
#ifdef _WIN32
        _putenv_s( "SGNS_COINGECKO_URL", base.c_str() );
        _putenv_s( "SGNS_PRICE_FALLBACK_URL", base.c_str() );
#else
        setenv( "SGNS_COINGECKO_URL", base.c_str(), /*overwrite=*/1 );
        setenv( "SGNS_PRICE_FALLBACK_URL", base.c_str(), /*overwrite=*/1 );
#endif
        sgns::AccountManagementTestAccess::ResetPriceManager( node_ );
    }
};

TEST_F( AccountManagement, CantSelectAccountThatWasNotAdded )
{
    ASSERT_TRUE( node_->SelectAccount( "foobar" ).has_error() );
}

TEST_F( AccountManagement, CanSelectAccountThatWasAdded )
{
    auto old_account_address = node_->GetAddress();
    auto new_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path ).first->GetAddress();
    ASSERT_TRUE( node_->SelectAccount( new_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
    ASSERT_EQ( node_->GetAddress(), new_account_address );
    // Can go back to previous account
    ASSERT_TRUE( node_->SelectAccount( old_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
}

TEST_F( AccountManagement, TransferAccount )
{
    ASSERT_TRUE(
        node_->MintTokens( 200, sgns::test::NextMintSourceHash(), "test", TOKEN_ID, "", GeniusNode::TIMEOUT_MINT )
            .has_value() );
    auto balance               = node_->GetBalance();
    auto other_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path ).first->GetAddress();
    ASSERT_TRUE( node_->TransferAccount( other_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
    ASSERT_EQ( node_->GetBalance(), balance );
}

TEST_F( AccountManagement, CanDeleteAccount )
{
    auto old_account_address = node_->GetAddress();
    auto new_account_address = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, path ).first->GetAddress();
    ASSERT_TRUE( node_->SelectAccount( new_account_address ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node not synced" );
    ASSERT_TRUE( node_->DeleteAccount( old_account_address ).has_value() );
    ASSERT_TRUE( node_->SelectAccount( old_account_address ).has_error() );
}

TEST_F( AccountManagement, SetPayoutAddress )
{
    auto path_requester = path.parent_path() / "am_node_req";
    auto path_receiver  = path.parent_path() / "am_node_rec";

    try
    {
        test::removeAllWithRetry( path_requester.string() );
        test::removeAllWithRetry( path_receiver.string() );
    }
    catch ( ... )
    {
    }

    // All nodes in this test are non-processors.
    // is_processor is now read exclusively from sgns_config.json (defaults to true).
    boost::filesystem::create_directories( path_receiver );
    sgns::GeniusNode::WriteNetworkConfig( path_receiver.generic_string() + '/',
                                          /*port_seed=*/0,
                                          /*auto_dht=*/false );
    auto receiver_authority = WriteTrustedNodeConfig(
        path_receiver, "2071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6009f", "Light", false );
    ASSERT_TRUE( receiver_authority );
    boost::filesystem::create_directories( path_requester );
    sgns::GeniusNode::WriteNetworkConfig( path_requester.generic_string() + '/',
                                          /*port_seed=*/0,
                                          /*auto_dht=*/false );
    auto requester_authority = WriteTrustedNodeConfig(
        path_requester, "55189b416eb4267bbe16391adc33d9e30c297e6b7ee72be91b0bcc7b76c437c0", "Light", false );
    ASSERT_TRUE( requester_authority );

    auto node_receiver = sgns::GeniusNode::New(
        { "0xcafe", "0.35", "1.0", TOKEN_ID, path_receiver.generic_string() + '/' },
        sgns::FromPrivateKey{ "2071868aaf52ce5451a533dc5d9050c2024183e0dcb6bb55777c4ba617c6009f" } );
    auto node_requester = sgns::GeniusNode::New(
        { "0xcafe", "0.35", "1.0", TOKEN_ID, path_requester.generic_string() + '/' },
        sgns::FromPrivateKey{ "55189b416eb4267bbe16391adc33d9e30c297e6b7ee72be91b0bcc7b76c437c0" } );
    node_receiver->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );
    node_requester->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );

    node_->AddPeers(
        { node_receiver->GetPubSub()->GetInterfaceAddress(), node_requester->GetPubSub()->GetInterfaceAddress() } );
    node_receiver->AddPeers( { node_requester->GetPubSub()->GetInterfaceAddress() } );

    ConfirmConfiguredTrust( node_receiver );
    ConfirmConfiguredTrust( node_requester );

    test::assertWaitForCondition( [&] { return node_receiver->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_receiver not synced" );
    ASSERT_EQ( node_receiver->GetState(), GeniusNode::NodeState::READY );

    test::assertWaitForCondition( [&] { return node_requester->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_requester not synced" );
    ASSERT_EQ( node_requester->GetState(), GeniusNode::NodeState::READY );

    ASSERT_TRUE( node_->SetPayoutAddress( node_receiver->GetAddress() ).has_value() );
    test::assertWaitForCondition( [&] { return node_->GetState() == GeniusNode::NodeState::READY; },
                                  std::chrono::milliseconds( 50000 ),
                                  "node_ not synced" );
    ASSERT_EQ( node_->GetState(), GeniusNode::NodeState::READY );

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
	  "source_uri_param": "file://[basepath]data/ballet.data",
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
	  "source_uri_param": "file://[basepath]data/frisbee3.data",
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
        "source_uri_param": "file://[basepath]model.mnn",
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
        "source_uri_param": "file://[basepath]model.mnn",
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
    auto        procmgr   = sgns::sgprocessing::ProcessingManager::Create( json_data );
    // Hermetic price (Phase 4 D-12): the fixture's loopback stub serves the
    // scripted genius-ai price, so cost and posting never touch the
    // rate-limited live CoinGecko API (CI runner IPs are blocked there,
    // which used to break this test with NO_PRICE).
    auto gnus_price = node_requester->GetGNUSPrice();
    ASSERT_TRUE( gnus_price ) << "hermetic GNUS price unavailable: " << gnus_price.error().message();
    uint64_t cost = node_requester->GetProcessCost( *procmgr.value() ).minions;
    ASSERT_GT( cost, 0u );
    // Assets live in the source tree. Deriving this from the binary location broke
    // whenever the build layout changed (multi-config or ABI subdirectory).
    std::string bin_path = std::string( SGNS_PROCESSING_ASSETS_DIR ) + "/";
    std::replace( bin_path.begin(), bin_path.end(), '\\', '/' );
    boost::replace_all( json_data, "[basepath]", bin_path );

    auto mint_result = node_requester->MintTokens( 50000000000,
                                                   sgns::test::NextMintSourceHash(),
                                                   "test",
                                                   TOKEN_ID,
                                                   "",
                                                   std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) );
    ASSERT_TRUE( mint_result.has_value() ) << "Mint transaction failed or timed out";

    auto balance_worker    = node_->GetBalance();
    auto balance_receiver  = node_receiver->GetBalance();
    auto balance_requester = node_requester->GetBalance();

    auto postjob = node_requester->ProcessImage( json_data );
    ASSERT_TRUE( postjob ) << "post job error: " << postjob.error().message();
    ASSERT_EQ( node_requester->WaitForEscrowRelease( postjob.value(), std::chrono::milliseconds( 300000 ) ),
               TransactionManager::TransactionStatus::CONFIRMED );

    assertWaitForCondition(
        [&]
        {
            auto result = node_requester->GetBalance();
            return result < balance_requester;
        },
        std::chrono::milliseconds( 20000 ),
        "Requester balance not updated in time" );
    ASSERT_TRUE( node_requester->GetBalance() < balance_requester );

    assertWaitForCondition(
        [&]
        {
            auto result = node_receiver->GetBalance();
            std::cout << "Rec: " << node_receiver->GetBalance() << " Req: " << node_requester->GetBalance() << '\n';
            return result > balance_receiver;
        },
        std::chrono::milliseconds( 40000 ),
        "Receiver balance not updated in time" );
}

namespace
{
    /// @brief Same job json as SetPayoutAddress (single ballet_image input,
    ///        one inference pass): block_len 4860000, chunk geometry, and
    ///        [basepath] substitution identical — only the price scenario
    ///        differs between the tests.
    std::string ClaimedPriceJobJson( const std::string &basepath )
    {
        static const std::string json_data = R"(
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
	  "source_uri_param": "file://[basepath]data/ballet.data",
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
    }
  ],

  "passes": [
    {
      "name": "ballet_pose_inference",
      "type": "inference",
      "description": "Run PoseNet inference on ballet image",
      "model": {
        "source_uri_param": "file://[basepath]model.mnn",
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
    }
  ]
}
       )";
        std::string with_basepath = json_data;
        boost::replace_all( with_basepath, "[basepath]", basepath );
        return with_basepath;
    }
} // namespace

// WIRE-02: the claimed price stamped on the wire Task is the exact price
// that sized the escrow, from ONE quote (D-06-01). The sequencing stub
// serves P1 for the escrow-sizing fetch (inside ProcessImage) and P2 for a
// LATER direct GetProcessCost call: the posted task must still claim P1 and
// the escrow hold must still carry CalculateCostMinions(4860000, P1).
// A second price read inside ProcessImage would fetch P2 and desync the
// claim from the escrow — exactly what this test rejects.
TEST_F( AccountManagement, ClaimedPriceMatchesEscrowQuote )
{
    // The poster uses the fixture node_ (already READY with trusted config).
    const double P1 = 0.19; // escrow-sizing price served inside ProcessImage
    const double P2 = 0.07; // later, different price — proves single-read

    // Funds for the escrow (no processing peers needed for the claim).
    ASSERT_TRUE( node_->MintTokens( 50000000000,
                                    sgns::test::NextMintSourceHash(),
                                    "test",
                                    TOKEN_ID,
                                    "",
                                    std::chrono::milliseconds( GeniusNode::TIMEOUT_MINT ) )
                    .has_value() );
    ASSERT_TRUE( node_->GetBalance() > 0 );

    std::string json_data = ClaimedPriceJobJson( std::string( SGNS_PROCESSING_ASSETS_DIR ) + "/" );
    auto        procmgr   = sgns::sgprocessing::ProcessingManager::Create( json_data );
    ASSERT_TRUE( procmgr ) << "ProcessingManager::Create failed";
    auto blockLen = procmgr.value()->ParseBlockSize();
    ASSERT_TRUE( blockLen ) << "ParseBlockSize failed";
    ASSERT_EQ( blockLen.value(), 4860000u );

    // Script P1 (Gnus envelope with an old fetchedAt so nothing is ever
    // served from L1) and repoint the node's tiers at the sequencing stub.
    sequencingStub_.OnPath( "/v1/prices", { 200, "application/json", NonFreshGnusEnvelopeBody( P1 ) } );
    RepointPriceTiersAtSequencingStub();

    // Sanity: one network fetch resolves P1 through the whole chain.
    {
        auto price = node_->GetGNUSPrice();
        ASSERT_TRUE( price ) << "GetGNUSPrice failed: "
                             << ( price ? "" : price.error().message() );
        ASSERT_DOUBLE_EQ( price.value(), P1 );
    }

    // Post the job: escrow is sized AND the claim is stamped from this quote.
    auto postjob = node_->ProcessImage( json_data );
    ASSERT_TRUE( postjob ) << "post job error: " << postjob.error().message();

    // The posted task, as persisted in the node's own queue.
    auto myTaskIds = node_->GetMyTaskIds();
    ASSERT_FALSE( myTaskIds.empty() ) << "ProcessImage did not track the posted task id";
    const auto task_id = myTaskIds.front();
    auto       posted_task = sgns::AccountManagementTestAccess::GetPostedTask( node_, task_id );
    ASSERT_TRUE( posted_task ) << "posted task not retrievable from queue";
    EXPECT_DOUBLE_EQ( posted_task.value().claimed_price(), P1 );

    // The escrow hold carries the SAME price's minion cost.
    auto manager = node_->GetTransactionManager();
    ASSERT_TRUE( manager );
    auto escrow_tx = sgns::MultiAccountTestAccess::GetEscrowTransaction( manager.value(), postjob.value() );
    ASSERT_TRUE( escrow_tx ) << "escrow transaction not found by tx hash";
    auto expected_minions = sgns::TokenAmount::CalculateCostMinions( blockLen.value(), P1 );
    ASSERT_TRUE( expected_minions ) << "CalculateCostMinions failed";
    const auto *escrow = dynamic_cast<const sgns::EscrowTransaction *>( escrow_tx.get() );
    ASSERT_NE( escrow, nullptr ) << "tx is not an EscrowTransaction";
    EXPECT_EQ( escrow->GetAmount(), expected_minions.value() );

    // Change the served price to P2 and re-read cost directly: the claim on
    // the wire stays P1 (stamped at post time); the new quote would give a
    // different minion count. This is the desync detector: had ProcessImage
    // used a second GetGNUSPrice() call, claim and escrow would disagree.
    sequencingStub_.OnPath( "/v1/prices", { 200, "application/json", NonFreshGnusEnvelopeBody( P2 ) } );
    uint64_t cost_at_P2 = node_->GetProcessCost( *procmgr.value() ).minions;
    ASSERT_GT( cost_at_P2, 0u );
    auto expected_at_P2 = sgns::TokenAmount::CalculateCostMinions( blockLen.value(), P2 );
    ASSERT_TRUE( expected_at_P2 );
    EXPECT_EQ( cost_at_P2, expected_at_P2.value() );
    EXPECT_NE( cost_at_P2, expected_minions.value() ) << "P1/P2 must produce different costs for the test to bite";
    // Re-read the persisted task: the wire claim is immutable at P1.
    auto reread_task = sgns::AccountManagementTestAccess::GetPostedTask( node_, task_id );
    ASSERT_TRUE( reread_task );
    EXPECT_DOUBLE_EQ( reread_task.value().claimed_price(), P1 );
}

// WIRE-01: claimed_price survives a serialize/parse round-trip bit-exactly.
// The asserted value is a copied literal, never computed, so EXPECT_DOUBLE_EQ
// proves wire fidelity of the double (research anti-pattern: no float math).
TEST( TaskClaimedPriceWire, RoundTripPreservesDouble )
{
    const double kClaimed = 0.123456789012345; // full double precision, copied

    SGProcessing::Task out;
    out.set_ipfs_block_id( "wire-rt-task" );
    out.set_claimed_price( kClaimed );
    ASSERT_EQ( out.claimed_price(), kClaimed );

    const std::string bytes = out.SerializeAsString();
    ASSERT_FALSE( bytes.empty() );

    SGProcessing::Task in;
    ASSERT_TRUE( in.ParseFromString( bytes ) );
    EXPECT_DOUBLE_EQ( in.claimed_price(), kClaimed );
    EXPECT_EQ( in.ipfs_block_id(), "wire-rt-task" );
}

// WIRE-01 back-compat: bytes from a Task that only ever set fields 1-5 (what
// every pre-Phase-6 node emits) parse with claimed_price()==0.0 — the proto3
// default meaning "absent" — while the legacy fields stay intact.
TEST( TaskClaimedPriceWire, OldMessageParsesAsZero )
{
    SGProcessing::Task legacy;
    legacy.set_ipfs_block_id( "legacy-task" );
    legacy.set_json_data( R"({"job":"legacy"})" );
    legacy.set_random_seed( 0.5f );
    legacy.set_results_channel( "RESULT_CHANNEL_ID_1" );
    legacy.set_escrow_path( "/escrow/legacy" );

    const std::string legacyBytes = legacy.SerializeAsString();
    ASSERT_FALSE( legacyBytes.empty() );

    SGProcessing::Task parsed;
    ASSERT_TRUE( parsed.ParseFromString( legacyBytes ) );
    EXPECT_DOUBLE_EQ( parsed.claimed_price(), 0.0 );
    EXPECT_EQ( parsed.ipfs_block_id(), "legacy-task" );
    EXPECT_EQ( parsed.json_data(), R"({"job":"legacy"})" );
    EXPECT_FLOAT_EQ( parsed.random_seed(), 0.5f );
    EXPECT_EQ( parsed.results_channel(), "RESULT_CHANNEL_ID_1" );
    EXPECT_EQ( parsed.escrow_path(), "/escrow/legacy" );
}
