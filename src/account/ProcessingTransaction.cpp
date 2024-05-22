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
    ProcessingTransaction::ProcessingTransaction( const std::string &job_id, const std::string &subtask_id,
                                                  const SGTransaction::DAGStruct &dag ) :

        IGeniusTransactions( "process", SetDAGWithType( dag, "process" ) ), //
        job_id_( job_id ), subtask_id_( subtask_id )
    {
        auto hasher_   = std::make_shared<sgns::crypto::HasherImpl>();
        auto hash_data = hasher_->blake2b_256( SerializeByteVector() );
        dag_st.set_data_hash( hash_data.toReadableString() );
        hash_data = hasher_->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );
        job_hash_ = uint256_t{ "0x" + hash_data.toReadableString() };
    }

    std::vector<uint8_t> ProcessingTransaction::SerializeByteVector()
    {
        SGTransaction::ProcessingTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        tx_struct.set_mpc_magic_key( 0 );
        tx_struct.set_offset( 0 );
        tx_struct.set_job_cid( job_id_ );
        tx_struct.set_subtask_cid( subtask_id_ );
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );
        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }

    ProcessingTransaction ProcessingTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::ProcessingTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse TransferTx from array." << std::endl;
        }

        return { tx_struct.job_cid(), tx_struct.subtask_cid(), tx_struct.dag_struct() }; // Return new instance
    }
}
