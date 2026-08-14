/**
 * @file       processing_validation_core_test.cpp
 * @brief      Live CTest coverage for ProcessingValidationCore::ValidateResults (Phase 15, XNODE-01b/XNODE-02)
 */

#include <gtest/gtest.h>

#include "processing/processing_validation_core.hpp"

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
