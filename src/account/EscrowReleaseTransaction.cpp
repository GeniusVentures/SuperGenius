/**
 * @file EscrowReleaseTransaction.cpp
 * @brief Implementation of the EscrowReleaseTransaction class.
 */

#include "account/EscrowReleaseTransaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crypto/hasher/hasher_impl.hpp"
#include "base/blob.hpp"
#include <iostream>

namespace sgns
{

    EscrowReleaseTransaction::EscrowReleaseTransaction( UTXOTxParameters         params,
                                                        uint64_t                 release_amount,
                                                        std::string              release_address,
                                                        std::string              escrow_source,
                                                        std::string              original_escrow_hash,
                                                        SGTransaction::DAGStruct dag ) :
        IGeniusTransactions( "escrow-release", SetDAGWithType( std::move( dag ), "escrow-release" ) ),
        utxo_params_( std::move( params ) ),
        release_amount_( release_amount ),
        release_address_( std::move( release_address ) ),
        escrow_source_( std::move( escrow_source ) ),
        original_escrow_hash_( std::move( original_escrow_hash ) )
    {
    }

    EscrowReleaseTransaction EscrowReleaseTransaction::New( UTXOTxParameters         params,
                                                            uint64_t                 release_amount,
                                                            std::string              release_address,
                                                            std::string              escrow_source,
                                                            std::string              original_escrow_hash,
                                                            SGTransaction::DAGStruct dag )
    {
        EscrowReleaseTransaction instance( std::move( params ),
                                           release_amount,
                                           std::move( release_address ),
                                           std::move( escrow_source ),
                                           std::move( original_escrow_hash ),
                                           std::move( dag ) );
        instance.FillHash();
        return instance;
    }

    std::vector<uint8_t> EscrowReleaseTransaction::SerializeByteVector()
    {
        SGTransaction::EscrowReleaseTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( this->dag_st );
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
            out_proto->set_token_id( out.token_id.bytes().data(), out.token_id.size() );
        }
        tx_struct.set_release_amount( release_amount_ );
        tx_struct.set_release_address( release_address_ );
        tx_struct.set_escrow_source( escrow_source_ );
        tx_struct.set_original_escrow_hash( original_escrow_hash_ );
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
            outputs.push_back( { out_proto.encrypted_amount(),
                                 out_proto.dest_addr(),
                                 TokenID::FromBytes( out_proto.token_id().data(), out_proto.token_id().size() ) } );
        }
        UTXOTxParameters utxo_params( inputs, outputs );
        auto             releaseTx = std::make_shared<EscrowReleaseTransaction>(
            EscrowReleaseTransaction( std::move( utxo_params ),
                                      tx_struct.release_amount(),
                                      tx_struct.release_address(),
                                      tx_struct.escrow_source(),
                                      tx_struct.original_escrow_hash(),
                                      tx_struct.dag_struct() ) );
        return releaseTx;
    }

    UTXOTxParameters EscrowReleaseTransaction::GetUTXOParameters() const
    {
        return utxo_params_;
    }

    uint64_t EscrowReleaseTransaction::GetReleaseAmount() const
    {
        return release_amount_;
    }

    std::string EscrowReleaseTransaction::GetReleaseAddress() const
    {
        return release_address_;
    }

    std::string EscrowReleaseTransaction::GetEscrowSource() const
    {
        return escrow_source_;
    }

    std::string EscrowReleaseTransaction::GetOriginalEscrowHash() const
    {
        return original_escrow_hash_;
    }

    std::string EscrowReleaseTransaction::GetTransactionSpecificPath()
    {
        return GetType();
    }

} // namespace sgns
