/**
 * @file       GeniusProver.cpp
 * @brief      
 * @date       2024-09-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "GeniusProver.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, GeniusProver::ProverError, e )
{
    using ProverError = sgns::GeniusProver::ProverError;
    switch ( e )
    {
        case ProverError::INVALID_PROOF_GENERATED:
            return "The provided proof is verified to be false.";
    }
    return "Unknown error";
}

outcome::result<GeniusProver::ProofType> GeniusProver::GenerateProof(
    const std::vector<GeniusAssigner::AssignerOutput> &assigner_outputs,
    const boost::filesystem::path                     &proof_file_ )
{
    auto constrains_sys = MakePlonkConstraintSystem( assigner_outputs.at( 0 ).constrains );

    auto [plonk_table_desc, assignment_table] = MakePlonkTableDescription( assigner_outputs.at( 0 ).table );

    std::size_t table_rows_log = std::ceil( std::log2( plonk_table_desc.rows_amount ) );

    auto fri_params = MakeFRIParams( table_rows_log, 1, expand_factor_ );

    std::size_t permutation_size =
        plonk_table_desc.witness_columns + plonk_table_desc.public_input_columns + component_constant_columns_;
    LpcScheme lpc_scheme( fri_params );

    PublicPreprocessedData public_preprocessed_data(
        crypto3::zk::snark::placeholder_public_preprocessor<BlueprintFieldType, PlaceholderParams>::process(
            constrains_sys,
            assignment_table.move_public_table(),
            plonk_table_desc,
            lpc_scheme,
            permutation_size ) );

    PrivatePreprocessedData private_preprocessed_data(
        nil::crypto3::zk::snark::placeholder_private_preprocessor<BlueprintFieldType, PlaceholderParams>::process(
            constrains_sys,
            assignment_table.move_private_table(),
            plonk_table_desc ) );

    ProofSnarkType proof = ProverType::process( public_preprocessed_data,
                                                private_preprocessed_data,
                                                plonk_table_desc,
                                                constrains_sys,
                                                lpc_scheme );
    BOOST_LOG_TRIVIAL( info ) << "Proof generated";

    if ( !VerifyProof( proof, public_preprocessed_data, plonk_table_desc, constrains_sys, lpc_scheme ) )
    {
        return outcome::failure( ProverError::INVALID_PROOF_GENERATED );
    }

    BOOST_LOG_TRIVIAL( info ) << "Writing proof to " << proof_file_;
    auto filled_placeholder_proof = FillPlaceholderProof( proof, fri_params );
    bool res                      = encode_marshalling_to_file( proof_file_, filled_placeholder_proof, true );
    if ( res )
    {
        BOOST_LOG_TRIVIAL( info ) << "Proof written";
    }
    return ProofType;
}

outcome::result<GeniusProver::ProofType> GeniusProver::GenerateProof(
    const boost::filesystem::path &circuit_file,
    const boost::filesystem::path &assignment_table_file )
{
}

bool GeniusProver::VerifyProof( const ProofSnarkType         &proof,
                                const PublicPreprocessedData &public_data,
                                const TableDescriptionType   &desc,
                                const ConstraintSystemType   &constrains_sys,
                                const LpcScheme              &scheme ) const
{
    BOOST_LOG_TRIVIAL( info ) << "Verifying proof...";
    bool verification_result =
        crypto3::zk::snark::placeholder_verifier<BlueprintFieldType, PlaceholderParams>::process( public_data,
                                                                                                  proof,
                                                                                                  desc,
                                                                                                  constrains_sys,
                                                                                                  scheme );

    if ( verification_result )
    {
        BOOST_LOG_TRIVIAL( info ) << "Proof is verified";
    }
    else
    {
        BOOST_LOG_TRIVIAL( error ) << "Proof verification failed";
    }

    return verification_result;
}

GeniusProver::ConstraintSystemType GeniusProver::MakePlonkConstraintSystem(
    const GeniusAssigner::PlonkConstraintSystemType &constrains )
{
    ConstraintSystemType constraint_sys(
        crypto3::marshalling::types::make_plonk_constraint_system<ProverEndianess, ConstraintSystemType>(
            constrains ) );

    return constraint_sys;
}

std::pair<GeniusProver::TableDescriptionType, GeniusProver::AssignmentTableType> GeniusProver::
    MakePlonkTableDescription( const GeniusAssigner::PlonkAssignTableType &table )
{
    //auto marshalled_table =
    //    decode_marshalling_from_file<GeniusAssigner::PlonkAssignTableType>( assignment_table_file );
    //if ( !marshalled_table )
    //{
    //    return false;
    //}
    //auto [table_description, assignment_table] =
    return crypto3::marshalling::types::make_assignment_table<ProverEndianess, AssignmentTableType>( table );
}

GeniusProver::FriParams GeniusProver::MakeFRIParams( std::size_t degree_log,
                                                     const int   max_step      = 1,
                                                     std::size_t expand_factor = 0 )
{
    std::size_t r = degree_log - 1;

    return Lpc::fri_type::params_type(
        ( 1 << degree_log ) - 1, // max_degree
        crypto3::math::calculate_domain_set<BlueprintFieldType>( degree_log + expand_factor, r ),
        generate_random_step_list( r, max_step ),
        expand_factor );
}

GeniusProver::PublicPreprocessedData GeniusProver::ConstructPublicPreprocessedData(
    const ConstraintSystemType &constrains,
    const TableDescriptionType &desc,
    const AssignmentTableType  &tbl,
    const FriParams            &fri_params )
{
    std::size_t permutation_size = desc.witness_columns + desc.public_input_columns + component_constant_columns_;

    PublicPreprocessedData public_preprocessed_data( PublicPreprocessor::process( constrains,
                                                                                  tbl.move_public_table(),
                                                                                  desc,
                                                                                  LpcScheme( fri_params ),
                                                                                  permutation_size ) );

    return public_preprocessed_data;
}

GeniusProver::PrivatePreprocessedData GeniusProver::ConstructPrivatePreprocessedData()
{
}
