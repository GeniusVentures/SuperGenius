/**
 * @file       GeniusAssigner.hpp
 * @brief      Header file of the assigner from bytecode to circuit
 * @date       2024-09-09
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef _GENIUS_ASSIGNER_HPP_
#define _GENIUS_ASSIGNER_HPP_

#include <cstdint>
#include <cstdio>
#include <nil/marshalling/status_type.hpp>
#include <nil/marshalling/field_type.hpp>
#include <nil/marshalling/endianness.hpp>
#include <nil/crypto3/zk/snark/arithmetization/plonk/params.hpp>
#include <nil/crypto3/zk/snark/arithmetization/plonk/constraint_system.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/constraint_system.hpp>
#include <nil/crypto3/marshalling/zk/types/plonk/assignment_table.hpp>
#include <nil/crypto3/algebra/curves/pallas.hpp>
#include <nil/crypto3/algebra/fields/arithmetic_params/pallas.hpp>
#include <nil/crypto3/algebra/curves/bls12.hpp>
#include <nil/blueprint/assigner.hpp>
#include <nil/blueprint/utils/satisfiability_check.hpp>


#include <boost/log/trivial.hpp>
#include "outcome/outcome.hpp"




using namespace nil;

namespace sgns
{
    class GeniusAssigner
    {
        using BlueprintFieldType   = typename crypto3::algebra::curves::pallas::base_field_type;
        using AssignerEndianess    = nil::marshalling::option::big_endian;
        using ArithmetizationType  = crypto3::zk::snark::plonk_constraint_system<BlueprintFieldType>;
        using ConstraintSystemType = crypto3::zk::snark::plonk_constraint_system<BlueprintFieldType>;
        using TableDescriptionType = crypto3::zk::snark::plonk_table_description<BlueprintFieldType>;
        using AssignmentTableType  = nil::blueprint::assignment_proxy<ArithmetizationType>;

    public:
        using PlonkConstraintSystemType =
            crypto3::marshalling::types::plonk_constraint_system<nil::marshalling::field_type<AssignerEndianess>,
                                                                 ConstraintSystemType>;
        using PlonkAssignTableType =
            crypto3::marshalling::types::plonk_assignment_table<nil::marshalling::field_type<AssignerEndianess>,
                                                                AssignmentTableType>;

        GeniusAssigner() :
            gen_mode_( nil::blueprint::generation_mode::assignments() | nil::blueprint::generation_mode::circuit() )

        {
            TableDescriptionType desc( WITNESS_COLUMNS,
                                       PUBLIC_INPUT_COLUMNS,
                                       COMPONENT_CONSTANT_COLUMNS + LOOKUP_CONSTANT_COLUMNS,
                                       COMPONENT_SELECTOR_COLUMNS + LOOKUP_SELECTOR_COLUMNS );

            assigner_instance_ = std::make_shared<nil::blueprint::assigner<BlueprintFieldType>>(
                desc,
                STACK_SIZE,
                LOG_LEVEL,
                MAX_NUM_PROVERS,
                TARGET_PROVER,
                gen_mode_,
                std::string( POLICY ),
                nil::blueprint::print_format::no_print,
                std::string( CHECK_VALIDITY ) == "check" );
        }

        ~GeniusAssigner() = default;

        enum class AssignerError
        {
            EMPTY_BYTECODE = 0,
            BYTECODE_MISMATCH,
            HAS_SIZE_ESTIMATION,
            TABLE_PATH_ERROR,
            CIRCUIT_PATH_ERROR,
            SELECTOR_COLUMNS_INVALID,
            WRONG_TARGET_PROVER,
            UNSATISFIED_CIRCUIT,
            TABLE_CIRCUIT_NUM_MISMATCH,
            CIRCUIT_NOT_FOUND,
        };

        struct AssignerOutput
        {
            explicit AssignerOutput( PlonkConstraintSystemType new_constrains, PlonkAssignTableType new_table ) :
                constrains( std::move( new_constrains ) ), table( std::move( new_table ) )
            {
            }

            PlonkConstraintSystemType constrains;
            PlonkAssignTableType      table;
        };

        outcome::result<std::vector<AssignerOutput>> GenerateCircuitAndTable( const std::vector<int> &public_inputs,
                                                                              const std::vector<int> &private_inputs,
                                                                              const std::string &bytecode_file_path );
        outcome::result<void> PrintCircuitAndTable( const std::vector<AssignerOutput> &public_inputs,
                                                    const std::string                 &table_path,
                                                    const std::string                 &circuit_path );

    private:
        //TODO - Check if better on cmakelists, since the zkllvm executable does it like this for plonk_table_description variables
        constexpr static const std::size_t                         WITNESS_COLUMNS            = 15;
        constexpr static const std::size_t                         PUBLIC_INPUT_COLUMNS       = 1;
        constexpr static const std::size_t                         COMPONENT_CONSTANT_COLUMNS = 5;
        constexpr static const std::size_t                         LOOKUP_CONSTANT_COLUMNS    = 30;
        constexpr static const std::size_t                         COMPONENT_SELECTOR_COLUMNS = 50;
        constexpr static const std::size_t                         LOOKUP_SELECTOR_COLUMNS    = 6;
        constexpr static const long                                STACK_SIZE                 = 4000000;
        constexpr static const boost::log::trivial::severity_level LOG_LEVEL       = boost::log::trivial::info;
        constexpr static const std::uint32_t                       MAX_NUM_PROVERS = 1;
        constexpr static const std::uint32_t    TARGET_PROVER  = std::numeric_limits<std::uint32_t>::max();
        constexpr static const std::string_view POLICY         = "default";
        constexpr static const std::string_view CHECK_VALIDITY = "";

        std::shared_ptr<nil::blueprint::assigner<BlueprintFieldType>> assigner_instance_;
        nil::blueprint::generation_mode                               gen_mode_;

        enum class PrintTableKind
        {
            SINGLE_PROVER = 0,
            MULTI_PROVER  = 1,
        };

        struct TableVectors
        {
            std::vector<typename AssignmentTableType::field_type::value_type> witness_values;
            std::vector<typename AssignmentTableType::field_type::value_type> public_input_values;
            std::vector<typename AssignmentTableType::field_type::value_type> constant_values;
            std::vector<typename AssignmentTableType::field_type::value_type> selector_values;
        };

        PlonkAssignTableType BuildPlonkAssignmentTable( const AssignmentTableType &table_proxy,
                                                        PrintTableKind             print_kind,
                                                        std::uint32_t              ComponentConstantColumns,
                                                        std::uint32_t              ComponentSelectorColumns );

        std::uint32_t GetUsableRowsAmount( const AssignmentTableType &table_proxy, PrintTableKind print_kind );

        TableVectors GetTableVectors( const AssignmentTableType &table_proxy,
                                      PrintTableKind             print_kind,
                                      std::uint32_t              ComponentConstantColumns,
                                      std::uint32_t              ComponentSelectorColumns,
                                      std::uint32_t              padded_rows_amount );

        PlonkConstraintSystemType BuildPlonkConstraintSystem(
            const nil::blueprint::circuit_proxy<ArithmetizationType> &circuit_proxy,
            const AssignmentTableType                                &table_proxy,
            bool                                                      rename_required );

        template <typename ValueType, typename ContainerType>
        void FillVectorValue( std::vector<ValueType>                   &table_values,
                              const ContainerType                      &table_col,
                              typename std::vector<ValueType>::iterator start );
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, GeniusAssigner::AssignerError );

#endif
