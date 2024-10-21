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
#include <boost/json.hpp>
#include "proof/TransferProof.hpp"
#include "proof/ProcessingProof.hpp"
#include "proof/circuits/TransactionVerifierCircuit.hpp"

TEST( GeniusProofsTest, ValidTransferProof )
{
    auto TxProof = sgns::TransferProof( 1000, 10 );

    auto proof_result = TxProof.GenerateFullProof();

    if ( proof_result.has_error() )
    {
        // Print the error information
        auto error = proof_result.error();
        std::cerr << "Proof Error occurred: " << error.message() << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( proof_result.has_error() ) << "Proof Expected success but got an error!";

    EXPECT_TRUE( TxProof.VerifyFullProof( proof_result.value() ) );
}

class IBasicProofTest : public sgns::IBasicProof
{
public:
    // Expose protectedMethod as public for testing purposes
    using sgns::IBasicProof::DeSerializeBaseProof;
};

class TransferProofTest : public sgns::TransferProof
{
public:
    // Expose protectedMethod as public for testing purposes
    using sgns::TransferProof::GenerateJsonParameters;
    using sgns::TransferProof::TransferProof;
};

TEST( GeniusProofsTest, InvalidTransferProof )
{
    auto TxProof = sgns::TransferProof( 1000, 500 );

    auto proof_result = TxProof.GenerateFullProof();

    if ( proof_result.has_error() )
    {
        // Print the error information
        auto error = proof_result.error();
        std::cerr << "Proof Error occurred: " << error.message() << std::endl; // Assuming error has a message method
    }
    ASSERT_FALSE( proof_result.has_error() ) << "Proof Expected success but got an error!";
    auto base_proof = IBasicProofTest::DeSerializeBaseProof( proof_result.value() );

    auto AnotherTxProof = TransferProofTest( 1200, 700 );

    auto wrong_parameters = AnotherTxProof.GenerateJsonParameters();

    // Assert that the function was successful (i.e., no error occurred)

    EXPECT_FALSE( TxProof.VerifyFullProof( wrong_parameters,
                                           base_proof.value().proof_data(),
                                           std::string( TransactionCircuit ) ) );
}
