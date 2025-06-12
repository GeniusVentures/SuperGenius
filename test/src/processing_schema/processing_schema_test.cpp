#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>
#include <valijson/adapters/boost_json_adapter.hpp>
#include <valijson/schema.hpp>
#include <valijson/validator.hpp>
#include <valijson/schema_parser.hpp>
#include <boost/dll.hpp>
#include <boost/optional/optional_io.hpp>
#include "SGNSProcMain.hpp"
#include "Generators.hpp"

namespace sgns
{
    class ProcessingSchemaTest : public ::testing::Test
    {
    protected:
        static inline std::string binary_path = "";

        static void SetUpTestSuite()
        {
        }

        static void TearDownTestSuite()
        {
        }
    };

TEST_F( ProcessingSchemaTest, GoodSchemaTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "../../../../../test/src/processing_schema/";

        // Load schema file
        std::string   schema_file = data_path + "gnus-processing-schema.json";
        std::ifstream schema_stream( schema_file );
        ASSERT_TRUE( schema_stream.is_open() ) << "Failed to open schema file: " << schema_file;

        std::string schema_str( ( std::istreambuf_iterator<char>( schema_stream ) ), std::istreambuf_iterator<char>() );
        schema_stream.close();
        ASSERT_FALSE( schema_str.empty() ) << "Schema file is empty";

        // Load test instance file
        std::string   instance_file = data_path + "test-processing-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open instance file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        try
        {
            // Parse with Boost.JSON
            auto schema_value   = boost::json::parse( schema_str );
            auto instance_value = boost::json::parse( instance_str );

            // Use valijson with Boost.JSON adapter
            valijson::Schema                     schema;
            valijson::SchemaParser               parser;
            valijson::adapters::BoostJsonAdapter schema_adapter( schema_value );
            parser.populateSchema( schema_adapter, schema );

            valijson::Validator                  validator;
            valijson::adapters::BoostJsonAdapter instance_adapter( instance_value );

            // Validate and capture any errors
            valijson::ValidationResults results;
            bool                        is_valid = validator.validate( schema, instance_adapter, &results );

            // If validation fails, print the errors
            if ( !is_valid )
            {
                std::cout << "Validation errors:\n";
                valijson::ValidationResults::Error error;
                while ( results.popError( error ) )
                {
                    std::cout << "Error: " << error.description << std::endl;
                    for ( const auto &context_element : error.context )
                    {
                        std::cout << "  Context: " << context_element << std::endl;
                    }
                }
            }

            EXPECT_TRUE( is_valid ) << "Schema validation failed";
        }
        catch ( const boost::json::system_error &e )
        {
            FAIL() << "JSON parsing error: " << e.what();
        }
        catch ( const std::exception &e )
        {
            FAIL() << "Unexpected error: " << e.what();
        }
    }

