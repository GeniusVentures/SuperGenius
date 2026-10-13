#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>
#include <vector>
#include <limits>
#include <map>
#include <regex>
#include <boost/dll.hpp>
#include <boost/asio.hpp>
#include <boost/optional/optional_io.hpp>
#include <processingbase/ProcessingManager.hpp>

namespace sgns
{
    // Helper function to patch relative file:// URIs in JSON to absolute paths
    // This ensures tests work correctly regardless of working directory
    static std::string PatchJsonUrisToAbsolute( const std::string &json_str, const std::string &bin_path )
    {
        std::string result;
        std::string normalized_bin_path = bin_path;

        // Normalize backslashes to forward slashes in bin_path
        for ( auto &c : normalized_bin_path )
        {
            if ( c == '\\' )
            {
                c = '/';
            }
        }

        // Pattern to match file:// URIs with relative paths (not already absolute)
        // Matches: file://path/to/file
        // Excludes: file:///absolute/path (triple slash) or file://C:/windows/path
        std::regex relative_file_uri_pattern( R"delim("(file://(?!/)(?![A-Za-z]:)[^"]+)")delim" );

        // Use regex_iterator to find and replace all matches manually
        size_t               last_pos = 0;
        std::sregex_iterator iter( json_str.begin(), json_str.end(), relative_file_uri_pattern );
        std::sregex_iterator end;

        while ( iter != end )
        {
            // Add text before the match
            result += json_str.substr( last_pos, iter->position() - last_pos );

            std::string original_uri = ( *iter )[1].str();
            // Extract the relative part after "file://"
            std::string relative_path = original_uri.substr( 7 ); // Skip "file://"

            // Build absolute URI using bin_path
            std::string absolute_uri = "file://" + normalized_bin_path + relative_path;

            // Add the replacement
            result += "\"" + absolute_uri + "\"";

            last_pos = iter->position() + iter->length();
            ++iter;
        }

        // Add any remaining text after the last match
        result += json_str.substr( last_pos );

        return result;
    }

    class ProcessingDatatypesTest : public ::testing::Test
    {
    protected:
        static inline std::string                        binary_path;
        static inline std::string                        data_path;
        static inline std::map<std::string, std::string> patched_json_cache = {};

        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "processing_datatypes/";
        }

        static void TearDownTestSuite() {}

        static const std::string &BinPath()
        {
            EnsurePaths();
            return binary_path;
        }

        static const std::string &DataPath()
        {
            EnsurePaths();
            return data_path;
        }

        static const std::string &PatchedJson( const std::string &filename )
        {
            auto cached = patched_json_cache.find( filename );
            if ( cached != patched_json_cache.end() )
            {
                return cached->second;
            }

            const std::string instance_file = DataPath() + filename;
            std::ifstream     instance_stream( instance_file );
            if ( !instance_stream.is_open() )
            {
                ADD_FAILURE() << "Failed to open instance file: " << instance_file;
                auto inserted = patched_json_cache.emplace( filename, std::string{} );
                return inserted.first->second;
            }

            std::string instance_str = std::string( std::istreambuf_iterator<char>( instance_stream ),
                                                    std::istreambuf_iterator<char>() );
            if ( instance_str.empty() )
            {
                ADD_FAILURE() << "Instance file is empty: " << instance_file;
            }

            auto inserted = patched_json_cache.emplace( filename, PatchJsonUrisToAbsolute( instance_str, BinPath() ) );
            return inserted.first->second;
        }

    private:
        static void EnsurePaths()
        {
            if ( binary_path.empty() )
            {
                binary_path = boost::dll::program_location().parent_path().string() + "/";
                data_path   = binary_path + "processing_datatypes/";
            }
        }
    };

    TEST_F( ProcessingDatatypesTest, StringInputValidationTest )
    {
        const std::string &instance_str = PatchedJson( "string-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        // Create ProcessingManager and initialize with JSON
        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
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
        // After patching, the URI should be absolute
        ASSERT_TRUE( inputs[0].get_source_uri_param().find( "file://" ) == 0 );
        ASSERT_TRUE( inputs[0].get_source_uri_param().find( "test_input.txt" ) != std::string::npos );

        // Validate string input does NOT require dimensions (unlike texture2D)
        ASSERT_FALSE( inputs[0].get_dimensions().has_value() ) << "String input should not have dimensions";

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
        // After patching, the URI should be absolute and contain the filename
        const auto &model_uri = model.get_source_uri_param();
        ASSERT_TRUE( model_uri.find( "file://" ) == 0 ) << "URI should start with file://";
        ASSERT_TRUE( model_uri.find( "bert-tiny.mnn" ) != std::string::npos ) << "URI should contain the filename";

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
        std::cout << "Input type: " << static_cast<int>( inputs[0].get_type() ) << " (STRING)" << std::endl;
        std::cout << "Model format: " << static_cast<int>( model.get_format() ) << " (MNN)" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, StringInputConstraintsTest )
    {
        const std::string &instance_str = PatchedJson( "string-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        // Parse and verify CheckProcessValidity succeeds for string inputs
        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "ProcessingManager validation should pass for valid string input: " << manager_result.error().message();

        std::cout << "String input passed CheckProcessValidity (no dimension requirements)" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, StringInputProcessingTest )
    {
        const std::string &instance_str = PatchedJson( "string-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        // Create ProcessingManager and initialize with JSON
        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        // Get the processing data to access model nodes
        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        // Note: Must be a copy, not reference - get_model().value() may return temporary
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        // Create IO context for async operations
        auto ioc = std::make_shared<boost::asio::io_context>();

        // Create vector to store chunk hashes
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string> output_locations;
        // Get a mutable copy of the first input node
        sgns::ModelNode model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager..." << std::endl;

        // Call Process() - this will load the model and text file and run inference
        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }
    }

    TEST_F( ProcessingDatatypesTest, Texture3DValidationTest )
    {
        const std::string &instance_str = PatchedJson( "texture3d-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "texture3d-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (texture3D)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }
    }

    TEST_F( ProcessingDatatypesTest, Texture1DValidationTest )
    {
        const std::string &instance_str = PatchedJson( "texture1d-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "texture1d-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (texture1D)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "texture1d_output.raw";
        const std::string reference_file = DataPath() + "texture1d_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        float  output_min    = std::numeric_limits<float>::infinity();
        float  output_max    = -std::numeric_limits<float>::infinity();
        size_t nonzero_count = 0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );

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
        const std::string &instance_str = PatchedJson( "bool-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "bool-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (bool)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "bool_output.raw";
        const std::string reference_file = DataPath() + "bool_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Bool output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, BufferValidationTest )
    {
        const std::string &instance_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (buffer)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "buffer_output.raw";
        const std::string reference_file = DataPath() + "buffer_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Buffer output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, FloatValidationTest )
    {
        const std::string &instance_str = PatchedJson( "float-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "float-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (float)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "float_output.raw";
        const std::string reference_file = DataPath() + "float_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Float output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, IntValidationTest )
    {
        const std::string &instance_str = PatchedJson( "int-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "int-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (int)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "int_output.raw";
        const std::string reference_file = DataPath() + "int_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Int output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Mat2ValidationTest )
    {
        const std::string &instance_str = PatchedJson( "mat2-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "mat2-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (mat2)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "mat2_output.raw";
        const std::string reference_file = DataPath() + "mat2_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Mat2 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Mat3ValidationTest )
    {
        const std::string &instance_str = PatchedJson( "mat3-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "mat3-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (mat3)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "mat3_output.raw";
        const std::string reference_file = DataPath() + "mat3_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Mat3 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Mat4ValidationTest )
    {
        const std::string &instance_str = PatchedJson( "mat4-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "mat4-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (mat4)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "mat4_output.raw";
        const std::string reference_file = DataPath() + "mat4_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Mat4 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Vec2ValidationTest )
    {
        const std::string &json_data = PatchedJson( "vec2-processing-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Instance file is empty";

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();
        auto inputs  = manager->GetProcessingData().get_inputs();
        ASSERT_EQ( inputs.size(), 1 ) << "Expected 1 input";
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::VEC2 );
    }

    TEST_F( ProcessingDatatypesTest, Vec2ProcessingTest )
    {
        const std::string &json_data = PatchedJson( "vec2-processing-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Instance file is empty";

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        // Create mock model node
        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (vec2)..." << std::endl;
        auto proc_result = manager->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( proc_result ) << "Process failed: " << proc_result.error().message();

        ASSERT_FALSE( proc_result.value().empty() ) << "Result hash should not be empty";
        ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";

        const std::string output_file    = DataPath() + "vec2_output.raw";
        const std::string reference_file = DataPath() + "vec2_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double mean_abs_diff = 0.0;
        double max_abs_diff  = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( max_abs_diff, diff );
        }

        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Vec2 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, TensorValidationTest )
    {
        const std::string &instance_str = PatchedJson( "tensor-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "tensor-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (tensor)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "tensor_output.raw";
        const std::string reference_file = DataPath() + "tensor_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Tensor output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, TextureCubeValidationTest )
    {
        const std::string &instance_str = PatchedJson( "texturecube-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
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
        const std::string &instance_str = PatchedJson( "texturecube-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        auto manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (textureCube)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        const std::string output_file    = DataPath() + "texturecube_output.raw";
        const std::string reference_file = DataPath() + "texturecube_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double max_abs_diff  = 0.0;
        double mean_abs_diff = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( diff, max_abs_diff );
        }
        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "TextureCube output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Vec3ValidationTest )
    {
        const std::string &json_data = PatchedJson( "vec3-processing-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Instance file is empty";

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();
        auto inputs  = manager->GetProcessingData().get_inputs();
        ASSERT_EQ( inputs.size(), 1 ) << "Expected 1 input";
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::VEC3 );
    }

    TEST_F( ProcessingDatatypesTest, Vec3ProcessingTest )
    {
        const std::string &json_data = PatchedJson( "vec3-processing-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Instance file is empty";

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        // Create mock model node
        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (vec3)..." << std::endl;
        auto proc_result = manager->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( proc_result ) << "Process failed: " << proc_result.error().message();

        ASSERT_FALSE( proc_result.value().empty() ) << "Result hash should not be empty";
        ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";

        const std::string output_file    = DataPath() + "vec3_output.raw";
        const std::string reference_file = DataPath() + "vec3_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double mean_abs_diff = 0.0;
        double max_abs_diff  = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( max_abs_diff, diff );
        }

        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Vec3 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    TEST_F( ProcessingDatatypesTest, Vec4ValidationTest )
    {
        const std::string &json_data = PatchedJson( "vec4-processing-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Instance file is empty";

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();
        auto inputs  = manager->GetProcessingData().get_inputs();
        ASSERT_EQ( inputs.size(), 1 ) << "Expected 1 input";
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::VEC4 );
    }

    TEST_F( ProcessingDatatypesTest, Vec4ProcessingTest )
    {
        const std::string &json_data = PatchedJson( "vec4-processing-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Instance file is empty";

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result ) << result.error().message();

        auto manager = result.value();

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        // Create mock model node
        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (vec4)..." << std::endl;
        auto proc_result = manager->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( proc_result ) << "Process failed: " << proc_result.error().message();

        ASSERT_FALSE( proc_result.value().empty() ) << "Result hash should not be empty";
        ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";

        const std::string output_file    = DataPath() + "vec4_output.raw";
        const std::string reference_file = DataPath() + "vec4_output_pt.raw";

        std::ifstream output_stream( output_file, std::ios::binary );
        ASSERT_TRUE( output_stream.is_open() ) << "Failed to open output file: " << output_file;

        std::ifstream reference_stream( reference_file, std::ios::binary );
        if ( !reference_stream.is_open() )
        {
            GTEST_SKIP() << "Reference output file not found: " << reference_file;
        }

        output_stream.seekg( 0, std::ios::end );
        reference_stream.seekg( 0, std::ios::end );
        const auto output_size    = output_stream.tellg();
        const auto reference_size = reference_stream.tellg();
        ASSERT_EQ( output_size, reference_size ) << "Output size mismatch";

        output_stream.seekg( 0, std::ios::beg );
        reference_stream.seekg( 0, std::ios::beg );

        std::vector<float> output_data( static_cast<size_t>( output_size ) / sizeof( float ) );
        std::vector<float> reference_data( static_cast<size_t>( reference_size ) / sizeof( float ) );

        output_stream.read( reinterpret_cast<char *>( output_data.data() ), output_size );
        reference_stream.read( reinterpret_cast<char *>( reference_data.data() ), reference_size );

        double mean_abs_diff = 0.0;
        double max_abs_diff  = 0.0;
        for ( size_t i = 0; i < output_data.size(); ++i )
        {
            const double diff  = std::abs( static_cast<double>( output_data[i] ) -
                                           static_cast<double>( reference_data[i] ) );
            mean_abs_diff     += diff;
            max_abs_diff       = std::max( max_abs_diff, diff );
        }

        mean_abs_diff /= static_cast<double>( output_data.size() );

        std::cout << "Vec4 output diff: mean=" << mean_abs_diff << " max=" << max_abs_diff << std::endl;

        ASSERT_LT( mean_abs_diff, 1e-3 ) << "Mean absolute diff too large";
        ASSERT_LT( max_abs_diff, 1e-2 ) << "Max absolute diff too large";
    }

    // =======================================================================
    // Phase 09: 5 new MNN processor types (audio, image, ml, string, volume)
    // =======================================================================

    TEST_F( ProcessingDatatypesTest, AudioValidationTest )
    {
        const std::string &instance_str = PatchedJson( "audio-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
        ASSERT_EQ( inputs.size(), 1 );
        ASSERT_EQ( inputs[0].get_name(), "inputAudio" );
        // Audio input type is schema-level — verify the input is valid
        ASSERT_TRUE( inputs[0].get_source_uri_param().find( "audio_input.raw" ) != std::string::npos );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 256 );
        ASSERT_EQ( dims.get_block_len().value(), 256 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 256 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );

        const auto &outputs = processing.get_outputs();
        ASSERT_EQ( outputs.size(), 1 );
        ASSERT_EQ( outputs[0].get_name(), "audioOutput" );
        ASSERT_EQ( outputs[0].get_type(), sgns::DataType::TENSOR );

        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_name(), "audio_inference" );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        ASSERT_EQ( model.get_format(), sgns::ModelFormat::MNN );

        std::cout << "Audio validation test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, AudioProcessingTest )
    {
        const std::string &instance_str = PatchedJson( "audio-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (audio)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        std::cout << "Audio processing test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, ImageValidationTest )
    {
        const std::string &instance_str = PatchedJson( "image-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
        ASSERT_EQ( inputs.size(), 1 );
        ASSERT_EQ( inputs[0].get_name(), "inputImage" );
        // Image input type is schema-level — verify the input is valid
        ASSERT_TRUE( inputs[0].get_source_uri_param().find( "image_input.raw" ) != std::string::npos );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 32 );
        ASSERT_EQ( dims.get_height().value(), 32 );
        ASSERT_EQ( dims.get_block_len().value(), 32 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );

        const auto &outputs = processing.get_outputs();
        ASSERT_EQ( outputs.size(), 1 );
        ASSERT_EQ( outputs[0].get_name(), "imageOutput" );
        ASSERT_EQ( outputs[0].get_type(), sgns::DataType::TENSOR );

        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_name(), "image_inference" );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        ASSERT_EQ( model.get_format(), sgns::ModelFormat::MNN );

        std::cout << "Image validation test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, ImageProcessingTest )
    {
        const std::string &instance_str = PatchedJson( "image-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (image)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        std::cout << "Image processing test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, MlValidationTest )
    {
        const std::string &instance_str = PatchedJson( "ml-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
        ASSERT_EQ( inputs.size(), 1 );
        // ML input type is schema-level — verify the input is valid
        ASSERT_TRUE( inputs[0].get_source_uri_param().find( "ml_input.raw" ) != std::string::npos );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 64 );
        ASSERT_EQ( dims.get_block_len().value(), 64 );
        ASSERT_EQ( dims.get_chunk_stride().value(), 64 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );

        const auto &outputs = processing.get_outputs();
        ASSERT_EQ( outputs.size(), 1 );
        ASSERT_EQ( outputs[0].get_name(), "mlOutput" );
        ASSERT_EQ( outputs[0].get_type(), sgns::DataType::TENSOR );

        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_name(), "ml_inference" );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        ASSERT_EQ( model.get_format(), sgns::ModelFormat::MNN );

        std::cout << "ML validation test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, MlProcessingTest )
    {
        const std::string &instance_str = PatchedJson( "ml-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (ml)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        std::cout << "ML processing test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, StringConformanceValidationTest )
    {
        const std::string &instance_str = PatchedJson( "string-conformance-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
        ASSERT_EQ( inputs.size(), 1 );
        ASSERT_EQ( inputs[0].get_name(), "inputText" );
        ASSERT_EQ( inputs[0].get_type(), sgns::DataType::STRING );

        const auto &outputs = processing.get_outputs();
        ASSERT_EQ( outputs.size(), 1 );
        ASSERT_EQ( outputs[0].get_name(), "textEmbedding" );
        ASSERT_EQ( outputs[0].get_type(), sgns::DataType::TENSOR );

        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_name(), "string_conformance_inference" );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        ASSERT_EQ( model.get_format(), sgns::ModelFormat::MNN );
        ASSERT_TRUE( model.get_source_uri_param().find( "string_tiny.mnn" ) != std::string::npos );

        std::cout << "String conformance validation test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, StringConformanceProcessingTest )
    {
        const std::string &instance_str = PatchedJson( "string-conformance-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (string conformance)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        std::cout << "String conformance processing test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, VolumeValidationTest )
    {
        const std::string &instance_str = PatchedJson( "volume-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &inputs     = processing.get_inputs();
        ASSERT_EQ( inputs.size(), 1 );
        // Volume input type is schema-level — verify the input is valid
        ASSERT_TRUE( inputs[0].get_source_uri_param().find( "volume_input.raw" ) != std::string::npos );
        ASSERT_TRUE( inputs[0].get_dimensions().has_value() );
        auto dims = inputs[0].get_dimensions().value();
        ASSERT_EQ( dims.get_width().value(), 16 );
        ASSERT_EQ( dims.get_height().value(), 16 );
        ASSERT_EQ( dims.get_chunk_count().value(), 16 );
        ASSERT_EQ( dims.get_block_len().value(), 16 );
        ASSERT_TRUE( inputs[0].get_format().has_value() );
        ASSERT_EQ( inputs[0].get_format().value(), sgns::InputFormat::FLOAT32 );

        const auto &outputs = processing.get_outputs();
        ASSERT_EQ( outputs.size(), 1 );
        ASSERT_EQ( outputs[0].get_name(), "volumeOutput" );
        ASSERT_EQ( outputs[0].get_type(), sgns::DataType::TENSOR );

        const auto &passes = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_name(), "volume_inference" );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        ASSERT_EQ( model.get_format(), sgns::ModelFormat::MNN );

        std::cout << "Volume validation test passed successfully" << std::endl;
    }

    TEST_F( ProcessingDatatypesTest, VolumeProcessingTest )
    {
        const std::string &instance_str = PatchedJson( "volume-processing-definition.json" );
        ASSERT_FALSE( instance_str.empty() ) << "Instance file is empty";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( instance_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        ASSERT_NE( manager, nullptr ) << "ProcessingManager is null";

        auto        processing = manager->GetProcessingData();
        const auto &passes     = processing.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_EQ( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        std::cout << "Calling Process() on ProcessingManager (volume)..." << std::endl;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( process_result.has_value() )
        {
            std::cout << "Process() succeeded!" << std::endl;
            std::cout << "Result hash size: " << process_result.value().size() << " bytes" << std::endl;
            std::cout << "Number of chunk hashes: " << chunkhashes.size() << std::endl;

            ASSERT_FALSE( process_result.value().empty() ) << "Result hash should not be empty";
            ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";
        }
        else
        {
            std::cout << "Process() failed: " << process_result.error().message() << std::endl;
            FAIL() << "Process() should succeed: " << process_result.error().message();
        }

        std::cout << "Volume processing test passed successfully" << std::endl;
    }
}
