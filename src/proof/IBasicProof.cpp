/**
 * @file       IBasicProof.cpp
 * @brief      
 * @date       2024-10-08
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "IBasicProof.hpp"
#include "GeniusAssigner.hpp"
#include "GeniusProver.hpp"
#include "NilFileHelper.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, IBasicProof::Error, e )
{
    using ProofError = sgns::IBasicProof::Error;
    switch ( e )
    {
        case ProofError::INSUFFICIENT_FUNDS:
            return "No sufficient funds for the transaction";
        case ProofError::INVALID_PROOF:
            return "The generated proof os not valid";
        case ProofError::BYTECODE_NOT_FOUND:
            return "The bytecode was not found";
        case ProofError::INVALID_CIRCUIT:
            return "The provided bytecode is invalid";
        case ProofError::INVALID_PROTO_PROOF:
            return "The protobuf deserialized data has no valid proof packet";
        case ProofError::INVALID_PROOF_TYPE:
            return "The protobuf deserialized data has a type we can't parser";
        case ProofError::UNEXPECTED_PROOF_TYPE:
            return "The type of proof doesn't match the expected type";
        case ProofError::INVALID_PUBLIC_PARAMETERS:
            return "The public parameters are invalid";
    }
    return "Unknown error";
}

namespace sgns
{

    IBasicProof::IBasicProof( std::string bytecode_payload ) : bytecode_payload_( std::move( bytecode_payload ) )

    {
    }

    outcome::result<SGProof::BaseProofProto> IBasicProof::DeSerializeBaseProof( const std::vector<uint8_t> &proof_data )
    {
        SGProof::BaseProofProto base_proof_struct;
        if ( !base_proof_struct.ParseFromArray( proof_data.data(), proof_data.size() ) )
        {
            return Error::INVALID_PROTO_PROOF;
        }
        return base_proof_struct;
    }

    outcome::result<SGProof::BaseProofData> IBasicProof::GenerateProof()
    {
        SGProof::BaseProofData retval;
        if ( bytecode_payload_ == "Bypass" )
        {
            return retval;
        }
        auto [public_inputs_json_array, private_inputs_json_array] = GenerateJsonParameters();

        GeniusAssigner assigner;
        GeniusProver   prover;

        OUTCOME_TRY( ( auto &&, assign_value ),
                     assigner.GenerateCircuitAndTable( public_inputs_json_array,
                                                       private_inputs_json_array,
                                                       bytecode_payload_ ) );
        OUTCOME_TRY( ( auto &&, proof_value ), prover.CreateProof( assign_value.at( 0 ) ) );

        auto proof_vector = prover.WriteProofToVector( proof_value.proof );
        retval.set_snark( std::string( proof_vector.begin(), proof_vector.end() ) );
        retval.set_type( GetProofType() );
        return retval;
    }

    outcome::result<std::vector<uint8_t>> IBasicProof::GenerateFullProof()
    {
        OUTCOME_TRY( ( auto &&, proof_value ), GenerateProof() );
        OUTCOME_TRY( ( auto &&, full_proof_data ), SerializeFullProof( proof_value ) );

        return full_proof_data;
    }

    static GeniusProver::ProofType GetSnarkFromProto( const SGProof::BaseProofData &proof_proto_data )
    {
        GeniusProver::ProofType snark;
        const std::string      &string_data = proof_proto_data.snark();
        std::vector<uint8_t>    proof_vector( string_data.begin(), string_data.end() );
        auto                    write_iter = proof_vector.begin();
        snark.read( write_iter, proof_vector.size() );

        return snark;
    }

    outcome::result<bool> IBasicProof::VerifyFullProof( const std::vector<uint8_t> &proof_data )
    {
        OUTCOME_TRY( ( auto &&, base_proof ), DeSerializeBaseProof( proof_data ) );

        auto ParameterDeserializer = PublicParamDeSerializers.find( base_proof.proof_data().type() );

        if ( ParameterDeserializer == PublicParamDeSerializers.end() )
        {
            return Error::INVALID_PROOF_TYPE;
        }
        auto bytecode = ByteCodeMap.find( base_proof.proof_data().type() );

        if ( bytecode == ByteCodeMap.end() )
        {
            return Error::BYTECODE_NOT_FOUND;
        }
        if ( bytecode->second == "Bypass" )
        {
            return true;
        }

        OUTCOME_TRY( ( auto &&, parameter_pair ), ParameterDeserializer->second( proof_data ) );

        return VerifyFullProof( parameter_pair, base_proof.proof_data(), bytecode->second );
    }

    outcome::result<bool> IBasicProof::VerifyFullProof(
        const std::pair<boost::json::array, boost::json::array> &parameters,
        const SGProof::BaseProofData                            &proof_data,
        std::string                                              proof_bytecode )

    {
        auto [public_inputs_json_array, private_inputs_json_array] = parameters;

        auto snark = GetSnarkFromProto( proof_data );

        GeniusAssigner assigner;

        OUTCOME_TRY( ( auto &&, assign_value ),
                     assigner.GenerateCircuitAndTable( public_inputs_json_array,
                                                       private_inputs_json_array,
                                                       std::move( proof_bytecode ) ) );

        GeniusProver::GeniusProof genius_proof( snark, assign_value.at( 0 ).constrains, assign_value.at( 0 ).table );

        return GeniusProver::VerifyProof( genius_proof );
    }
}
