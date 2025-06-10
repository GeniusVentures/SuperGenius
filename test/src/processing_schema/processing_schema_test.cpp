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
#include "GeneratedSchema/SGNSProcMain.hpp"
#include "GeneratedSchema/Generators.hpp"

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

        auto          data = nlohmann::json::parse( instance_str );
        sgns::SgnsProcessing processing;
        sgns::from_json( data, processing );
        std::cout << processing.get_author() << std::endl;
        std::cout << processing.get_inputs()[0].get_description() << std::endl;
    }
}
