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
using namespace nil::crypto3;
using namespace nil::blueprint;

namespace sgns
{
    class GeniusAssigner
    {
        using BlueprintFieldType = typename algebra::curves::pallas::base_field_type;

    public:
        GeniusAssigner()
        {
            nil::crypto3::zk::snark::plonk_table_description<BlueprintFieldType> desc(
                WITNESS_COLUMNS,
                PUBLIC_INPUT_COLUMNS,
                COMPONENT_CONSTANT_COLUMNS + LOOKUP_CONSTANT_COLUMNS,
                COMPONENT_SELECTOR_COLUMNS + LOOKUP_SELECTOR_COLUMNS );

            nil::blueprint::generation_mode gen_mode =
                nil::blueprint::generation_mode::assignments() | nil::blueprint::generation_mode::circuit();
            nil::blueprint::print_format circuit_output_print_format = no_print;

            nil::blueprint::assigner<BlueprintFieldType> assigner_instance( desc,
                                                                            STACK_SIZE,
                                                                            LOG_LEVEL,
                                                                            MAX_NUM_PROVERS,
                                                                            TARGET_PROVER,
                                                                            gen_mode,
                                                                            std::string( POLICY ),
                                                                            circuit_output_print_format,
                                                                            std::string( CHECK_VALIDITY ) == "check" );

            boost::json::value public_input_json_value;
            if ( !read_json( "./pub_input.inp", public_input_json_value ) )
            {
                std::cout << "Error reading public input" << std::endl;
            }

            boost::json::value private_input_json_value;
            if ( !read_json( "./pub_input.inp", private_input_json_value ) )
            {
                std::cout << "Error reading private input" << std::endl;
            }
            if ( !assigner_instance.parse_ir_file( "./bytecode.ll" ) )
            {
                std::cout << "Error parsing bytecode" << std::endl;
            }
        }

        ~GeniusAssigner() = default;

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
    };
}

#endif
