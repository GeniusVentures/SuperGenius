#include <gtest/gtest.h>

#include <list>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio.hpp>

#include <processingbase/ProcessingManager.hpp>

#include "processing/processing_subtask_queue_accessor_impl.hpp"
#include "processing/processing_subtask_queue_manager.hpp"
#include "processing/processing_tasksplit.hpp"
#include "processing/proto/SGProcessing.pb.h"
#include "src/processing/processing_mock.hpp"
#include "testutil/processing_conformance_fixture.hpp"

// SECV-02 (Phase 15 Plan 04): the full-pipeline counter-test D-05 mandates.
//
// SECV-01 (secv01_counter_test.cpp) proved a corrupted MNN model still
// diverges at the whole-job artifactId level. It never touches
// ValidateResults/SubTaskResult/subtask-queue plumbing at all -- Phase 15's
// own concatenation-bug fix (XNODE-01b) and tolerance fallback (XNODE-02)
// operate one layer deeper, on per-chunk hashes shared across two subtasks.
// This test proves that combined mechanism -- the fixed ValidateResults
// PLUS its tolerance fallback acting together -- still catches a
// deliberately corrupted subtask result, reached exclusively through
// SubTaskQueueAccessorImpl's real public API (AssignSubTasks/GrabSubTask),
// never a direct ValidateResults call.
//
// Two real ProcessingManager::Process() runs (correct vs. corrupted MNN
// model, mirroring SECV-01's exact job-pair methodology) produce genuine,
// different chunk hashes and real file:// output locations. Those real
// values are then attached to two synthetic SGProcessing::SubTask objects
// built via ProcessTaskSplitter::SplitTask's existing
// addvalidationsubtask=true production code path -- NOT hand-built protobuf
// objects -- so both subtasks reference the identical ProcessingChunk (chunk
// index 0), the exact "two subtasks, one chunk" shape XNODE-01b's fix
// targets.
//
// The job JSON uses a single, non-overlapping chunk ("width": 64,
// "block_len": 64, no "chunk_stride") -- distinct from SECV-01's own
// overlapping-window fixture -- so the tolerance fallback's blob-to-chunk
// slicing (Plan 15-02's AttemptToleranceFallback) is exact, not
// approximated, per RESEARCH.md's Pitfall 3.
//
// sgns::test::ProcessingCoreImpl (processing_mock.hpp) does not override
// ProcessingCore::GetTaskQueue(), so it inherits the base class's default
// nullptr -- this test deliberately exercises Plan 15-01's D-04
// fixed-constant tolerance fallback (no schema-declared quantScale/
// byteQuantMode resolvable for this mock), the loosest legitimate
// configuration and therefore the strongest form of this proof.
//
// Deviation from the plan's literal text: SECV-01's existing
// secv01-corrupted-float_model.mnn fixture (a single-byte flip at offset
// 15360) was empirically confirmed NOT to diverge at the single-window
// (width=64, block_len=64) granularity this test requires -- its post-
// quantization SHA-256 chunk hash for window [0,64) is bit-identical to the
// correct model's for this narrower scenario (the divergence SECV-01 proves
// only manifests once stitched across all 15 of its own overlapping
// windows). A new, dedicated fixture --
// fixtures/secv02-corrupted-float_model.mnn -- was created instead,
// following the exact same "byte-perturbed copy, empirically verified
// in-place" methodology as SECV-01's own fixture-creation step (Phase 12
// Task 1): 100 consecutive 4-byte-aligned floats' most-significant byte
// (sign+exponent) starting at the same offset (15360) are XORed with 0xFF,
// confirmed to still load via MNN::Interpreter (ASSERT_TRUE on both
// ProcessingManager::Create() and Process() below) and to produce a
// genuinely different post-quantization window-0 chunk hash (asserted
// below).

namespace sgns
{
    class Secv02CounterTest : public ProcessorConformanceFixture
    {
    };

