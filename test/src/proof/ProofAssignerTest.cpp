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

    //EXPECT_TRUE( GeniusProver.generate_to_file( false,
    //                                            "../../../../../../test/src/proof/circuit.crct0",
    //                                            "../../../../../../test/src/proof/table.tbl0",
    //                                            "../../../../../../test/src/proof/proof.bin" ) );
    //GeniusProver.GenerateProof( assign_result.value(),
    //                            "../../../../../../test/src/proof/sgnus_proof.bin" );
    auto assign_value = assign_result.value().at( 0 );
    auto proof_result = GeniusProver.GenerateProof( assign_value );
    if ( proof_result.has_error() )
    {
        // Print the error information
        auto error = proof_result.error();
        std::cerr << "Proof Error occurred: " << error.message() << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( proof_result.has_error() ) << "Proof Expected success but got an error!";

    EXPECT_TRUE( GeniusProver.VerifyProof( proof_result.value(), assign_value) );
    EXPECT_TRUE(
        GeniusProver.WriteProofToFile( proof_result.value(), "../../../../../../test/src/proof/sgnus_proof.bin" ) );
    //                                                  GeniusAssigner.table_description_,
    //                                                  "../../../../../../test/src/proof/sgnus_proof.bin" );
    //
    //if ( proof_result.has_error() )
    //{
    //    // Print the error information
    //    auto error = proof_result.error();
    //    std::cerr << "Proof Error occurred: " << error.message() << std::endl; // Assuming error has a message method
    //}
    //// Assert that the function was successful (i.e., no error occurred)
    //ASSERT_FALSE( proof_result.has_error() ) << "Proof Expected success but got an error!";
}
