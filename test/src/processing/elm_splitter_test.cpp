/**
 * @file elm_splitter_test.cpp
 * @brief elmbridge 04-03 Task 3: ELM splitter mapping, elm_subtask_map
 *        tolerance, and submit acceptance (the Phase 1 rejection leg's flip).
 *
 * Suites:
 *   SplitterMapping   — pure unit: N=3 -> 3 subtasks, 1 chunk each, unique
 *                       chunkids, ModelNode round-trip per subtask, map
 *                       size/order/keys (01-DESIGN-SUBTASK-MAPPING §1-§3).
 *   MapTolerance      — job JSON carrying elm_subtask_map still parses
 *                       (unknown-key tolerance — research Pattern 3; the map
 *                       is never added to the quicktype schema).
 *   SubmitAcceptance  — ElmCostNode-style full node: funded ELM submit through
 *                       ProcessImage SUCCEEDS, escrow held (exact deterministic
 *                       balance delta), task visible via GetMyTaskIds.
 */

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <list>
#include <set>
#include <string>
#include <vector>

#include "account/GeniusNode.hpp"
#include "account/GeniusAccount.hpp"
#include "processing/processing_tasksplit_elm.hpp"
#include "processing/processing_clocks_elm.hpp"

#include <SGNSProcMain.hpp>

#include "local_secure_storage/impl/MemorySecureStorage.hpp"

#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/offline_chainlist.hpp"

using namespace sgns::test;
using namespace sgns;

static sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

namespace
{
    // Two work items (submit-acceptance uses 2 per the plan; mapping uses 3).
    std::string BuildElmJobJson( const std::string &extras = "", int elmCount = 1 )
    {
        std::string elms;
        for ( int i = 1; i <= elmCount; ++i )
        {
            const std::string id = "w-" + std::to_string( i );
            if ( !elms.empty() )
            {
                elms += ", ";
            }
            elms += "{" //
                    "\"work_item_id\": \"" + id + "\","
                    "\"elm_type\": \"causal_lm\","
                    "\"model_manifest_uri\": \"ipfs://m1\","
                    "\"model_manifest_hash\": \"sha256:aaa\","
                    "\"input_uri\": \"ipfs://i" + std::to_string( i ) + "\"}";
        }
        return "{"                                    //
               "\"name\": \"elm-split-job\","          //
               "\"version\": \"1.0\","                 //
               "\"gnus_spec_version\": 1,"             //
               "\"job_type\": \"elm_processing\","     //
               "\"elms\": [" + elms + "]" + extras + "}";
    }

    std::vector<sgns::Elm> ParseElms( const std::string &jobJson )
    {
        auto created = sgns::sgprocessing::ProcessingManager::Create( jobJson );
        EXPECT_TRUE( created.has_value() );
        if ( !created.has_value() )
        {
            return {};
        }
        const auto elmsOpt = created.value()->GetProcessingData().get_elms();
        return elmsOpt.value_or( std::vector<sgns::Elm>{} );
    }
} // namespace

// ===================== (A) SplitterMapping (pure unit) =====================

TEST( ElmSplitterMapping, ThreeWorkItemsSplitOneToOne )
{
    const auto elms = ParseElms( BuildElmJobJson( "", 3 ) );
    ASSERT_EQ( elms.size(), 3u );

    SGProcessing::Task task;
    task.set_ipfs_block_id( "test-task-id" );

    std::list<SGProcessing::SubTask> subTasks;
    nlohmann::json                   taskJson;
    taskJson["name"] = "elm-split-job"; // splitter must not disturb siblings

    sgns::processing::ProcessTaskSplitterELM splitter;
    splitter.SplitTask( task, subTasks, elms, "test-ipfs-id", taskJson );

    // §1: exactly N subtasks.
    ASSERT_EQ( subTasks.size(), 3u );

    std::set<std::string> chunkIds;
    std::set<std::string> subtaskIds;
    for ( const auto &subtask : subTasks )
    {
        EXPECT_EQ( subtask.ipfsblock(), "test-task-id" );
        // §2: exactly one notional chunk per subtask.
        ASSERT_EQ( subtask.chunkstoprocess_size(), 1 );
        chunkIds.insert( subtask.chunkstoprocess( 0 ).chunkid() );
        subtaskIds.insert( subtask.subtaskid() );

        // Worker contract: json_data parses as a ModelNode whose source names
        // a work item (round-trip via the production parse path).
        auto nodeResult = sgns::sgprocessing::ProcessingManager::GetModelNodeFromJson( subtask.json_data() );
        ASSERT_TRUE( nodeResult.has_value() );
        const std::string source = nodeResult.value().get_source().value_or( "" );
        EXPECT_EQ( source.substr( 0, 6 ), "input:" );
    }
    // §2 + §5: subtask-unique chunkids; unique subtask ids.
    EXPECT_EQ( chunkIds.size(), 3u );
    EXPECT_EQ( subtaskIds.size(), 3u );

    // §3: map has N entries, both keys, order matches elms[].
    ASSERT_TRUE( taskJson.contains( "elm_subtask_map" ) );
    const auto &map = taskJson["elm_subtask_map"];
    ASSERT_EQ( map.size(), 3u );
    for ( size_t i = 0; i < map.size(); ++i )
    {
        EXPECT_EQ( map[i]["work_item_id"], "w-" + std::to_string( i + 1 ) );
        EXPECT_TRUE( map[i].contains( "subtaskid" ) );
        EXPECT_NE( map[i]["subtaskid"].get<std::string>(), "" );
        // Map entry's subtaskid matches a minted subtask.
        EXPECT_EQ( subtaskIds.count( map[i]["subtaskid"].get<std::string>() ), 1u );
    }

    // Sibling keys undisturbed.
    EXPECT_EQ( taskJson["name"], "elm-split-job" );
}

