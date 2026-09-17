/**
 * @file       processing_validation_core_test.cpp
 * @brief      Live CTest coverage for ProcessingValidationCore::ValidateResults (Phase 15, XNODE-01b/XNODE-02)
 */

#include <algorithm>
#include <cstring>
#include <fstream>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "processing/processing_validation_core.hpp"
#include "ParameterType.hpp"
#include "tools/capture/capture_file_format.hpp"
#include "util/diff_utils.hpp"

using namespace sgns::processing;

/**
 * @given A subtask queue with one subtask and no result for it
 * @when ValidateResults is called
 * @then It still returns an error (unchanged pre-existing behavior)
 */
TEST(ProcessingValidationCoreTest, NoResultsForSubtaskStillFails)
{
    SGProcessing::SubTaskCollection subTasks;
    SGProcessing::ProcessingChunk   chunk1;
    chunk1.set_chunkid( "CHUNK_1" );
    chunk1.set_n_subchunks( 1 );

    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_1" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }

    std::map<std::string, SGProcessing::SubTaskResult> results;

    ProcessingValidationCore validationCore;
    std::set<std::string>    invalidSubTaskIds;
    auto                     validate_res = validationCore.ValidateResults( subTasks, results, invalidSubTaskIds );
    ASSERT_TRUE( validate_res.has_error() );
}

/**
 * @given Two subtasks sharing one ProcessingChunk reporting identical chunk_hashes
 * @when ValidateResults is called
 * @then It returns success with an empty invalidSubTaskIds (SC2 -- unchanged pre-existing behavior)
 */
TEST(ProcessingValidationCoreTest, IdenticalHashesStillPass)
{
    SGProcessing::SubTaskCollection subTasks;
    SGProcessing::ProcessingChunk   chunk1;
    chunk1.set_chunkid( "CHUNK_1" );
    chunk1.set_n_subchunks( 1 );

    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_1" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }
    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_2" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }

    std::map<std::string, SGProcessing::SubTaskResult> results;
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.add_chunk_hashes( "1" );
        subTaskResult.set_subtaskid( "SUBTASK_1" );
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.add_chunk_hashes( "1" );
        subTaskResult.set_subtaskid( "SUBTASK_2" );
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }

    ProcessingValidationCore validationCore;
    std::set<std::string>    invalidSubTaskIds;
    auto                     validate_res = validationCore.ValidateResults( subTasks, results, invalidSubTaskIds );
    ASSERT_FALSE( validate_res.has_error() );
    ASSERT_TRUE( invalidSubTaskIds.empty() );
}

/**
 * @given Two subtasks sharing one ProcessingChunk reporting DIFFERENT chunk_hashes, no jobParameters/fetchOutputData
 * @when ValidateResults is called (the plain 3-arg call)
 * @then It returns an error and both subtask ids appear in invalidSubTaskIds -- proves the concatenation bug
 *       is fixed: today's pre-fix code silently returned success for this exact case (SC1)
 */
TEST(ProcessingValidationCoreTest, DifferingHashesNoToleranceCapabilityFail)
{
    SGProcessing::SubTaskCollection subTasks;
    SGProcessing::ProcessingChunk   chunk1;
    chunk1.set_chunkid( "CHUNK_1" );
    chunk1.set_n_subchunks( 1 );

    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_1" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }
    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_2" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }

    std::map<std::string, SGProcessing::SubTaskResult> results;
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.add_chunk_hashes( "1" );
        subTaskResult.set_subtaskid( "SUBTASK_1" );
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.add_chunk_hashes( "2" );
        subTaskResult.set_subtaskid( "SUBTASK_2" );
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }

    ProcessingValidationCore validationCore;
    std::set<std::string>    invalidSubTaskIds;
    auto                     validate_res = validationCore.ValidateResults( subTasks, results, invalidSubTaskIds );
    ASSERT_TRUE( validate_res.has_error() );
    ASSERT_EQ( 2u, invalidSubTaskIds.size() );
    ASSERT_TRUE( invalidSubTaskIds.find( "SUBTASK_1" ) != invalidSubTaskIds.end() );
    ASSERT_TRUE( invalidSubTaskIds.find( "SUBTASK_2" ) != invalidSubTaskIds.end() );
}

