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
#include "proof/GeniusProver.hpp"
#include "proof/TransferProof.hpp"
#include "proof/ProcessingProof.hpp"
#include "proof/circuits/TransactionVerifierCircuit.hpp"

TEST( GeniusProofsTest, ValidTransferProof )
{
    auto TxProof = sgns::TransferProof( 1000, 500 );

    auto proof_result = TxProof.GenerateFullProof();

    if ( proof_result.has_error() )
    {
        // Print the error information
        auto error = proof_result.error();
        std::cerr << "Proof Error occurred: " << error.message() << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( proof_result.has_error() ) << "Proof Expected success but got an error!";

    auto verification_result = TxProof.VerifyFullProof( proof_result.value() );
    if ( verification_result.has_error() )
    {
        // Print the error information
        auto error = verification_result.error();
        std::cerr << "Verification Error occurred: " << error.message()
                  << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( verification_result.has_error() ) << "Proof Expected success but got an error!";
    EXPECT_TRUE( verification_result.value() );
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
    auto TxProof2 = TransferProofTest( 1000, 600 );

    auto proof_result = TxProof2.GenerateFullProof();

    if ( proof_result.has_error() )
    {
        // Print the error information
        auto error = proof_result.error();
        std::cerr << "Proof Error occurred: " << error.message() << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( proof_result.has_error() ) << "Proof Expected success but got an error!";

    auto verification_result = TxProof2.VerifyFullProof( proof_result.value() );
    if ( verification_result.has_error() )
    {
        // Print the error information
        auto error = verification_result.error();
        std::cerr << "Verification Error occurred: " << error.message()
                  << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( verification_result.has_error() ) << "Verification Expected success but got an error!";
    EXPECT_TRUE( verification_result.value() );

    auto base_proof = IBasicProofTest::DeSerializeBaseProof( proof_result.value() );

    auto AnotherTxProof = TransferProofTest( 700, 1200 );

    auto wrong_parameters = AnotherTxProof.GenerateJsonParameters();
    auto right_parameters = TxProof2.GenerateJsonParameters();

    std::vector<sgns::GeniusProver::ParameterType> parameters;

    verification_result = TxProof2.VerifyFullProof( wrong_parameters,
                                                    base_proof.value().proof_data(),
                                                    std::string( TransactionCircuit ) );
    if ( verification_result.has_error() )
    {
        // Print the error information
        auto error = verification_result.error();
        std::cerr << "Verification Error occurred: " << error.message()
                  << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( verification_result.has_error() ) << "Verification Expected success but got an error!";
    EXPECT_FALSE( verification_result.value() );
    verification_result = TxProof2.VerifyFullProof( right_parameters,
                                                    base_proof.value().proof_data(),
                                                    std::string( TransactionCircuit ) );
    if ( verification_result.has_error() )
    {
        // Print the error information
        auto error = verification_result.error();
        std::cerr << "Verification Error occurred: " << error.message()
                  << std::endl; // Assuming error has a message method
    }
    // Assert that the function was successful (i.e., no error occurred)
    ASSERT_FALSE( verification_result.has_error() ) << "Verification Expected success but got an error!";
    EXPECT_TRUE( verification_result.value() );
}