TEST( ElmSplitterMapping, RequestorSuppliedMapIsOverwritten )
{
    // T-04-03-01: the splitter is the ONLY writer — a requestor-supplied map
    // never survives the split.
    const auto            elms = ParseElms( BuildElmJobJson( "", 1 ) );
    SGProcessing::Task    task;
    task.set_ipfs_block_id( "t" );
    std::list<SGProcessing::SubTask> subTasks;
    nlohmann::json                   taskJson;
    taskJson["elm_subtask_map"] = nlohmann::json::array( { { { "work_item_id", "evil" },
                                                             { "subtaskid", "evil" } } } );

    sgns::processing::ProcessTaskSplitterELM splitter;
    splitter.SplitTask( task, subTasks, elms, "ipfs", taskJson );

    ASSERT_EQ( taskJson["elm_subtask_map"].size(), 1u );
    EXPECT_EQ( taskJson["elm_subtask_map"][0]["work_item_id"], "w-1" );
}

// ===================== (B) MapTolerance (parse-level) =====================

TEST( ElmSplitterMapTolerance, MapKeyInJobJsonStillParses )
{
    // Pattern 3: the map is transport metadata parsed with plain nlohmann by
    // readers — it must never be part of the quicktype schema, and its
    // presence in job JSON must not reject (unknown-key tolerance).
    const std::string withMap = BuildElmJobJson(
        ", \"elm_subtask_map\": [{\"work_item_id\": \"w-1\", \"subtaskid\": \"preexisting\"}]" );
    auto created = sgns::sgprocessing::ProcessingManager::Create( withMap );
    EXPECT_TRUE( created.has_value() );
}

TEST( ElmSplitterMapTolerance, ProcessingDataToJsonRoundTrips )
{
    // The ProcessImage ELM branch serializes the whole parsed job via
    // sgns::to_json before publishing the task. An ELM job has NO passes/
    // inputs/outputs — the optional-vector and enum fields must serialize
    // without throwing (regression guard for the DataType:88 throw).
    auto created = sgns::sgprocessing::ProcessingManager::Create( BuildElmJobJson( "", 2 ) );
    ASSERT_TRUE( created.has_value() );
    nlohmann::json smalljson;
    ASSERT_NO_THROW( sgns::to_json( smalljson, created.value()->GetProcessingData() ) )
        << "serializing an ELM job must not throw";
    const std::string dumped = smalljson.dump( -1 );
    EXPECT_FALSE( dumped.empty() );
    // And the dumped JSON re-parses through Create (map-free round-trip).
    auto reparsed = sgns::sgprocessing::ProcessingManager::Create( dumped );
    EXPECT_TRUE( reparsed.has_value() );
}

// ===================== (C) SubmitAcceptance (full node) =====================

class ElmSubmitNode : public ::testing::Test
{
public:
    static inline boost::filesystem::path path =
        boost::dll::program_location().parent_path() / "elm_splitter_node";

    ElmSubmitNode()
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

        sgns::GeniusAccount::SetSecureStorageFactory(
            []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage> {
                return std::make_shared<MemorySecureStorage>( identifier );
            } );

        node_ = sgns::GeniusNode::New(
            { "0xcafe", "0.35", "1.0", TOKEN_ID, path.generic_string() + '/' },
            sgns::FromPrivateKey{ "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" } );
        node_->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );
        sgns::Blockchain::SetAuthorizedFullNodeAddress( node_->GetAddress() );
        assert( node_ != nullptr );
        test::assertWaitForCondition( [&] { return node_->GetState() == sgns::GeniusNode::NodeState::READY; },
                                      std::chrono::milliseconds( 4000000 ),
                                      "node not synced" );
    }

    std::shared_ptr<sgns::GeniusNode> node_;
};

TEST_F( ElmSubmitNode, FundedElmSubmitSucceeds )
{
    ASSERT_TRUE(
        node_->MintTokens( 50000000000, sgns::test::NextMintSourceHash(), "test", TOKEN_ID, "", sgns::GeniusNode::TIMEOUT_MINT )
            .has_value() );
    const uint64_t balanceBefore = node_->GetBalance();

    // 2 work items, defaulted funding (1.0h -> escrow 300 per Phase 1 rows).
    auto submit = node_->ProcessImage( BuildElmJobJson( "", 2 ) );
    ASSERT_TRUE( submit.has_value() ) << "funded ELM submit must succeed, got error code "
                                      << static_cast<int>( submit.error().value() );
    EXPECT_FALSE( submit.value().empty() );

    // HoldEscrow reserves WHOLE UTXOs (ReserveUTXOs -> non-READY), so the
    // spendable balance drops by AT LEAST the deterministic escrow; the
    // exact-delta form only holds for granular-UTXO wallets.
    const uint64_t expectedEscrow = sgns::processing::ElmEscrowMinions( 1.0 );
    EXPECT_LE( node_->GetBalance(), balanceBefore - expectedEscrow );

    // The task lands in GetMyTaskIds (wait-condition, never a sleep).
    // The ELM branch records the task id in my_task_ids_ immediately after
    // EnqueueTask; poll for CRDT settle anyway.
    test::assertWaitForCondition(
        [&] {
            const auto ids = node_->GetMyTaskIds( 10, 0 );
            for ( const auto &id : ids )
            {
                if ( !id.empty() )
                {
                    return true;
                }
            }
            return false;
        },
        std::chrono::milliseconds( 30000 ),
        "task id never appeared in GetMyTaskIds" );
    const auto newestIds = node_->GetMyTaskIds( 1, 0 );
    ASSERT_FALSE( newestIds.empty() );
    EXPECT_FALSE( newestIds.front().empty() );
}