namespace
{
    std::vector<uint8_t> FloatToBytes( float value )
    {
        std::vector<uint8_t> bytes( sizeof( float ) );
        std::memcpy( bytes.data(), &value, sizeof( float ) );
        return bytes;
    }

    std::vector<sgns::Parameter> MakeQuantScaleParameters( double quantScale )
    {
        sgns::Parameter param;
        param.set_name( "quantScale" );
        param.set_type( sgns::ParameterType::FLOAT );
        param.set_parameter_default( quantScale );
        return std::vector<sgns::Parameter>{ param };
    }

    /**
     * @brief Reads an entire file into a byte vector, mirroring capture_diff.cpp's ReadFileBytes.
     * @return true on success; false if the file cannot be opened.
     */
    bool ReadFileBytes( const std::string &path, std::vector<uint8_t> &out )
    {
        std::ifstream stream( path, std::ios::binary );
        if ( !stream.is_open() )
        {
            return false;
        }
        out.assign( std::istreambuf_iterator<char>( stream ), std::istreambuf_iterator<char>() );
        return true;
    }
} // namespace

/**
 * @given Two subtasks reporting differing chunk_hashes for the same shared chunk, a jobParameters
 *        declaring quantScale=32768.0, and a fetchOutputData capability whose fetched buffers differ by
 *        strictly less than 2.0/32768.0
 * @when ValidateResults is called (the 5-arg call)
 * @then It reports a match, not a mismatch (SC3, XNODE-02)
 */
TEST(ProcessingValidationCoreTest, DifferingHashesWithinToleranceStillPass)
{
    SGProcessing::SubTaskCollection subTasks;
    SGProcessing::ProcessingChunk   chunk1;
    chunk1.set_chunkid( "CHUNK_1" );
    chunk1.set_n_subchunks( 1 );

    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_1" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }
    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_2" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }

    std::map<std::string, SGProcessing::SubTaskResult> results;
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.add_chunk_hashes( "1" );
        subTaskResult.set_subtaskid( "SUBTASK_1" );
        subTaskResult.set_ipfs_results_data_id( "stub://a" );
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.add_chunk_hashes( "2" );
        subTaskResult.set_subtaskid( "SUBTASK_2" );
        subTaskResult.set_ipfs_results_data_id( "stub://b" );
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }

    auto jobParameters = MakeQuantScaleParameters( 32768.0 );

    const std::vector<uint8_t> bufferA = FloatToBytes( 1.0f );
    const std::vector<uint8_t> bufferB = FloatToBytes( 1.0f + 1e-6f ); // strictly less than 2.0/32768.0

    auto fetchOutputData = [&]( const std::string &uri ) -> outcome::result<std::vector<uint8_t>>
    {
        if ( uri == "stub://a" )
        {
            return bufferA;
        }
        if ( uri == "stub://b" )
        {
            return bufferB;
        }
        return outcome::failure( std::make_error_code( std::errc::io_error ) );
    };

    ProcessingValidationCore validationCore;
    std::set<std::string>    invalidSubTaskIds;
    auto                     validate_res =
        validationCore.ValidateResults( subTasks, results, invalidSubTaskIds, &jobParameters, fetchOutputData );
    ASSERT_FALSE( validate_res.has_error() );
    ASSERT_TRUE( invalidSubTaskIds.empty() );
}

/**
 * @given The same setup as DifferingHashesWithinToleranceStillPass, but the fetched buffers differ by
 *        strictly more than 2.0/32768.0
 * @when ValidateResults is called (the 5-arg call)
 * @then It still reports a genuine mismatch (SC4) -- the fallback does not mask every divergence
 */
