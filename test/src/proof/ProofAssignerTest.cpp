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

TEST( ProofAssignerTest, GenerateCircuitAndTableFromFile )
{
    std::cout << "Current working directory: " << std::filesystem::current_path() << std::endl;

    auto             GeniusAssigner = sgns::GeniusAssigner();
    std::vector<int> public_inputs  = { 5, 11 };
    // EXPECT_EQ(GeniusAssigner.GenerateCircuitAndTable(public_inputs,{},"./bytecode.ll"),AssignerError::EMPTY_BYTECODE);
    auto assign_result = GeniusAssigner.GenerateCircuitAndTable( public_inputs, {}, "../../../../../../test/src/proof/bytecode.ll" );
    ASSERT_TRUE(assign_result.has_value()); 

    EXPECT_EQ(assign_result.value().size(),1);
    auto print_result = GeniusAssigner.PrintCircuitAndTable( assign_result.value(), "../../../../../../test/src/proof/table.tbl", "../../../../../../test/src/proof/circuit.crct" );
    ASSERT_FALSE(print_result.has_error()); 
}
