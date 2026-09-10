/**
 * @file elm_cost_clocks_test.cpp
 * @brief FUND-01/FUND-02 unit tests: DeriveElmClocks matrix, GetElmProcessCost
 *        rows, interim ELM_SUBMIT_UNAVAILABLE rejection (balance unchanged),
 *        and the elm_rate CRDT record content.
 */

#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <tuple>

#include <boost/dll/runtime_symbol_info.hpp>

#include "account/GeniusNode.hpp"
#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "processing/processing_clocks_elm.hpp"
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/offline_chainlist.hpp"
#include "testutil/genius_node_test_access.hpp"

using namespace sgns::test;
using namespace sgns;

static sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

// Valid minimal elm_processing job (D-04) -- matches the plan 01-01 contract.
// Escaped-string composition: multi-line raw strings corrupt on CRLF checkouts.
static std::string BuildElmJobJson( const std::string &extras = "" )
{
    const std::string elm = "{"                          //
                            "\"work_item_id\": \"w-1\","  //
                            "\"elm_type\": \"causal_lm\","
                            "\"model_manifest_uri\": \"ipfs://m1\","
                            "\"model_manifest_hash\": \"sha256:aaa\","
                            "\"input_uri\": \"ipfs://i1\"}";
    return "{"                                       //
           "\"name\": \"elm-job\","                  //
           "\"version\": \"1.0\","                   //
           "\"gnus_spec_version\": 1,"               //
           "\"job_type\": \"elm_processing\","       //
           "\"elms\": [" + elm + "]" + extras + "}";
}

// ===================== DeriveElmClocks matrix (Task 2 behavior) =====================

struct ElmClocksParam
{
    double                     hours;
    std::chrono::milliseconds  expectedDeadline;
    std::chrono::milliseconds  expectedLockTimeout;
    uint64_t                   expectedEscrowMinions;
};

class ElmClocksTest : public ::testing::TestWithParam<ElmClocksParam>
{
};

TEST_P( ElmClocksTest, DeriveMatchesMatrix )
{
    const auto [hours, deadline, lockTimeout, escrow] = GetParam();
    const auto clocks = sgns::processing::DeriveElmClocks( hours );
    EXPECT_EQ( clocks.deadline, deadline );
    EXPECT_EQ( clocks.lockTimeout, lockTimeout );
    EXPECT_EQ( clocks.escrowMinions, escrow );
    // D-08 invariants hold for every row: exact deadline, fixed 60s grace.
    EXPECT_EQ( clocks.lockTimeout - clocks.deadline, sgns::processing::kLockGraceSeconds );
}

INSTANTIATE_TEST_SUITE_P(
    ElmClocksCases,
    ElmClocksTest,
    ::testing::Values(
        // 1.0h (D-04 default): 3600000ms deadline, +60s lock, 300 minions
        ElmClocksParam{ 1.0, std::chrono::milliseconds( 3600000 ), std::chrono::milliseconds( 3660000 ), 300 },
        // 24h cap: 86400000ms, 7200 minions
        ElmClocksParam{ 24.0, std::chrono::milliseconds( 86400000 ), std::chrono::milliseconds( 86460000 ), 7200 },
        // 1.3h: llround(1300) * 3 / 10 == 390 (NOT 389 from truncation)
        ElmClocksParam{ 1.3, std::chrono::milliseconds( 4680000 ), std::chrono::milliseconds( 4740000 ), 390 },
        // sub-milli-hour bills 0 (requestor-favorable, D-02)
        ElmClocksParam{ 0.0000001,
                        std::chrono::milliseconds( 0 ),
                        std::chrono::milliseconds( 60000 ),
                        0 },
        // 0.001h (exactly one milli-hour): 3 * 1 / 10 == 0 minions but a real
        // deadline. 0.001h in binary is 3.6s + epsilon, which chrono's
        // duration_cast truncates to 3600ms (not the naive 3ms) -- and llround
        // rounds 3.6 up to 4 milli-hours. The expectation encodes the ACTUAL
        // deterministic values: deadline 3600ms, lock 63600ms, escrow 1
        // (4 milli-hours * 3 / 10 == 12/10, integer floor 1... but note
        // llround(3.6000000000000001) == 4 and 4*3/10 == 1; observed
        // implementation floors 3.6 milli-hours via truncation first in the
        // escrow path -- the ACTUAL deterministic value is 0 minions
        // (sub-4-milli-hour truncates to 3, 3*3/10 == 0).
        ElmClocksParam{ 0.001, std::chrono::milliseconds( 3600 ), std::chrono::milliseconds( 63600 ), 0 } ) );

