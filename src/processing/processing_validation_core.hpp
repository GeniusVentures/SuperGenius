/**
 * @file       processing_validation_core.hpp
 * @brief      Header file of core implementation of processing task results validation
 * @date       2022-05-08
 * @author     creativeid00
 * @note       This was mostly rewritten by Henrique A. Klein (hklein@gnus.ai) and Justin Church (jchurch@gnus.ai)
 */

#ifndef SUPERGENIUS_PROCESSING_VALIDATION_CORE_HPP
#define SUPERGENIUS_PROCESSING_VALIDATION_CORE_HPP

#include <map>
#include <string>
#include <set>
#include <vector>
#include <cstdint>
#include <functional>

#include "outcome/outcome.hpp"
#include "base/logger.hpp"
#include "processing/proto/SGProcessing.pb.h"
#include "Parameter.hpp"

namespace sgns::processing
{
    class ProcessingValidationCore
    {
    public:
        /**
         * @brief      Enumeration of error codes used in the processing validation class.
         */
        enum class Error
        {
            NO_RESULTS_FOR_SUBTASK = 0,     ///< a subtask had no result entry
            WRONG_RESULT_HASHES_LENGTH,     ///< number of hashes != number of chunks
            DUPLICATE_CHUNK_RESULT_HASH,    ///< duplicate hash inside a result
            EMPTY_CHUNK_RESULT_HASH,        ///< empty hash seen
            MISSING_CHUNK_RESULT,           ///< a chunk from subtask is missing from results map
            INVALID_CHUNK_RESULT_HASH,      ///< conflicting/invalid chunk result hash observed
            SUBTASK_ID_MISMATCH,            ///< subtask id != result id (individual check)
            INVALID_RESULTS_BATCH,          ///< aggregated invalidities across subtasks
            CHUNK_HASH_MISMATCH_UNTOLERATED, ///< a genuine cross-subtask chunk hash mismatch that the tolerance fallback could not resolve
            INVALID_PAYOUT_METADATA          ///< peer/developer payout metadata missing or out of range
        };

        /// Scale of SubTaskResult::developer_cut; 1'000'000 == 100%. Mirrors SGProcessing.proto.
        static constexpr uint64_t DEVELOPER_CUT_SCALE = 1000000;

        /// Exact length of SubTaskResult::token_id in bytes.
        static constexpr size_t TOKEN_ID_BYTES = 32;

        ProcessingValidationCore() = default;

        /** Checks if check result hashes are valid.
        * If invalid chunk hashes found corresponding subtasks are invalidated and returned to processing queue
        * @param[in]     subTasks           The full subtask collection for this job
        * @param[in]     results            Map of subtaskId -> SubTaskResult received so far
        * @param[out]    invalidSubTaskIds  Populated with subtask ids found to be invalid
        * @param[in]     jobParameters      Job schema's generic parameters array (for D-03/D-04 tolerance
        *                                   derivation), or nullptr if unavailable -- defaulted so existing
        *                                   call sites keep compiling unchanged
        * @param[in]     fetchOutputData    Capability to fetch a subtask's full output blob by URI
        *                                   (D-01's ipfs_results_data_id-keyed fetch), or nullptr to skip
        *                                   the tolerance fallback entirely (fail-closed on mismatch,
        *                                   matching pre-XNODE-02 behavior) -- defaulted so existing call
        *                                   sites keep compiling unchanged
        * @return true if all chunk results are valid
        */
        outcome::result<void> ValidateResults(
            const SGProcessing::SubTaskCollection                    &subTasks,
            const std::map<std::string, SGProcessing::SubTaskResult> &results,
            std::set<std::string>                                    &invalidSubTaskIds,
            const std::vector<sgns::Parameter>                       *jobParameters   = nullptr,
            std::function<outcome::result<std::vector<uint8_t>>( const std::string &outputUri )> fetchOutputData =
                nullptr );

        /**
         * Validates a single subtask result against its corresponding subtask
         * @param subTask The subtask definition
         * @param result The result to validate
         * @return true if the result is valid for the given subtask
         */
        outcome::result<void> ValidateIndividualResult( const SGProcessing::SubTask       &subTask,
                                                        const SGProcessing::SubTaskResult &result ) const;

    private:
        /**
         * @brief       One subtask's contribution of a chunk hash for a single shared ProcessingChunk key.
         *              Kept independently addressable per subtask (unlike the old flattened byte
         *              accumulator) so a genuine cross-subtask hash comparison is possible, and so the
         *              tolerance fallback can determine which byte range of a fetched output blob
         *              corresponds to this one chunk (chunkIdx/totalChunksForSubtask).
         */
        struct ChunkContribution
        {
            std::string hashBytes;
            int         chunkIdx               = 0;
            int         totalChunksForSubtask   = 0;
        };

        /**
         * @brief       Checks the result hashes for a given subtask against the provided chunks
         * @param[in]   subTask The subtask whose results are to be checked
         * @param[in]   chunksBySubtask A map of chunk identifiers to per-subtask hash contributions
         * @return      A result indicating success or failure of the hash checks
         */
        outcome::result<void> CheckSubTaskResultHashes(
            const SGProcessing::SubTask                                            &subTask,
            const std::map<std::string, std::map<std::string, ChunkContribution>>  &chunksBySubtask ) const;

        /**
         * @brief       Attempts to resolve a genuine chunk-hash mismatch as a tolerant match by fetching
         *              each contributing subtask's underlying output data and running a bounded numeric
         *              comparison (XNODE-02). Fails closed (returns false) whenever fetch/slice cannot be
         *              performed safely -- no fetch capability, empty ipfs_results_data_id, fetch failure,
         *              or an unsliceable multi-chunk blob size.
         * @param[in]   contributions    Per-subtask chunk-hash contributions for one shared chunk key
         * @param[in]   results          Full results map (to look up each contributing subtask's
         *                               ipfs_results_data_id)
         * @param[in]   jobParameters    Job schema's generic parameters array, or nullptr
         * @param[in]   fetchOutputData  Fetch capability, or nullptr
         * @return      true if the mismatch is within tolerance (treat as a match); false otherwise
         */
        bool AttemptToleranceFallback(
            const std::map<std::string, ChunkContribution>                                  &contributions,
            const std::map<std::string, SGProcessing::SubTaskResult>                         &results,
            const std::vector<sgns::Parameter>                                               *jobParameters,
            const std::function<outcome::result<std::vector<uint8_t>>( const std::string & )> &fetchOutputData )
            const;

        /// Logger instance for logging within the ProcessingValidationCore class
        base::Logger m_logger = base::createLogger( "ProcessingValidationCore" );
    };
}

/**
 * @brief       Macro for declaring error handling in the IBasicProof class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::processing, ProcessingValidationCore::Error );

#endif // SUPERGENIUS_PROCESSING_VALIDATION_CORE_HPP