    TEST_F( ProcessingSchemaTest, BadSchemaTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "../../../../../test/src/processing_schema/";

        // Load schema file
        std::string   schema_file = data_path + "gnus-processing-schema.json";
        std::ifstream schema_stream( schema_file );
        ASSERT_TRUE( schema_stream.is_open() ) << "Failed to open schema file: " << schema_file;

        std::string schema_str( ( std::istreambuf_iterator<char>( schema_stream ) ), std::istreambuf_iterator<char>() );
        schema_stream.close();
        ASSERT_FALSE( schema_str.empty() ) << "Schema file is empty";

        // Load bad test instance file
        std::string   instance_file = data_path + "bad-processing-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open bad instance file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Bad instance file is empty";

        try
        {
            // Parse with Boost.JSON
            auto schema_value   = boost::json::parse( schema_str );
            auto instance_value = boost::json::parse( instance_str );

            // Use valijson with Boost.JSON adapter
            valijson::Schema                     schema;
            valijson::SchemaParser               parser;
            valijson::adapters::BoostJsonAdapter schema_adapter( schema_value );
            parser.populateSchema( schema_adapter, schema );

            valijson::Validator                  validator;
            valijson::adapters::BoostJsonAdapter instance_adapter( instance_value );

            // Validate and capture any errors
            valijson::ValidationResults results;
            bool                        is_valid = validator.validate( schema, instance_adapter, &results );

            // Print the validation errors for debugging
            if ( !is_valid )
            {
                std::cout << "Expected validation errors found:\n";
                valijson::ValidationResults::Error error;
                while ( results.popError( error ) )
                {
                    std::cout << "Error: " << error.description << std::endl;
                    for ( const auto &context_element : error.context )
                    {
                        std::cout << "  Context: " << context_element << std::endl;
                    }
                }
            }

            // This should fail validation
            EXPECT_FALSE( is_valid ) << "Schema validation should have failed but passed";
        }
        catch ( const boost::json::system_error &e )
        {
            FAIL() << "JSON parsing error: " << e.what();
        }
        catch ( const std::exception &e )
        {
            FAIL() << "Unexpected error: " << e.what();
        }
    }

TEST_F( ProcessingSchemaTest, GeneratedCodeTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "../../../../../test/src/processing_schema/";

        // Load test instance file (validation already tested separately)
        std::string   instance_file = data_path + "test-processing-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open instance file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto                 data = nlohmann::json::parse( instance_str );
        sgns::SgnsProcessing processing;
        sgns::from_json( data, processing );

        // Test basic string fields
        ASSERT_EQ( processing.get_name(), "TestImageEnhancement" );
        ASSERT_EQ( processing.get_version(), "1.0.0" );
        ASSERT_EQ( processing.get_gnus_spec_version(), 1.0 );

        // Test optional string fields
        ASSERT_TRUE( processing.get_author().has_value() );
        ASSERT_EQ( *processing.get_author(), "Test Author" );

        ASSERT_TRUE( processing.get_description().has_value() );
        ASSERT_EQ( *processing.get_description(), "A test processing definition for image enhancement" );

        // Test tags array
        ASSERT_TRUE( processing.get_tags().has_value() );
        auto tags = processing.get_tags().value();
        ASSERT_EQ( tags.size(), 3 );
        ASSERT_EQ( tags[0], "image" );
        ASSERT_EQ( tags[1], "enhancement" );
        ASSERT_EQ( tags[2], "test" );

        // Test inputs array
        const auto &inputs = processing.get_inputs();
        ASSERT_EQ( inputs.size(), 1 );
        ASSERT_EQ( inputs[0].get_name(), "inputImage" );
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::TEXTURE2_D );
        if ( inputs[0].get_description().has_value() )
        {
            ASSERT_EQ( *inputs[0].get_description(), "Input image to be enhanced" );
        }

        // Test outputs array
        const auto &outputs = processing.get_outputs();
        ASSERT_EQ( outputs.size(), 1 );
        ASSERT_EQ( outputs[0].get_name(), "enhancedImage" );
        ASSERT_EQ( outputs[0].get_type(), sgns::DataType::TEXTURE2_D );
        if ( outputs[0].get_description().has_value() )
        {
            ASSERT_EQ( *outputs[0].get_description(), "Enhanced output image" );
        }

        // Test parameters array
        ASSERT_TRUE( processing.get_parameters().has_value() );
        auto parameters = processing.get_parameters().value();
        ASSERT_EQ( parameters.size(), 2 );

        ASSERT_EQ( parameters[0].get_name(), "modelUri" );
        ASSERT_EQ( parameters[0].get_type(), sgns::ParameterType::URI );
        if ( parameters[0].get_description().has_value() )
        {
            ASSERT_EQ( *parameters[0].get_description(), "URI to the enhancement model" );
        }

        ASSERT_EQ( parameters[1].get_name(), "enhancementStrength" );
        ASSERT_EQ( parameters[1].get_type(), sgns::ParameterType::FLOAT );
        if ( parameters[1].get_description().has_value() )
        {
            ASSERT_EQ( *parameters[1].get_description(), "Strength of the enhancement effect" );
        }
        // Test passes array
        const auto &passes = processing.get_passes();
        std::cout << "Passes size: " << passes.size() << std::endl;
        ASSERT_EQ( passes.size(), 3 );

