/**
 * @file       ProcessingTransaction.hpp
 * @brief      Transaction of processing data
 * @date       2024-03-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef _PROCESSING_TRANSACTION_HPP_
#define _PROCESSING_TRANSACTION_HPP_
#include <string>

#include <boost/multiprecision/cpp_int.hpp>

#include "account/IGeniusTransactions.hpp"

namespace sgns
{
    using namespace boost::multiprecision;

    class ProcessingTransaction : public IGeniusTransactions
    {
    public:
        static std::shared_ptr<ProcessingTransaction> DeSerializeByteVector( const std::vector<uint8_t> &data );

        static ProcessingTransaction New( std::string              job_id,
                                          std::vector<std::string> subtask_ids,
                                          std::vector<std::string> node_addresses,
                                          SGTransaction::DAGStruct dag,
                                          std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key );

        ~ProcessingTransaction() override = default;

        std::vector<uint8_t> SerializeByteVector() override;

        uint256_t GetJobHash() const
        {
            return job_hash_;
        }

        std::vector<std::string> GetSubtaskIDs() const
        {
            return subtask_ids_;
        }
        std::vector<std::string> GetNodeAddresses() const
        {
            return node_addresses_;
        }

        std::string GetTaskID() const
        {
            return job_id_;
        }

        std::string GetTransactionSpecificPath() override
        {
            boost::format processing_fmt( GetType() + "/%s" );

            processing_fmt % job_id_;
            return processing_fmt.str();
        }

    private:
        ProcessingTransaction( std::string              job_id,
                               std::vector<std::string> subtask_ids,
                               std::vector<std::string> node_addresses,
                               SGTransaction::DAGStruct dag );

        std::string              job_id_;         ///< Job ID
        uint256_t                job_hash_;       ///< Job ID
        std::vector<std::string> subtask_ids_;    ///< SubTask ID
        std::vector<std::string> node_addresses_; ///< Addresses/ID of processors
        //uint256_t   hash_process_data; ///< Hash of the process data
        //std::vector<uint8_t> raw_data;          ///<The data being processed

        /**
         * @brief       Registers the deserializer for the transfer transaction type.
         * @return      A boolean indicating successful registration.
         */
        static inline bool Register()
        {
            RegisterDeserializer( "process", &ProcessingTransaction::DeSerializeByteVector );
            return true;
        }

        /** 
         * @brief       Static variable to ensure registration happens on inclusion of header file.
         */
        static inline bool registered = Register();
    };
}

#endif
