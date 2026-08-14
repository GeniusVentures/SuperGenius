/**
 * @file       processing_validation_core.cpp
 * @brief      Source file of core implementation of processing task results validation
 * @date       2022-05-08
 * @author     creativeid00
 * @note       This was mostly rewritten by Henrique A. Klein (hklein@gnus.ai) and Justin Church (jchurch@gnus.ai)
 */

#include <optional>
#include <unordered_set>
#include "processing_validation_core.hpp"
#include "processing/processing_subtask_queue.hpp"
#include "processing/processing_subtask_queue_channel.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::processing, ProcessingValidationCore::Error, e )
{
    using ValidationError = sgns::processing::ProcessingValidationCore::Error;
    switch ( e )
    {
        case ValidationError::NO_RESULTS_FOR_SUBTASK:
            return "Subtask was finalized with no results";
        case ValidationError::WRONG_RESULT_HASHES_LENGTH:
            return "The hashes length doesn't match the chunks to process length";
        case ValidationError::DUPLICATE_CHUNK_RESULT_HASH:
            return "A duplicate chunk result hash was found";
        case ValidationError::EMPTY_CHUNK_RESULT_HASH:
            return "Empty chunk result hash was found";
        case ValidationError::MISSING_CHUNK_RESULT:
            return "Missing chunk result found";
        case ValidationError::INVALID_CHUNK_RESULT_HASH:
            return "The chunk result hash is invalid";
        case ValidationError::SUBTASK_ID_MISMATCH:
            return "The subtask id doesn't match the result id";
        case ValidationError::INVALID_RESULTS_BATCH:
            return "The results batch is invalid";
        case ValidationError::CHUNK_HASH_MISMATCH_UNTOLERATED:
            return "Chunk hash mismatch could not be resolved as a tolerant match";
    }
    return "Unknown error";
}

namespace sgns::processing
{

    ProcessingValidationCore::ProcessingValidationCore() {}

