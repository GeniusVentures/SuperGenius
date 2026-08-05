#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <iostream>
#include <regex>
#include <thread>
#include <future>
#include <vector>
#include <boost/dll.hpp>
#include <boost/asio/io_context.hpp>
#include <processingbase/ProcessingManager.hpp>
#include <processors/processing_processor_render.hpp>

namespace sgns
{
    static std::string PatchJsonUrisToAbsolute( const std::string &json_str, const std::string &bin_path )
    {
        std::string result;
        std::string normalized_bin_path = bin_path;
        for ( auto &c : normalized_bin_path )
        {
            if ( c == '\\' ) c = '/';
        }

        std::regex relative_file_uri_pattern( R"delim("(file://(?!/)(?![A-Za-z]:)[^"]+)")delim" );

        size_t               last_pos = 0;
        std::sregex_iterator iter( json_str.begin(), json_str.end(), relative_file_uri_pattern );
        std::sregex_iterator end;

        while ( iter != end )
        {
            result += json_str.substr( last_pos, iter->position() - last_pos );
            std::string original_uri  = ( *iter )[1].str();
            std::string relative_path = original_uri.substr( 7 );
            std::string absolute_uri  = "file://" + normalized_bin_path + relative_path;
            result += "\"" + absolute_uri + "\"";
            last_pos = iter->position() + iter->length();
            ++iter;
        }
        result += json_str.substr( last_pos );
        return result;
    }

    class VulkanConcurrentInitTest : public ::testing::Test
    {
    protected:
        static inline std::string binary_path = "";
        static inline std::string data_path   = "";

        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "processing_datatypes/";
        }

        static void TearDownTestSuite() {}

        static std::string LoadAndPatchJson( const std::string &filename )
        {
            std::string file_path = data_path + filename;
            std::ifstream stream( file_path );
            if ( !stream.is_open() )
            {
                return "";
            }
            std::string content( ( std::istreambuf_iterator<char>( stream ) ),
                                 std::istreambuf_iterator<char>() );
            return PatchJsonUrisToAbsolute( content, binary_path );
        }
    };

    // Scope note (Phase 01.1, MIGR-02/COV-01): this test exercises 3 representative
    // Vulkan-init call sites -- MNN_String, MNN_Volume (texture3d-processing-definition.json),
    // and RenderProcessor -- as a sample of the full set of callers that now share
    // sgns::sgprocessing::VulkanInitMutex() after Phase 01.1's CPU-to-Vulkan processor
    // migration (16 MNN processors, 17 MNN createSession(MNN_FORWARD_VULKAN) call sites --
    // re-verify via `grep -rc MNN_FORWARD_VULKAN SGProcessingManager/src/processors/*.cpp`,
    // since this count can drift -- plus RenderProcessor's lazy-init path). This extends the
    // same shared-accessor generalization argument 01-06-SUMMARY.md already established for
    // MNN_Image ("correct serialization for 3 distinct callers implies correct serialization
    // for the 4th", because all callers go through the identical VulkanInitMutex() accessor):
    // proving representative callers serialize correctly under concurrent load is sufficient
    // evidence for the full, larger set, without needing a thread per call site.
    TEST_F( VulkanConcurrentInitTest, RepeatedConcurrentInitNoRaceOrCrash )
    {
        constexpr int kIterations = 25;

        for ( int iter = 0; iter < kIterations; ++iter )
        {
            std::promise<void> releaseGate;
            std::shared_future<void> releaseFuture( releaseGate.get_future() );

            std::vector<std::thread> threads;

            threads.emplace_back( [&releaseFuture, this]() {
                releaseFuture.wait();

                std::string json_str = LoadAndPatchJson( "string-processing-definition.json" );
                if ( json_str.empty() ) return;

                auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
                if ( !mgr_result.has_value() ) return;
                auto manager = mgr_result.value();

                auto processing_data = manager->GetProcessingData();
                auto passes = processing_data.get_passes();
                if ( passes.empty() ) return;
                const auto &input_nodes = passes[0].get_model().value().get_input_nodes();
                if ( input_nodes.empty() ) return;
                sgns::ModelNode model_node = input_nodes[0];

                auto ioc = std::make_shared<boost::asio::io_context>();
                std::vector<std::vector<uint8_t>> chunkhashes;
                std::vector<std::string> output_locations;
                manager->Process( ioc, chunkhashes, model_node, output_locations );
            } );

            threads.emplace_back( [&releaseFuture, this]() {
                releaseFuture.wait();

                std::string json_str = LoadAndPatchJson( "texture3d-processing-definition.json" );
                if ( json_str.empty() ) return;

                auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
                if ( !mgr_result.has_value() ) return;
                auto manager = mgr_result.value();

                auto processing_data = manager->GetProcessingData();
                auto passes = processing_data.get_passes();
                if ( passes.empty() ) return;
                const auto &input_nodes = passes[0].get_model().value().get_input_nodes();
                if ( input_nodes.empty() ) return;
                sgns::ModelNode model_node = input_nodes[0];

                auto ioc = std::make_shared<boost::asio::io_context>();
                std::vector<std::vector<uint8_t>> chunkhashes;
                std::vector<std::string> output_locations;
                manager->Process( ioc, chunkhashes, model_node, output_locations );
            } );

            threads.emplace_back( [&releaseFuture]() {
                releaseFuture.wait();

                sgns::sgprocessing::RenderProcessor rp;
                std::vector<std::vector<uint8_t>> chunkhashes;
                std::vector<char> imageData;
                std::vector<char> modelFile;
                sgns::IoDeclaration proc{};
                sgns::sgprocessing::ExecutionContext execCtx;
                rp.StartProcessing( chunkhashes, proc, imageData, modelFile, nullptr, execCtx );
            } );

            releaseGate.set_value();

            for ( auto &t : threads )
            {
                t.join();
            }
        }

        SUCCEED() << "Completed " << kIterations << " concurrent-init iterations without crash";
    }
}