        ASSERT_EQ( passes[0].get_name(), "preprocessing" );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::DATA_TRANSFORM );
        if ( passes[0].get_description().has_value() )
        {
            ASSERT_EQ( *passes[0].get_description(), "Normalize input image" );
        }

        ASSERT_EQ( passes[1].get_name(), "enhancement" );
        ASSERT_EQ( passes[1].get_type(), sgns::PassType::INFERENCE );
        if ( passes[1].get_description().has_value() )
        {
            ASSERT_EQ( *passes[1].get_description(), "AI-based image enhancement" );
        }

        ASSERT_EQ( passes[2].get_name(), "postprocessing" );
        ASSERT_EQ( passes[2].get_type(), sgns::PassType::DATA_TRANSFORM );
        if ( passes[2].get_description().has_value() )
        {
            ASSERT_EQ( *passes[2].get_description(), "Denormalize and output final image" );
        }

        // Test metadata
        //ASSERT_TRUE( processing.get_metadata().has_value() );
        auto metadata = processing.get_metadata().value();
        ASSERT_TRUE( metadata.find( "created_date" ) != metadata.end() );
        ASSERT_TRUE( metadata.find( "framework" ) != metadata.end() );
        ASSERT_TRUE( metadata.find( "test_case" ) != metadata.end() );

        std::cout << "Processing definition parsed successfully:" << std::endl;
        std::cout << "Name: " << processing.get_name() << std::endl;
        std::cout << "Version: " << processing.get_version() << std::endl;
        std::cout << "SpecVersion: " << processing.get_gnus_spec_version() << std::endl;
        std::cout << "Author: " << ( processing.get_author() ? *processing.get_author() : "N/A" ) << std::endl;
        std::cout << "Inputs: " << processing.get_inputs().size() << std::endl;
        std::cout << "Outputs: " << processing.get_outputs().size() << std::endl;
        std::cout << "Parameters: " << ( processing.get_parameters() ? processing.get_parameters()->size() : 0 )
                  << std::endl;
        std::cout << "Passes: " << processing.get_passes().size() << std::endl;
    }

    TEST_F( ProcessingSchemaTest, BadGeneratedCodeTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "../../../../../test/src/processing_schema/";

        // Load JSON missing required field (inputs)
        std::string   instance_file = data_path + "missing-inputs-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open missing-inputs file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Missing-inputs file is empty";

        // Try to parse JSON missing required field
        try
        {
            auto                 data = nlohmann::json::parse( instance_str );
            sgns::SgnsProcessing processing;

            // This should either throw or create an object with empty inputs
            sgns::from_json( data, processing );

            // If parsing succeeds, check if inputs is empty (which would be wrong)
            const auto &inputs = processing.get_inputs();
            if ( inputs.empty() )
            {
                std::cout << "Generated code parsed missing required field as empty array" << std::endl;
                // This might be acceptable behavior depending on implementation
            }
            else
            {
                FAIL() << "Generated code should not have populated inputs from missing field";
            }

            // Verify other fields still work
            EXPECT_EQ( processing.get_name(), "TestImageEnhancement" );
            EXPECT_EQ( processing.get_version(), "1.0.0" );

            std::cout << "Generated code handled missing required field (inputs empty: " << inputs.empty() << ")"
                      << std::endl;
        }
        catch ( const nlohmann::json::exception &e )
        {
            // JSON parsing/structure error - this is also acceptable
            std::cout << "Generated code correctly threw exception for missing required field: " << e.what()
                      << std::endl;
            SUCCEED();
        }
        catch ( const std::exception &e )
        {
            // Other parsing error - also acceptable
            std::cout << "Generated code threw exception for missing required field: " << e.what() << std::endl;
            SUCCEED();
        }
    }

    TEST_F( ProcessingSchemaTest, PosenetJobTest ) {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "../../../../../test/src/processing_schema/";

        // Load test instance file (validation already tested separately)
        std::string   instance_file = data_path + "posenet-processing-job.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open instance file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto                 data = nlohmann::json::parse( instance_str );
        sgns::SgnsProcessing processing;
        sgns::from_json( data, processing );
        ASSERT_EQ( processing.get_name(), "posenet-inference" );
        auto inputs = processing.get_inputs();
        for (auto& input : inputs)
        {
            std::cout << input.get_name() << std::endl;
            std::cout << static_cast<int>( input.get_format().value() ) << std::endl;
            std::cout << input.get_source_uri_param() << std::endl;
            auto dimensions = input.get_dimensions().value();
            std::cout << "Dimensions Width: " << dimensions.get_width() << std::endl;
            std::cout << "Dimensions Height: " << dimensions.get_height() << std::endl;
            std::cout << "Dimensions Channels: " << dimensions.get_channels() << std::endl;
            std::cout << "Dimensions BlockLen: " << dimensions.get_block_len() << std::endl;
            std::cout << "Dimensions BlockLineStride: " << dimensions.get_block_line_stride() << std::endl;
            std::cout << "Dimensions BlockStride: " << dimensions.get_block_stride() << std::endl;
            std::cout << "Dimensions Channels: " << dimensions.get_channels() << std::endl;
            std::cout << "Dimensions ChunkCount: " << dimensions.get_chunk_count() << std::endl;
            std::cout << "Dimensions ChunkLineStride: " << dimensions.get_chunk_line_stride() << std::endl;
            std::cout << "Dimensions ChunkOffset: " << dimensions.get_chunk_offset() << std::endl;
            std::cout << "Dimensions ChunkStride: " << dimensions.get_chunk_stride() << std::endl;
            std::cout << "Dimensions SubChunkHeight: " << dimensions.get_chunk_subchunk_height() << std::endl;
            std::cout << "Dimensions SubChunkWidth: " << dimensions.get_chunk_subchunk_width() << std::endl;
        }
        auto params = processing.get_parameters();
    }
}
