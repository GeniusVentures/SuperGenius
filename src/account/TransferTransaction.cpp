/**
 * @file       TransferTransaction.cpp
 * @brief      
 * @date       2024-04-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "TransferTransaction.hpp"

#include "base/blob.hpp"

namespace sgns
{
    TransferTransaction::TransferTransaction( std::vector<OutputDestInfo> destinations,
                                              std::vector<InputUTXOInfo>  inputs,
                                              SGTransaction::DAGStruct    dag ) :
        GeniusTransaction( "transfer", SetDAGWithType( std::move( dag ), "transfer" ) ), //
        input_tx_( std::move( inputs ) ),                                                  //
        outputs_( std::move( destinations ) )                                              //
    {
    }

    TransferTransaction TransferTransaction::New( std::vector<InputUTXOInfo>  inputs,
                                                  std::vector<OutputDestInfo> destinations,
                                                  SGTransaction::DAGStruct    dag )
    {
        TransferTransaction instance( std::move( destinations ), std::move( inputs ), std::move( dag ) );

        instance.FillHash();
        return instance;
    }

    std::vector<uint8_t> TransferTransaction::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::TransferTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();

        for ( const auto &[txid_hash_, output_idx_, signature_] : input_tx_ )
        {
            SGTransaction::TransferUTXOInput *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( txid_hash_.toReadableString() );
            input_proto->set_output_index( output_idx_ );
            input_proto->set_signature( signature_.data(), signature_.size() );
        }
        for ( const auto &[encrypted_amount, dest_address, token_id] : outputs_ )
        {
            SGTransaction::TransferOutput *output_proto = utxo_proto_params->add_outputs();
            output_proto->set_encrypted_amount( encrypted_amount );
            output_proto->set_dest_addr( dest_address );
            output_proto->set_token_id( token_id.bytes().data(), token_id.size() );
        }
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            std::cerr << "Failed to serialize transaction\n";
        }
        return serialized_proto;
    }

    EmbeddedTransaction TransferTransaction::SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        SGTransaction::TransferTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();

        for ( const auto &[txid_hash_, output_idx_, signature_] : input_tx_ )
        {
            SGTransaction::TransferUTXOInput *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( txid_hash_.toReadableString() );
            input_proto->set_output_index( output_idx_ );
            input_proto->set_signature( signature_.data(), signature_.size() );
        }
        for ( const auto &[encrypted_amount, dest_address, token_id] : outputs_ )
        {
            SGTransaction::TransferOutput *output_proto = utxo_proto_params->add_outputs();
            output_proto->set_encrypted_amount( encrypted_amount );
            output_proto->set_dest_addr( dest_address );
            output_proto->set_token_id( token_id.bytes().data(), token_id.size() );
        }
        *embedded.mutable_transfer() = tx_struct;
        return embedded;
    }

    std::shared_ptr<TransferTransaction> TransferTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::TransferTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse TransferTx from array.\n";
        }
        std::vector<InputUTXOInfo>   inputs;
        SGTransaction::UTXOTxParams *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( int i = 0; i < utxo_proto_params->inputs_size(); ++i )
        {
            const SGTransaction::TransferUTXOInput &input_proto = utxo_proto_params->inputs( i );

            InputUTXOInfo curr;
            curr.txid_hash_  = base::Hash256::fromReadableString( input_proto.tx_id_hash() ).value();
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

    bool TransferTransaction::HasUTXOParameters() const
    {
        return true;
    }

    std::optional<UTXOTxParameters> TransferTransaction::GetUTXOParametersOpt() const
    {
        return UTXOTxParameters{ input_tx_, outputs_ };
    }

    std::unordered_set<std::string> TransferTransaction::GetTopics() const
    {
        auto topics = GeniusTransaction::GetTopics();

        for ( const auto &dest : GetDstInfos() )
        {
            topics.emplace( dest.dest_address );
        }

        return topics;
    }
}
