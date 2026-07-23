/**
 * @file       MintTransactionV2.cpp
 * @brief      UTXO-aware mint transaction implementation.
 * @date       2026-03-18
 */
#include "account/MintTransactionV2.hpp"

#include <algorithm>
#include <cctype>

#include "base/blob.hpp"

namespace sgns
{
    MintTransactionV2::MintTransactionV2( UTXOTxParameters         utxo_params,
                                          std::string              chain_id,
                                          TokenID                  token_id,
                                          SGTransaction::DAGStruct dag ) :
        GeniusTransaction( "mint-v2", SetDAGWithType( std::move( dag ), "mint-v2" ) ),
        utxo_params_( std::move( utxo_params ) ),
        chain_id_( std::move( chain_id ) ),
        token_id_( std::move( token_id ) )
    {
    }

    std::vector<uint8_t> MintTransactionV2::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::MintTxV2 tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        tx_struct.set_chain_id( chain_id_ );

        auto *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( const auto &[txid_hash_, output_idx_, signature_] : utxo_params_.first )
        {
            auto *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( txid_hash_.toReadableString() );
            input_proto->set_output_index( output_idx_ );
            input_proto->set_signature( signature_.data(), signature_.size() );
        }

        for ( const auto &[encrypted_amount, dest_address, token_id] : utxo_params_.second )
        {
            auto *output_proto = utxo_proto_params->add_outputs();
            output_proto->set_encrypted_amount( encrypted_amount );
            output_proto->set_dest_addr( dest_address );
            output_proto->set_token_id( token_id.bytes().data(), token_id.size() );
        }

        const auto amount = GetAmount();
        const auto token  = GetTokenID();
        tx_struct.set_amount( amount );
        tx_struct.set_token_id( token.bytes().data(), token.size() );

