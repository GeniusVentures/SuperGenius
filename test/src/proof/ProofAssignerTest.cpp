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
    auto result = GeniusAssigner.GenerateCircuitAndTable( public_inputs, {}, "./bytecode.ll" );
    ASSERT_TRUE(result.has_value()); 
}
