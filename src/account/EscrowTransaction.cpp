/**
 * @file       EscrowTransaction.cpp
 * @brief      
 * @date       2024-04-24
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/EscrowTransaction.hpp"

#include <utility>

#include "base/blob.hpp"

namespace sgns
{
    EscrowTransaction::EscrowTransaction( UTXOTxParameters         params,
                                          uint64_t                 amount,
                                          SGTransaction::DAGStruct dag ) :
        GeniusTransaction( "escrow-hold", SetDAGWithType( std::move( dag ), "escrow-hold" ) ),
        utxo_params_( std::move( params ) ),
        amount_( amount )
    {
    }

    EscrowTransaction EscrowTransaction::New( UTXOTxParameters         params,
                                              uint64_t                 amount,
                                              SGTransaction::DAGStruct dag )
    {
        EscrowTransaction instance( std::move( params ), amount, std::move( dag ) );
        instance.FillHash();
        return instance;
    }

    std::vector<uint8_t> EscrowTransaction::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::EscrowTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();

        for ( const auto &[txid_hash_, output_idx_, signature_] : utxo_params_.first )
        {
            SGTransaction::TransferUTXOInput *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( txid_hash_.toReadableString() );
            input_proto->set_output_index( output_idx_ );
            input_proto->set_signature( signature_.data(), signature_.size() );
        }
        for ( const auto &[encrypted_amount, dest_address, token_id] : utxo_params_.second )
        {
            SGTransaction::TransferOutput *output_proto = utxo_proto_params->add_outputs();
            output_proto->set_encrypted_amount( encrypted_amount );
            output_proto->set_dest_addr( dest_address );
            output_proto->set_token_id( token_id.bytes().data(), token_id.size() );
        }
        tx_struct.set_amount( amount_ );
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            std::cerr << "Failed to serialize transaction\n";
        }

        return serialized_proto;
    }

    EmbeddedTransaction EscrowTransaction::SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        SGTransaction::EscrowTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();

        for ( const auto &[txid_hash_, output_idx_, signature_] : utxo_params_.first )
        {
            SGTransaction::TransferUTXOInput *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( txid_hash_.toReadableString() );
            input_proto->set_output_index( output_idx_ );
            input_proto->set_signature( signature_.data(), signature_.size() );
        }
        for ( const auto &[encrypted_amount, dest_address, token_id] : utxo_params_.second )
        {
            SGTransaction::TransferOutput *output_proto = utxo_proto_params->add_outputs();
            output_proto->set_encrypted_amount( encrypted_amount );
            output_proto->set_dest_addr( dest_address );
            output_proto->set_token_id( token_id.bytes().data(), token_id.size() );
        }
        tx_struct.set_amount( amount_ );
        *embedded.mutable_escrow() = tx_struct;
        return embedded;
    }

    std::shared_ptr<EscrowTransaction> EscrowTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::EscrowTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse EscrowTx from array." << std::endl;
            return nullptr;
        }
        std::vector<InputUTXOInfo>   inputs;
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( int i = 0; i < utxo_proto_params->inputs_size(); ++i )
        {
            const SGTransaction::TransferUTXOInput &input_proto = utxo_proto_params->inputs( i );

            InputUTXOInfo curr;
            curr.txid_hash_  = ( base::Hash256::fromReadableString( input_proto.tx_id_hash() ) ).value();
            curr.output_idx_ = input_proto.output_index();
            curr.signature_  = std::vector<uint8_t>( input_proto.signature().cbegin(), input_proto.signature().cend() );
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
        return std::make_shared<EscrowTransaction>(
            EscrowTransaction( UTXOTxParameters{ inputs, outputs }, tx_struct.amount(), tx_struct.dag_struct() ) );
    }
}
