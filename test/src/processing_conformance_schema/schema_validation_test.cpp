#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#include <processingbase/ProcessingManager.hpp>
#include "testutil/processing_conformance_fixture.hpp"

namespace sgns
{
    class SchemaValidationTest : public ProcessorConformanceFixture
    {
    protected:
        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "fixtures/";
        }
    };

    // -----------------------------------------------------------------------
    // Valid input tests
    // -----------------------------------------------------------------------

    TEST_F( SchemaValidationTest, ValidInferencePass )
    {
        const std::string &json_str = PatchedJson( "valid-inference.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Valid INFERENCE pass should succeed: " << manager_result.error().message();

        const auto &manager  = manager_result.value();
        const auto  passes   = manager->GetProcessingData().get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );

        std::cout << "ValidInferencePass: schema accepted correctly" << std::endl;
    }

    TEST_F( SchemaValidationTest, ValidRenderPass )
    {
        const std::string &json_str = PatchedJson( "valid-render.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Valid RENDER pass should succeed: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        const auto  passes  = manager->GetProcessingData().get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::RENDER );

        std::cout << "ValidRenderPass: schema accepted correctly" << std::endl;
    }

    // -----------------------------------------------------------------------
    // Invalid input tests — INFERENCE
    // -----------------------------------------------------------------------

    TEST_F( SchemaValidationTest, InvalidMissingModel )
    {
        const std::string &json_str = PatchedJson( "invalid-missing-model.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() ) << "INFERENCE pass with no model should fail";
        // Error message should reference the missing model
        std::string err = manager_result.error().message();
        ASSERT_TRUE( err.find( "model" ) != std::string::npos ||
                     err.find( "Model" ) != std::string::npos )
            << "Error should mention missing model: " << err;

        std::cout << "InvalidMissingModel: correctly rejected: " << err << std::endl;
    }

    TEST_F( SchemaValidationTest, InvalidBadModelFormat )
    {
        const std::string &json_str = PatchedJson( "invalid-bad-format.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() ) << "Model with UNKNOWN format should fail";
        std::string err = manager_result.error().message();
        ASSERT_TRUE( err.find( "format" ) != std::string::npos ||
                     err.find( "Format" ) != std::string::npos )
            << "Error should mention invalid format: " << err;

        std::cout << "InvalidBadModelFormat: correctly rejected: " << err << std::endl;
    }

    TEST_F( SchemaValidationTest, InvalidUnknownPassType )
    {
        const std::string &json_str = PatchedJson( "invalid-unknown-passtype.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() ) << "Unknown PassType should fail";
        std::string err = manager_result.error().message();
        ASSERT_TRUE( err.find( "UNKNOWN_TYPE" ) != std::string::npos ||
                     err.find( "type" ) != std::string::npos ||
                     err.find( "Type" ) != std::string::npos )
            << "Error should reference the unknown pass type: " << err;

        std::cout << "InvalidUnknownPassType: correctly rejected (regression BugD verified): " << err << std::endl;
    }

    // -----------------------------------------------------------------------
    // Invalid input tests — RENDER
    // -----------------------------------------------------------------------

    TEST_F( SchemaValidationTest, RenderMissingShader )
    {
        const std::string &json_str = PatchedJson( "render-missing-shader.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() ) << "RENDER pass with no shader should fail";
        std::string err = manager_result.error().message();
        ASSERT_TRUE( err.find( "shader" ) != std::string::npos ||
                     err.find( "Shader" ) != std::string::npos )
            << "Error should mention missing shader: " << err;

        std::cout << "RenderMissingShader: correctly rejected: " << err << std::endl;
    }

    TEST_F( SchemaValidationTest, RenderBadSpirvRef )
    {
        const std::string &json_str = PatchedJson( "render-bad-spirv.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() ) << "RENDER pass with bad SPIR-V ref should fail";
        std::string err = manager_result.error().message();
        ASSERT_TRUE( err.find( "SPIR" ) != std::string::npos ||
                     err.find( "spv" ) != std::string::npos ||
                     err.find( "shader" ) != std::string::npos ||
                     err.find( "file" ) != std::string::npos )
            << "Error should mention SPIR-V or file issue: " << err;

        std::cout << "RenderBadSpirvRef: correctly rejected: " << err << std::endl;
    }

    // -----------------------------------------------------------------------
    // Edge cases
    // -----------------------------------------------------------------------

    TEST_F( SchemaValidationTest, EmptyJobDefinition )
    {
        const std::string json_str = "{}";
        auto              manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() ) << "Empty JSON should fail";

        std::cout << "EmptyJobDefinition: correctly rejected: " << manager_result.error().message() << std::endl;
    }

    TEST_F( SchemaValidationTest, MissingPassesArray )
    {
        const std::string json_str = R"({"name": "no-passes", "version": "1.0"})";
        auto              manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() ) << "JSON without passes should fail";

        std::cout << "MissingPassesArray: correctly rejected: " << manager_result.error().message() << std::endl;
    }

    TEST_F( SchemaValidationTest, MultiplePassesValid )
    {
        const std::string &json_str = PatchedJson( "valid-inference.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Multiple valid passes should succeed: " << manager_result.error().message();

        std::cout << "MultiplePassesValid: schema accepted" << std::endl;
    }

} // namespace sgns
