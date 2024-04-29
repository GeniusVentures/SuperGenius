/**
 * @file       TransferTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/TransferTransaction.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "base/blob.hpp"

namespace sgns
{
    TransferTransaction::TransferTransaction( const std::vector<OutputDestInfo> &destinations, const std::vector<InputUTXOInfo> &inputs,
                                              const SGTransaction::DAGStruct &dag ) :
        IGeniusTransactions( "transfer", SetDAGWithType( dag, "transfer" ) ), //
        input_tx_( inputs ),                                                  //
        outputs_( destinations )                                              //
    {
        auto hasher_ = std::make_shared<sgns::crypto::HasherImpl>();
        auto hash    = hasher_->blake2b_256( SerializeByteVector() );
        dag_st.set_data_hash( hash.toReadableString() );
    }
    std::vector<uint8_t> TransferTransaction::SerializeByteVector()
    {
        SGTransaction::TransferTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        tx_struct.set_token_id( 0 );

        for ( const auto &input : input_tx_ )
        {
            SGTransaction::TransferUTXOInput *input_proto = tx_struct.add_inputs();
            input_proto->set_tx_id_hash( input.txid_hash_.toReadableString() );
            input_proto->set_output_index( input.output_idx_ );
            input_proto->set_signature( input.signature_ );
        }
        for ( const auto &output : outputs_ )
        {
            SGTransaction::TransferOutput *output_proto = tx_struct.add_outputs();
            ;
            output_proto->set_encrypted_amount( output.encrypted_amount.str() );
            output_proto->set_dest_addr( output.dest_address.str() );
        }
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }
    TransferTransaction TransferTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {

        SGTransaction::TransferTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse TransferTx from array." << std::endl;
        }
        std::vector<InputUTXOInfo> inputs;
        for ( int i = 0; i < tx_struct.inputs_size(); ++i )
        {
            const SGTransaction::TransferUTXOInput &input_proto = tx_struct.inputs( i );

            InputUTXOInfo curr;
            curr.txid_hash_  = ( base::Hash256::fromReadableString( input_proto.tx_id_hash() ) ).value();
            curr.output_idx_ = input_proto.output_index();
            curr.signature_  = input_proto.signature();
            inputs.push_back( curr );
        }
        std::vector<OutputDestInfo> outputs;
        for ( int i = 0; i < tx_struct.outputs_size(); ++i )
        {
            const SGTransaction::TransferOutput &output_proto = tx_struct.outputs( i );

            OutputDestInfo curr{ uint256_t{ output_proto.encrypted_amount() }, uint256_t{ output_proto.dest_addr() } };
            outputs.push_back( curr );
        }

        return TransferTransaction( outputs, inputs, tx_struct.dag_struct() ); // Return new instance
    }

    const std::vector<OutputDestInfo> TransferTransaction::GetDstInfos() const
    {
        return outputs_;
    }
    const std::vector<InputUTXOInfo> TransferTransaction::GetInputInfos() const
    {
        return input_tx_;
    }

}