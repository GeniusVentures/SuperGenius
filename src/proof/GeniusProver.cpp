/**
 * @file       GeniusProver.cpp
 * @brief      
 * @date       2024-09-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "GeniusProver.hpp"
#include "NilFileHelper.hpp"

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

namespace sgns
{

    outcome::result<GeniusProver::ProofType> GeniusProver::GenerateProof(
        const std::vector<GeniusAssigner::AssignerOutput> &assigner_outputs )
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

        if ( !VerifyProof( proof, public_preprocessed_data, plonk_table_desc, constrains_sys, lpc_scheme ) )
        {
            return outcome::failure( ProverError::INVALID_PROOF_GENERATED );
        }

        auto filled_placeholder_proof = FillPlaceholderProof( proof, fri_params );

        return filled_placeholder_proof;
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
        return crypto3::zk::snark::placeholder_verifier<BlueprintFieldType, PlaceholderParams>::process( public_data,
                                                                                                         proof,
                                                                                                         desc,
                                                                                                         constrains_sys,
                                                                                                         scheme );
    }

    bool GeniusProver::WriteProofToFile( const ProofType &proof, const std::string &path )
    {
        bool          ret = false;
        std::ofstream oproof;
        oproof.open( path, std::ios_base::out );
        if ( oproof.is_open() )
        {
            NilFileHelper::PrintMarshalledData( proof, oproof, false );

            oproof.close();
            ret = true;
        }

        return ret;
    }

    GeniusProver::ConstraintSystemType GeniusProver::MakePlonkConstraintSystem(
        const GeniusAssigner::PlonkConstraintSystemType &constrains )
    {
        ConstraintSystemType constraint_sys(
            crypto3::marshalling::types::make_plonk_constraint_system<ProverEndianess, ConstraintSystemType>(
                constrains ) );

        return constraint_sys;
    }

    GeniusProver::PlonkTablePair GeniusProver::MakePlonkTableDescription(
        const GeniusAssigner::PlonkAssignTableType &table )
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
                                                         const int   max_step,
                                                         std::size_t expand_factor )
    {
        std::size_t r = degree_log - 1;

        return Lpc::fri_type::params_type(
            ( 1 << degree_log ) - 1, // max_degree
            crypto3::math::calculate_domain_set<BlueprintFieldType>( degree_log + expand_factor, r ),
            GenerateRandomStepList( r, max_step ),
            expand_factor );
    }

    GeniusProver::ProofType GeniusProver::FillPlaceholderProof( const ProofSnarkType &proof,
                                                                const FriParams      &commitment_params )
    {
        using TTypeBase = nil::marshalling::field_type<ProverEndianess>;

        nil::marshalling::types::array_list<
            TTypeBase,
            typename crypto3::marshalling::types::commitment<TTypeBase,
                                                             typename ProofSnarkType::commitment_scheme_type>::type,
            nil::marshalling::option::sequence_size_field_prefix<
                nil::marshalling::types::integral<TTypeBase, std::uint8_t>>>
            filled_commitments;
        for ( const auto &it : proof.commitments )
        {
            filled_commitments.value().push_back(
                crypto3::marshalling::types::fill_commitment<ProverEndianess,
                                                             typename ProofSnarkType::commitment_scheme_type>(
                    it.second ) );
        }

        return ProofType( std::make_tuple(
            filled_commitments,
            crypto3::marshalling::types::fill_placeholder_evaluation_proof<ProverEndianess, ProofSnarkType>(
                proof.eval_proof,
                commitment_params ) ) );
    }

    std::vector<std::size_t> GeniusProver::GenerateRandomStepList( const std::size_t r, const int max_step )
    {
        using Distribution = std::uniform_int_distribution<int>;
        static std::random_device random_engine;

        std::vector<std::size_t> step_list;
        std::size_t              steps_sum = 0;
        while ( steps_sum != r )
        {
            if ( r - steps_sum <= max_step )
            {
                while ( r - steps_sum != 1 )
                {
                    step_list.emplace_back( r - steps_sum - 1 );
                    steps_sum += step_list.back();
                }
                step_list.emplace_back( 1 );
                steps_sum += step_list.back();
            }
            else
            {
                step_list.emplace_back( Distribution( 1, max_step )( random_engine ) );
                steps_sum += step_list.back();
            }
        }
        return step_list;
    }
}
