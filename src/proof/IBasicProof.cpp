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
    }
    return "Unknown error";
}

namespace sgns
{

    IBasicProof::IBasicProof( std::string bytecode_payload ) :
        bytecode_payload_( std::move( bytecode_payload ) ),    //
        assigner_( std::make_shared<sgns::GeniusAssigner>() ), //
        prover_( std::make_shared<sgns::GeniusProver>() )      //
    {
    }

    std::vector<uint8_t> IBasicProof::SerializeProof( const SGProof::BaseProofData &proof )
    {
        size_t               size = proof.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        proof.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }

    SGProof::BaseProofData IBasicProof::DeSerializeProof( const std::vector<uint8_t> &proof_data )
    {
        SGProof::BaseProofData proof_struct;
        if ( !proof_struct.ParseFromArray( proof_data.data(), proof_data.size() ) )
        {
            std::cerr << "Failed to parse ProofStruct from array." << std::endl;
        }
        return proof_struct;
    }

    outcome::result<SGProof::BaseProofData> IBasicProof::GenerateProof()
    {
        SGProof::BaseProofData retval;
        auto [public_inputs_json_array, private_inputs_json_array] = GenerateJsonParameters();

        auto hidden_assigner = std::static_pointer_cast<sgns::GeniusAssigner>( assigner_ );
        auto hidden_prover   = std::static_pointer_cast<sgns::GeniusProver>( prover_ );

        OUTCOME_TRY( ( auto &&, assign_value ),
                     hidden_assigner->GenerateCircuitAndTable( public_inputs_json_array,
                                                               private_inputs_json_array,
                                                               bytecode_payload_ ) );
        OUTCOME_TRY( ( auto &&, proof_value ), hidden_prover->CreateProof( assign_value.at( 0 ) ) );

        auto proof_vector = hidden_prover->WriteProofToVector( proof_value.proof );
        //std::cout << "proof_vector size" << proof_vector.size() << std::endl;
        retval.set_snark( std::string( proof_vector.begin(), proof_vector.end() ) );
        retval.set_type( "Transfer" );
        return retval;
    }

    outcome::result<std::vector<uint8_t>> IBasicProof::GenerateFullProof()
    {
        OUTCOME_TRY( ( auto &&, proof_value ), GenerateProof() );
        OUTCOME_TRY( ( auto &&, public_parameters_vector ), SerializePublicParameters() );
        auto basic_proof_vector = SerializeProof( proof_value );
        basic_proof_vector.insert( basic_proof_vector.end(),
                                   public_parameters_vector.begin(),
                                   public_parameters_vector.end() );
        return basic_proof_vector;
    }

    outcome::result<bool> IBasicProof::VerifyFullProof( const std::vector<uint8_t> &proof_data )
    {
        SGProof::BaseProofProto base_proof; //proof_data
        if ( !base_proof.ParseFromArray( proof_data.data(), proof_data.size() ) )
        {
            return Error::INVALID_PROTO_PROOF;
        }

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
        OUTCOME_TRY( ( auto &&, parameter_pair ),
                     ParameterDeserializer->second( proof_data ));
        auto [public_inputs_json_array, private_inputs_json_array] = parameter_pair;

        GeniusProver::ProofType snark;
        const std::string      &string_data = base_proof.proof_data().snark();
        std::vector<uint8_t>    proof_vector( string_data.begin(), string_data.end() );
        auto                    write_iter = proof_vector.begin();
        snark.read( write_iter, proof_vector.size() );

        auto assigner = std::make_shared<sgns::GeniusAssigner>();

        OUTCOME_TRY( ( auto &&, assign_value ),
                     assigner->GenerateCircuitAndTable( public_inputs_json_array,
                                                        private_inputs_json_array,
                                                        bytecode->second ) );

        GeniusProver::GeniusProof genius_proof( snark, assign_value.at( 0 ).constrains, assign_value.at( 0 ).table );

        return GeniusProver::VerifyProof( genius_proof );
    }
}
