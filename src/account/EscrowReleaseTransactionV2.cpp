/**
 * @file EscrowReleaseTransactionV2.cpp
 * @brief V2 escrow payment codec and peer-independent hold slot identity.
 */
#include "account/EscrowReleaseTransactionV2.hpp"

#include <limits>

#include "account/InputValidators.hpp"

namespace sgns
{
    // Registration allocation failure is fatal during process startup, as for the existing transaction codecs.
    // NOLINTNEXTLINE(bugprone-throwing-static-initialization)
    const bool EscrowReleaseTransactionV2::registered_ = []()
    {
        RegisterDeserializer( "escrow-release-v2", &EscrowReleaseTransactionV2::DeSerializeByteVector );
        return true;
    }();

    EscrowReleaseTransactionV2::EscrowReleaseTransactionV2( UTXOTxParameters params, SGTransaction::DAGStruct dag ) :
        GeniusTransaction( "escrow-release-v2", SetDAGWithType( std::move( dag ), "escrow-release-v2" ) ),
        utxo_params_( std::move( params ) )
    {
    }

    EscrowReleaseTransactionV2 EscrowReleaseTransactionV2::New( std::vector<InputUTXOInfo>  inputs,
                                                                std::vector<OutputDestInfo> destinations,
                                                                SGTransaction::DAGStruct    dag )
    {
        EscrowReleaseTransactionV2 payment( { std::move( inputs ), std::move( destinations ) }, std::move( dag ) );
        payment.FillHash();
        return payment;
    }

    SGTransaction::EscrowReleaseTxV2 EscrowReleaseTransactionV2::PopulateMessage(
        const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::EscrowReleaseTxV2 message;
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
        return message;
    }

    std::vector<uint8_t> EscrowReleaseTransactionV2::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        const auto  message = PopulateMessage( dag );
        std::string bytes;
        if ( !message.SerializeToString( &bytes ) )
        {
            return {};
        }
        return { bytes.begin(), bytes.end() };
    }

    EmbeddedTransaction EscrowReleaseTransactionV2::SerializeToEmbeddedTransaction(
        const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        *embedded.mutable_escrow_release_v2() = PopulateMessage( dag );
        return embedded;
    }

    std::shared_ptr<EscrowReleaseTransactionV2> EscrowReleaseTransactionV2::DeSerializeByteVector(
        const std::vector<uint8_t> &data )
    {
        SGTransaction::EscrowReleaseTxV2 message;
        if ( data.size() > static_cast<size_t>( std::numeric_limits<int>::max() ) ||
             !message.ParseFromArray( data.data(), static_cast<int>( data.size() ) ) || !message.has_dag_struct() ||
             message.dag_struct().type() != "escrow-release-v2" || !message.has_utxo_params() )
        {
            return nullptr;
        }
        const auto &params = message.utxo_params();
        if ( params.inputs_size() != 1 || params.outputs_size() == 0 ||
             params.inputs( 0 ).output_index() != input_validator_constants::ESCROW_LOCK_OUTPUT_INDEX )
        {
            return nullptr;
        }
        const auto &input = params.inputs( 0 );
        const auto  hash  = base::Hash256::fromHex( input.tx_id_hash() );
        if ( !hash )
        {
            return nullptr;
        }
        std::vector<InputUTXOInfo> inputs{
            { hash.value(), input.output_index(), { input.signature().begin(), input.signature().end() } } };
        std::vector<OutputDestInfo> outputs;
        outputs.reserve( params.outputs_size() );
        for ( const auto &output : params.outputs() )
        {
            if ( output.token_id().size() != TokenID::ByteArray{}.size() || output.dest_addr().empty() )
            {
                return nullptr;
            }
            outputs.push_back( { output.encrypted_amount(),
                                 output.dest_addr(),
                                 TokenID::FromBytes( output.token_id().data(), output.token_id().size() ) } );
        }
        auto payment = std::make_shared<EscrowReleaseTransactionV2>(
            EscrowReleaseTransactionV2( { std::move( inputs ), std::move( outputs ) }, message.dag_struct() ) );
        // Reject encodings that reconstruction would change beneath the retained signature and hash.
        if ( payment->SerializeByteVector() != data )
        {
            return nullptr;
        }
        return payment;
    }

    UTXOTxParameters EscrowReleaseTransactionV2::GetUTXOParameters() const
    {
        return utxo_params_;
    }

    std::vector<InputUTXOInfo> EscrowReleaseTransactionV2::GetInputInfos() const
    {
        return utxo_params_.first;
    }

    std::vector<OutputDestInfo> EscrowReleaseTransactionV2::GetDstInfos() const
    {
        return utxo_params_.second;
    }

    bool EscrowReleaseTransactionV2::HasUTXOParameters() const
    {
        return true;
    }

    std::optional<UTXOTxParameters> EscrowReleaseTransactionV2::GetUTXOParametersOpt() const
    {
        return utxo_params_;
    }

    std::string EscrowReleaseTransactionV2::GetTransactionSpecificPath() const
    {
        return GetType();
    }

    std::unordered_set<std::string> EscrowReleaseTransactionV2::GetTopics() const
    {
        auto topics = GeniusTransaction::GetTopics();
        for ( const auto &output : utxo_params_.second )
        {
            topics.emplace( output.dest_address );
        }
        return topics;
    }

    std::string EscrowReleaseTransactionV2::GetSlotID() const
    {
        if ( utxo_params_.first.size() != 1 ||
             utxo_params_.first.front().output_idx_ != input_validator_constants::ESCROW_LOCK_OUTPUT_INDEX )
        {
            return {};
        }
        const auto &input = utxo_params_.first.front();
        return "escrow-release-v2:" + input.txid_hash_.toReadableString() + ":" + std::to_string( input.output_idx_ );
    }
}
