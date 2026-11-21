/**
 * @file EscrowReleaseTransaction.cpp
 * @brief Historical six-field status codec adapted to the current transaction API.
 */
#include "account/EscrowReleaseTransaction.hpp"

#include <limits>

namespace sgns
{
    // Registration allocation failure is fatal during process startup, as for the existing transaction codecs.
    // NOLINTNEXTLINE(bugprone-throwing-static-initialization)
    const bool EscrowReleaseTransaction::registered_ = []()
    {
        RegisterDeserializer( "escrow-release", &EscrowReleaseTransaction::DeSerializeByteVector );
        return true;
    }();

    EscrowReleaseTransaction::EscrowReleaseTransaction( UTXOTxParameters         params,
                                                        uint64_t                 release_amount,
                                                        std::string              release_address,
                                                        std::string              escrow_source,
                                                        std::string              original_escrow_hash,
                                                        SGTransaction::DAGStruct dag ) :
        GeniusTransaction( "escrow-release", SetDAGWithType( std::move( dag ), "escrow-release" ) ),
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
        EscrowReleaseTransaction marker( std::move( params ),
                                         release_amount,
                                         std::move( release_address ),
                                         std::move( escrow_source ),
                                         std::move( original_escrow_hash ),
                                         std::move( dag ) );
        marker.FillHash();
        return marker;
    }

    SGTransaction::EscrowReleaseTx EscrowReleaseTransaction::PopulateMessage(
        const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::EscrowReleaseTx message;
        *message.mutable_dag_struct() = dag;
        auto *params                  = message.mutable_utxo_params();
        for ( const auto &input : utxo_params_.first )
        {
            auto *wire = params->add_inputs();
            wire->set_tx_id_hash( input.txid_hash_.toReadableString() );
            wire->set_output_index( input.output_idx_ );
            wire->set_signature( input.signature_.data(), input.signature_.size() );
        }
        for ( const auto &output : utxo_params_.second )
        {
            auto *wire = params->add_outputs();
            wire->set_encrypted_amount( output.encrypted_amount );
            wire->set_dest_addr( output.dest_address );
            wire->set_token_id( output.token_id.bytes().data(), output.token_id.size() );
        }
        message.set_release_amount( release_amount_ );
        message.set_release_address( release_address_ );
        message.set_escrow_source( escrow_source_ );
        message.set_original_escrow_hash( original_escrow_hash_ );
        return message;
    }

    std::vector<uint8_t> EscrowReleaseTransaction::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        std::string bytes;
        if ( !PopulateMessage( dag ).SerializeToString( &bytes ) )
        {
            return {};
        }
        return { bytes.begin(), bytes.end() };
    }

    EmbeddedTransaction EscrowReleaseTransaction::SerializeToEmbeddedTransaction(
        const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        *embedded.mutable_escrow_release() = PopulateMessage( dag );
        return embedded;
    }

    std::shared_ptr<EscrowReleaseTransaction> EscrowReleaseTransaction::DeSerializeByteVector(
        const std::vector<uint8_t> &data )
    {
        SGTransaction::EscrowReleaseTx message;
        if ( data.size() > static_cast<size_t>( std::numeric_limits<int>::max() ) ||
             !message.ParseFromArray( data.data(), static_cast<int>( data.size() ) ) || !message.has_dag_struct() ||
             message.dag_struct().type() != "escrow-release" )
        {
            return nullptr;
        }
        UTXOTxParameters params;
        for ( const auto &input : message.utxo_params().inputs() )
        {
            const auto hash = base::Hash256::fromHex( input.tx_id_hash() );
            if ( !hash )
            {
                return nullptr;
            }
            params.first.push_back(
                { hash.value(), input.output_index(), { input.signature().begin(), input.signature().end() } } );
        }
        for ( const auto &output : message.utxo_params().outputs() )
        {
            // Historical GNUS marker outputs deliberately omit default token bytes.
            if ( output.token_id().size() > TokenID::ByteArray{}.size() )
            {
                return nullptr;
            }
            params.second.push_back( { output.encrypted_amount(),
                                       output.dest_addr(),
                                       TokenID::FromBytes( output.token_id().data(), output.token_id().size() ) } );
        }
        return std::make_shared<EscrowReleaseTransaction>( EscrowReleaseTransaction( std::move( params ),
                                                                                     message.release_amount(),
                                                                                     message.release_address(),
                                                                                     message.escrow_source(),
                                                                                     message.original_escrow_hash(),
                                                                                     message.dag_struct() ) );
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

    std::string EscrowReleaseTransaction::GetTransactionSpecificPath() const
    {
        return GetType();
    }

    std::unordered_set<std::string> EscrowReleaseTransaction::GetTopics() const
    {
        auto topics = GeniusTransaction::GetTopics();
        topics.emplace( escrow_source_ );
        return topics;
    }
}
