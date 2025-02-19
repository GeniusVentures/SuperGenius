/**
 * @file       GeniusAssigner.cpp
 * @brief      Source file of the assigner from bytecode to circuit
 * @date       2024-09-09
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <boost/json/src.hpp>

#include "GeniusAssigner.hpp"
#include "NilFileHelper.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, GeniusAssigner::AssignerError, e )
{
    using AssignerError = sgns::GeniusAssigner::AssignerError;
    switch ( e )
    {
        case AssignerError::EMPTY_BYTECODE:
            return "No bytecode IR file provided. Please check the file path";
        case AssignerError::BYTECODE_MISMATCH:
            return "Bytecode IR doesn't match the public/private parameters";
        case AssignerError::HAS_SIZE_ESTIMATION:
            return "Has Size estimation. Don't know what that means";
        case AssignerError::TABLE_PATH_ERROR:
            return "The chosen file path for the table file can't be opened";
        case AssignerError::CIRCUIT_PATH_ERROR:
            return "The chosen file path for the circuit file can't be opened";
        case AssignerError::SELECTOR_COLUMNS_INVALID:
            return "Number of selector columns is invalid";
        case AssignerError::WRONG_TARGET_PROVER:
            return "The target prover is outside the prover's range";
        case AssignerError::UNSATISFIED_CIRCUIT:
            return "The generated circuit and table are not consistent with each other";
        case AssignerError::TABLE_CIRCUIT_NUM_MISMATCH:
            return "The number of circuit and tables don't match";
        case AssignerError::CIRCUIT_NOT_FOUND:
            return "The desired circuit wasn't found";
    }
    return "Unknown error";
}

namespace sgns
{

    outcome::result<std::vector<GeniusAssigner::AssignerOutput>> GeniusAssigner::GenerateCircuitAndTable(
        const boost::json::array &public_inputs_json,
        const boost::json::array &private_inputs_json,
        const std::string        &bytecode_file_payload )
    {
        if ( !assigner_instance_->parse_ir_buffer( bytecode_file_payload.data() ) )
        {
            return outcome::failure( AssignerError::EMPTY_BYTECODE );
        }
        if ( !assigner_instance_->evaluate( public_inputs_json, private_inputs_json ) )
        {
            return outcome::failure( AssignerError::BYTECODE_MISMATCH );
        }
        if ( gen_mode_.has_size_estimation() )
        {
            return outcome::failure( AssignerError::HAS_SIZE_ESTIMATION );
        }

        // pack lookup tables
        if ( assigner_instance_->circuits[0].get_reserved_tables().size() > 0 )
        {
            std::vector<std::size_t> lookup_columns_indices;
            lookup_columns_indices.resize( LOOKUP_CONSTANT_COLUMNS );
            // fill ComponentConstantColumns, ComponentConstantColumns + 1, ...
            std::iota( lookup_columns_indices.begin(), lookup_columns_indices.end(), COMPONENT_CONSTANT_COLUMNS );
            // check if lookup selectors were not used
            auto max_used_selector_idx = assigner_instance_->assignments[0].selectors_amount() - 1;
            while ( max_used_selector_idx > 0 )
            {
                max_used_selector_idx--;
                if ( assigner_instance_->assignments[0].selector( max_used_selector_idx ).size() > 0 )
                {
                    break;
                }
            }

            if ( max_used_selector_idx >= COMPONENT_SELECTOR_COLUMNS )
            {
                return outcome::failure( AssignerError::SELECTOR_COLUMNS_INVALID );
            }
            std::uint32_t max_lookup_rows    = 500000;
            auto          usable_rows_amount = crypto3::zk::snark::pack_lookup_tables_horizontal(
                assigner_instance_->circuits[0].get_reserved_indices(),
                assigner_instance_->circuits[0].get_reserved_tables(),
                assigner_instance_->circuits[0].get(),
                assigner_instance_->assignments[0].get(),
                lookup_columns_indices,
                max_used_selector_idx + 1,
                0,
                max_lookup_rows );
        }

        constexpr std::uint32_t invalid_target_prover = std::numeric_limits<std::uint32_t>::max();

        std::vector<PlonkAssignTableType>      AssignmentTableVector;
        std::vector<PlonkConstraintSystemType> PlonkCircuitVector;
        // print assignment tables and circuits
        if ( assigner_instance_->assignments.size() == 1 &&
             ( TARGET_PROVER == 0 || TARGET_PROVER == invalid_target_prover ) )
        {
            // print assignment table
            if ( gen_mode_.has_assignments() )
            {
                auto AssignmentTable = BuildPlonkAssignmentTable( assigner_instance_->assignments[0],
                                                                  PrintTableKind::SINGLE_PROVER,
                                                                  COMPONENT_CONSTANT_COLUMNS,
                                                                  COMPONENT_SELECTOR_COLUMNS );
                AssignmentTableVector.push_back( AssignmentTable );
            }

            // print circuit
            if ( gen_mode_.has_circuit() )
            {
                auto PlonkCircuit = BuildPlonkConstraintSystem( assigner_instance_->circuits[0],
                                                                assigner_instance_->assignments[0],
                                                                false );
                PlonkCircuitVector.push_back( PlonkCircuit );
            }
            if ( ( CHECK_VALIDITY == "check" ) && gen_mode_.has_assignments() && gen_mode_.has_circuit() )
            {
                if ( !nil::blueprint::is_satisfied( assigner_instance_->circuits[0].get(),
                                                    assigner_instance_->assignments[0].get() ) )
                {
                    return outcome::failure( AssignerError::UNSATISFIED_CIRCUIT );
                }
            }
        }
        else if ( assigner_instance_->assignments.size() > 1 &&
                  ( TARGET_PROVER < assigner_instance_->assignments.size() || TARGET_PROVER == invalid_target_prover ) )
        {
            std::uint32_t start_idx = ( TARGET_PROVER == invalid_target_prover ) ? 0 : TARGET_PROVER;
            std::uint32_t end_idx =
                ( TARGET_PROVER == invalid_target_prover ) ? assigner_instance_->assignments.size() : TARGET_PROVER + 1;
            for ( std::uint32_t idx = start_idx; idx < end_idx; idx++ )
            {
                // print assignment table
                if ( gen_mode_.has_assignments() )
                {
                    auto AssignmentTable = BuildPlonkAssignmentTable( assigner_instance_->assignments[idx],
                                                                      PrintTableKind::MULTI_PROVER,
                                                                      COMPONENT_CONSTANT_COLUMNS,
                                                                      COMPONENT_SELECTOR_COLUMNS );
                    AssignmentTableVector.push_back( AssignmentTable );
                }

                // print circuit
                if ( gen_mode_.has_circuit() )
                {
                    auto PlonkCircuit = BuildPlonkConstraintSystem( assigner_instance_->circuits[idx],
                                                                    assigner_instance_->assignments[idx],
                                                                    ( idx > 0 ) );
                    PlonkCircuitVector.push_back( PlonkCircuit );
                }
                if ( ( CHECK_VALIDITY == "check" ) && gen_mode_.has_assignments() && gen_mode_.has_circuit() )
                {
                    assigner_instance_->assignments[idx].set_check( true );
                    bool is_accessible = nil::blueprint::is_satisfied( assigner_instance_->circuits[idx],
                                                                       assigner_instance_->assignments[idx] );
                    assigner_instance_->assignments[idx].set_check( false );
                    if ( !is_accessible )
                    {
                        return outcome::failure( AssignerError::UNSATISFIED_CIRCUIT );
                    }
                }
            }
        }
        else
        {
            return outcome::failure( AssignerError::WRONG_TARGET_PROVER );
        }

        if ( AssignmentTableVector.size() != PlonkCircuitVector.size() )
        {
            return outcome::failure( AssignerError::TABLE_CIRCUIT_NUM_MISMATCH );
        }

        std::vector<AssignerOutput> outputs;
        for ( size_t i = 0; i < AssignmentTableVector.size(); ++i )
        {
            outputs.push_back( AssignerOutput{ PlonkCircuitVector.at( i ), AssignmentTableVector.at( i ) } );
        }

        return outputs;
    }

    outcome::result<std::vector<GeniusAssigner::AssignerOutput>> GeniusAssigner::GenerateCircuitAndTable(
        const std::vector<int> &public_inputs,
        const std::vector<int> &private_inputs,
        const std::string      &bytecode_file_payload )
    {
        boost::json::array public_inputs_json_array;
        boost::json::array private_inputs_json_array;

        // Loop through the regular_array and create boost::json::objects for each element
        for ( const int &value : public_inputs )
        {
            boost::json::object obj;
            obj["field"] = value;                      // Add {"field": value} to the object
            public_inputs_json_array.push_back( obj ); // Add the object to the boost::json::array
        }
        for ( const int &value : private_inputs )
        {
            boost::json::object obj;
            obj["field"] = value;                       // Add {"field": value} to the object
            private_inputs_json_array.push_back( obj ); // Add the object to the boost::json::array
        }

        return GenerateCircuitAndTable( public_inputs_json_array, private_inputs_json_array, bytecode_file_payload );
    }

    outcome::result<void> GeniusAssigner::PrintCircuitAndTable( const std::vector<AssignerOutput> &assigner_outputs,
                                                                const std::string                 &table_path,
                                                                const std::string                 &circuit_path )
    {
        for ( size_t i = 0; i < assigner_outputs.size(); ++i )
        {
            std::ofstream otable;
            otable.open( table_path + std::to_string( i ), std::ios_base::binary | std::ios_base::out );
            if ( !otable )
            {
                return outcome::failure( AssignerError::TABLE_PATH_ERROR );
            }
            NilFileHelper::PrintMarshalledData<PlonkAssignTableType>( assigner_outputs.at( i ).table, otable );

            otable.close();

            std::ofstream ocircuit;
            ocircuit.open( circuit_path + std::to_string( i ), std::ios_base::binary | std::ios_base::out );
            if ( !ocircuit )
            {
                return outcome::failure( AssignerError::CIRCUIT_PATH_ERROR );
            }
            if ( i >= assigner_instance_->circuits.size() )
            {
                return outcome::failure( AssignerError::CIRCUIT_NOT_FOUND );
            }

            NilFileHelper::PrintMarshalledData<PlonkConstraintSystemType>( assigner_outputs.at( i ).constrains,
                                                                           ocircuit );

            ocircuit.close();
        }

        return outcome::success();
    }

    GeniusAssigner::PlonkAssignTableType GeniusAssigner::BuildPlonkAssignmentTable(
        const AssignmentTableType &table_proxy,
        PrintTableKind             print_kind,
        std::uint32_t              ComponentConstantColumns,
        std::uint32_t              ComponentSelectorColumns )
    {
        std::uint32_t total_size;
        std::uint32_t shared_size        = static_cast<std::uint32_t>( print_kind );
        std::uint32_t public_input_size  = table_proxy.public_inputs_amount();
        std::uint32_t usable_rows_amount = GetUsableRowsAmount( table_proxy, print_kind );
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

        using TTypeBase = nil::marshalling::field_type<AssignerEndianess>;

        auto table_vectors = GetTableVectors( table_proxy,
                                              print_kind,
                                              ComponentConstantColumns,
                                              ComponentSelectorColumns,
                                              padded_rows_amount );

        auto filled_val = PlonkAssignTableType( std::make_tuple(
            nil::marshalling::types::integral<TTypeBase, std::size_t>( table_proxy.witnesses_amount() ),
            nil::marshalling::types::integral<TTypeBase, std::size_t>( table_proxy.public_inputs_amount() +
                                                                       shared_size ),
            nil::marshalling::types::integral<TTypeBase, std::size_t>( table_proxy.constants_amount() ),
            nil::marshalling::types::integral<TTypeBase, std::size_t>( table_proxy.selectors_amount() ),
            nil::marshalling::types::integral<TTypeBase, std::size_t>( usable_rows_amount ),
            nil::marshalling::types::integral<TTypeBase, std::size_t>( padded_rows_amount ),
            nil::crypto3::marshalling::types::
                fill_field_element_vector<typename AssignmentTableType::field_type::value_type, AssignerEndianess>(
                    table_vectors.witness_values ),
            nil::crypto3::marshalling::types::
                fill_field_element_vector<typename AssignmentTableType::field_type::value_type, AssignerEndianess>(
                    table_vectors.public_input_values ),
            nil::crypto3::marshalling::types::
                fill_field_element_vector<typename AssignmentTableType::field_type::value_type, AssignerEndianess>(
                    table_vectors.constant_values ),
            nil::crypto3::marshalling::types::
                fill_field_element_vector<typename AssignmentTableType::field_type::value_type, AssignerEndianess>(
                    table_vectors.selector_values ) ) );

        return filled_val;
    }

    std::uint32_t GeniusAssigner::GetUsableRowsAmount( const AssignmentTableType &table_proxy,
                                                       PrintTableKind             print_kind )
    {
        std::uint32_t           usable_rows_amount     = 0;
        std::uint32_t           max_shared_size        = 0;
        std::uint32_t           max_witness_size       = 0;
        std::uint32_t           max_public_inputs_size = 0;
        std::uint32_t           max_constant_size      = 0;
        std::uint32_t           max_selector_size      = 0;
        std::uint32_t           shared_size            = static_cast<std::uint32_t>( print_kind );
        std::uint32_t           witness_size           = table_proxy.witnesses_amount();
        std::uint32_t           constant_size          = table_proxy.constants_amount();
        std::uint32_t           selector_size          = table_proxy.selectors_amount();
        std::set<std::uint32_t> lookup_constant_cols   = {};
        std::set<std::uint32_t> lookup_selector_cols   = {};

        if ( print_kind == PrintTableKind::MULTI_PROVER )
        {
            witness_size         = 0;
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

    GeniusAssigner::TableVectors GeniusAssigner::GetTableVectors( const AssignmentTableType &table_proxy,
                                                                  PrintTableKind             print_kind,
                                                                  std::uint32_t              ComponentConstantColumns,
                                                                  std::uint32_t              ComponentSelectorColumns,
                                                                  std::uint32_t              padded_rows_amount )
    {
        std::uint32_t shared_size = static_cast<std::uint32_t>( print_kind );
        TableVectors  table_vectors;
        table_vectors.witness_values.resize( padded_rows_amount * table_proxy.witnesses_amount(), 0 );
        table_vectors.public_input_values.resize( padded_rows_amount *
                                                      ( table_proxy.public_inputs_amount() + shared_size ),
                                                  0 );
        table_vectors.constant_values.resize( padded_rows_amount * table_proxy.constants_amount(), 0 );
        table_vectors.selector_values.resize( padded_rows_amount * table_proxy.selectors_amount(), 0 );
        if ( print_kind == PrintTableKind::SINGLE_PROVER )
        {
            auto it = table_vectors.witness_values.begin();
            for ( std::uint32_t i = 0; i < table_proxy.witnesses_amount(); i++ )
            {
                FillVectorValue<typename AssignmentTableType::field_type::value_type,
                                typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                    table_vectors.witness_values,
                    table_proxy.witness( i ),
                    it );
                it += padded_rows_amount;
            }
            it = table_vectors.public_input_values.begin();
            for ( std::uint32_t i = 0; i < table_proxy.public_inputs_amount(); i++ )
            {
                FillVectorValue<typename AssignmentTableType::field_type::value_type,
                                typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                    table_vectors.public_input_values,
                    table_proxy.public_input( i ),
                    it );
                it += padded_rows_amount;
            }
            it = table_vectors.constant_values.begin();
            for ( std::uint32_t i = 0; i < table_proxy.constants_amount(); i++ )
            {
                FillVectorValue<typename AssignmentTableType::field_type::value_type,
                                typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                    table_vectors.constant_values,
                    table_proxy.constant( i ),
                    it );
                it += padded_rows_amount;
            }
            it = table_vectors.selector_values.begin();
            for ( std::uint32_t i = 0; i < table_proxy.selectors_amount(); i++ )
            {
                FillVectorValue<typename AssignmentTableType::field_type::value_type,
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
                FillVectorValue<typename AssignmentTableType::field_type::value_type,
                                typename crypto3::zk::snark::plonk_column<BlueprintFieldType>>(
                    table_vectors.public_input_values,
                    table_proxy.public_input( i ),
                    it_pub_inp );
                it_pub_inp += padded_rows_amount;
            }
            for ( std::uint32_t i = 0; i < shared_size; i++ )
            {
                FillVectorValue<typename AssignmentTableType::field_type::value_type,
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
                FillVectorValue<typename AssignmentTableType::field_type::value_type,
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
                FillVectorValue<typename AssignmentTableType::field_type::value_type,
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

    GeniusAssigner::PlonkConstraintSystemType GeniusAssigner::BuildPlonkConstraintSystem(
        const nil::blueprint::circuit_proxy<ArithmetizationType> &circuit_proxy,
        const AssignmentTableType                                &table_proxy,
        bool                                                      rename_required )
    {
        using variable_type = crypto3::zk::snark::plonk_variable<typename AssignmentTableType::field_type::value_type>;

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
                         first_var.rotation == static_cast<int32_t>(row) )
                    {
                        constraint.first =
                            variable_type( first_var.index, local_row, first_var.relative, first_var.type );
                    }
                    if ( ( second_var.type == variable_type::column_type::witness ||
                           second_var.type == variable_type::column_type::constant ) &&
                         second_var.rotation == static_cast<int32_t>(row) )
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

        auto filled_val = PlonkConstraintSystemType( std::make_tuple(
            nil::crypto3::marshalling::types::
                fill_plonk_gates<AssignerEndianess, typename ConstraintSystemType::gates_container_type::value_type>(
                    used_gates ),
            nil::crypto3::marshalling::types::fill_plonk_copy_constraints<AssignerEndianess,
                                                                          typename ConstraintSystemType::variable_type>(
                used_copy_constraints ),
            nil::crypto3::marshalling::types::fill_plonk_lookup_gates<
                AssignerEndianess,
                typename ConstraintSystemType::lookup_gates_container_type::value_type>( used_lookup_gates ),
            nil::crypto3::marshalling::types::fill_plonk_lookup_tables<
                AssignerEndianess,
                typename ConstraintSystemType::lookup_tables_type::value_type>( used_lookup_tables ) ) );

        return filled_val;
    }

    template <typename ValueType, typename ContainerType>
    void GeniusAssigner::FillVectorValue( std::vector<ValueType>                   &table_values,
                                          const ContainerType                      &table_col,
                                          typename std::vector<ValueType>::iterator start )
    {
        std::copy( table_col.begin(), table_col.end(), start );
    }
}
