/**
 * @file       EscrowTransaction.cpp
 * @brief      
 * @date       2024-04-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/EscrowTransaction.hpp"

#include <utility>

#include "crypto/hasher/hasher_impl.hpp"
#include "base/blob.hpp"

namespace sgns
{
    EscrowTransaction::EscrowTransaction( UTXOTxParameters                params,
                                          uint64_t                        num_chunks,
                                          uint256_t                       dest_addr,
                                          float                           cut,
                                          const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "escrow", SetDAGWithType( dag, "escrow" ) ),
        num_chunks_( num_chunks ),
        dev_addr( std::move( dest_addr ) ),
        dev_cut( cut ),
        utxo_params_( std::move( params ) )
    {
        auto hasher_ = std::make_shared<sgns::crypto::HasherImpl>();
        auto hash    = hasher_->blake2b_256( SerializeByteVector() );
        dag_st.set_data_hash( hash.toReadableString() );
    }

    std::vector<uint8_t> EscrowTransaction::SerializeByteVector()
    {
        SGTransaction::EscrowTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();

        for ( const auto &input : utxo_params_.inputs_ )
        {
            SGTransaction::TransferUTXOInput *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( input.txid_hash_.toReadableString() );
            input_proto->set_output_index( input.output_idx_ );
            input_proto->set_signature( input.signature_ );
        }
        for ( const auto &output : utxo_params_.outputs_ )
        {
            SGTransaction::TransferOutput *output_proto = utxo_proto_params->add_outputs();
            output_proto->set_encrypted_amount( output.encrypted_amount.str() );
            output_proto->set_dest_addr( output.dest_address.str() );
        }
        tx_struct.set_num_chunks( num_chunks_ );
        tx_struct.set_dev_addr( dev_addr.str() );
        tx_struct.set_dev_cut( dev_cut );
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }

    EscrowTransaction EscrowTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::EscrowTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse EscrowTx from array." << std::endl;
        }
        std::vector<InputUTXOInfo>   inputs;
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( int i = 0; i < utxo_proto_params->inputs_size(); ++i )
        {
            const SGTransaction::TransferUTXOInput &input_proto = utxo_proto_params->inputs( i );

            InputUTXOInfo curr;
            curr.txid_hash_  = ( base::Hash256::fromReadableString( input_proto.tx_id_hash() ) ).value();
            curr.output_idx_ = input_proto.output_index();
            curr.signature_  = input_proto.signature();
            inputs.push_back( curr );
        }
        std::vector<OutputDestInfo> outputs;
        for ( int i = 0; i < utxo_proto_params->outputs_size(); ++i )
        {
            const SGTransaction::TransferOutput &output_proto = utxo_proto_params->outputs( i );

            OutputDestInfo curr{ uint256_t{ output_proto.encrypted_amount() }, uint256_t{ output_proto.dest_addr() } };
            outputs.push_back( curr );
        }
        uint64_t  num_chunks = tx_struct.num_chunks();
        float     dev_cut    = tx_struct.dev_cut();
        uint256_t dev_addr( tx_struct.dev_addr() );
        return { UTXOTxParameters{ inputs, outputs }, num_chunks, dev_addr, dev_cut, tx_struct.dag_struct() };
    }

    uint64_t EscrowTransaction::GetNumChunks() const
    {
        return num_chunks_;
    }
}
