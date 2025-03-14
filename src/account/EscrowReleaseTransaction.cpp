#include "account/EscrowReleaseTransaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crypto/hasher/hasher_impl.hpp"
#include "base/blob.hpp"
#include <iostream>

namespace sgns
{
    // Updated private constructor.
    EscrowReleaseTransaction::EscrowReleaseTransaction( UTXOTxParameters         params,
                                                        uint64_t                 release_amount,
                                                        std::string              release_address,
                                                        std::string              escrow_source, // new parameter
                                                        SGTransaction::DAGStruct dag ) :
        IGeniusTransactions( "escrowrelease", SetDAGWithType( std::move( dag ), "escrowrelease" ) ),
        utxo_params_( std::move( params ) ),
        release_amount_( release_amount ),
        release_address_( std::move( release_address ) ),
        escrow_source_( std::move( escrow_source ) )
    {
    }

    // Updated factory method.
    EscrowReleaseTransaction EscrowReleaseTransaction::New( UTXOTxParameters         params,
                                                            uint64_t                 release_amount,
                                                            std::string              release_address,
                                                            std::string              escrow_source, // new parameter
                                                            SGTransaction::DAGStruct dag )
    {
        EscrowReleaseTransaction instance( std::move( params ),
                                           release_amount,
                                           std::move( release_address ),
                                           std::move( escrow_source ), // store the escrow source separately
                                           std::move( dag ) );

        instance.FillHash();
        return instance;
    }

    // The SerializeByteVector() and DeSerializeByteVector() methods
    // should be updated to include escrow_source_.
    // (For brevity, you may add escrow_source_ as an additional field in the proto message
    // or append it to the serialized data, ensuring symmetry in deserialization.)
    // Here is an illustrative example for serialization:

    std::vector<uint8_t> EscrowReleaseTransaction::SerializeByteVector()
    {
        SGTransaction::EscrowReleaseTx tx_struct;
        // Copy the DAG structure.
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
        // Copy UTXO parameters.
        auto *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( const auto &input : utxo_params_.inputs_ )
        {
            auto *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( input.txid_hash_.toReadableString() );
            input_proto->set_output_index( input.output_idx_ );
            input_proto->set_signature( input.signature_ );
        }
        for ( const auto &out : utxo_params_.outputs_ )
        {
            auto *out_proto = utxo_proto_params->add_outputs();
            out_proto->set_encrypted_amount( out.encrypted_amount );
            out_proto->set_dest_addr( out.dest_address );
        }

        // Copy release amount and address.
        tx_struct.set_release_amount( release_amount_ );
        tx_struct.set_release_address( release_address_ );
        // New: copy escrow source.
        tx_struct.set_escrow_source( escrow_source_ );

        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );
        tx_struct.SerializeToArray( serialized_proto.data(), static_cast<int>( size ) );
        return serialized_proto;
    }

    std::shared_ptr<EscrowReleaseTransaction> EscrowReleaseTransaction::DeSerializeByteVector(
        const std::vector<uint8_t> &data )
    {
        SGTransaction::EscrowReleaseTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), static_cast<int>( data.size() ) ) )
        {
            std::cerr << "Failed to parse EscrowReleaseTx from array." << std::endl;
            return nullptr;
        }

        std::vector<InputUTXOInfo> inputs;
        auto                      *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( int i = 0; i < utxo_proto_params->inputs_size(); ++i )
        {
            const auto   &input_proto = utxo_proto_params->inputs( i );
            InputUTXOInfo curr;
            auto          maybe_hash = base::Hash256::fromReadableString( input_proto.tx_id_hash() );
            if ( !maybe_hash )
            {
                std::cerr << "Invalid hash in input" << std::endl;
                return nullptr;
            }
            curr.txid_hash_  = maybe_hash.value();
            curr.output_idx_ = input_proto.output_index();
            curr.signature_  = input_proto.signature();
            inputs.push_back( curr );
        }

        std::vector<OutputDestInfo> outputs;
        for ( int i = 0; i < utxo_proto_params->outputs_size(); ++i )
        {
            const auto &out_proto = utxo_proto_params->outputs( i );
            outputs.push_back( { out_proto.encrypted_amount(), out_proto.dest_addr() } );
        }

        UTXOTxParameters utxo_params( inputs, outputs );

        // Retrieve escrow_source from the proto.
        std::string escrow_source = tx_struct.escrow_source();

        auto releaseTx = std::make_shared<EscrowReleaseTransaction>(
            EscrowReleaseTransaction( std::move( utxo_params ),
                                      tx_struct.release_amount(),
                                      tx_struct.release_address(),
                                      escrow_source, // new field
                                      tx_struct.dag_struct() ) );

        return releaseTx;
    }
}
