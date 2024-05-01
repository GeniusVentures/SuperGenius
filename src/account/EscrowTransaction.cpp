/**
 * @file       EscrowTransaction.cpp
 * @brief      
 * @date       2024-04-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/EscrowTransaction.hpp"

namespace sgns
{
    EscrowTransaction::EscrowTransaction( const UTXOTxParameters &params, const uint64_t &num_chunks, const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "escrow", SetDAGWithType( dag, "escrow" ) ), //
        utxo_params_( params ),                                           //
        num_chunks_( num_chunks )                                         //
    {
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
        uint64_t num_chunks = tx_struct.num_chunks();
        return EscrowTransaction( UTXOTxParameters{ inputs, outputs }, num_chunks, tx_struct.dag_struct() ); // Return new instance
    }
    uint64_t EscrowTransaction::GetNumChunks() const
    {
        return num_chunks_;
    }
}