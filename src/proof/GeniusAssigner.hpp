/**
 * @file       GeniusAssigner.hpp
 * @brief      Header file of the assigner from bytecode to circuit
 * @date       2024-09-09
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

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
#include <nil/blueprint/asserts.hpp>
#include <nil/blueprint/utils/satisfiability_check.hpp>

#include <boost/json/src.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>
#include "outcome/outcome.hpp"

//#include <nil/crypto3/algebra/curves/ed25519.hpp>
//#include <nil/crypto3/algebra/fields/arithmetic_params/ed25519.hpp>
//#include <nil/crypto3/algebra/fields/arithmetic_params/bls12.hpp>

//#include <nil/crypto3/zk/snark/arithmetization/plonk/params.hpp>
//#include <nil/crypto3/zk/snark/arithmetization/plonk/constraint_system.hpp>

//#include <ios>

//#include <nil/crypto3/hash/sha2.hpp>
//#include <nil/crypto3/hash/poseidon.hpp>

//#include <nil/marshalling/status_type.hpp>
//#include <nil/marshalling/field_type.hpp>
//#include <nil/marshalling/endianness.hpp>

//#include <llvm/Support/CommandLine.h>
//#include <llvm/Support/Signals.h>

#ifndef _GENIUS_ASSIGNER_HPP_
#define _GENIUS_ASSIGNER_HPP_

using namespace nil;
//using namespace nil::crypto3;
using namespace nil::blueprint;

namespace sgns
{
    class GeniusAssigner
    {
        using BlueprintFieldType   = typename crypto3::algebra::curves::pallas::base_field_type;
        using AssignerEndianess    = nil::marshalling::option::big_endian;
        using ArithmetizationType  = crypto3::zk::snark::plonk_constraint_system<BlueprintFieldType>;
        using ConstraintSystemType = crypto3::zk::snark::plonk_constraint_system<BlueprintFieldType>;

    public:
        using PlonkConstraintSystemType =
            crypto3::marshalling::types::plonk_constraint_system<nil::marshalling::field_type<AssignerEndianess>,
                                                                 ConstraintSystemType>;
        using PlonkAssignTableType =
            crypto3::marshalling::types::plonk_assignment_table<nil::marshalling::field_type<AssignerEndianess>,
                                                                assignment_proxy<ArithmetizationType>>;
        using TableDescriptionType = crypto3::zk::snark::plonk_table_description<BlueprintFieldType>;

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

        GeniusAssigner() :
            gen_mode_( nil::blueprint::generation_mode::assignments() | nil::blueprint::generation_mode::circuit() ),
            table_description_( WITNESS_COLUMNS,
                                PUBLIC_INPUT_COLUMNS,
                                COMPONENT_CONSTANT_COLUMNS + LOOKUP_CONSTANT_COLUMNS,
                                COMPONENT_SELECTOR_COLUMNS + LOOKUP_SELECTOR_COLUMNS )

        {
            nil::crypto3::zk::snark::plonk_table_description<BlueprintFieldType> desc(
                WITNESS_COLUMNS,
                PUBLIC_INPUT_COLUMNS,
                COMPONENT_CONSTANT_COLUMNS + LOOKUP_CONSTANT_COLUMNS,
                COMPONENT_SELECTOR_COLUMNS + LOOKUP_SELECTOR_COLUMNS );

            nil::blueprint::print_format circuit_output_print_format = no_print;

            assigner_instance_ = std::make_shared<nil::blueprint::assigner<BlueprintFieldType>>(
                desc,
                STACK_SIZE,
                LOG_LEVEL,
                MAX_NUM_PROVERS,
                TARGET_PROVER,
                gen_mode_,
                std::string( POLICY ),
                circuit_output_print_format,
                std::string( CHECK_VALIDITY ) == "check" );
        }

        outcome::result<std::vector<AssignerOutput>> GenerateCircuitAndTable( const std::vector<int> &public_inputs,
                                                                              const std::vector<int> &private_inputs,
                                                                              const std::string &bytecode_file_path );
        outcome::result<void> PrintCircuitAndTable( const std::vector<AssignerOutput> &public_inputs,
                                                    const std::string                 &table_path,
                                                    const std::string                 &circuit_path );

        ~GeniusAssigner() = default;
        crypto3::zk::snark::plonk_table_description<BlueprintFieldType> table_description_;

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

        enum class print_table_kind
        {
            MULTI_PROVER,
            SINGLE_PROVER
        };

        bool read_json( std::string input_file_name, boost::json::value &input_json_value )
        {
            std::ifstream input_file( input_file_name.c_str() );
            if ( !input_file.is_open() )
            {
                std::cerr << "Could not open the file - '" << input_file_name << "'" << std::endl;
                return false;
            }

            boost::json::stream_parser p;
            boost::json::error_code    ec;
            while ( !input_file.eof() )
            {
                char input_string[256];
                input_file.read( input_string, sizeof( input_string ) - 1 );
                input_string[input_file.gcount()] = '\0';
                p.write( input_string, ec );
                if ( ec )
                {
                    std::cerr << "JSON parsing of input failed" << std::endl;
                    return false;
                }
            }
            p.finish( ec );
            if ( ec )
            {
                std::cerr << "JSON parsing of input failed" << std::endl;
                return false;
            }
            input_json_value = p.release();
            if ( !input_json_value.is_array() )
            {
                std::cerr << "Array of arguments is expected in JSON file" << std::endl;
                return false;
            }
            return true;
        }

        template <typename ValueType, typename ContainerType>
        void fill_vector_value( std::vector<ValueType>                   &table_values,
                                const ContainerType                      &table_col,
                                typename std::vector<ValueType>::iterator start )
        {
            std::copy( table_col.begin(), table_col.end(), start );
        }

        template <typename Endianness, typename ArithmetizationType, typename BlueprintFieldType>
        nil::crypto3::marshalling::types::plonk_assignment_table<nil::marshalling::field_type<Endianness>,
                                                                 assignment_proxy<ArithmetizationType>>
        BuildPlonkAssignmentTable( const assignment_proxy<ArithmetizationType> &table_proxy,
                                   print_table_kind                             print_kind,
                                   std::uint32_t                                ComponentConstantColumns,
                                   std::uint32_t                                ComponentSelectorColumns )
        {
            using AssignmentTableType = assignment_proxy<ArithmetizationType>;

            std::uint32_t total_size;
            std::uint32_t shared_size        = ( print_kind == print_table_kind::MULTI_PROVER ) ? 1 : 0;
            std::uint32_t public_input_size  = table_proxy.public_inputs_amount();
            std::uint32_t usable_rows_amount = GetUsableRowsAmount<ArithmetizationType>( table_proxy, print_kind );
            //std::uint32_t total_columns      = table_proxy.witnesses_amount() + shared_size +
            //                              table_proxy.public_inputs_amount() + table_proxy.selectors_amount() +
            //                              table_proxy.selectors_amount();

            std::uint32_t padded_rows_amount = std::pow( 2, std::ceil( std::log2( usable_rows_amount ) ) );
            if ( padded_rows_amount == usable_rows_amount )
            {
                padded_rows_amount *= 2;
            }
            if ( padded_rows_amount < 8 )
            {
                padded_rows_amount = 8;
            }
            //total_size = padded_rows_amount * total_columns;

            using TTypeBase = nil::marshalling::field_type<Endianness>;
            using table_value_marshalling_type =
                nil::crypto3::marshalling::types::plonk_assignment_table<TTypeBase, AssignmentTableType>;

            using column_type = typename crypto3::zk::snark::plonk_column<BlueprintFieldType>;

            auto table_vectors = GetTableVectors<AssignmentTableType, BlueprintFieldType>( table_proxy,
                                                                                           print_kind,
                                                                                           ComponentConstantColumns,
                                                                                           ComponentSelectorColumns,
                                                                                           padded_rows_amount );

            auto filled_val = table_value_marshalling_type( std::make_tuple(
                nil::marshalling::types::integral<TTypeBase, std::size_t>( table_proxy.witnesses_amount() ),
                nil::marshalling::types::integral<TTypeBase, std::size_t>( table_proxy.public_inputs_amount() +
                                                                           shared_size ),
                nil::marshalling::types::integral<TTypeBase, std::size_t>( table_proxy.constants_amount() ),
                nil::marshalling::types::integral<TTypeBase, std::size_t>( table_proxy.selectors_amount() ),
                nil::marshalling::types::integral<TTypeBase, std::size_t>( usable_rows_amount ),
                nil::marshalling::types::integral<TTypeBase, std::size_t>( padded_rows_amount ),
                nil::crypto3::marshalling::types::
                    fill_field_element_vector<typename AssignmentTableType::field_type::value_type, Endianness>(
                        table_vectors.witness_values ),
                nil::crypto3::marshalling::types::
                    fill_field_element_vector<typename AssignmentTableType::field_type::value_type, Endianness>(
                        table_vectors.public_input_values ),
                nil::crypto3::marshalling::types::
                    fill_field_element_vector<typename AssignmentTableType::field_type::value_type, Endianness>(
                        table_vectors.constant_values ),
                nil::crypto3::marshalling::types::
                    fill_field_element_vector<typename AssignmentTableType::field_type::value_type, Endianness>(
                        table_vectors.selector_values ) ) );

            return filled_val;
        }

        template <typename ArithmetizationType>
        std::uint32_t GetUsableRowsAmount( const assignment_proxy<ArithmetizationType> &table_proxy,
                                           print_table_kind                             print_kind )
        {
            std::uint32_t           usable_rows_amount     = 0;
            std::uint32_t           max_shared_size        = 0;
            std::uint32_t           max_witness_size       = 0;
            std::uint32_t           max_public_inputs_size = 0;
            std::uint32_t           max_constant_size      = 0;
            std::uint32_t           max_selector_size      = 0;
            std::uint32_t           shared_size            = 0;
            std::uint32_t           witness_size           = table_proxy.witnesses_amount();
            std::uint32_t           constant_size          = table_proxy.constants_amount();
            std::uint32_t           selector_size          = table_proxy.selectors_amount();
            std::set<std::uint32_t> lookup_constant_cols   = {};
            std::set<std::uint32_t> lookup_selector_cols   = {};

            if ( print_kind == print_table_kind::MULTI_PROVER )
            {
                witness_size         = 0;
                shared_size          = 1;
                constant_size        = 0;
                selector_size        = 0;
                usable_rows_amount   = table_proxy.get_used_rows().size();
                lookup_constant_cols = table_proxy.get_lookup_constant_cols();
                lookup_selector_cols = table_proxy.get_lookup_selector_cols();
            }

            for ( std::uint32_t i = 0; i < table_proxy.public_inputs_amount(); i++ )
            {
                max_public_inputs_size = std::max( max_public_inputs_size, table_proxy.public_input_column_size( i ) );
            }
            for ( std::uint32_t i = 0; i < shared_size; i++ )
            {
                max_shared_size = std::max( max_shared_size, table_proxy.shared_column_size( i ) );
            }

            for ( std::uint32_t i = 0; i < witness_size; i++ )
            {
                max_witness_size = std::max( max_witness_size, table_proxy.witness_column_size( i ) );
            }
            for ( std::uint32_t i = 0; i < constant_size; i++ )
            {
                max_constant_size = std::max( max_constant_size, table_proxy.constant_column_size( i ) );
            }
            for ( const auto &i : lookup_constant_cols )
            {
                max_constant_size = std::max( max_constant_size, table_proxy.constant_column_size( i ) );
            }
            for ( std::uint32_t i = 0; i < selector_size; i++ )
            {
                max_selector_size = std::max( max_selector_size, table_proxy.selector_column_size( i ) );
            }
            for ( const auto &i : lookup_selector_cols )
            {
                max_selector_size = std::max( max_selector_size, table_proxy.selector_column_size( i ) );
            }

            usable_rows_amount = std::max( { usable_rows_amount,
                                             max_shared_size,
                                             max_witness_size,
                                             max_public_inputs_size,
                                             max_constant_size,
                                             max_selector_size } );
            return usable_rows_amount;
        }

        template <typename AssignmentTableType>
        struct AssignTableValueVectors
        {
            std::vector<typename AssignmentTableType::field_type::value_type> witness_values;
            std::vector<typename AssignmentTableType::field_type::value_type> public_input_values;
            std::vector<typename AssignmentTableType::field_type::value_type> constant_values;
            std::vector<typename AssignmentTableType::field_type::value_type> selector_values;
        };

        template <typename AssignmentTableType, typename BlueprintFieldType>
        AssignTableValueVectors<AssignmentTableType> GetTableVectors(
            const assignment_proxy<ArithmetizationType> &table_proxy,
            print_table_kind                             print_kind,
            std::uint32_t                                ComponentConstantColumns,
            std::uint32_t                                ComponentSelectorColumns,
            std::uint32_t                                padded_rows_amount )
        {
            std::uint32_t shared_size = ( print_kind == print_table_kind::MULTI_PROVER ) ? 1 : 0;
            AssignTableValueVectors<AssignmentTableType> table_vectors;
            table_vectors.witness_values.resize( padded_rows_amount * table_proxy.witnesses_amount(), 0 );
            table_vectors.public_input_values.resize( padded_rows_amount *
                                                          ( table_proxy.public_inputs_amount() + shared_size ),
                                                      0 );
            table_vectors.constant_values.resize( padded_rows_amount * table_proxy.constants_amount(), 0 );
            table_vectors.selector_values.resize( padded_rows_amount * table_proxy.selectors_amount(), 0 );
            if ( print_kind == print_table_kind::SINGLE_PROVER )
            {
                auto it = table_vectors.witness_values.begin();
                for ( std::uint32_t i = 0; i < table_proxy.witnesses_amount(); i++ )
                {
                    fill_vector_value<typename AssignmentTableType::field_type::value_type,
                                      typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                        table_vectors.witness_values,
                        table_proxy.witness( i ),
                        it );
                    it += padded_rows_amount;
                }
                it = table_vectors.public_input_values.begin();
                for ( std::uint32_t i = 0; i < table_proxy.public_inputs_amount(); i++ )
                {
                    fill_vector_value<typename AssignmentTableType::field_type::value_type,
                                      typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                        table_vectors.public_input_values,
                        table_proxy.public_input( i ),
                        it );
                    it += padded_rows_amount;
                }
                it = table_vectors.constant_values.begin();
                for ( std::uint32_t i = 0; i < table_proxy.constants_amount(); i++ )
                {
                    fill_vector_value<typename AssignmentTableType::field_type::value_type,
                                      typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                        table_vectors.constant_values,
                        table_proxy.constant( i ),
                        it );
                    it += padded_rows_amount;
                }
                it = table_vectors.selector_values.begin();
                for ( std::uint32_t i = 0; i < table_proxy.selectors_amount(); i++ )
                {
                    fill_vector_value<typename AssignmentTableType::field_type::value_type,
                                      typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                        table_vectors.selector_values,
                        table_proxy.selector( i ),
                        it );
                    it += padded_rows_amount;
                }
            }
            else
            {
                const auto   &rows          = table_proxy.get_used_rows();
                const auto   &selector_rows = table_proxy.get_used_selector_rows();
                std::uint32_t witness_idx   = 0;
                // witness
                for ( std::size_t i = 0; i < table_proxy.witnesses_amount(); i++ )
                {
                    const auto    column_size = table_proxy.witness_column_size( i );
                    std::uint32_t offset      = 0;
                    for ( const auto &j : rows )
                    {
                        if ( j < column_size )
                        {
                            table_vectors.witness_values[witness_idx + offset] = table_proxy.witness( i, j );
                            offset++;
                        }
                    }
                    witness_idx += padded_rows_amount;
                }
                // public input
                auto it_pub_inp = table_vectors.public_input_values.begin();
                for ( std::uint32_t i = 0; i < table_proxy.public_inputs_amount(); i++ )
                {
                    fill_vector_value<typename AssignmentTableType::field_type::value_type,
                                      typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                        table_vectors.public_input_values,
                        table_proxy.public_input( i ),
                        it_pub_inp );
                    it_pub_inp += padded_rows_amount;
                }
                for ( std::uint32_t i = 0; i < shared_size; i++ )
                {
                    fill_vector_value<typename AssignmentTableType::field_type::value_type,
                                      typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                        table_vectors.public_input_values,
                        table_proxy.shared( i ),
                        it_pub_inp );
                    it_pub_inp += padded_rows_amount;
                }
                // constant
                std::uint32_t constant_idx = 0;
                for ( std::uint32_t i = 0; i < ComponentConstantColumns; i++ )
                {
                    const auto    column_size = table_proxy.constant_column_size( i );
                    std::uint32_t offset      = 0;
                    for ( const auto &j : rows )
                    {
                        if ( j < column_size )
                        {
                            table_vectors.constant_values[constant_idx + offset] = table_proxy.constant( i, j );
                            offset++;
                        }
                    }

                    constant_idx += padded_rows_amount;
                }

                auto it_const = table_vectors.constant_values.begin() + constant_idx;
                for ( std::uint32_t i = ComponentConstantColumns; i < table_proxy.constants_amount(); i++ )
                {
                    fill_vector_value<typename AssignmentTableType::field_type::value_type,
                                      typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                        table_vectors.constant_values,
                        table_proxy.constant( i ),
                        it_const );
                    it_const     += padded_rows_amount;
                    constant_idx += padded_rows_amount;
                }
                // selector
                std::uint32_t selector_idx = 0;
                for ( std::uint32_t i = 0; i < ComponentSelectorColumns; i++ )
                {
                    const auto    column_size = table_proxy.selector_column_size( i );
                    std::uint32_t offset      = 0;
                    for ( const auto &j : rows )
                    {
                        if ( j < column_size )
                        {
                            if ( selector_rows.find( j ) != selector_rows.end() )
                            {
                                table_vectors.selector_values[selector_idx + offset] = table_proxy.selector( i, j );
                            }
                            offset++;
                        }
                    }
                    selector_idx += padded_rows_amount;
                }

                auto it_selector = table_vectors.selector_values.begin();
                for ( std::uint32_t i = ComponentSelectorColumns; i < table_proxy.selectors_amount(); i++ )
                {
                    fill_vector_value<typename AssignmentTableType::field_type::value_type,
                                      typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                        table_vectors.selector_values,
                        table_proxy.selector( i ),
                        it_selector );
                    it_selector  += padded_rows_amount;
                    selector_idx += padded_rows_amount;
                }
            }

            return table_vectors;
        }

        template <typename Endianness, typename ArithmetizationType>
        void print_assignment_table(
            const nil::crypto3::marshalling::types::plonk_assignment_table<nil::marshalling::field_type<Endianness>,
                                                                           assignment_proxy<ArithmetizationType>>
                         &table,
            std::ostream &out = std::cout )
        {
            std::vector<std::uint8_t> cv;
            cv.resize( table.length(), 0x00 );
            auto                          write_iter = cv.begin();
            nil::marshalling::status_type status     = table.write( write_iter, cv.size() );
            out.write( reinterpret_cast<char *>( cv.data() ), cv.size() );
        }

        template <typename Endianness, typename ArithmetizationType, typename ConstraintSystemType>
        nil::crypto3::marshalling::types::plonk_constraint_system<nil::marshalling::field_type<Endianness>,
                                                                  ConstraintSystemType>
        BuildPlonkConstraintSystem( const circuit_proxy<ArithmetizationType>    &circuit_proxy,
                                    const assignment_proxy<ArithmetizationType> &table_proxy,
                                    bool                                         rename_required )
        {
            using TTypeBase = nil::marshalling::field_type<Endianness>;
            using value_marshalling_type =
                nil::crypto3::marshalling::types::plonk_constraint_system<TTypeBase, ConstraintSystemType>;
            using AssignmentTableType = assignment_proxy<ArithmetizationType>;
            using variable_type =
                crypto3::zk::snark::plonk_variable<typename AssignmentTableType::field_type::value_type>;

            const auto                                         &gates          = circuit_proxy.gates();
            const auto                                         &used_gates_idx = circuit_proxy.get_used_gates();
            typename ConstraintSystemType::gates_container_type used_gates;
            for ( const auto &it : used_gates_idx )
            {
                used_gates.push_back( gates[it] );
            }

            const auto &copy_constraints = circuit_proxy.copy_constraints();
            typename ConstraintSystemType::copy_constraints_container_type used_copy_constraints;
            const auto &used_copy_constraints_idx = circuit_proxy.get_used_copy_constraints();
            for ( const auto &it : used_copy_constraints_idx )
            {
                used_copy_constraints.push_back( copy_constraints[it] );
            }

            if ( rename_required )
            {
                const auto   &used_rows = table_proxy.get_used_rows();
                std::uint32_t local_row = 0;
                for ( const auto &row : used_rows )
                {
                    for ( auto &constraint : used_copy_constraints )
                    {
                        const auto first_var  = constraint.first;
                        const auto second_var = constraint.second;
                        if ( ( first_var.type == variable_type::column_type::witness ||
                               first_var.type == variable_type::column_type::constant ) &&
                             first_var.rotation == row )
                        {
                            constraint.first =
                                variable_type( first_var.index, local_row, first_var.relative, first_var.type );
                        }
                        if ( ( second_var.type == variable_type::column_type::witness ||
                               second_var.type == variable_type::column_type::constant ) &&
                             second_var.rotation == row )
                        {
                            constraint.second =
                                variable_type( second_var.index, local_row, second_var.relative, second_var.type );
                        }
                    }
                    local_row++;
                }
            }
            const auto                                                &lookup_gates = circuit_proxy.lookup_gates();
            typename ConstraintSystemType::lookup_gates_container_type used_lookup_gates;
            const auto &used_lookup_gates_idx = circuit_proxy.get_used_lookup_gates();
            for ( const auto &it : used_lookup_gates_idx )
            {
                used_lookup_gates.push_back( lookup_gates[it] );
            }

            const auto                                       &lookup_tables = circuit_proxy.lookup_tables();
            typename ConstraintSystemType::lookup_tables_type used_lookup_tables;
            const auto &used_lookup_tables_idx = circuit_proxy.get_used_lookup_tables();
            for ( const auto &it : used_lookup_tables_idx )
            {
                used_lookup_tables.push_back( lookup_tables[it] );
            }

            auto filled_val = value_marshalling_type( std::make_tuple(
                nil::crypto3::marshalling::types::
                    fill_plonk_gates<Endianness, typename ConstraintSystemType::gates_container_type::value_type>(
                        used_gates ),
                nil::crypto3::marshalling::types::
                    fill_plonk_copy_constraints<Endianness, typename ConstraintSystemType::variable_type>(
                        used_copy_constraints ),
                nil::crypto3::marshalling::types::fill_plonk_lookup_gates<
                    Endianness,
                    typename ConstraintSystemType::lookup_gates_container_type::value_type>( used_lookup_gates ),
                nil::crypto3::marshalling::types::
                    fill_plonk_lookup_tables<Endianness, typename ConstraintSystemType::lookup_tables_type::value_type>(
                        used_lookup_tables ) ) );

            return filled_val;
        }

        template <typename Endianness, typename ConstraintSystemType>
        void print_circuit(
            const nil::crypto3::marshalling::types::plonk_constraint_system<nil::marshalling::field_type<Endianness>,
                                                                            ConstraintSystemType> &circuit,

            std::ostream &out = std::cout )

        {
            std::vector<std::uint8_t> cv;
            cv.resize( circuit.length(), 0x00 );
            auto                          write_iter = cv.begin();
            nil::marshalling::status_type status     = circuit.write( write_iter, cv.size() );
            out.write( reinterpret_cast<char *>( cv.data() ), cv.size() );
        }
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, GeniusAssigner::AssignerError );

#endif