    TEST_F( Secv02CounterTest, CorruptedSubtaskResultStillCaughtBySubTaskQueueAccessor )
    {
        // Two float-processing-definition.json-shaped inline jobs, identical
        // in every field except which .mnn file the model parameter points
        // at -- the real float_model.mnn (correct) vs this plan's dedicated
        // byte-perturbed copy, fixtures/secv02-corrupted-float_model.mnn
        // (corrupted; see the file-level comment above for why this differs
        // from SECV-01's own fixture). Single, non-overlapping chunk
        // ("width" == "block_len", no "chunk_stride") keeps the tolerance
        // fallback's byte-slicing exact (Pitfall 3). The two jobs deliberately
        // write to DIFFERENT output filenames (unlike secv01_counter_test.cpp,
        // which never re-reads its saved files) -- this test later fetches
        // each run's saved output file by URI (D-01), so both files must
        // remain independently present on disk, not have the second run's
        // save silently overwrite the first's.
        const std::string correctJson = R"({
            "name": "secv02-mnn-correct",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "inputFloat",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "float",
                    "dimensions": { "width": 64, "block_len": 64 },
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "floatOutput",
                    "source_uri_param": "file://processing_datatypes/float_output.raw",
                    "type": "tensor"
                }
            ],
            "parameters": [
                {
                    "name": "modelUri",
                    "type": "uri",
                    "default": "file://processing_datatypes/float_model.mnn"
                }
            ],
            "passes": [
                {
                    "name": "float_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_datatypes/float_model.mnn",
                        "format": "MNN",
                        "batch_size": 1,
                        "input_nodes": [
                            { "name": "input", "type": "tensor", "source": "input:inputFloat", "shape": [1, 64] }
                        ],
                        "output_nodes": [
                            { "name": "output", "type": "tensor", "target": "output:floatOutput", "shape": [1, 64] }
                        ]
                    }
                }
            ]
        })";

        const std::string corruptedJson = R"({
            "name": "secv02-mnn-corrupted",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "inputFloat",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "float",
                    "dimensions": { "width": 64, "block_len": 64 },
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "floatOutput",
                    "source_uri_param": "file://processing_datatypes/float_output_corrupted.raw",
                    "type": "tensor"
                }
            ],
            "parameters": [
                {
                    "name": "modelUri",
                    "type": "uri",
                    "default": "file://processing_conformance_security/fixtures/secv02-corrupted-float_model.mnn"
                }
            ],
            "passes": [
                {
                    "name": "float_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_conformance_security/fixtures/secv02-corrupted-float_model.mnn",
                        "format": "MNN",
                        "batch_size": 1,
                        "input_nodes": [
                            { "name": "input", "type": "tensor", "source": "input:inputFloat", "shape": [1, 64] }
                        ],
                        "output_nodes": [
                            { "name": "output", "type": "tensor", "target": "output:floatOutput", "shape": [1, 64] }
                        ]
                    }
                }
            ]
        })";

        auto patchedCorrect   = PatchJsonUrisToAbsolute( correctJson, BinPath() );
        auto patchedCorrupted = PatchJsonUrisToAbsolute( corruptedJson, BinPath() );

        auto rCorrect = sgns::sgprocessing::ProcessingManager::Create( patchedCorrect );
        ASSERT_TRUE( rCorrect.has_value() ) << "Correct MNN job should be accepted: "
                                             << rCorrect.error().message();
        auto rCorrupted = sgns::sgprocessing::ProcessingManager::Create( patchedCorrupted );
        ASSERT_TRUE( rCorrupted.has_value() ) << "Corrupted-model MNN job should still be accepted "
                                                  "(a structurally-valid-but-wrong model, not a "
                                                  "malformed job): "
                                               << rCorrupted.error().message();

        const auto &managerCorrect   = rCorrect.value();
        const auto &managerCorrupted = rCorrupted.value();

        auto        pCorrect      = managerCorrect->GetProcessingData();
        const auto &passesCorrect = pCorrect.get_passes();
        ASSERT_EQ( passesCorrect.size(), 1 );
        ASSERT_TRUE( passesCorrect[0].get_model().has_value() );
        const auto modelCorrect      = passesCorrect[0].get_model().value();
        const auto inputNodesCorrect = modelCorrect.get_input_nodes();
        ASSERT_GE( inputNodesCorrect.size(), 1 );

        auto        pCorrupted      = managerCorrupted->GetProcessingData();
        const auto &passesCorrupted = pCorrupted.get_passes();
        ASSERT_EQ( passesCorrupted.size(), 1 );
        ASSERT_TRUE( passesCorrupted[0].get_model().has_value() );
        const auto modelCorrupted      = passesCorrupted[0].get_model().value();
        const auto inputNodesCorrupted = modelCorrupted.get_input_nodes();
        ASSERT_GE( inputNodesCorrupted.size(), 1 );

        auto                              iocCorrect = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrect;
        std::vector<std::string>          outputLocationsCorrect;
        sgns::ModelNode                   modelNodeCorrect = inputNodesCorrect[0];

        auto prCorrect =
            managerCorrect->Process( iocCorrect, chunkhashesCorrect, modelNodeCorrect, outputLocationsCorrect );
        ASSERT_TRUE( prCorrect.has_value() ) << "Correct MNN run: " << prCorrect.error().message();
        ASSERT_GE( chunkhashesCorrect.size(), 1u );
        ASSERT_GE( outputLocationsCorrect.size(), 1u );
        ASSERT_FALSE( outputLocationsCorrect[0].empty() );

        auto                              iocCorrupted = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrupted;
        std::vector<std::string>          outputLocationsCorrupted;
        sgns::ModelNode                   modelNodeCorrupted = inputNodesCorrupted[0];

        auto prCorrupted = managerCorrupted->Process( iocCorrupted,
                                                        chunkhashesCorrupted,
                                                        modelNodeCorrupted,
                                                        outputLocationsCorrupted );
        ASSERT_TRUE( prCorrupted.has_value() ) << "Corrupted-model MNN run: " << prCorrupted.error().message();
        ASSERT_GE( chunkhashesCorrupted.size(), 1u );
        ASSERT_GE( outputLocationsCorrupted.size(), 1u );
        ASSERT_FALSE( outputLocationsCorrupted[0].empty() );

        // Real chunk hashes -- convert each run's raw hash bytes to a std::string
        // the same way sgns::test::ProcessingCoreImpl::ProcessSubTask does for
        // chunk_hashes (byte-for-byte reinterpretation, not re-hashing).
        std::string hashStringCorrect( reinterpret_cast<const char *>( chunkhashesCorrect[0].data() ),
                                       chunkhashesCorrect[0].size() );
        std::string hashStringCorrupted( reinterpret_cast<const char *>( chunkhashesCorrupted[0].data() ),
                                         chunkhashesCorrupted[0].size() );
        ASSERT_NE( hashStringCorrect, hashStringCorrupted )
            << "Precondition: correct and corrupted-model runs must produce genuinely different chunk "
               "hashes, or this test would be vacuous";

        // Build the shared task/subtask pair via ProcessTaskSplitter's existing
        // addvalidationsubtask=true production code path -- produces exactly two
        // SGProcessing::SubTasks referencing the identical ProcessingChunk (chunk
        // index 0): the first with a real generated UUID subtaskid, the second
        // literally named "subtask_validation".
        SGProcessing::Task task;
        task.set_ipfs_block_id( "secv02-task" );

        std::list<SGProcessing::SubTask> subTasksList;
        sgns::processing::ProcessTaskSplitter().SplitTask( task,
                                                            subTasksList,
                                                            /*json_data=*/std::string( "{}" ),
                                                            /*numchunks=*/1,
                                                            /*addvalidationsubtask=*/true,
                                                            /*ipfsid=*/"secv02-task" );
        ASSERT_EQ( subTasksList.size(), 2u );

        const std::string subtaskCorrectId   = subTasksList.front().subtaskid();
        const std::string subtaskCorruptedId = subTasksList.back().subtaskid();
        ASSERT_EQ( subtaskCorruptedId, "subtask_validation" );
        ASSERT_NE( subtaskCorrectId, subtaskCorruptedId );
        ASSERT_EQ( subTasksList.front().chunkstoprocess( 0 ).SerializeAsString(),
                   subTasksList.back().chunkstoprocess( 0 ).SerializeAsString() )
            << "Precondition: both subtasks must reference the identical shared ProcessingChunk";

        // Two SGProcessing::SubTaskResult objects: genuinely different real chunk
        // hashes (correct vs. corrupted model run) for the same shared chunk, and
        // real file:// output locations ProcessingManager::Process() already wrote.
        SGProcessing::SubTaskResult resultCorrect;
        resultCorrect.set_subtaskid( subtaskCorrectId );
        resultCorrect.add_chunk_hashes( hashStringCorrect );
        resultCorrect.set_ipfs_results_data_id( outputLocationsCorrect[0] );

        SGProcessing::SubTaskResult resultCorrupted;
        resultCorrupted.set_subtaskid( subtaskCorruptedId );
        resultCorrupted.add_chunk_hashes( hashStringCorrupted );
        resultCorrupted.set_ipfs_results_data_id( outputLocationsCorrupted[0] );

        // Locally-constructed plumbing, no real P2P networking: a fresh io_context,
        // a stub subtask-queue channel, a queue manager bound to it, a mock result
        // storage, and a mock ProcessingCore whose inherited GetTaskQueue() returns
        // nullptr -- deliberately exercising Plan 15-01's D-04 fixed-constant
        // tolerance fallback, the strongest form of this proof.
        auto localContext  = std::make_shared<boost::asio::io_context>();
        auto queueChannel   = std::make_shared<sgns::test::ProcessingSubTaskQueueChannelImpl>();
        auto queueManager =
            std::make_shared<sgns::processing::ProcessingSubTaskQueueManager>( queueChannel,
                                                                                localContext,
                                                                                "SECV02_NODE",
                                                                                []( const std::string & ) {} );
        auto resultStorage    = std::make_shared<sgns::test::SubTaskResultStorageMock>();
        auto processingCoreMock = std::make_shared<sgns::test::ProcessingCoreImpl>();

        bool        errorFired = false;
        std::string errorMessage;
        auto        processingErrorSink = [&errorFired, &errorMessage]( const std::string &message )
        {
            errorFired   = true;
            errorMessage = message;
        };

        auto accessor = std::make_shared<sgns::processing::SubTaskQueueAccessorImpl>(
            nullptr, // gossipPubSub -- no real P2P networking needed
            queueManager,
            resultStorage,
            []( const SGProcessing::TaskResult & ) {}, // taskResultProcessingSink
            processingErrorSink,
            processingCoreMock );

        ASSERT_TRUE( accessor->AssignSubTasks( subTasksList ) );

        resultStorage->AddSubTaskResult( resultCorrect );
        resultStorage->AddSubTaskResult( resultCorrupted );

        // GrabSubTask internally runs UpdateResultsFromStorage (loads both results
        // from the mock storage), and once IsProcessed() is true,
        // FinalizeQueueProcessing -> the fixed ValidateResults with a real fetch of
        // both on-disk file:// outputs and a real numeric diff over genuinely
        // different correct-vs-corrupted MNN output bytes.
        accessor->GrabSubTask( []( boost::optional<const SGProcessing::SubTask &> ) {} );

        EXPECT_TRUE( errorFired ) << "A deliberately corrupted subtask result must still be caught by the "
                                     "fixed ValidateResults plus its tolerance fallback acting together "
                                     "-- SECV-02";
        EXPECT_EQ( errorMessage, "Invalid results for the entire task" );

        std::cout << "CorruptedSubtaskResultStillCaughtBySubTaskQueueAccessor: deliberately corrupted "
                     "subtask result was caught via SubTaskQueueAccessorImpl's real public API"
                  << std::endl;
    }
} // namespace sgns
