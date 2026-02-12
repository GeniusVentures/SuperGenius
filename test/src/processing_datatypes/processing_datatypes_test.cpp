#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>
#include <boost/dll.hpp>
#include <boost/asio.hpp>
#include <boost/optional/optional_io.hpp>
#include "SGNSProcMain.hpp"
#include "Generators.hpp"
#include <processingbase/ProcessingManager.hpp>

namespace sgns
{
    class ProcessingDatatypesTest : public ::testing::Test
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

    TEST_F( ProcessingDatatypesTest, StringInputValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        // Load test instance file
        std::string   instance_file = data_path + "string-processing-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open instance file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        // Create ProcessingManager and initialize with JSON
        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() ) << "Failed to create ProcessingManager: " 
                                                   << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        // Get the processing data to validate
        auto processing = manager->GetProcessingData();

        // Test basic fields
        ASSERT_EQ( processing.get_name(), "bert-tiny-string-test" );
        ASSERT_EQ( processing.get_version(), "1.0.0" );
        ASSERT_EQ( processing.get_gnus_spec_version(), 1.0 );

        // Test inputs array - should have one string input
        const auto &inputs = processing.get_inputs();
        ASSERT_EQ( inputs.size(), 1 ) << "Should have exactly 1 input";
        ASSERT_EQ( inputs[0].get_name(), "inputText" );
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::STRING );
        ASSERT_EQ( inputs[0].get_source_uri_param(), "file://processing_datatypes/test_input.txt" );

        // Validate string input does NOT require dimensions (unlike texture2D)
        ASSERT_FALSE( inputs[0].get_dimensions().has_value() ) 
            << "String input should not have dimensions";

        // Test outputs array
        const auto &outputs = processing.get_outputs();
        ASSERT_EQ( outputs.size(), 1 );
        ASSERT_EQ( outputs[0].get_name(), "textEmbedding" );
        ASSERT_EQ( outputs[0].get_type(), sgns::DataType::TENSOR );

        // Test parameters
        ASSERT_TRUE( processing.get_parameters().has_value() );
        auto parameters = processing.get_parameters().value();
        ASSERT_EQ( parameters.size(), 4 );

        // Validate modelUri parameter
        ASSERT_EQ( parameters[0].get_name(), "modelUri" );
        ASSERT_EQ( parameters[0].get_type(), sgns::ParameterType::URI );

        // Validate tokenizerMode parameter
        ASSERT_EQ( parameters[1].get_name(), "tokenizerMode" );
        ASSERT_EQ( parameters[1].get_type(), sgns::ParameterType::STRING );

        // Validate textInput parameter
        ASSERT_EQ( parameters[2].get_name(), "textInput" );
        ASSERT_EQ( parameters[2].get_type(), sgns::ParameterType::STRING );

        // Validate maxLength parameter
        ASSERT_EQ( parameters[3].get_name(), "maxLength" );
        ASSERT_EQ( parameters[3].get_type(), sgns::ParameterType::INT );

        // Test passes array
        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_name(), "text_inference" );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );

        // Validate model configuration
        ASSERT_TRUE( passes[0].get_model().has_value() );
        // Note: Must be a copy, not reference - get_model().value() may return temporary
        const auto model = passes[0].get_model().value();
        ASSERT_EQ( model.get_format(), sgns::ModelFormat::MNN );
        ASSERT_TRUE( !model.get_source_uri_param().empty() );
        ASSERT_EQ( model.get_source_uri_param(), "file://processing_datatypes/bert-tiny.mnn" );

        // Validate input nodes
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );
        ASSERT_EQ( input_nodes[0].get_name(), "input_ids" );
        ASSERT_EQ( input_nodes[0].get_type(), sgns::DataType::TENSOR );
        ASSERT_TRUE( input_nodes[0].get_source().has_value() );
        ASSERT_EQ( input_nodes[0].get_source().value(), "input:inputText" );

        // Validate output nodes
        const auto &output_nodes = model.get_output_nodes();
        ASSERT_EQ( output_nodes.size(), 1 );
        ASSERT_EQ( output_nodes[0].get_name(), "output" );
        ASSERT_EQ( output_nodes[0].get_type(), sgns::DataType::TENSOR );
        ASSERT_TRUE( output_nodes[0].get_target().has_value() );
        ASSERT_EQ( output_nodes[0].get_target().value(), "output:textEmbedding" );

        std::cout << "String input validation test passed successfully" << std::endl;
        std::cout << "Input type: " << static_cast<int>(inputs[0].get_type()) << " (STRING)" << std::endl;
        std::cout << "Model format: " << static_cast<int>(model.get_format()) << " (MNN)" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, StringInputConstraintsTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        // Load test instance file
        std::string   instance_file = data_path + "string-processing-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() );

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();

        // Parse and verify CheckProcessValidity succeeds for string inputs
        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() ) 
            << "ProcessingManager validation should pass for valid string input: "
            << manager_result.error().message();

        std::cout << "String input passed CheckProcessValidity (no dimension requirements)" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, StringInputProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        // Load test instance file
        std::string   instance_file = data_path + "string-processing-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open instance file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        // Create ProcessingManager and initialize with JSON
        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() ) << "Failed to create ProcessingManager: " 
                                                   << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        // Get the processing data to access model nodes
        auto processing = manager->GetProcessingData();
        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        
        ASSERT_TRUE( passes[0].get_model().has_value() );
        // Note: Must be a copy, not reference - get_model().value() may return temporary
        const auto model = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );
        
        // Create IO context for async operations
        auto ioc = std::make_shared<boost::asio::io_context>();
        
        // Create vector to store chunk hashes
        std::vector<std::vector<uint8_t>> chunkhashes;
        
        // Get a mutable copy of the first input node
        sgns::ModelNode model_node = input_nodes[0];
        
        std::cout << "Calling Process() on ProcessingManager..." << std::endl;
        
        // Call Process() - this will load the model and text file and run inference
        auto process_result = manager->Process( ioc, chunkhashes, model_node );
        
        if ( process_result.has_value() ) {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;
            
            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        } else {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }
    }

    TEST_F( ProcessingDatatypesTest, Texture3DValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "texture3d-processing-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open instance file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() ) << "Failed to create ProcessingManager: "
                                                   << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto processing = manager->GetProcessingData();
        const auto &inputs = processing.get_inputs();
        ASSERT_EQ( inputs.size(), 1 );
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::TEXTURE3_D );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 512 );
        ASSERT_EQ( dims.get_height().value(), 512 );
        ASSERT_EQ( dims.get_chunk_count().value(), 38 );
        ASSERT_EQ( dims.get_chunk_subchunk_width().value(), 96 );
        ASSERT_EQ( dims.get_chunk_subchunk_height().value(), 96 );
        ASSERT_EQ( dims.get_block_len().value(), 96 );
    }

    TEST_F( ProcessingDatatypesTest, Texture3DProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "texture3d-processing-definition.json";
        std::ifstream instance_stream( instance_file );
        ASSERT_TRUE( instance_stream.is_open() ) << "Failed to open instance file: " << instance_file;

        std::string instance_str( ( std::istreambuf_iterator<char>( instance_stream ) ),
                                  std::istreambuf_iterator<char>() );
        instance_stream.close();
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() ) << "Failed to create ProcessingManager: "
                                                   << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto processing = manager->GetProcessingData();
        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        sgns::ModelNode model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (texture3D)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node );

        if ( process_result.has_value() ) {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        } else {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }
    }
}
