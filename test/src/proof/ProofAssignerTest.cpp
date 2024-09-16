/**
 * @file       ProofAssignerTest.cpp
 * @brief      Source file with the Genius Assigner test
 * @date       2024-09-10
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#include <iostream>
#include <filesystem>
#include <gtest/gtest.h>
#include "testutil/outcome.hpp"
#include "proof/GeniusAssigner.hpp"
#include "proof/GeniusProver.hpp"

TEST( ProofAssignerTest, GenerateProof )
{
    auto             GeniusAssigner = sgns::GeniusAssigner();
    std::vector<int> public_inputs  = { 5, 11 };
    auto             assign_result =
        GeniusAssigner.GenerateCircuitAndTable( public_inputs, {}, "../../../../../../test/src/proof/bytecode.ll" );
    ASSERT_TRUE( assign_result.has_value() );

    EXPECT_EQ( assign_result.value().size(), 1 );
    auto print_result = GeniusAssigner.PrintCircuitAndTable( assign_result.value(),
                                                             "../../../../../../test/src/proof/table.tbl",
                                                             "../../../../../../test/src/proof/circuit.crct" );

    if ( print_result.has_error() )
    {
        // Print the error information
        auto error = print_result.error();
        std::cerr << "Error occurred: " << error.message() << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( print_result.has_error() ) << "Expected success but got an error!";

    auto GeniusProver = sgns::GeniusProver();

    EXPECT_TRUE( GeniusProver.generate_to_file( false,
                                                "../../../../../../test/src/proof/circuit.crct0",
                                                "../../../../../../test/src/proof/table.tbl0",
                                                "../../../../../../test/src/proof/proof.bin" ) );
}
