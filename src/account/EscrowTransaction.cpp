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
    EscrowTransaction::EscrowTransaction( UTXOTxParameters         params,
                                          uint64_t                 amount,
                                          std::string              dev_addr,
                                          uint64_t                 peers_cut,
                                          SGTransaction::DAGStruct dag ) :
        IGeniusTransactions( "escrow-hold", SetDAGWithType( std::move( dag ), "escrow-hold" ) ),
        utxo_params_( std::move( params ) ),
        amount_( std::move( amount ) ),
        dev_addr_( std::move( dev_addr ) ),
        peers_cut_( peers_cut )
    {
    }

    EscrowTransaction EscrowTransaction::New( UTXOTxParameters                                params,
                                              uint64_t                                        amount,
                                              std::string                                     dev_addr,
                                              uint64_t                                        peers_cut,
                                              SGTransaction::DAGStruct                        dag,
                                              std::shared_ptr<ethereum::EthereumKeyGenerator> eth_key )
    {
        EscrowTransaction instance( std::move( params ), amount, std::move( dev_addr ), peers_cut, std::move( dag ) );
        instance.FillHash();
        instance.MakeSignature( std::move( eth_key ) );
        return instance;
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
            output_proto->set_encrypted_amount( output.encrypted_amount );
            output_proto->set_dest_addr( output.dest_address );
            output_proto->set_token_id( output.token_id.bytes().data(), output.token_id.size() );
        }
        tx_struct.set_amount( amount_ );
        tx_struct.set_dev_addr( dev_addr_ );
        tx_struct.set_peers_cut( peers_cut_ );
        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
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
        uint64_t amount    = tx_struct.amount();
        uint64_t peers_cut = tx_struct.peers_cut();
        return std::make_shared<EscrowTransaction>( EscrowTransaction( UTXOTxParameters{ inputs, outputs },
                                                                       amount,
                                                                       tx_struct.dev_addr(),
                                                                       peers_cut,
                                                                       tx_struct.dag_struct() ) );
    }
}
