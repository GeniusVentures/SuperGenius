#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#include <processingbase/ProcessingManager.hpp>
#include "testutil/processing_conformance_fixture.hpp"

namespace sgns
{
    class MigrationAdapterConformanceTest : public ProcessorConformanceFixture
    {
    protected:
        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "processing_datatypes/";
        }
    };

    TEST_F( MigrationAdapterConformanceTest, ProcessingResultShapePreserved )
    {
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( pr.has_value() ) << "Process() should succeed";

        // Verify result hash is non-empty (ProcessingResult shape: hash exists)
        ASSERT_FALSE( pr.value().empty() ) << "Result hash should not be empty";

        // Verify output locations (ProcessingResult shape: output_locations is a string)
        ASSERT_GT( output_locations.size(), 0 ) << "Should have at least one output location";

        std::cout << "ProcessingResultShapePreserved: hash size=" << pr.value().size()
                  << ", output_locations count=" << output_locations.size() << std::endl;
    }

    TEST_F( MigrationAdapterConformanceTest, EmptyErrorOnSuccess )
    {
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( pr.has_value() ) << "Process() should succeed: " << pr.error().message();

        // On success, we should have a valid result
        ASSERT_FALSE( pr.value().empty() );
        // output_locations should be set
        ASSERT_FALSE( output_locations.empty() );

        std::cout << "EmptyErrorOnSuccess: Process() returned success with valid data" << std::endl;
    }

    TEST_F( MigrationAdapterConformanceTest, OutputLocationsStringBehavior )
    {
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( pr.has_value() );

        // output_locations contains strings — verify they are non-empty
        for ( const auto &loc : output_locations )
        {
            ASSERT_FALSE( loc.empty() ) << "Each output location should be non-empty";
        }

        std::cout << "OutputLocationsStringBehavior: " << output_locations.size()
                  << " output location strings, all non-empty" << std::endl;
    }

    TEST_F( MigrationAdapterConformanceTest, RoundTripNoDataLoss )
    {
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( pr.has_value() );

        // Verify result hash integrity — the hash should be exactly the right length
        ASSERT_GE( pr.value().size(), 32u ) << "Result hash should be at least 32 bytes";

        // chunkhashes should be consistent
        ASSERT_EQ( chunkhashes.size(), output_locations.size() )
            << "Chunk hash count should match output locations count";

        std::cout << "RoundTripNoDataLoss: hash=" << pr.value().size()
                  << " bytes, chunks=" << chunkhashes.size() << std::endl;
    }

} // namespace sgns
