/**
 * @file       GeniusProver.hpp
 * @brief      
 * @date       2024-09-13
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _GENIUS_PROVER_HPP_
#define _GENIUS_PROVER_HPP_

#include <string>

using namespace nil;

namespace sgns
{
    class GeniusProver
    {
        using BlueprintFieldType   = typename crypto3::algebra::curves::pallas::base_field_type;
        using ConstraintSystemType = crypto3::zk::snark::plonk_constraint_system<BlueprintField>;
        using ProverEndianess      = nil::marshalling::option::big_endian;

    public:
        GeniusProver( std::size_t component_constant_columns = COMPONENT_CONSTANT_COLUMNS_DEFAULT,
                      std::size_t expand_factor              = EXPAND_FACTOR_DEFAULT ) :
            : component_constant_columns_( component_constant_columns ), expand_factor_( expand_factor )
        {
        }

        outcome::result<void> GenerateProof()
        {
        }

    private:
        constexpr static const std::size_t COMPONENT_CONSTANT_COLUMNS_DEFAULT = 5;
        constexpr static const std::size_t EXPAND_FACTOR_DEFAULT              = 2;

        const std::size_t component_constant_columns_;
        const std::size_t expand_factor_;

        ConstraintSystemType constraint_system_;

        bool prepare_for_operation( const boost::filesystem::path &circuit_file,
                                    const boost::filesystem::path &assignment_table_file )
        {
            using TTypeBase = marshalling::field_type<ProverEndianess>;
            using ConstraintMarshalling =
                nil::crypto3::marshalling::types::plonk_constraint_system<TTypeBase, ConstraintSystemType>;

            using Column          = crypto3::zk::snark::plonk_column<BlueprintFieldType>;
            using AssignmentTable = crypto3::zk::snark::plonk_table<BlueprintFieldType, Column>;

            {
                auto marshalled_value = detail::decode_marshalling_from_file<ConstraintMarshalling>( circuit_file );
                if ( !marshalled_value )
                {
                    return false;
                }
                constraint_system_.emplace(
                    crypto3::marshalling::types::make_plonk_constraint_system<ProverEndianess, ConstraintSystemType>(
                        *marshalled_value ) );
            }

            using TableValueMarshalling =
                crypto3::marshalling::types::plonk_assignment_table<TTypeBase, AssignmentTable>;
            auto marshalled_table =
                detail::decode_marshalling_from_file<TableValueMarshalling>( assignment_table_file );
            if ( !marshalled_table )
            {
                return false;
            }
            auto [table_description, assignment_table] =
                nil::crypto3::marshalling::types::make_assignment_table<ProverEndianess, AssignmentTable>(
                    *marshalled_table );
            table_description_.emplace( table_description );

            // Lambdas and grinding bits should be passed threw preprocessor directives
            std::size_t table_rows_log = std::ceil( std::log2( table_description_->rows_amount ) );

            fri_params_.emplace(
                detail::create_fri_params<typename Lpc::fri_type, BlueprintFieldType>( table_rows_log,
                                                                                       1,
                                                                                       expand_factor_ ) );

            std::size_t permutation_size = table_description_->witness_columns +
                                           table_description_->public_input_columns + component_constant_columns_;
            lpc_scheme_.emplace( *fri_params_ );

            BOOST_LOG_TRIVIAL( info ) << "Preprocessing public data";
            public_preprocessed_data_.emplace(
                nil::crypto3::zk::snark::placeholder_public_preprocessor<BlueprintFieldType, PlaceholderParams>::
                    process( *constraint_system_,
                             assignment_table.move_public_table(),
                             *table_description_,
                             *lpc_scheme_,
                             permutation_size ) );

            BOOST_LOG_TRIVIAL( info ) << "Preprocessing private data";
            private_preprocessed_data_.emplace(
                nil::crypto3::zk::snark::placeholder_private_preprocessor<BlueprintField, PlaceholderParams>::process(
                    *constraint_system_,
                    assignment_table.move_private_table(),
                    *table_description_ ) );
            return true;
        }
    };
}

#endif
