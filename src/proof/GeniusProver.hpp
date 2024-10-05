/**
 * @file       GeniusProver.hpp
 * @brief      Header file of the prover from circuit to zkproof
 * @date       2024-09-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _GENIUS_PROVER_HPP_
#define _GENIUS_PROVER_HPP_

#include <string>
#include <cstdint>

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

#include <boost/test/unit_test.hpp> // TODO: remove this. Required only because of an incorrect assert check in zk

#include "GeniusAssigner.hpp"

#include <nil/marshalling/status_type.hpp>
#include <nil/marshalling/field_type.hpp>
#include <nil/marshalling/endianness.hpp>
#include <nil/crypto3/zk/snark/arithmetization/plonk/params.hpp>
#include <nil/crypto3/zk/snark/arithmetization/plonk/constraint_system.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/constraint_system.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/assignment_table.hpp>
#include <nil/crypto3/algebra/curves/pallas.hpp>
#include <nil/crypto3/algebra/fields/arithmetic_params/pallas.hpp>

#include <nil/crypto3/marshalling/zk/types/placeholder/common_data.hpp>
#include <nil/crypto3/marshalling/zk/types/placeholder/proof.hpp>
#include <nil/crypto3/math/algorithms/calculate_domain_set.hpp>
#include <nil/crypto3/multiprecision/cpp_int.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/detail/placeholder_policy.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/params.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/preprocessor.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/profiling.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/proof.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/prover.hpp>
#include <nil/crypto3/zk/snark/systems/plonk/placeholder/verifier.hpp>

#include <nil/crypto3/marshalling/zk/types/commitments/lpc.hpp>

#include <nil/crypto3/algebra/curves/pallas.hpp>
#include <nil/crypto3/hash/keccak.hpp>

#include "outcome/outcome.hpp"

using namespace nil;

namespace sgns
{
    /**
     * @brief      Prover class of SuperGenius
     */
    class GeniusProver
    {
        using BlueprintFieldType   = typename crypto3::algebra::curves::pallas::base_field_type;
        using HashType             = crypto3::hashes::keccak_1600<256>;
        using ProverEndianess      = nil::marshalling::option::big_endian;
        using ConstraintSystemType = crypto3::zk::snark::plonk_constraint_system<BlueprintFieldType>;
        using TableDescriptionType = crypto3::zk::snark::plonk_table_description<BlueprintFieldType>;
        using PlonkColumn          = crypto3::zk::snark::plonk_column<BlueprintFieldType>;
        using AssignmentTableType  = crypto3::zk::snark::plonk_table<BlueprintFieldType, PlonkColumn>;
        using PublicTableType      = crypto3::zk::snark::plonk_public_table<BlueprintFieldType, PlonkColumn>;
        using LpcParams         = crypto3::zk::commitments::list_polynomial_commitment_params<HashType, HashType, 9, 2>;
        using Lpc               = crypto3::zk::commitments::list_polynomial_commitment<BlueprintFieldType, LpcParams>;
        using LpcScheme         = typename crypto3::zk::commitments::lpc_commitment_scheme<Lpc>;
        using FriParams         = typename Lpc::fri_type::params_type;
        using CircuitParams     = crypto3::zk::snark::placeholder_circuit_params<BlueprintFieldType>;
        using PlaceholderParams = crypto3::zk::snark::placeholder_params<CircuitParams, LpcScheme>;
        using PublicPreprocessor =
            crypto3::zk::snark::placeholder_public_preprocessor<BlueprintFieldType, PlaceholderParams>;
        using PublicPreprocessedData = PublicPreprocessor::preprocessed_data_type;
        using PrivatePreprocessor =
            crypto3::zk::snark::placeholder_private_preprocessor<BlueprintFieldType, PlaceholderParams>;
        using PrivatePreprocessedData = PrivatePreprocessor::preprocessed_data_type;
        using ProofSnarkType          = crypto3::zk::snark::placeholder_proof<BlueprintFieldType, PlaceholderParams>;
        using ProofType  = crypto3::marshalling::types::placeholder_proof<nil::marshalling::field_type<ProverEndianess>,
                                                                          ProofSnarkType>;
        using ProverType = crypto3::zk::snark::placeholder_prover<BlueprintFieldType, PlaceholderParams>;
        using PlonkTablePair = std::pair<TableDescriptionType, AssignmentTableType>;

    public:
        /**
         * @brief       Constructs a GeniusProver with component constant columns and expand factor
         * @param[in]   component_constant_columns 
         * @param[in]   expand_factor 
         */
        GeniusProver( std::size_t component_constant_columns = COMPONENT_CONSTANT_COLUMNS_DEFAULT,
                      std::size_t expand_factor              = EXPAND_FACTOR_DEFAULT ) :
            component_constant_columns_( component_constant_columns ), expand_factor_( expand_factor )
        {
        }

        enum class ProverError
        {
            INVALID_PROOF_GENERATED = 0,
            TABLE_PATH_ERROR,
            CIRCUIT_PATH_ERROR,
            PROOF_PATH_ERROR,
            EMPTY_PROOF
        };

        struct GeniusProof
        {
            GeniusProof( ProofType new_proof, ConstraintSystemType new_constrains, PublicTableType new_table ) :
                proof( std::move( new_proof ) ),           //
                constrains( std::move( new_constrains ) ), //
                table( std::move( new_table ) )            //
            {
            }

            ProofType            proof;
            ConstraintSystemType constrains;
            PublicTableType      table;
            std::uint64_t        usable_rows_amount;
            std::uint64_t        rows_amount;
        };

        outcome::result<GeniusProof> CreateProof( const GeniusAssigner::AssignerOutput &assigner_outputs ) const;

        outcome::result<GeniusProof> CreateProof( const std::string &circuit_file,
                                                  const std::string &assignment_table_file ) const;

        bool VerifyProof( const GeniusProof &proof ) const;
        bool VerifyProof( const ProofType &proof, const GeniusAssigner::AssignerOutput &assigner_outputs ) const;

        bool VerifyProof( const ProofSnarkType         &proof,
                          const PublicPreprocessedData &public_data,
                          const TableDescriptionType   &desc,
                          const ConstraintSystemType   &constrains_sys,
                          const LpcScheme              &scheme ) const;

        bool                 WriteProofToFile( const ProofType &proof, const std::string &path ) const;
        std::vector<uint8_t> WriteProofToVector( const ProofType &proof ) const;

    private:
        constexpr static const std::size_t COMPONENT_CONSTANT_COLUMNS_DEFAULT = 5;
        constexpr static const std::size_t EXPAND_FACTOR_DEFAULT              = 2;

        const std::size_t component_constant_columns_;
        const std::size_t expand_factor_;

        ConstraintSystemType MakePlonkConstraintSystem(
            const GeniusAssigner::PlonkConstraintSystemType &constrains ) const;

        PlonkTablePair MakePlonkTableDescription( const GeniusAssigner::PlonkAssignTableType &table ) const;

        FriParams MakeFRIParams( std::size_t rows_amount, const int max_step = 1, std::size_t expand_factor = 0 ) const;

        std::vector<std::size_t> GenerateRandomStepList( const std::size_t r, const int max_step ) const;

        ProofType FillPlaceholderProof( const ProofSnarkType &proof, const FriParams &commitment_params ) const;
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, GeniusProver::ProverError );

#endif
