#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>
#include <vector>
#include <limits>
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
        ASSERT_EQ( dims.get_width().value(), 253 );
        ASSERT_EQ( dims.get_height().value(), 253 );
        ASSERT_EQ( dims.get_chunk_count().value(), 94 );
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

    TEST_F( ProcessingDatatypesTest, Texture1DValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "texture1d-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::TEXTURE1_D );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 256 );
        ASSERT_EQ( dims.get_block_len().value(), 256 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 256 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );
    }

    TEST_F( ProcessingDatatypesTest, Texture1DProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "texture1d-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (texture1D)..." << std::endl;

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

        const std::string output_file = data_path + "texture1d_output.raw";
        const std::string reference_file = data_path + "texture1d_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        float output_min = std::numeric_limits<float>::infinity();
        float output_max = -std::numeric_limits<float>::infinity();
        size_t nonzero_count = 0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }

            output_min = std::min( output_min, output_data[i] );
            output_max = std::max( output_max, output_data[i] );
            if ( std::abs( output_data[i] ) > 1e-8f )
            {
                ++nonzero_count;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Texture1D output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;
        std::cout << "Texture1D output stats: min=" << output_min << " max=" << output_max
                  << " nonzero=" << nonzero_count << "/" << output_data.size() << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, BoolValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "bool-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::BOOL );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 8 );
        ASSERT_EQ( dims.get_block_len().value(), 8 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 8 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );
    }

    TEST_F( ProcessingDatatypesTest, BoolProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "bool-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (bool)..." << std::endl;

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

        const std::string output_file = data_path + "bool_output.raw";
        const std::string reference_file = data_path + "bool_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Bool output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, BufferValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "buffer-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::BUFFER );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 16 );
        ASSERT_EQ( dims.get_block_len().value(), 16 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 16 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::INT8 );
    }

    TEST_F( ProcessingDatatypesTest, BufferProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "buffer-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (buffer)..." << std::endl;

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

        const std::string output_file = data_path + "buffer_output.raw";
        const std::string reference_file = data_path + "buffer_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Buffer output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, FloatValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "float-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::FLOAT );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 512 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );
    }

    TEST_F( ProcessingDatatypesTest, FloatProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "float-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (float)..." << std::endl;

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

        const std::string output_file = data_path + "float_output.raw";
        const std::string reference_file = data_path + "float_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Float output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, IntValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "int-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::INT );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 512 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::INT32 );
    }

    TEST_F( ProcessingDatatypesTest, IntProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "int-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (int)..." << std::endl;

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

        const std::string output_file = data_path + "int_output.raw";
        const std::string reference_file = data_path + "int_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Int output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Mat2ValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "mat2-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::MAT2 );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 16 );
        ASSERT_EQ( dims.get_block_len().value(), 8 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 4 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );
    }

    TEST_F( ProcessingDatatypesTest, Mat2ProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "mat2-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (mat2)..." << std::endl;

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

        const std::string output_file = data_path + "mat2_output.raw";
        const std::string reference_file = data_path + "mat2_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Mat2 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Mat3ValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "mat3-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::MAT3 );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 12 );
        ASSERT_EQ( dims.get_block_len().value(), 6 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 3 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );
    }

    TEST_F( ProcessingDatatypesTest, Mat3ProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "mat3-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (mat3)..." << std::endl;

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

        const std::string output_file = data_path + "mat3_output.raw";
        const std::string reference_file = data_path + "mat3_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Mat3 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Mat4ValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "mat4-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::MAT4 );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 10 );
        ASSERT_EQ( dims.get_block_len().value(), 5 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 2 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );
    }

    TEST_F( ProcessingDatatypesTest, Mat4ProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "mat4-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (mat4)..." << std::endl;

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

        const std::string output_file = data_path + "mat4_output.raw";
        const std::string reference_file = data_path + "mat4_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Mat4 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Vec2ValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "processing_datatypes/";

        std::string   instance_file = data_path + "vec2-processing-definition.json";
        std::ifstream ifs( instance_file );
        ASSERT_TRUE( ifs ) << "Could not open " << instance_file;

        std::string json_data( ( std::istreambuf_iterator<char>( ifs ) ),
                               std::istreambuf_iterator<char>() );

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();
        auto inputs = manager->GetProcessingData().get_inputs();
        ASSERT_EQ( inputs.size(), 1 ) << "Expected 1 input";
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::VEC2 );
    }

    TEST_F( ProcessingDatatypesTest, Vec2ProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "processing_datatypes/";

        std::string   instance_file = data_path + "vec2-processing-definition.json";
        std::ifstream ifs( instance_file );
        ASSERT_TRUE( ifs ) << "Could not open " << instance_file;

        std::string json_data( ( std::istreambuf_iterator<char>( ifs ) ),
                               std::istreambuf_iterator<char>() );

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();

        auto processing = manager->GetProcessingData();
        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        // Create mock model node
        auto ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        sgns::ModelNode model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (vec2)..." << std::endl;
        auto proc_result = manager->Process( ioc, chunkhashes, model_node );
        ASSERT_TRUE( proc_result ) << "Process failed: " << proc_result.error().message();

        ASSERT_FALSE( proc_result.value().empty() ) << "Result hash should not be empty";
        ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";

        const std::string output_file = data_path + "vec2_output.raw";
        const std::string reference_file = data_path + "vec2_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double mean_abs_diff = 0.0;
        double max_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            max_abs_diff = std::max( max_abs_diff, diff );
        }

        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Vec2 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, TensorValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "tensor-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::TENSOR );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 256 );
        ASSERT_EQ( dims.get_block_len().value(), 64 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 32 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );
    }

    TEST_F( ProcessingDatatypesTest, TensorProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "tensor-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (tensor)..." << std::endl;

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

        const std::string output_file = data_path + "tensor_output.raw";
        const std::string reference_file = data_path + "tensor_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Tensor output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, TextureCubeValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "texturecube-processing-definition.json";
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
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::TEXTURE_CUBE );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 64 );
        ASSERT_EQ( dims.get_height().value(), 64 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::RGB8 );
    }

    TEST_F( ProcessingDatatypesTest, TextureCubeProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "./processing_datatypes/";

        std::string   instance_file = data_path + "texturecube-processing-definition.json";
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

        std::cout << "Calling Process() on ProcessingManager (textureCube)..." << std::endl;

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

        const std::string output_file = data_path + "texturecube_output.raw";
        const std::string reference_file = data_path + "texturecube_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            if ( diff > max_abs_diff )
            {
                max_abs_diff = diff;
            }
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "TextureCube output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Vec3ValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "processing_datatypes/";

        std::string   instance_file = data_path + "vec3-processing-definition.json";
        std::ifstream ifs( instance_file );
        ASSERT_TRUE( ifs ) << "Could not open " << instance_file;

        std::string json_data( ( std::istreambuf_iterator<char>( ifs ) ),
                               std::istreambuf_iterator<char>() );

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();
        auto inputs = manager->GetProcessingData().get_inputs();
        ASSERT_EQ( inputs.size(), 1 ) << "Expected 1 input";
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::VEC3 );
    }

    TEST_F( ProcessingDatatypesTest, Vec3ProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "processing_datatypes/";

        std::string   instance_file = data_path + "vec3-processing-definition.json";
        std::ifstream ifs( instance_file );
        ASSERT_TRUE( ifs ) << "Could not open " << instance_file;

        std::string json_data( ( std::istreambuf_iterator<char>( ifs ) ),
                               std::istreambuf_iterator<char>() );

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();

        auto processing = manager->GetProcessingData();
        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        // Create mock model node
        auto ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        sgns::ModelNode model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (vec3)..." << std::endl;
        auto proc_result = manager->Process( ioc, chunkhashes, model_node );
        ASSERT_TRUE( proc_result ) << "Process failed: " << proc_result.error().message();

        ASSERT_FALSE( proc_result.value().empty() ) << "Result hash should not be empty";
        ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";

        const std::string output_file = data_path + "vec3_output.raw";
        const std::string reference_file = data_path + "vec3_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double mean_abs_diff = 0.0;
        double max_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            max_abs_diff = std::max( max_abs_diff, diff );
        }

        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Vec3 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Vec4ValidationTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "processing_datatypes/";

        std::string   instance_file = data_path + "vec4-processing-definition.json";
        std::ifstream ifs( instance_file );
        ASSERT_TRUE( ifs ) << "Could not open " << instance_file;

        std::string json_data( ( std::istreambuf_iterator<char>( ifs ) ),
                               std::istreambuf_iterator<char>() );

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();
        auto inputs = manager->GetProcessingData().get_inputs();
        ASSERT_EQ( inputs.size(), 1 ) << "Expected 1 input";
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::VEC4 );
    }

    TEST_F( ProcessingDatatypesTest, Vec4ProcessingTest )
    {
        std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
        std::string data_path = bin_path + "processing_datatypes/";

        std::string   instance_file = data_path + "vec4-processing-definition.json";
        std::ifstream ifs( instance_file );
        ASSERT_TRUE( ifs ) << "Could not open " << instance_file;

        std::string json_data( ( std::istreambuf_iterator<char>( ifs ) ),
                               std::istreambuf_iterator<char>() );

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();

        auto processing = manager->GetProcessingData();
        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        // Create mock model node
        auto ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        sgns::ModelNode model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (vec4)..." << std::endl;
        auto proc_result = manager->Process( ioc, chunkhashes, model_node );
        ASSERT_TRUE( proc_result ) << "Process failed: " << proc_result.error().message();

        ASSERT_FALSE( proc_result.value().empty() ) << "Result hash should not be empty";
        ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";

        const std::string output_file = data_path + "vec4_output.raw";
        const std::string reference_file = data_path + "vec4_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double mean_abs_diff = 0.0;
        double max_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff = std::abs( static_cast<double>( output_data[i] ) -
                                          static_cast<double>( reference_data[i] ) );
            mean_abs_diff += diff;
            max_abs_diff = std::max( max_abs_diff, diff );
        }

        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Vec4 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }
}