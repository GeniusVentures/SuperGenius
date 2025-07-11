/**
 * @file       TransferTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "TransferTransaction.hpp"

#include "crypto/hasher/hasher_impl.hpp"
#include "base/blob.hpp"

namespace sgns
{
    TransferTransaction::TransferTransaction( std::vector<OutputDestInfo> destinations,
                                              std::vector<InputUTXOInfo>  inputs,
                                              SGTransaction::DAGStruct    dag ) :
        IGeniusTransactions( "transfer", SetDAGWithType( std::move( dag ), "transfer" ) ), //
        input_tx_( std::move( inputs ) ),                                                  //
        outputs_( std::move( destinations ) )                                              //
    {
    }

    TransferTransaction TransferTransaction::New( std::vector<OutputDestInfo>                     destinations,
                                                  std::vector<InputUTXOInfo>                      inputs,
                                                  SGTransaction::DAGStruct                        dag,
                                                  std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key )
    {
        TransferTransaction instance( std::move( destinations ), std::move( inputs ), std::move( dag ) );

        instance.FillHash();
        instance.MakeSignature( std::move( eth_key ) );
        return instance;
    }

    std::vector<uint8_t> TransferTransaction::SerializeByteVector()
    {
        SGTransaction::TransferTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();

        for ( const auto &input : input_tx_ )
        {
            SGTransaction::TransferUTXOInput *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( input.txid_hash_.toReadableString() );
            input_proto->set_output_index( input.output_idx_ );
            input_proto->set_signature( input.signature_ );
        }
        for ( const auto &output : outputs_ )
        {
            SGTransaction::TransferOutput *output_proto = utxo_proto_params->add_outputs();
            output_proto->set_encrypted_amount( output.encrypted_amount );
            output_proto->set_dest_addr( output.dest_address );
            output_proto->set_token_id( output.token_id.bytes().data(), output.token_id.size() );
        }
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }

    std::shared_ptr<TransferTransaction> TransferTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::TransferTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse TransferTx from array." << std::endl;
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

            OutputDestInfo curr{ output_proto.encrypted_amount(),
                                 output_proto.dest_addr(),
                                 TokenID::FromBytes( output_proto.token_id().data(), output_proto.token_id().size() ) };
            outputs.push_back( curr );
        }

        return std::make_shared<TransferTransaction>( TransferTransaction( outputs, inputs, tx_struct.dag_struct() ) );
    }

    std::vector<OutputDestInfo> TransferTransaction::GetDstInfos() const
    {
        return outputs_;
    }

    std::vector<InputUTXOInfo> TransferTransaction::GetInputInfos() const
    {
        return input_tx_;
    }

}
