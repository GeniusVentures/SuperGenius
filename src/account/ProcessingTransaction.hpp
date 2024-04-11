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
        ProcessingTransaction( uint256_t hash );
        ~ProcessingTransaction() = default;

        std::vector<uint8_t>         SerializeByteVector() override;
        static ProcessingTransaction DeSerializeByteVector( const std::vector<uint8_t> &data );

    private:
        //std::string          job_id;            ///< Job ID
        //std::string          subtask_id;        ///< SubTask ID
        uint256_t hash_process_data; ///< Hash of the process data
        //std::vector<uint8_t> raw_data;          ///<The data being processed
    };
}

#endif