    outcome::result<void> ProcessingValidationCore::ValidateResults(
        const SGProcessing::SubTaskCollection                                            &subTasks,
        const std::map<std::string, SGProcessing::SubTaskResult>                         &results,
        std::set<std::string>                                                             &invalidSubTaskIds,
        const std::vector<sgns::Parameter>                                               *jobParameters,
        std::function<outcome::result<std::vector<uint8_t>>( const std::string &outputUri )> fetchOutputData )
    {

        std::optional<std::error_code> error;

        // Compare result hashes for each chunk
        // If a chunk hashes didn't match each other add the all subtasks with invalid hashes to VALID ITEMS LIST
        // Keyed on chunkKey -> {subtaskId -> ChunkContribution}: each subtask's contribution stays
        // independently addressable (fixes the old concatenation bug, which appended every contributing
        // subtask's chunk-hash bytes onto one shared buffer instead of comparing them).
        std::map<std::string, std::map<std::string, ChunkContribution>> chunksBySubtask;

        for ( int itemIdx = 0; itemIdx < subTasks.items_size(); ++itemIdx )
        {
            const auto &subTask  = subTasks.items( itemIdx );
            auto        itResult = results.find( subTask.subtaskid() );
            if ( itResult != results.end() )
            {
                if ( itResult->second.chunk_hashes_size() != subTask.chunkstoprocess_size() )
                {
                    m_logger->error( "WRONG_RESULT_HASHES_LENGTH {}: {} {}",
                                     subTask.subtaskid(),
                                     itResult->second.chunk_hashes_size(),
                                     subTask.chunkstoprocess_size() );
                    invalidSubTaskIds.insert( subTask.subtaskid() );
                    if ( !error )
                    {
                        error = make_error_code(Error::WRONG_RESULT_HASHES_LENGTH);
                    }
                }
                else
                {
                    for ( int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx )
                    {
                        chunksBySubtask[subTask.chunkstoprocess( chunkIdx ).SerializeAsString()][subTask.subtaskid()] =
                            ChunkContribution{ itResult->second.chunk_hashes( chunkIdx ),
                                               chunkIdx,
                                               subTask.chunkstoprocess_size() };
                    }
                }
            }
            else
            {
                // Since all subtasks are processed a result should be found for all of them
                m_logger->error( "NO_RESULTS_FOUND {} on ", subTask.subtaskid() );
                invalidSubTaskIds.insert( subTask.subtaskid() );
                if ( !error )
                {
                    error = make_error_code(Error::NO_RESULTS_FOR_SUBTASK);
                }
            }
        }

        // Genuine cross-subtask comparison pass: for each chunk key contributed to by 2+ subtasks, either
        // every contribution agrees (trivial match) or a real mismatch exists that must be resolved --
        // via the tolerance fallback (XNODE-02) -- before being declared a genuine divergence (XNODE-01b).
        // Runs before the per-subtask CheckSubTaskResultHashes loop below so an already-invalidated
        // subtask (NO_RESULTS_FOR_SUBTASK/WRONG_RESULT_HASHES_LENGTH) is not double-processed there.
        for ( const auto &[chunkKey, contributions] : chunksBySubtask )
        {
            if ( contributions.size() < 2 )
            {
                continue; // nothing to compare -- only one subtask contributed this chunk
            }

            const auto &firstHash = contributions.begin()->second.hashBytes;
            bool        allMatch  = true;
            for ( const auto &[subtaskId, contribution] : contributions )
            {
                if ( contribution.hashBytes != firstHash )
                {
                    allMatch = false;
                    break;
                }
            }

            if ( allMatch )
            {
                continue; // genuine match (SC2)
            }

            // Genuine hash mismatch -- the exact case the old concatenation bug silently missed.
            if ( !AttemptToleranceFallback( contributions, results, jobParameters, fetchOutputData ) )
            {
                for ( const auto &[subtaskId, contribution] : contributions )
                {
                    invalidSubTaskIds.insert( subtaskId );
                }
                if ( !error )
                {
                    error = make_error_code( Error::CHUNK_HASH_MISMATCH_UNTOLERATED );
                }
            }
        }

        for ( int itemIdx = 0; itemIdx < subTasks.items_size(); ++itemIdx )
        {
            const auto &subTask = subTasks.items( itemIdx );
            if ( invalidSubTaskIds.find( subTask.subtaskid() ) != invalidSubTaskIds.end() )
            {
                m_logger->trace( "Subtask already invalid {}, no need to check chunk hashes ", subTask.subtaskid() );
                continue;
            }

            auto subtaskCheck = CheckSubTaskResultHashes( subTask, chunksBySubtask );
            if ( subtaskCheck.has_failure() )
            {
                invalidSubTaskIds.insert( subTask.subtaskid() );
                if ( !error )
                {
                    error = subtaskCheck.error();
                }
            }
        }

        if ( error )
        {
            return outcome::failure( *error );
        }
        return outcome::success();
    }

