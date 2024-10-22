/**
 * @file       ProcessingTransaction.hpp
 * @brief      Transaction of processing data
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _PROCESSING_TRANSACTION_HPP_
#define _PROCESSING_TRANSACTION_HPP_
#include <string>
#include "account/IGeniusTransactions.hpp"
#include <boost/multiprecision/cpp_int.hpp>

using namespace boost::multiprecision;

namespace sgns
{

    class ProcessingTransaction : public IGeniusTransactions
    {
    public:
        ProcessingTransaction( const std::string &job_id, const std::string &subtask_id, const SGTransaction::DAGStruct &dag );
        ~ProcessingTransaction() override = default;

        std::vector<uint8_t>         SerializeByteVector() override;
        static ProcessingTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );

        uint256_t GetJobHash() const
        {
            return job_hash_;
        }
        std::string GetSubtaskID() const
        {
            return subtask_id_;
        }

        std::string GetTaskID() const
        {
            return subtask_id_;
        }

        std::string GetTransactionSpecificPath() override
        {
            boost::format processing_fmt( GetType() + "/%s/%s" );

            processing_fmt % job_id_ % subtask_id_;
            return processing_fmt.str();
        }

    private:
        std::string job_id_;            ///< Job ID
        uint256_t   job_hash_;          ///< Job ID
        std::string subtask_id_;        ///< SubTask ID
        std::string chunk_id_;        ///< SubTask ID
        //uint256_t   hash_process_data; ///< Hash of the process data
        //std::vector<uint8_t> raw_data;          ///<The data being processed
    };
}

#endif
