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

#include "outcome/outcome.hpp"
#include "base/logger.hpp"
#include "processing/proto/SGProcessing.pb.h"

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
            NO_RESULTS_FOR_SUBTASK = 0,  ///< a subtask had no result entry
            WRONG_RESULT_HASHES_LENGTH,  ///< number of hashes != number of chunks
            DUPLICATE_CHUNK_RESULT_HASH, ///< duplicate hash inside a result
            EMPTY_CHUNK_RESULT_HASH,     ///< empty hash seen
            MISSING_CHUNK_RESULT,        ///< a chunk from subtask is missing from results map
            INVALID_CHUNK_RESULT_HASH,   ///< conflicting/invalid chunk result hash observed
            SUBTASK_ID_MISMATCH,         ///< subtask id != result id (individual check)
            INVALID_RESULTS_BATCH,       ///< aggregated invalidities across subtasks
            INVALID_PAYOUT_METADATA      ///< peer/developer payout metadata missing or out of range
        };

        /// Scale of SubTaskResult::developer_cut; 1'000'000 == 100%. Mirrors SGProcessing.proto.
        static constexpr uint64_t DEVELOPER_CUT_SCALE = 1000000;

        /// Exact length of SubTaskResult::token_id in bytes.
        static constexpr size_t TOKEN_ID_BYTES = 32;

        ProcessingValidationCore() = default;

        /** Checks if check result hashes are valid.
        * If invalid chunk hashes found corresponding subtasks are invalidated and returned to processing queue
        * @return true if all chunk results are valid
        */
        outcome::result<void> ValidateResults( const SGProcessing::SubTaskCollection                    &subTasks,
                                               const std::map<std::string, SGProcessing::SubTaskResult> &results,
                                               std::set<std::string> &invalidSubTaskIds );

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
         * @brief       Checks the result hashes for a given subtask against the provided chunks
         * @param[in]   subTask The subtask whose results are to be checked
         * @param[in]   chunks A map of chunk identifiers to their corresponding byte vectors
         * @return      A result indicating success or failure of the hash checks
         */
        outcome::result<void> CheckSubTaskResultHashes(
            const SGProcessing::SubTask                       &subTask,
            const std::map<std::string, std::vector<uint8_t>> &chunks ) const;

        /// Logger instance for logging within the ProcessingValidationCore class
        base::Logger m_logger = base::createLogger( "ProcessingValidationCore" );
    };
}

/**
 * @brief       Macro for declaring error handling in the IBasicProof class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::processing, ProcessingValidationCore::Error );

#endif // SUPERGENIUS_PROCESSING_VALIDATION_CORE_HPP