    outcome::result<void> ProcessingValidationCore::ValidateIndividualResult(
        const SGProcessing::SubTask       &subTask,
        const SGProcessing::SubTaskResult &result ) const
    {
        // Check 1: Verify subtask IDs match
        if ( subTask.subtaskid() != result.subtaskid() )
        {
            m_logger->error( "SUBTASK_ID_MISMATCH: expected {}, got {}", subTask.subtaskid(), result.subtaskid() );
            return outcome::failure( Error::SUBTASK_ID_MISMATCH );
        }

        // Check 2: Verify hash count matches chunk count
        if ( result.chunk_hashes_size() != subTask.chunkstoprocess_size() )
        {
            m_logger->error( "WRONG_RESULT_HASHES_LENGTH {}: {} {}",
                             subTask.subtaskid(),
                             result.chunk_hashes_size(),
                             subTask.chunkstoprocess_size() );
            return outcome::failure( Error::WRONG_RESULT_HASHES_LENGTH );
        }

        // Check 3: Verify no duplicate hashes
        std::unordered_set<std::string> encounteredHashes;
        for ( int chunkIdx = 0; chunkIdx < result.chunk_hashes_size(); ++chunkIdx )
        {
            const std::string &chunkHash = result.chunk_hashes( chunkIdx );

            if ( !encounteredHashes.insert( chunkHash ).second )
            {
                const auto &chunk = subTask.chunkstoprocess( chunkIdx );
                m_logger->error( "DUPLICATE_CHUNK_RESULT_HASH [{}, {}]", subTask.subtaskid(), chunk.chunkid() );
                return outcome::failure( Error::DUPLICATE_CHUNK_RESULT_HASH );
            }

            // Check 4: Verify hash is not empty
            if ( chunkHash.empty() )
            {
                const auto &chunk = subTask.chunkstoprocess( chunkIdx );
                m_logger->error( "EMPTY_CHUNK_RESULT_HASH [{}, {}]", subTask.subtaskid(), chunk.chunkid() );
                return outcome::failure( Error::EMPTY_CHUNK_RESULT_HASH );
            }
        }

        return outcome::success();
    }

    outcome::result<void> ProcessingValidationCore::CheckSubTaskResultHashes(
        const SGProcessing::SubTask                                            &subTask,
        const std::map<std::string, std::map<std::string, ChunkContribution>> &chunksBySubtask ) const
    {
        std::unordered_set<std::string> encounteredHashes;
        for ( int chunkIdx = 0; chunkIdx < subTask.chunkstoprocess_size(); ++chunkIdx )
        {
            const auto &chunk = subTask.chunkstoprocess( chunkIdx );
            auto        it    = chunksBySubtask.find( chunk.SerializeAsString() );
            if ( it != chunksBySubtask.end() )
            {
                auto contributionIt = it->second.find( subTask.subtaskid() );
                if ( contributionIt == it->second.end() )
                {
                    // Should not happen -- this subtask's own contribution was just inserted in the
                    // build loop above -- but guard defensively rather than dereference blindly.
                    m_logger->error( "NO_CHUNK_RESULT_FOUND [{}, {}]", subTask.subtaskid(), chunk.chunkid() );
                    return outcome::failure( Error::MISSING_CHUNK_RESULT );
                }

                const std::string &chunkHash = contributionIt->second.hashBytes;
                if ( !encounteredHashes.insert( chunkHash ).second )
                {
                    m_logger->error( "INVALID_CHUNK_RESULT_HASH [{}, {}]", subTask.subtaskid(), chunk.chunkid() );
                    return outcome::failure( Error::INVALID_CHUNK_RESULT_HASH );
                }
            }
            else
            {
                m_logger->error( "NO_CHUNK_RESULT_FOUND [{}, {}]", subTask.subtaskid(), chunk.chunkid() );
                return outcome::failure( Error::MISSING_CHUNK_RESULT );
            }
        }
        return outcome::success();
    }

    bool ProcessingValidationCore::AttemptToleranceFallback(
        const std::map<std::string, ChunkContribution>                                  &contributions,
        const std::map<std::string, SGProcessing::SubTaskResult>                         &results,
        const std::vector<sgns::Parameter>                                               *jobParameters,
        const std::function<outcome::result<std::vector<uint8_t>>( const std::string & )> &fetchOutputData ) const
    {
        // Task 2 implements the real fetch+slice+diff logic (XNODE-02). Until then, this stub keeps
        // Task 1 a complete, independently-correct fix for XNODE-01b: any genuine hash mismatch fails
        // closed (matches pre-XNODE-02 behavior) even before the tolerance layer exists.
        (void)contributions;
        (void)results;
        (void)jobParameters;
        (void)fetchOutputData;
        return false;
    }

}