        size_t               size = tx_struct.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );
        if ( !tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            std::cerr << "Failed to Serialize MintTxV2 to array" << std::endl;
        }
        return serialized_proto;
    }

    EmbeddedTransaction MintTransactionV2::SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        SGTransaction::MintTxV2 tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );
        tx_struct.set_chain_id( chain_id_ );

        auto *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( const auto &[txid_hash_, output_idx_, signature_] : utxo_params_.first )
        {
            auto *input_proto = utxo_proto_params->add_inputs();
            input_proto->set_tx_id_hash( txid_hash_.toReadableString() );
            input_proto->set_output_index( output_idx_ );
            input_proto->set_signature( signature_.data(), signature_.size() );
        }

        for ( const auto &[encrypted_amount, dest_address, token_id] : utxo_params_.second )
        {
            auto *output_proto = utxo_proto_params->add_outputs();
            output_proto->set_encrypted_amount( encrypted_amount );
            output_proto->set_dest_addr( dest_address );
            output_proto->set_token_id( token_id.bytes().data(), token_id.size() );
        }

        const auto amount = GetAmount();
        const auto token  = GetTokenID();
        tx_struct.set_amount( amount );
        tx_struct.set_token_id( token.bytes().data(), token.size() );

        *embedded.mutable_mint_v2() = tx_struct;
        return embedded;
    }

    std::shared_ptr<MintTransactionV2> MintTransactionV2::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::MintTxV2 tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse MintTxV2 from array\n";
            return nullptr;
        }

        uint64_t    amount  = tx_struct.amount();
        std::string chainid = tx_struct.chain_id();
        TokenID     tokenid = TokenID::FromBytes( tx_struct.token_id().data(), tx_struct.token_id().size() );

        std::vector<InputUTXOInfo> inputs;
        auto                      *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( int i = 0; i < utxo_proto_params->inputs_size(); ++i )
        {
            const auto &input_proto = utxo_proto_params->inputs( i );
            const auto &hash_text   = input_proto.tx_id_hash();
            const bool  canonical_hash_text =
                hash_text.size() == 64 &&
                std::all_of(
                    hash_text.begin(),
                    hash_text.end(),
                    []( unsigned char c )
                    {
                        return std::isdigit( c ) != 0 || ( c >= 'a' && c <= 'f' );
                    } );
            if ( !canonical_hash_text )
            {
                std::cerr << "Invalid or noncanonical hash in mint-v2 input." << std::endl;
                return nullptr;
            }

            auto        maybe_hash  = base::Hash256::fromReadableString( input_proto.tx_id_hash() );
            if ( !maybe_hash || input_proto.tx_id_hash() != maybe_hash.value().toReadableString() )
            {
                std::cerr << "Invalid or noncanonical hash in mint-v2 input." << std::endl;
                return nullptr;
            }

            inputs.push_back( { maybe_hash.value(),
                                input_proto.output_index(),
                                std::vector<uint8_t>( input_proto.signature().cbegin(), input_proto.signature().cend() ) } );
        }

        std::vector<OutputDestInfo> outputs;
        for ( int i = 0; i < utxo_proto_params->outputs_size(); ++i )
        {
            const auto &output_proto = utxo_proto_params->outputs( i );
            outputs.push_back( { output_proto.encrypted_amount(),
                                 output_proto.dest_addr(),
                                 TokenID::FromBytes( output_proto.token_id().data(), output_proto.token_id().size() ) } );
        }

        if ( outputs.empty() )
        {
            outputs.push_back( { amount, tx_struct.dag_struct().source_addr(), tokenid } );
        }

        return std::make_shared<MintTransactionV2>( MintTransactionV2( { std::move( inputs ), std::move( outputs ) },
                                                                       chainid,
                                                                       tokenid,
                                                                       tx_struct.dag_struct() ) );
    }

    uint64_t MintTransactionV2::GetAmount() const
    {
        if ( utxo_params_.second.empty() )
        {
            return 0;
        }
        return utxo_params_.second.front().encrypted_amount;
    }

    TokenID MintTransactionV2::GetTokenID() const
    {
        if ( utxo_params_.second.empty() )
        {
            return token_id_;
        }
        return utxo_params_.second.front().token_id;
    }

    std::string MintTransactionV2::GetChainId() const
    {
        return chain_id_;
    }

    UTXOTxParameters MintTransactionV2::GetUTXOParameters() const
    {
        return utxo_params_;
    }

    bool MintTransactionV2::HasUTXOParameters() const
    {
        return true;
    }

    std::optional<UTXOTxParameters> MintTransactionV2::GetUTXOParametersOpt() const
    {
        return utxo_params_;
    }

    std::unordered_set<std::string> MintTransactionV2::GetTopics() const
    {
        auto topics = GeniusTransaction::GetTopics();
        for ( const auto &output : utxo_params_.second )
        {
            topics.emplace( output.dest_address );
        }
        return topics;
    }

    MintTransactionV2 MintTransactionV2::New( uint64_t                 new_amount,
                                               std::string              chain_id,
                                               TokenID                  token_id,
                                               SGTransaction::DAGStruct dag,
                                               std::vector<InputUTXOInfo> mint_inputs,
                                               std::string              mint_destination )
    {
        if ( mint_destination.empty() )
        {
            mint_destination = dag.source_addr();
        }

        std::vector<OutputDestInfo> mint_outputs{ { new_amount, mint_destination, token_id } };
        MintTransactionV2           instance( { std::move( mint_inputs ), std::move( mint_outputs ) },
                                     std::move( chain_id ),
                                     std::move( token_id ),
                                     std::move( dag ) );
        instance.FillHash();
        return instance;
    }

    outcome::result<std::string> MintTransactionV2::GetSlotPreimage() const
    {
        static constexpr std::string_view kPrefix    = "mint-v2:";
        static constexpr std::string_view kSeparator = ":";

        const bool canonical_chain =
            !chain_id_.empty() && ( chain_id_ == "0" || chain_id_.front() != '0' ) &&
            std::all_of(
                chain_id_.begin(),
                chain_id_.end(),
                []( unsigned char c )
                {
                    return std::isdigit( c ) != 0;
                } );
        if ( !canonical_chain || utxo_params_.first.size() != 1 )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        const auto &input     = utxo_params_.first.front();
        const auto  burn_hash = input.txid_hash_.toReadableString();
        const bool  canonical_hash =
            burn_hash.size() == 64 &&
            std::all_of(
                burn_hash.begin(),
                burn_hash.end(),
                []( unsigned char c )
                {
                    return std::isdigit( c ) != 0 || ( c >= 'a' && c <= 'f' );
                } );
        if ( !canonical_hash )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        std::string preimage( kPrefix );
        preimage += chain_id_;
        preimage += kSeparator;
        preimage += burn_hash;
        preimage += kSeparator;
        preimage += std::to_string( input.output_idx_ );
        return preimage;
    }

}