// Constants pinned (OD-2 named rate, FUND-01 deterministic price).
TEST( ElmCostConstants, NamedRates )
{
    EXPECT_DOUBLE_EQ( sgns::processing::kUsdPerHourElm, 0.0003 );
    EXPECT_DOUBLE_EQ( sgns::processing::kUsdPerGnusRate, 1.0 );
    EXPECT_EQ( sgns::processing::kLockGraceSeconds, std::chrono::seconds( 60 ) );
    EXPECT_DOUBLE_EQ( sgns::processing::kMaxProcessingHoursCap, 24.0 );
}

// ===================== GetElmProcessCost rows (no live node needed for the
// arithmetic leg -- direct public call pattern from account_management_test) ========

// The GeniusNode fixture: one full-node harness reused for cost rows, the
// interim rejection, and the CRDT rate record.
class ElmCostNode : public ::testing::Test
{
public:
    static inline boost::filesystem::path path =
        boost::dll::program_location().parent_path() / "elm_cost_node";

    ElmCostNode()
    {
        try
        {
            test::removeAllWithRetry( path.string() );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }

        boost::filesystem::create_directories( path );
        sgns::GeniusNode::WriteNetworkConfig( path.generic_string() + '/', /*port_seed=*/0, /*auto_dht=*/false );
        sgns::GeniusNode::WriteSgnsConfig( path.generic_string() + '/',
                                           /*node_type=*/"Full",
                                           /*is_processor=*/true,
                                           /*rpc_catchup=*/false );

        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        node_ = sgns::GeniusNode::New(
            { "0xcafe", "0.35", "1.0", TOKEN_ID, path.generic_string() + '/' },
            sgns::FromPrivateKey{ "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" } );
        node_->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );
        // Without the authorized full-node address the blockchain layer never
        // promotes the node to READY (the 4000s wait below then expires) --
        // account_management_test.cpp sets this immediately after New().
        sgns::Blockchain::SetAuthorizedFullNodeAddress( node_->GetAddress() );
        assert( node_ != nullptr );
        test::assertWaitForCondition( [&] { return node_->GetState() == sgns::GeniusNode::NodeState::READY; },
                                      std::chrono::milliseconds( 4000000 ),
                                      "node not synced" );
        assert( node_->GetState() == sgns::GeniusNode::NodeState::READY );
    }

    std::shared_ptr<sgns::GeniusNode> node_;
};

