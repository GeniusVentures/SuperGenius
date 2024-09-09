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

            nil::blueprint::generation_mode gen_mode = nil::blueprint::generation_mode::assignments() | nil::blueprint::generation_mode::circuit();

            //nil::blueprint::assigner<BlueprintFieldType> assigner_instance( desc,
            //                                                                STACK_SIZE,
            //                                                                std::string(LOG_LEVEL),
            //                                                                MAX_NUM_PROVERS,
            //                                                                TARGET_PROVER,
            //                                                                gen_mode,
            //                                                                policy,
            //                                                                circuit_output_print_format,
            //                                                                check_validity );
        }

        ~GeniusAssigner() = default;

    private:
        //TODO - Check if better on cmakelists, since the zkllvm executable does it like this.
        constexpr static const std::size_t      WITNESS_COLUMNS            = 15;
        constexpr static const std::size_t      PUBLIC_INPUT_COLUMNS       = 1;
        constexpr static const std::size_t      COMPONENT_CONSTANT_COLUMNS = 5;
        constexpr static const std::size_t      LOOKUP_CONSTANT_COLUMNS    = 30;
        constexpr static const std::size_t      COMPONENT_SELECTOR_COLUMNS = 50;
        constexpr static const std::size_t      LOOKUP_SELECTOR_COLUMNS    = 6;
        constexpr static const long             STACK_SIZE                 = 4000000;
        constexpr static const std::string_view LOG_LEVEL                  = "info";
        constexpr static const std::uint32_t    MAX_NUM_PROVERS            = 1;
        constexpr static const std::uint32_t    TARGET_PROVER              = std::numeric_limits<std::uint32_t>::max();
    };
}

#endif
