/**
 * @file       GeniusAssigner.cpp
 * @brief      Source file of the assigner from bytecode to circuit
 * @date       2024-09-09
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include "GeniusAssigner.hpp"

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

    outcome::result<GeniusAssigner::AssignerOutput> GeniusAssigner::GenerateCircuitAndTable(
        const std::vector<int> &public_inputs,
        const std::vector<int> &private_inputs,
        const std::string      &bytecode_file_path )
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

        if ( !assigner_instance_->parse_ir_file( bytecode_file_path.data() ) )
        {
            return outcome::failure( AssignerError::EMPTY_BYTECODE );
        }
        if ( !assigner_instance_->evaluate( public_inputs_json_array, private_inputs_json_array ) )
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
                return outcome::failure( AssignerError::SELECTOR_IDX_INVALID );
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
                std::ofstream otable;
                otable.open( "./table.tbl", std::ios_base::binary | std::ios_base::out );
                if ( !otable )
                {
                    std::cout << "Something wrong with output " << "./table.tbl" << std::endl;
                }
                auto AssignmentTable =
                    BuildPlonkAssignmentTable<nil::marshalling::option::big_endian,
                                              ArithmetizationType,
                                              BlueprintFieldType>( assigner_instance_->assignments[0],
                                                                   print_table_kind::SINGLE_PROVER,
                                                                   COMPONENT_CONSTANT_COLUMNS,
                                                                   COMPONENT_SELECTOR_COLUMNS );
                AssignmentTableVector.push_back( AssignmentTable );
                print_assignment_table<nil::marshalling::option::big_endian, ArithmetizationType>( AssignmentTable,
                                                                                                   otable );

                otable.close();
            }

            // print circuit
            if ( gen_mode_.has_circuit() )
            {
                std::ofstream ocircuit;
                ocircuit.open( "./circuit.crct", std::ios_base::binary | std::ios_base::out );
                if ( !ocircuit )
                {
                    std::cout << "Something wrong with output " << "./circuit.crct" << std::endl;
                }
                auto PlonkCircuit =
                    BuildPlonkConstraintSystem<nil::marshalling::option::big_endian,
                                               ArithmetizationType,
                                               ConstraintSystemType>( assigner_instance_->circuits[0],
                                                                      assigner_instance_->assignments[0],
                                                                      false );
                PlonkCircuitVector.push_back( PlonkCircuit );
                print_circuit<nil::marshalling::option::big_endian, ConstraintSystemType>( PlonkCircuit, ocircuit );
                ocircuit.close();
            }
            if ( ( CHECK_VALIDITY == "check" ) && !PlonkCircuitVector.empty() && !AssignmentTableVector.empty() )
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
                    std::ofstream otable;
                    otable.open( "./table.tbl" + std::to_string( idx ), std::ios_base::binary | std::ios_base::out );
                    if ( !otable )
                    {
                        std::cout << "Something wrong with output " << "./table.tbl" + std::to_string( idx )
                                  << std::endl;
                    }
                    auto AssignmentTable =
                        BuildPlonkAssignmentTable<nil::marshalling::option::big_endian,
                                                  ArithmetizationType,
                                                  BlueprintFieldType>( assigner_instance_->assignments[idx],
                                                                       print_table_kind::MULTI_PROVER,
                                                                       COMPONENT_CONSTANT_COLUMNS,
                                                                       COMPONENT_SELECTOR_COLUMNS );
                    AssignmentTableVector.push_back( AssignmentTable );
                    print_assignment_table<nil::marshalling::option::big_endian, ArithmetizationType>( AssignmentTable,
                                                                                                       otable );

                    otable.close();
                }

                // print circuit
                if ( gen_mode_.has_circuit() )
                {
                    std::ofstream ocircuit;
                    ocircuit.open( "./circuit.crct" + std::to_string( idx ),
                                   std::ios_base::binary | std::ios_base::out );
                    if ( !ocircuit )
                    {
                        std::cout << "Something wrong with output " << "./circuit.crct" + std::to_string( idx )
                                  << std::endl;
                    }

                    ASSERT_MSG( idx < assigner_instance_->circuits.size(), "Not found circuit" );
                    auto PlonkCircuit =
                        BuildPlonkConstraintSystem<nil::marshalling::option::big_endian,
                                                   ArithmetizationType,
                                                   ConstraintSystemType>( assigner_instance_->circuits[idx],
                                                                          assigner_instance_->assignments[idx],
                                                                          ( idx > 0 ) );
                    PlonkCircuitVector.push_back( PlonkCircuit );
                    print_circuit<nil::marshalling::option::big_endian, ConstraintSystemType>( PlonkCircuit, ocircuit );

                    ocircuit.close();
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
        for ( auto i = 0; i < AssignmentTableVector.size(); ++i )
        {
            outputs.push_back( { PlonkCircuitVector.at( 1 ), AssignmentTableVector.at( i ) } );
        }

        return outputs;
    }

    void GeniusAssigner::PrintCircuitAndTable( const std::vector<AssignerOutput> &assigner_outputs,
                                               const std::string                 &table_path,
                                               const std::string                 &circuit_path )
    {
        for ( auto i = 0; i < assigner_outputs.size(); ++i )
        {
            std::ofstream otable;
            otable.open( table_path + std::to_string( i ), std::ios_base::binary | std::ios_base::out );
            if ( !otable )
            {
                return outcome::failure( AssignerError::TABLE_PATH_ERROR );
            }
            print_assignment_table<AssignerEndianess, ArithmetizationType>( assigner_outputs.at( i ).table, otable );

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

            print_circuit<nil::marshalling::option::big_endian, ConstraintSystemType>( assigner_outputs.at( i ).circuit,
                                                                                       ocircuit );

            ocircuit.close();
        }
    }
}