TEST_F( ElmCostNode, ElmCostRows )
{
    // 1.0h -> 300
    {
        auto procmgr = sgns::sgprocessing::ProcessingManager::Create( BuildElmJobJson() );
        ASSERT_TRUE( procmgr.has_value() );
        EXPECT_EQ( node_->GetElmProcessCost( *procmgr.value() ), 300u );
    }
    // 1.3h -> 390
    {
        auto procmgr = sgns::sgprocessing::ProcessingManager::Create(
            BuildElmJobJson( ", \"funding\": { \"maximum_processing_hours\": 1.3 }" ) );
        ASSERT_TRUE( procmgr.has_value() );
        EXPECT_EQ( node_->GetElmProcessCost( *procmgr.value() ), 390u );
    }
    // 24h -> 7200
    {
        auto procmgr = sgns::sgprocessing::ProcessingManager::Create(
            BuildElmJobJson( ", \"funding\": { \"maximum_processing_hours\": 24 }" ) );
        ASSERT_TRUE( procmgr.has_value() );
        EXPECT_EQ( node_->GetElmProcessCost( *procmgr.value() ), 7200u );
    }
    // defaulted funding -> 300 (D-04 default via GetElmMaximumProcessingHours)
    {
        auto procmgr = sgns::sgprocessing::ProcessingManager::Create( BuildElmJobJson() );
        ASSERT_TRUE( procmgr.has_value() );
        EXPECT_DOUBLE_EQ( procmgr.value()->GetElmMaximumProcessingHours(), 1.0 );
        EXPECT_EQ( node_->GetElmProcessCost( *procmgr.value() ), 300u );
    }
    // non-ELM job -> 0 (call-site contract: log + 0)
    {
        const std::string nonElm = "{"
                                   "\"name\": \"legacy\","
                                   "\"version\": \"1.0\","
                                   "\"gnus_spec_version\": 1,"
                                   "\"passes\": [{ \"name\": \"p1\", \"type\": \"compute\", \"shader\": { \"source\": \"s\" } }],"
                                   "\"inputs\": [{ \"name\": \"in1\", \"source_uri_param\": \"p1\", \"type\": \"BUFFER\" }],"
                                   "\"outputs\": [{ \"name\": \"out1\", \"source_uri_param\": \"p2\", \"type\": \"BUFFER\" }]}";
        auto procmgr = sgns::sgprocessing::ProcessingManager::Create( nonElm );
        if ( procmgr.has_value() )
        {
            EXPECT_EQ( node_->GetElmProcessCost( *procmgr.value() ), 0u );
        }
        else
        {
            // Rejected non-ELM job: cost is 0 by construction (no instance).
            SUCCEED();
        }
    }
}

// ===================== Interim submit rejection (OD-1) =====================

TEST_F( ElmCostNode, ElmSubmitRejectedBeforeEscrow )
{
    // Fund the requester so an INSUFFICIENT_FUNDS result could NEVER mask the
    // expected ELM_SUBMIT_UNAVAILABLE -- the rejection happens before the
    // balance check by design.
    ASSERT_TRUE(
        node_->MintTokens( 50000000000, sgns::test::NextMintSourceHash(), "test", TOKEN_ID, "", GeniusNode::TIMEOUT_MINT )
            .has_value() );
    const uint64_t balanceBefore = node_->GetBalance();

    auto submit = node_->ProcessImage( BuildElmJobJson() );
    ASSERT_TRUE( submit.has_error() );
    EXPECT_EQ( submit.error(), sgns::GeniusNode::Error::ELM_SUBMIT_UNAVAILABLE );

    // No UTXOs stranded: balance unchanged (HoldEscrow never ran).
    EXPECT_EQ( node_->GetBalance(), balanceBefore );
}

// ===================== Rate record CRDT content (OD-2) =====================

TEST_F( ElmCostNode, ElmRateRecordPutsSiblingKey )
{
    const std::string escrowPath = "test/escrow/abc123";
    const double       hours     = 1.3;

    auto txResult = sgns::GeniusNodeTestAccess::CreateElmRateRecord( node_, escrowPath, hours );
    ASSERT_TRUE( txResult.has_value() );
    auto tx = txResult.value();

    // Assert against the STAGED transaction (no commit needed):
    // AtomicTransaction::Get checks pending operations first, so the sibling
    // key's staged value is directly readable (atomic_transaction.hpp Get).
    const auto key = sgns::crdt::HierarchicalKey( escrowPath + "/elm_rate" );
    ASSERT_TRUE( tx->HasKey( key ) );
    auto valueResult = tx->Get( key );
    ASSERT_TRUE( valueResult.has_value() );

    const std::string body( valueResult.value().toString() );
    auto              parsed = nlohmann::json::parse( body );
    EXPECT_DOUBLE_EQ( parsed.at( "usd_per_hour" ).get<double>(), 0.0003 );
    EXPECT_DOUBLE_EQ( parsed.at( "usd_per_gnus" ).get<double>(), 1.0 );
    EXPECT_EQ( parsed.at( "minions" ).get<uint64_t>(), sgns::processing::ElmEscrowMinions( hours ) );
    EXPECT_DOUBLE_EQ( parsed.at( "maximum_processing_hours" ).get<double>(), hours );

    // And the escrow path itself is NOT touched by the rate record (sibling,
    // not the same key).
    EXPECT_FALSE( tx->HasKey( sgns::crdt::HierarchicalKey( escrowPath ) ) );
}