TEST(ProcessingValidationCoreTest, DifferingHashesExceedsToleranceFail)
{
    SGProcessing::SubTaskCollection subTasks;
    SGProcessing::ProcessingChunk   chunk1;
    chunk1.set_chunkid( "CHUNK_1" );
    chunk1.set_n_subchunks( 1 );

    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_1" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }
    {
        auto subtask = subTasks.add_items();
        subtask->set_subtaskid( "SUBTASK_2" );
        auto chunk = subtask->add_chunkstoprocess();
        chunk->CopyFrom( chunk1 );
    }

    std::map<std::string, SGProcessing::SubTaskResult> results;
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.add_chunk_hashes( "1" );
        subTaskResult.set_subtaskid( "SUBTASK_1" );
        subTaskResult.set_ipfs_results_data_id( "stub://a" );
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.add_chunk_hashes( "2" );
        subTaskResult.set_subtaskid( "SUBTASK_2" );
        subTaskResult.set_ipfs_results_data_id( "stub://b" );
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }

    auto jobParameters = MakeQuantScaleParameters( 32768.0 );

    const std::vector<uint8_t> bufferA = FloatToBytes( 1.0f );
    const std::vector<uint8_t> bufferB = FloatToBytes( 1.01f ); // strictly more than 2.0/32768.0

    auto fetchOutputData = [&]( const std::string &uri ) -> outcome::result<std::vector<uint8_t>>
    {
        if ( uri == "stub://a" )
        {
            return bufferA;
        }
        if ( uri == "stub://b" )
        {
            return bufferB;
        }
        return outcome::failure( std::make_error_code( std::errc::io_error ) );
    };

    ProcessingValidationCore validationCore;
    std::set<std::string>    invalidSubTaskIds;
    auto                     validate_res =
        validationCore.ValidateResults( subTasks, results, invalidSubTaskIds, &jobParameters, fetchOutputData );
    ASSERT_TRUE( validate_res.has_error() );
    ASSERT_EQ( 2u, invalidSubTaskIds.size() );
    ASSERT_TRUE( invalidSubTaskIds.find( "SUBTASK_1" ) != invalidSubTaskIds.end() );
    ASSERT_TRUE( invalidSubTaskIds.find( "SUBTASK_2" ) != invalidSubTaskIds.end() );
}

/**
 * @given Phase 13's real Mac/Windows MNN float32 .cap fixture pair (D-01: VALD-01's exact
 *        already-characterized pair, mnn-float_Fuus-Mac-mini.local---macOS_20260821T221542.cap vs
 *        mnn-float_Mofu---Windows_20260821T221735.cap)
 * @when Both files are parsed independently via DeserializeCaptureFile and each of the 15 chunk hashes
 *       is compared byte-for-byte between the two artifacts
 * @then The resulting per-chunk match pattern is identical to the already-published
 *       diff-mnn-float-refit.json's chunkHashesMatch array (14 true, 1 false at index 10) -- proving
 *       this test's own file parsing is correct before any new conclusion is drawn from ValidateResults'
 *       behavior
 */
TEST(ProcessingValidationCoreTest, Vald02ChunkHashSelfCheckMatchesPublishedRefit)
{
    const std::string macPath =
        std::string( VALD02_CAPTURES_DIR ) + "/mnn-float_Fuus-Mac-mini.local---macOS_20260821T221542.cap";
    const std::string winPath =
        std::string( VALD02_CAPTURES_DIR ) + "/mnn-float_Mofu---Windows_20260821T221735.cap";

    std::vector<uint8_t> bytesA;
    std::vector<uint8_t> bytesB;
    ASSERT_TRUE( ReadFileBytes( macPath, bytesA ) );
    ASSERT_TRUE( ReadFileBytes( winPath, bytesB ) );

    sgns::sgproccapture::CaptureFile captureA;
    sgns::sgproccapture::CaptureFile captureB;
    ASSERT_TRUE( sgns::sgproccapture::DeserializeCaptureFile( bytesA, captureA ) );
    ASSERT_TRUE( sgns::sgproccapture::DeserializeCaptureFile( bytesB, captureB ) );

    ASSERT_EQ( 1u, captureA.artifacts.size() );
    ASSERT_EQ( 1u, captureB.artifacts.size() );

    const auto &artifactA = captureA.artifacts[0];
    const auto &artifactB = captureB.artifacts[0];

    ASSERT_EQ( 15u, artifactA.chunkHashCount );
    ASSERT_EQ( 15u, artifactB.chunkHashCount );

    for ( uint32_t j = 0; j < 15u; ++j )
    {
        const bool chunkMatches = std::equal( artifactA.chunkHashes[j],
                                               artifactA.chunkHashes[j] + sgns::sgprocessing::SHA256_HASH_SIZE,
                                               artifactB.chunkHashes[j] );
        if ( j == 10u )
        {
            ASSERT_FALSE( chunkMatches ) << "chunk 10 is expected to still diverge per 13-SCOPE-BOUNDARY.md";
        }
        else
        {
            ASSERT_TRUE( chunkMatches ) << "chunk " << j << " is expected to match per diff-mnn-float-refit.json";
        }
    }
}

