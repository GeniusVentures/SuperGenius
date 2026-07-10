/**
 * @file       MigrationTransaction.cpp
 * @brief      One-time migration mint transaction implementation.
 * @date       2026-04-29
 */
#include "account/MigrationTransaction.hpp"

#include <fmt/format.h>

#include "base/blob.hpp"
#include "crypto/hasher.hpp"

namespace sgns
{
    MigrationTransaction::MigrationTransaction( UTXOTxParameters         utxo_params,
                                                std::string              from_version,
                                                TokenID                  token_id,
                                                SGTransaction::DAGStruct dag ) :
        GeniusTransaction( "migration", SetDAGWithType( std::move( dag ), "migration" ) ),
        utxo_params_( std::move( utxo_params ) ),
        from_version_( std::move( from_version ) ),
        token_id_( std::move( token_id ) )
    {
    }

    std::vector<uint8_t> MigrationTransaction::SerializeByteVector( const SGTransaction::DAGStruct &dag ) const
    {
        SGTransaction::MigrationTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );

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
        tx_struct.set_from_version( from_version_ );

        std::vector<uint8_t> serialized_proto( tx_struct.ByteSizeLong() );
        if ( !tx_struct.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            std::cerr << "Failed to Serialize MigrationTx to array" << std::endl;
        }
        return serialized_proto;
    }

    EmbeddedTransaction MigrationTransaction::SerializeToEmbeddedTransaction( const SGTransaction::DAGStruct &dag ) const
    {
        EmbeddedTransaction embedded;
        SGTransaction::MigrationTx tx_struct;
        tx_struct.mutable_dag_struct()->CopyFrom( dag );

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
        tx_struct.set_from_version( from_version_ );

        *embedded.mutable_migration() = tx_struct;
        return embedded;
    }

    std::shared_ptr<MigrationTransaction> MigrationTransaction::DeSerializeByteVector( const std::vector<uint8_t> &data )
    {
        SGTransaction::MigrationTx tx_struct;
        if ( !tx_struct.ParseFromArray( data.data(), data.size() ) )
        {
            std::cerr << "Failed to parse MigrationTx from array\n";
            return nullptr;
        }

        const uint64_t    amount            = tx_struct.amount();
        const std::string from_version      = tx_struct.from_version();
        const TokenID     token_id = TokenID::FromBytes( tx_struct.token_id().data(), tx_struct.token_id().size() );

        std::vector<InputUTXOInfo> inputs;
        auto                      *utxo_proto_params = tx_struct.mutable_utxo_params();
        for ( int i = 0; i < utxo_proto_params->inputs_size(); ++i )
        {
            const auto &input_proto = utxo_proto_params->inputs( i );
            auto        maybe_hash  = base::Hash256::fromReadableString( input_proto.tx_id_hash() );
            if ( !maybe_hash )
            {
                std::cerr << "Invalid hash in migration input." << std::endl;
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
            outputs.push_back( { amount, tx_struct.dag_struct().source_addr(), token_id } );
        }

        return std::make_shared<MigrationTransaction>(
            MigrationTransaction( { std::move( inputs ), std::move( outputs ) },
                                  from_version,
                                  token_id,
                                  tx_struct.dag_struct() ) );
    }

    uint64_t MigrationTransaction::GetAmount() const
    {
        if ( utxo_params_.second.empty() )
        {
            return 0;
        }
        return utxo_params_.second.front().encrypted_amount;
    }

    TokenID MigrationTransaction::GetTokenID() const
    {
        if ( utxo_params_.second.empty() )
        {
            return token_id_;
        }
        return utxo_params_.second.front().token_id;
    }

    std::string MigrationTransaction::GetChainId() const
    {
        return "migration";
    }

    UTXOTxParameters MigrationTransaction::GetUTXOParameters() const
    {
        return utxo_params_;
    }

    bool MigrationTransaction::HasUTXOParameters() const
    {
        return true;
    }

    std::optional<UTXOTxParameters> MigrationTransaction::GetUTXOParametersOpt() const
    {
        return utxo_params_;
    }

    std::string MigrationTransaction::GetFromVersion() const
    {
        return from_version_;
    }

    std::unordered_set<std::string> MigrationTransaction::GetTopics() const
    {
        auto topics = GeniusTransaction::GetTopics();
        for ( const auto &output : utxo_params_.second )
        {
            topics.emplace( output.dest_address );
        }
        return topics;
    }

    std::string MigrationTransaction::DeriveUniqueSourceKey( std::string_view from_version,
                                                             std::string_view source_address,
                                                             const TokenID   &token_id )
    {
        const auto payload = fmt::format( "migration:{}:{}:{}", from_version, source_address, token_id.ToHex() );
        return crypto::blake2b_256( std::vector<uint8_t>( payload.begin(), payload.end() ) ).toReadableString();
    }

    MigrationTransaction MigrationTransaction::New( uint64_t                 amount,
                                                    std::string              from_version,
                                                    TokenID                  token_id,
                                                    SGTransaction::DAGStruct dag,
                                                    std::string              destination )
    {
        if ( destination.empty() )
        {
            destination = dag.source_addr();
        }

        const auto source_key = DeriveUniqueSourceKey( from_version, dag.source_addr(), token_id );
        dag.set_uncle_hash( source_key );

        auto source_hash = base::Hash256::fromReadableString( source_key );
        std::vector<InputUTXOInfo> migration_inputs;
        if ( source_hash.has_value() )
        {
            migration_inputs.push_back( { source_hash.value(), 0, {} } );
        }

        std::vector<OutputDestInfo> migration_outputs{ { amount, destination, token_id } };
        MigrationTransaction        instance( { std::move( migration_inputs ), std::move( migration_outputs ) },
                                      std::move( from_version ),
                                      std::move( token_id ),
                                      std::move( dag ) );
        instance.FillHash();
        return instance;
    }
}
