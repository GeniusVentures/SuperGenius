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

    std::vector<uint8_t> IBasicProof::SerializeProof( const SGProof::ProofStruct &proof )
    {
        size_t               size = proof.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        proof.SerializeToArray( serialized_proto.data(), serialized_proto.size() );
        return serialized_proto;
    }

    outcome::result<SGProof::ProofStruct> IBasicProof::GenerateProof()
    {
        SGProof::ProofStruct retval;
        auto [public_inputs_json_array, private_inputs_json_array] = GenerateJsonParameters();

        auto hidden_assigner = std::static_pointer_cast<sgns::GeniusAssigner>( assigner_ );
        auto hidden_prover   = std::static_pointer_cast<sgns::GeniusProver>( prover_ );

        OUTCOME_TRY( ( auto &&, assign_value ),
                     hidden_assigner->GenerateCircuitAndTable( public_inputs_json_array,
                                                               private_inputs_json_array,
                                                               bytecode_payload_ ) );
        OUTCOME_TRY( ( auto &&, proof_value ), hidden_prover->CreateProof( assign_value.at( 0 ) ) );

        //if ( !hidden_prover->VerifyProof( proof_value ) )
        //{
        //    return outcome::failure( ProofError::INVALID_PROOF );
        //}
        auto proof_vector = hidden_prover->WriteProofToVector( proof_value.proof );
        retval.set_proof_data( std::string( proof_vector.begin(), proof_vector.end() ) );
        auto constrains_vector = NilFileHelper::GetMarshalledData( proof_value.constrains, false );
        retval.set_constrains( std::string( constrains_vector.begin(), constrains_vector.end() ) );
        auto plonk_table_vector = NilFileHelper::GetMarshalledData( proof_value.table, false );
        retval.set_plonk_table( std::string( plonk_table_vector.begin(), plonk_table_vector.end() ) );
        return retval;
    }
}