/**
 * @given Phase 13's real Mac/Windows MNN float32 .cap fixture pair fed through the actual production
 *        ValidateResults/AttemptToleranceFallback code path across all 15 chunks (D-02/D-03), built as
 *        two public SGProcessing::SubTaskCollection/SubTaskResult protobuf objects (ChunkContribution is
 *        a private struct and cannot be constructed directly), using the shipped S=2^15 quantScale
 *        (quantization.hpp)
 * @when ValidateResults is called (the 5-arg overload)
 * @then Chunk 10's real cross-hardware divergence (maxAbsDelta=3.0517578125e-05, independently
 *       cross-checked below via a direct IsFloatChunkWithinTolerance call) is within the production D-03
 *       tolerance bound (2.0/32768.0=6.103515625e-05), so the tolerance fallback resolves it as a match
 *       and ValidateResults reports no error / no invalidated subtasks -- the observed outcome, not an
 *       assumed one, per D-04
 */
TEST(ProcessingValidationCoreTest, Vald02FullFixtureValidateResultsToleranceOutcome)
{
    // Task 2 evidence capture: raise this process's log level so
    // AttemptToleranceFallback's existing "maxAbsDelta=... maxRelDelta=... withinTolerance=..."
    // debug log line (processing_validation_core.cpp, unmodified) becomes visible in ctest -V's
    // captured console output for chunk 10 -- test-file-scoped logging verbosity only, no
    // production code/behavior change.
    spdlog::set_level( spdlog::level::debug );

    const std::string macPath =
        std::string( VALD02_CAPTURES_DIR ) + "/mnn-float_Fuus-Mac-mini.local---macOS_20260821T221542.cap";
    const std::string winPath =
        std::string( VALD02_CAPTURES_DIR ) + "/mnn-float_Mofu---Windows_20260821T221735.cap";

    std::vector<uint8_t> bytesA;
    std::vector<uint8_t> bytesB;
    ASSERT_TRUE( ReadFileBytes( macPath, bytesA ) );
    ASSERT_TRUE( ReadFileBytes( winPath, bytesB ) );

    sgns::sgproccapture::CaptureFile captureA;
    sgns::sgproccapture::CaptureFile captureB;
    ASSERT_TRUE( sgns::sgproccapture::DeserializeCaptureFile( bytesA, captureA ) );
    ASSERT_TRUE( sgns::sgproccapture::DeserializeCaptureFile( bytesB, captureB ) );

    ASSERT_EQ( 1u, captureA.artifacts.size() );
    ASSERT_EQ( 1u, captureB.artifacts.size() );

    const auto &artifactA = captureA.artifacts[0];
    const auto &artifactB = captureB.artifacts[0];

    ASSERT_EQ( 15u, artifactA.chunkHashCount );
    ASSERT_EQ( 15u, artifactB.chunkHashCount );
    ASSERT_FALSE( captureA.rawRecordsPerArtifact.empty() );
    ASSERT_FALSE( captureB.rawRecordsPerArtifact.empty() );
    ASSERT_GE( captureA.rawRecordsPerArtifact[0].size(), 15u );
    ASSERT_GE( captureB.rawRecordsPerArtifact[0].size(), 15u );

    constexpr size_t kChunkCount = 15;

    SGProcessing::SubTaskCollection subTasks;
    {
        auto subtaskMac = subTasks.add_items();
        subtaskMac->set_subtaskid( "SUBTASK_MAC" );
        for ( size_t j = 0; j < kChunkCount; ++j )
        {
            SGProcessing::ProcessingChunk chunk;
            chunk.set_chunkid( "MNN_FLOAT_CHUNK_" + std::to_string( j ) );
            chunk.set_n_subchunks( 1 );
            subtaskMac->add_chunkstoprocess()->CopyFrom( chunk );
        }
    }
    {
        auto subtaskWin = subTasks.add_items();
        subtaskWin->set_subtaskid( "SUBTASK_WIN" );
        for ( size_t j = 0; j < kChunkCount; ++j )
        {
            SGProcessing::ProcessingChunk chunk;
            chunk.set_chunkid( "MNN_FLOAT_CHUNK_" + std::to_string( j ) );
            chunk.set_n_subchunks( 1 );
            subtaskWin->add_chunkstoprocess()->CopyFrom( chunk );
        }
    }

    std::map<std::string, SGProcessing::SubTaskResult> results;
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.set_subtaskid( "SUBTASK_MAC" );
        subTaskResult.set_ipfs_results_data_id( "stub://vald02-mac" );
        for ( size_t j = 0; j < kChunkCount; ++j )
        {
            subTaskResult.add_chunk_hashes( std::string( reinterpret_cast<const char *>( artifactA.chunkHashes[j] ),
                                                          sgns::sgprocessing::SHA256_HASH_SIZE ) );
        }
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }
    {
        SGProcessing::SubTaskResult subTaskResult;
        subTaskResult.set_subtaskid( "SUBTASK_WIN" );
        subTaskResult.set_ipfs_results_data_id( "stub://vald02-win" );
        for ( size_t j = 0; j < kChunkCount; ++j )
        {
            subTaskResult.add_chunk_hashes( std::string( reinterpret_cast<const char *>( artifactB.chunkHashes[j] ),
                                                          sgns::sgprocessing::SHA256_HASH_SIZE ) );
        }
        results.emplace( subTaskResult.subtaskid(), subTaskResult );
    }

    auto jobParameters = MakeQuantScaleParameters( 32768.0 );

    auto fetchOutputData = [&]( const std::string &uri ) -> outcome::result<std::vector<uint8_t>>
    {
        std::vector<uint8_t> blob;
        if ( uri == "stub://vald02-mac" )
        {
            for ( size_t j = 0; j < kChunkCount; ++j )
            {
                const auto &record = captureA.rawRecordsPerArtifact[0][j];
                blob.insert( blob.end(), record.quantizedBytes.begin(), record.quantizedBytes.end() );
            }
            return blob;
        }
        if ( uri == "stub://vald02-win" )
        {
            for ( size_t j = 0; j < kChunkCount; ++j )
            {
                const auto &record = captureB.rawRecordsPerArtifact[0][j];
                blob.insert( blob.end(), record.quantizedBytes.begin(), record.quantizedBytes.end() );
            }
            return blob;
        }
        return outcome::failure( std::make_error_code( std::errc::io_error ) );
    };

    ProcessingValidationCore validationCore;
    std::set<std::string>    invalidSubTaskIds;
    auto                     validate_res =
        validationCore.ValidateResults( subTasks, results, invalidSubTaskIds, &jobParameters, fetchOutputData );

    // Independent cross-check (D-02): directly extract chunk 10's two 256-byte slices and run
    // IsFloatChunkWithinTolerance directly -- a second, independent code path from ValidateResults,
    // proving this test's own byte extraction/slicing is correct regardless of ValidateResults' internal
    // uniform-division slicing.
    const std::vector<uint8_t> &sliceA = captureA.rawRecordsPerArtifact[0][10].quantizedBytes;
    const std::vector<uint8_t> &sliceB = captureB.rawRecordsPerArtifact[0][10].quantizedBytes;

    sgns::sgprocmanagerdiff::ElementDiffStats statsOut;
    const bool                                directWithinTolerance =
        sgns::sgprocmanagerdiff::IsFloatChunkWithinTolerance( sliceA, sliceB, &jobParameters, statsOut );

    ASSERT_DOUBLE_EQ( 3.0517578125e-05, statsOut.maxAbsDelta );
    // D-03 bound: 2.0 / 32768.0 = 6.103515625e-05 -- chunk 10's real measured delta sits within it.
    ASSERT_TRUE( directWithinTolerance );

    // ValidateResults' own full-fixture outcome (D-04): chunks 0-9/11-14 hash-match trivially (Test 1's
    // self-check, no fallback invoked); chunk 10's tolerance fallback is expected to engage and resolve
    // it as a match given the direct cross-check above -- so the overall outcome is expected to be
    // success with no invalidated subtasks. If the actual run contradicts this, do not force the
    // assertion to pass -- investigate whether it is a harness bug (per D-04's narrow exception) before
    // concluding the gap is genuinely still open.
    ASSERT_FALSE( validate_res.has_error() );
    ASSERT_TRUE( invalidSubTaskIds.empty() );
}
