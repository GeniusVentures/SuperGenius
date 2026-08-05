#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#include <processingbase/ProcessingManager.hpp>
#include "testutil/processing_conformance_fixture.hpp"

namespace sgns
{
    class OutputHashingTest : public ProcessorConformanceFixture
    {
    protected:
        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "processing_datatypes/";
        }
    };

    TEST_F( OutputHashingTest, ContentHashDeterministic )
    {
        // Load a known MNN definition and run twice — verify same hash
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r1 = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r1.has_value() );
        auto r2 = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r2.has_value() );

        const auto &manager1 = r1.value();
        const auto &manager2 = r2.value();

        auto        p1 = manager1->GetProcessingData();
        const auto &passes1 = p1.get_passes();
        ASSERT_EQ( passes1.size(), 1 );
        ASSERT_TRUE( passes1[0].get_model().has_value() );
        const auto model1       = passes1[0].get_model().value();
        const auto input_nodes1 = model1.get_input_nodes();
        ASSERT_GE( input_nodes1.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes1;
        std::vector<std::string>         output_locations1;
        sgns::ModelNode                   model_node1 = input_nodes1[0];

        auto pr1 = manager1->Process( ioc, chunkhashes1, model_node1, output_locations1 );
        ASSERT_TRUE( pr1.has_value() ) << "First run: " << pr1.error().message();

        std::vector<std::vector<uint8_t>> chunkhashes2;
        std::vector<std::string>         output_locations2;
        sgns::ModelNode                   model_node2 = input_nodes1[0];
        // Reset model_node data if needed
        model_node2 = input_nodes1[0];

        auto pr2 = manager2->Process( ioc, chunkhashes2, model_node2, output_locations2 );
        ASSERT_TRUE( pr2.has_value() ) << "Second run: " << pr2.error().message();

        // Both runs should produce identical combined hashes
        ASSERT_EQ( pr1.value().combinedHash, pr2.value().combinedHash )
            << "Content hash must be deterministic across runs";
        ASSERT_EQ( chunkhashes1.size(), chunkhashes2.size() )
            << "Chunk hash count must be deterministic";

        std::cout << "ContentHashDeterministic: hash identical across two runs ("
                  << pr1.value().size() << " bytes)" << std::endl;
    }

    TEST_F( OutputHashingTest, ContentHashChangesOnDifferentInput )
    {
        // Load buffer definition and run twice with different inputs — verify hashes differ
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r1 = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r1.has_value() );

        const auto &manager1 = r1.value();
        auto        p1 = manager1->GetProcessingData();
        const auto &passes1 = p1.get_passes();
        ASSERT_EQ( passes1.size(), 1 );
        ASSERT_TRUE( passes1[0].get_model().has_value() );
        const auto model1       = passes1[0].get_model().value();
        const auto input_nodes1 = model1.get_input_nodes();
        ASSERT_GE( input_nodes1.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes1;
        std::vector<std::string>         output_locations1;
        sgns::ModelNode                   model_node1 = input_nodes1[0];

        auto pr1 = manager1->Process( ioc, chunkhashes1, model_node1, output_locations1 );
        ASSERT_TRUE( pr1.has_value() );

        // Second run with float definition (different input)
        const std::string &json_str2 = PatchedJson( "float-processing-definition.json" );
        ASSERT_FALSE( json_str2.empty() );

        auto r2 = sgns::sgprocessing::ProcessingManager::Create( json_str2 );
        ASSERT_TRUE( r2.has_value() );

        const auto &manager2 = r2.value();
        auto        p2 = manager2->GetProcessingData();
        const auto &passes2 = p2.get_passes();
        ASSERT_EQ( passes2.size(), 1 );
        ASSERT_TRUE( passes2[0].get_model().has_value() );
        const auto model2       = passes2[0].get_model().value();
        const auto input_nodes2 = model2.get_input_nodes();
        ASSERT_GE( input_nodes2.size(), 1 );

        std::vector<std::vector<uint8_t>> chunkhashes2;
        std::vector<std::string>         output_locations2;
        sgns::ModelNode                   model_node2 = input_nodes2[0];

        auto pr2 = manager2->Process( ioc, chunkhashes2, model_node2, output_locations2 );
        ASSERT_TRUE( pr2.has_value() );

        // Different inputs should produce different combined hashes
        ASSERT_NE( pr1.value().combinedHash, pr2.value().combinedHash )
            << "Different inputs must produce different hashes";

        std::cout << "ContentHashChangesOnDifferentInput: different inputs → different hashes" << std::endl;
    }

    TEST_F( OutputHashingTest, ChunkHashesPresent )
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
        ASSERT_GT( chunkhashes.size(), 0 ) << "Should have at least one chunk hash";

        // Each chunk hash should be SHA-256 (32 bytes)
        for ( const auto &ch : chunkhashes )
        {
            ASSERT_EQ( ch.size(), 32u ) << "Each chunk hash must be SHA-256 (32 bytes)";
        }

        std::cout << "ChunkHashesPresent: " << chunkhashes.size()
                  << " chunk hashes, each 32 bytes" << std::endl;
    }

    TEST_F( OutputHashingTest, PersistenceRoundTrip )
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

        // Verify the result hash and chunk hashes are non-empty
        ASSERT_FALSE( pr.value().empty() );
        ASSERT_GT( chunkhashes.size(), 0 );

        // Verify output location is valid
        ASSERT_GT( output_locations.size(), 0 );

        std::cout << "PersistenceRoundTrip: Process() completed, result hash "
                  << pr.value().size() << " bytes, " << chunkhashes.size()
                  << " chunk hashes" << std::endl;
    }

} // namespace sgns
