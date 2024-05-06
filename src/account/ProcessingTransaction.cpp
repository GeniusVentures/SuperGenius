/**
 * @file       ProcessingTransaction.cpp
 * @brief      
 * @date       2024-04-11
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "account/ProcessingTransaction.hpp"

#include <utility>
#include "crypto/hasher/hasher_impl.hpp"

namespace sgns
{
    ProcessingTransaction::ProcessingTransaction( uint256_t hash, const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "processing", SetDAGWithType( dag, "processing" ) ), //
        hash_process_data( std::move( hash ) )
    {
        auto hasher_ = std::make_shared<sgns::crypto::HasherImpl>();
        auto hash_data = hasher_->blake2b_256(SerializeByteVector());
        dag_st.set_data_hash(hash_data.toReadableString());
    }

    std::vector<uint8_t> ProcessingTransaction::SerializeByteVector()
    {
        std::vector<uint8_t> serialized_class;
        export_bits( hash_process_data, std::back_inserter( serialized_class ), 8 );

        return serialized_class;
    }

    ProcessingTransaction ProcessingTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        uint256_t hash;
        import_bits( hash, data.begin(), data.end() );

        return { hash, {} }; // Return new instance
    }
}