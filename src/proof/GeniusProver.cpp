/**
 * @file       GeniusProver.cpp
 * @brief      Source file of the prover from circuit to zkproof
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
        case ProverError::TABLE_PATH_ERROR:
            return "The assignment table file is not on the informed path.";
        case ProverError::CIRCUIT_PATH_ERROR:
            return "The circuit file is not on the informed path.";
        case ProverError::PROOF_PATH_ERROR:
            return "The proof file can't be saved on the informed path.";
        case ProverError::EMPTY_PROOF:
            return "The constrains are zeroad, so the Proof circuit is a constant function";
    }
    return "Unknown error";
}

namespace sgns
{

    outcome::result<GeniusProver::GeniusProof> GeniusProver::CreateProof(
        const GeniusAssigner::AssignerOutput &assigner_outputs ) const
    {
        auto constrains_sys = MakePlonkConstraintSystem( assigner_outputs.constrains );

        if ( ( constrains_sys.max_gates_degree() == 0 ) && ( constrains_sys.max_lookup_gates_degree() == 0 ) )
        {
            return outcome::failure( ProverError::EMPTY_PROOF );
        }

        auto [plonk_table_desc, assignment_table] = MakePlonkTableDescription( assigner_outputs.table );

        auto fri_params = MakeFRIParams( plonk_table_desc.rows_amount, 1, EXPAND_FACTOR_DEFAULT );

        LpcScheme lpc_scheme( fri_params );

        std::size_t permutation_size = plonk_table_desc.witness_columns + plonk_table_desc.public_input_columns +
                                       COMPONENT_CONSTANT_COLUMNS_DEFAULT;

        PublicPreprocessedData  public_preprocessed_data( PublicPreprocessor::process( constrains_sys,
                                                                                      assignment_table.public_table(),
                                                                                      plonk_table_desc,
                                                                                      lpc_scheme,
                                                                                      permutation_size ) );
        PrivatePreprocessedData private_preprocessed_data(
            PrivatePreprocessor::process( constrains_sys, assignment_table.move_private_table(), plonk_table_desc ) );

        ProofSnarkType proof = ProverType::process( public_preprocessed_data,
                                                    private_preprocessed_data,
                                                    plonk_table_desc,
                                                    constrains_sys,
                                                    lpc_scheme );

        auto filled_placeholder_proof = FillPlaceholderProof( proof, fri_params );

        auto marshalled_constrains =
            crypto3::marshalling::types::fill_plonk_constraint_system<ProverEndianess, ConstraintSystemType>(
                constrains_sys );

        auto marshalled_table =
            crypto3::marshalling::types::fill_assignment_table<ProverEndianess, AssignmentTableType>(
                std::get<4>( assigner_outputs.table.value() ).value(),
                assignment_table );

        GeniusProof retval( std::move( filled_placeholder_proof ),
                            std::move( marshalled_constrains ),
                            std::move( marshalled_table ) );

        return retval;
    }

    outcome::result<GeniusProver::GeniusProof> GeniusProver::CreateProof(
        const std::string &circuit_file,
        const std::string &assignment_table_file ) const
    {
        std::ifstream itable;
        itable.open( assignment_table_file, std::ios_base::in | std::ios::binary | std::ios::ate );
        if ( !itable )
        {
            return outcome::failure( ProverError::TABLE_PATH_ERROR );
        }
        OUTCOME_TRY( ( auto &&, plonk_table ),
                     NilFileHelper::DecodeMarshalledData<GeniusAssigner::PlonkAssignTableType>( itable ) );
        itable.close();
        std::ifstream icircuit;
        icircuit.open( assignment_table_file, std::ios_base::in | std::ios::binary | std::ios::ate );
        if ( !icircuit )
        {
            return outcome::failure( ProverError::CIRCUIT_PATH_ERROR );
        }
        OUTCOME_TRY( ( auto &&, plonk_constrains ),
                     NilFileHelper::DecodeMarshalledData<GeniusAssigner::PlonkConstraintSystemType>( icircuit ) );
        icircuit.close();
        GeniusAssigner::AssignerOutput assigner_outputs( plonk_constrains, plonk_table );

        return CreateProof( { assigner_outputs } );
    }

    bool GeniusProver::VerifyProof( const GeniusProof &proof )
    {
        auto plonk_table_desc = GeniusAssigner::GetPlonkTableDescription();

        auto [constructed_desc, assignment_table] =
            crypto3::marshalling::types::make_assignment_table<ProverEndianess, AssignmentTableType>( proof.table );

        auto constrains_sys =
            crypto3::marshalling::types::make_plonk_constraint_system<ProverEndianess, ConstraintSystemType>(
                proof.constrains );

        plonk_table_desc.usable_rows_amount = constructed_desc.usable_rows_amount;
        plonk_table_desc.rows_amount        = constructed_desc.rows_amount;

        auto fri_params = MakeFRIParams( plonk_table_desc.rows_amount, 1, EXPAND_FACTOR_DEFAULT );

        LpcScheme lpc_scheme( fri_params );

        std::size_t permutation_size = plonk_table_desc.witness_columns + plonk_table_desc.public_input_columns +
                                       COMPONENT_CONSTANT_COLUMNS_DEFAULT;

        auto pub_input = assignment_table.public_inputs();
        //auto pub_input = pub_parameters;
        //for ( auto input_vector : pub_input )
        //{
        //    std::cout << "input :";
        //    for ( auto input : input_vector )
        //    {
        //        std::cout << ", " << input;
        //    }
        //    std::cout << std::endl;
        //}

        PublicPreprocessedData public_preprocessed_data(
            crypto3::zk::snark::placeholder_public_preprocessor<BlueprintFieldType, PlaceholderParams>::process(
                constrains_sys,
                assignment_table.public_table(),
                plonk_table_desc,
                lpc_scheme,
                permutation_size ) );

        auto proof_snark =
            crypto3::marshalling::types::make_placeholder_proof<ProverEndianess, ProofSnarkType>( proof.proof );
        return crypto3::zk::snark::placeholder_verifier<BlueprintFieldType, PlaceholderParams>::process(
            public_preprocessed_data,
            proof_snark,
            plonk_table_desc,
            constrains_sys,
            lpc_scheme,
            pub_input );
    }

    bool GeniusProver::VerifyProof( const ProofType &proof, const GeniusAssigner::AssignerOutput &assigner_outputs )
    {
        auto constrains_sys = MakePlonkConstraintSystem( assigner_outputs.constrains );

        auto [plonk_table_desc, assignment_table] = MakePlonkTableDescription( assigner_outputs.table );

        auto fri_params = MakeFRIParams( plonk_table_desc.rows_amount, 1, EXPAND_FACTOR_DEFAULT );

        LpcScheme lpc_scheme( fri_params );

        std::size_t permutation_size = plonk_table_desc.witness_columns + plonk_table_desc.public_input_columns +
                                       COMPONENT_CONSTANT_COLUMNS_DEFAULT;

        PublicPreprocessedData public_preprocessed_data(
            crypto3::zk::snark::placeholder_public_preprocessor<BlueprintFieldType, PlaceholderParams>::process(
                constrains_sys,
                assignment_table.move_public_table(),
                plonk_table_desc,
                lpc_scheme,
                permutation_size ) );

        auto proof_snark =
            crypto3::marshalling::types::make_placeholder_proof<ProverEndianess, ProofSnarkType>( proof );
        return crypto3::zk::snark::placeholder_verifier<BlueprintFieldType, PlaceholderParams>::process(
            public_preprocessed_data,
            proof_snark,
            plonk_table_desc,
            constrains_sys,
            lpc_scheme );
    }

    bool GeniusProver::VerifyProof( const ProofSnarkType         &proof,
                                    const PublicPreprocessedData &public_data,
                                    const TableDescriptionType   &desc,
                                    const ConstraintSystemType   &constrains_sys,
                                    const LpcScheme              &scheme,
                                    std::vector<ParameterType>    pub_parameters )
    {
        return crypto3::zk::snark::placeholder_verifier<BlueprintFieldType, PlaceholderParams>::process(
            public_data,
            proof,
            desc,
            constrains_sys,
            scheme,
            pub_parameters );
    }

    bool GeniusProver::WriteProofToFile( const ProofType &proof, const std::string &path ) const
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

    std::vector<uint8_t> GeniusProver::WriteProofToVector( const ProofType &proof ) const
    {
        return NilFileHelper::GetMarshalledData( proof, false );
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
        return crypto3::marshalling::types::make_assignment_table<ProverEndianess, AssignmentTableType>( table );
    }

    GeniusProver::FriParams GeniusProver::MakeFRIParams( std::size_t rows_amount,
                                                         const unsigned int max_step,
                                                         std::size_t expand_factor )
    {
        std::size_t table_rows_log = std::ceil( std::log2( rows_amount ) );
        std::size_t r              = table_rows_log - 1;

        return Lpc::fri_type::params_type(
            ( 1 << table_rows_log ) - 1, // max_degree
            crypto3::math::calculate_domain_set<BlueprintFieldType>( table_rows_log + expand_factor, r ),
            GenerateRandomStepList( r, max_step ),
            expand_factor );
    }

    GeniusProver::ProofType GeniusProver::FillPlaceholderProof( const ProofSnarkType &proof,
                                                                const FriParams      &commitment_params ) const
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

    std::vector<std::size_t> GeniusProver::GenerateRandomStepList( const std::size_t r, const unsigned int max_step )
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
