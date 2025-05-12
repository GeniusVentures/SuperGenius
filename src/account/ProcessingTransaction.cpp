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
    ProcessingTransaction::ProcessingTransaction( std::string              job_id,
                                                  std::vector<std::string> subtask_ids,
                                                  std::vector<std::string> node_addresses,
                                                  SGTransaction::DAGStruct dag ) :

        IGeniusTransactions( "process", SetDAGWithType( std::move( dag ), "process" ) ), //
        job_id_( std::move( job_id ) ),
        subtask_ids_( std::move( subtask_ids ) ),
        node_addresses_( std::move( node_addresses ) )
    {
        auto hasher_   = std::make_shared<sgns::crypto::HasherImpl>();
        auto hash_data = hasher_->blake2b_256( SerializeByteVector() );
        dag_st.set_data_hash( hash_data.toReadableString() );
        hash_data = hasher_->blake2b_256( std::vector<uint8_t>{ job_id.begin(), job_id.end() } );
        job_hash_ = uint256_t{ "0x" + hash_data.toReadableString() };
    }

    ProcessingTransaction ProcessingTransaction::New( std::string                                     job_id,
                                                      std::vector<std::string>                        subtask_ids,
                                                      std::vector<std::string>                        node_addresses,
                                                      SGTransaction::DAGStruct                        dag,
                                                      std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key )
    {
        ProcessingTransaction instance( std::move( job_id ),
                                        std::move( subtask_ids ),
                                        std::move( node_addresses ),
                                        std::move( dag ) );
        instance.FillHash();
        instance.MakeSignature( std::move( eth_key ) );
        return instance;
    }

    std::vector<uint8_t> ProcessingTransaction::SerializeByteVector()
    {
        SGTransaction::ProcessingTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        tx_struct.set_mpc_magic_key( 0 );
        tx_struct.set_offset( 0 );
        tx_struct.set_job_cid( job_id_ );
        for ( const auto &str : subtask_ids_ )
        {
            tx_struct.add_subtask_cids( str );
        }
        for ( const auto &str : node_addresses_ )
        {
            tx_struct.add_node_addresses( str );
        }
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );
        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }

    std::shared_ptr<ProcessingTransaction> ProcessingTransaction::DeSerializeByteVector(
        const std::vector<uint8_t> &data )
    {
        SGTransaction::ProcessingTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse TransferTx from array.\n";
        }

        std::vector<std::string> subtask_ids( tx_struct.subtask_cids().begin(), tx_struct.subtask_cids().end() );
        std::vector<std::string> node_addresses( tx_struct.node_addresses().begin(), tx_struct.node_addresses().end() );

        return std::make_shared<ProcessingTransaction>(
            ProcessingTransaction( tx_struct.job_cid(), subtask_ids, node_addresses, tx_struct.dag_struct() ) );
    }
}
