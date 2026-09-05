#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <iostream>
#include <boost/dll.hpp>
#include <boost/asio/io_context.hpp>
#include "SGNSProcMain.hpp"
#include "Generators.hpp"
#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>

namespace sgns
{
    class ProcessingDispatchTest : public ::testing::Test
    {
    protected:
        static inline std::string binary_path = "";

        static void SetUpTestSuite()
        {
        }

        static void TearDownTestSuite()
        {
        }

        static std::string LoadJson( const std::string &filename )
        {
            std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
            std::string data_path = bin_path + "./processing_dispatch/";
            std::string file_path = data_path + filename;

            std::ifstream stream( file_path );
            if ( !stream.is_open() )
            {
                return "";
            }
            std::string content( ( std::istreambuf_iterator<char>( stream ) ),
                                 std::istreambuf_iterator<char>() );
            return content;
        }

        /// Writes the happy-path fixture's vertex data (3 scalar floats, matching
        /// vertex_layout's single FLOAT32 entry's auto-computed 4-byte stride) as a real
        /// binary file at the path "file://processing_dispatch/happy-path-vertex-data.raw"
        /// resolves to -- following LoadJson's exact path-construction convention so the
        /// file lands where FileManager's "file" scheme actually looks for it.
        static void WriteHappyPathVertexData()
        {
            std::string bin_path  = boost::dll::program_location().parent_path().string() + "/";
            std::string data_path = bin_path + "./processing_dispatch/";
            std::string file_path = data_path + "happy-path-vertex-data.raw";

            float values[3] = { -0.5f, 0.0f, 0.5f };

            std::ofstream stream( file_path, std::ios::binary );
            stream.write( reinterpret_cast<const char *>( values ), sizeof( values ) );
            stream.close();
        }
    };

    TEST_F( ProcessingDispatchTest, RenderPassMissingShaderFailsValidity )
    {
        std::string json_data = LoadJson( "render-pass-missing-shader-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load missing-shader fixture";

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_FALSE( result.has_value() )
            << "ProcessingManager::Create should fail for a render pass with no shader";
    }

    TEST_F( ProcessingDispatchTest, RenderPassValidPassesCheckProcessValidity )
    {
        std::string json_data = LoadJson( "render-pass-valid-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load valid fixture";

        auto result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( result.has_value() )
            << "ProcessingManager::Create should succeed for a render pass with a shader";
    }

    TEST_F( ProcessingDispatchTest, RenderPassDispatchesToRenderProcessorNotNoProcessor )
    {
        std::string json_data = LoadJson( "render-pass-valid-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load valid fixture";

        auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( mgr_result.has_value() );

        auto manager = mgr_result.value();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:renderInput" ) );

        auto ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string> output_locations;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        ASSERT_FALSE( process_result.has_value() )
            << "Process() should fail with a dummy URI (no real shader data), "
               "but dispatch resolution should have succeeded";

        auto error = process_result.error();
        ASSERT_NE( error, sgns::sgprocessing::ProcessingManager::Error::NO_PROCESSOR )
            << "Dispatch should NOT return NO_PROCESSOR — "
               "the RenderProcessor factory was registered and the pass-type branch matched";
        ASSERT_NE( error, sgns::sgprocessing::ProcessingManager::Error::MISSING_INPUT )
            << "Input resolution should succeed — 'input:renderInput' is in the JSON input map";
    }

    TEST_F( ProcessingDispatchTest, ModelLessPassParseBlockSizeDoesNotCrash )
    {
        std::string json_data = LoadJson( "render-pass-valid-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load valid fixture";

        auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( mgr_result.has_value() );

        auto manager = mgr_result.value();

        auto block_size_result = manager->ParseBlockSize();
        ASSERT_TRUE( block_size_result.has_value() )
            << "ParseBlockSize() should succeed (model-less passes contribute 0 to block size)";
        ASSERT_EQ( block_size_result.value(), 0 )
            << "Model-less pass should contribute 0 to block_total_len";
    }

    TEST_F( ProcessingDispatchTest, RenderPassValidGlslShadersCompileAndValidateEndToEnd )
    {
        std::string json_data = LoadJson( "render-pass-valid-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load valid fixture";

        auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( mgr_result.has_value() );

        auto manager = mgr_result.value();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:renderInput" ) );

        auto                               ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>          output_locations;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        ASSERT_FALSE( process_result.has_value() )
            << "Process() should still fail (renderInput's data URI is bogus), "
               "but both real GLSL stages should have compiled+validated successfully first";

        auto error = process_result.error();
        ASSERT_EQ( error, sgns::sgprocessing::ProcessingManager::Error::INPUT_UNAVAIL )
            << "Failure must be specifically INPUT_UNAVAIL (missing render input data), "
               "NOT a shader-compile/validation/dispatch failure — proving the two valid "
               "GLSL stages compiled and validated successfully before dispatch reached the "
               "genuinely-missing render input";
        ASSERT_NE( error, sgns::sgprocessing::ProcessingManager::Error::SHADER_COMPILE_FAILED );
        ASSERT_NE( error, sgns::sgprocessing::ProcessingManager::Error::SPIRV_VALIDATION_FAILED );
        ASSERT_NE( error, sgns::sgprocessing::ProcessingManager::Error::NO_PROCESSOR );
        ASSERT_NE( error, sgns::sgprocessing::ProcessingManager::Error::MISSING_INPUT );
    }

    TEST_F( ProcessingDispatchTest, RenderPassMalformedGlslShaderFailsCompileNotCrash )
    {
        std::string json_data = LoadJson( "render-pass-malformed-glsl-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load malformed-GLSL fixture";

        auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( mgr_result.has_value() );

        auto manager = mgr_result.value();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:renderInput" ) );

        auto                               ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>          output_locations;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        ASSERT_FALSE( process_result.has_value() )
            << "Process() must fail cleanly (not crash) on malformed GLSL";
        ASSERT_EQ( process_result.error(), sgns::sgprocessing::ProcessingManager::Error::SHADER_COMPILE_FAILED )
            << "Malformed GLSL must be rejected with SHADER_COMPILE_FAILED";
    }

    TEST_F( ProcessingDispatchTest, RenderPassInvalidDirectSpirvFailsValidationNotCrash )
    {
        std::string json_data = LoadJson( "render-pass-invalid-spirv-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load invalid-direct-SPIR-V fixture";

        auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( mgr_result.has_value() );

        auto manager = mgr_result.value();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:renderInput" ) );

        auto                               ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>          output_locations;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        ASSERT_FALSE( process_result.has_value() )
            << "Process() must fail cleanly (not crash) on invalid direct SPIR-V bytes";
        ASSERT_EQ( process_result.error(), sgns::sgprocessing::ProcessingManager::Error::SPIRV_VALIDATION_FAILED )
            << "Invalid direct SPIR-V must be rejected with SPIRV_VALIDATION_FAILED";
    }

    TEST_F( ProcessingDispatchTest, RenderPassStillFailsCleanlyOnMissingRenderInputAfterFullWiring )
    {
        std::string json_data = LoadJson( "render-pass-valid-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load valid fixture";

        auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( mgr_result.has_value() );

        auto manager = mgr_result.value();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:renderInput" ) );

        auto                               ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>          output_locations;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        ASSERT_FALSE( process_result.has_value() )
            << "Process() should still fail (renderInput's data URI is bogus), confirming plan "
               "03-05's now-fully-wired StartProcessing() is never reached for this fixture";

        auto error = process_result.error();
        ASSERT_NE( error, sgns::sgprocessing::ProcessingManager::Error::PROCESSING_FAILED )
            << "Failure must NOT be PROCESSING_FAILED (which would mean StartProcessing() was "
               "reached and failed) -- it must still surface as the pre-existing fetch-level "
               "INPUT_UNAVAIL failure from GetCidForProc(), confirming this plan's new "
               "fully-wired StartProcessing() introduces no regression to the existing "
               "fetch-level failure path plans 03-01/03-02 already cover";
        ASSERT_EQ( error, sgns::sgprocessing::ProcessingManager::Error::INPUT_UNAVAIL )
            << "Failure must specifically be INPUT_UNAVAIL (missing render input data)";
    }

    TEST_F( ProcessingDispatchTest, LegacyHlslShaderTypeFailsCleanlyNotCrash )
    {
        std::string json_data = LoadJson( "render-pass-legacy-hlsl-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load legacy-hlsl fixture";

        auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );

        ASSERT_FALSE( mgr_result.has_value() )
            << "Create() must reject a legacy shader_stage.type: \"hlsl\" value (removed from the "
               "schema enum per D-10) cleanly, not silently accept it";
        ASSERT_EQ( mgr_result.error(), sgns::sgprocessing::ProcessingManager::Error::INVALID_JSON )
            << "A schema-invalid enum value must surface as INVALID_JSON, not propagate an "
               "uncaught exception (this is the previously-live crash vector Init()'s broadened "
               "catch(const std::exception&) is meant to close)";
    }

    TEST_F( ProcessingDispatchTest, RenderPassSameNodeRepeatedExecutionProducesBitExactHash )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        WriteHappyPathVertexData();

        std::string json_data = LoadJson( "render-pass-happy-path-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load happy-path fixture";

        std::vector<std::vector<uint8_t>> allHashes;

        for ( int i = 0; i < 10; ++i )
        {
            auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );
            ASSERT_TRUE( mgr_result.has_value() ) << "Iteration " << i << ": ProcessingManager::Create failed";

            auto manager = mgr_result.value();

            sgns::ModelNode model_node;
            model_node.set_source( std::string( "input:renderInput" ) );

            auto                               ioc = std::make_shared<boost::asio::io_context>();
            std::vector<std::vector<uint8_t>> chunkhashes;
            std::vector<std::string>          output_locations;

            auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

            ASSERT_TRUE( process_result.has_value() )
                << "Iteration " << i << " failed: "
                << ( process_result ? "" : process_result.error().message() )
                << " -- a real Vulkan-capable GPU is a hard prerequisite for this test; this failure "
                   "is either a real determinism/implementation bug or an environment without a "
                   "usable Vulkan device (no software fallback exists in this codebase)";

            const auto &iterHash = process_result.value().combinedHash;

            ASSERT_EQ( iterHash.size(), 32u )
                << "Iteration " << i << "'s combinedHash is not a real 32-byte digest -- a "
                   "hash-equality-only comparison across iterations can pass vacuously if "
                   "combinedHash is silently empty on every iteration";

            allHashes.push_back( iterHash );
        }

        for ( size_t i = 1; i < allHashes.size(); ++i )
        {
            ASSERT_EQ( allHashes[i], allHashes[0] )
                << "Iteration " << i << "'s hash diverged from iteration 0's hash -- DETV-01's "
                   "bit-exact same-node repeat-run claim does not hold for this render pass";
        }
    }

    /// E2E-01: proves a real render-pass job definition executes end-to-end through the
    /// actual ProcessingManager/RenderProcessor code path and produces a verified output
    /// hash. Single-run only -- kept conceptually distinct from
    /// RenderPassSameNodeRepeatedExecutionProducesBitExactHash's DETV-01 bit-exact-repeat
    /// concern (10x loop), even though both reuse the same happy-path fixture (D-35).
    TEST_F( ProcessingDispatchTest, RenderPassEndToEndProducesVerifiedOutputHash )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        WriteHappyPathVertexData();

        std::string json_data = LoadJson( "render-pass-happy-path-definition.json" );
        ASSERT_FALSE( json_data.empty() ) << "Could not load happy-path fixture";

        auto mgr_result = sgns::sgprocessing::ProcessingManager::Create( json_data );
        ASSERT_TRUE( mgr_result.has_value() ) << "ProcessingManager::Create failed";

        auto manager = mgr_result.value();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:renderInput" ) );

        auto                               ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>          output_locations;

        auto process_result = manager->Process( ioc, chunkhashes, model_node, output_locations );

        ASSERT_TRUE( process_result.has_value() )
            << "Process() failed: "
            << ( process_result ? "" : process_result.error().message() )
            << " -- a real Vulkan-capable GPU is a hard prerequisite for this test";

        const auto &hash = process_result.value().combinedHash;

        ASSERT_EQ( hash.size(), 32u ) << "Output hash must be a 32-byte SHA-256 digest";
        ASSERT_NE( hash, std::vector<uint8_t>( 32, 0 ) )
            << "Output hash must not be RenderProcessor::MakeError's all-zero sentinel vector";
    }

    // Proves texture_buffer's schema/validation gate (Phase 17 D-05, texturing's
    // "define contracts" half) in isolation: an input:-prefixed texture_buffer.source
    // must be accepted, an output:-prefixed one must be rejected -- both purely at
    // ProcessingManager::Create()'s CheckProcessValidity() gate, never calling
    // Process() (Wave 3's Plan 17-04 sampler/descriptor infrastructure does not
    // exist yet, so this test must not depend on it).
    TEST_F( ProcessingDispatchTest, RenderPassTextureBufferSourcePrefixGating )
    {
        const std::string jsonAcceptedInputPrefix = R"({
      "name": "render-pass-texture-buffer-accept",
      "version": "1.0.0",
      "gnus_spec_version": 1.0,
      "inputs": [
        {
          "name": "renderInput",
          "source_uri_param": "file://processing_dispatch/happy-path-vertex-data.raw",
          "type": "buffer",
          "dimensions": { "width": 1 }
        },
        {
          "name": "textureData",
          "source_uri_param": "file://processing_dispatch/texturing-source-image.raw",
          "type": "buffer",
          "dimensions": { "width": 1 }
        }
      ],
      "outputs": [
        {
          "name": "renderOutput",
          "source_uri_param": "file://processing_dispatch/happy-path-render-output.raw",
          "type": "image"
        }
      ],
      "passes": [
        {
          "name": "renderPass",
          "type": "render",
          "render_shader": {
            "stages": [
              { "stage": "vertex", "type": "glsl", "source": "file://processing_dispatch/scalar_position_vertex_shader.glsl", "entry_point": "main" },
              { "stage": "fragment", "type": "glsl", "source": "file://processing_dispatch/solid_red_fragment_shader.glsl", "entry_point": "main" }
            ]
          },
          "render_target": {
            "color_format": "RGBA8",
            "depth_format": "D32_SFLOAT",
            "width": 8,
            "height": 8,
            "clear_color": [0.0, 0.0, 0.0, 1.0],
            "clear_depth": 1.0
          },
          "vertex_buffer": { "source": "input:renderInput" },
          "vertex_layout": [
            { "name": "inPosition", "format": "FLOAT32", "offset": 0 }
          ],
          "pipeline_state": {
            "topology": "point_list",
            "cull_mode": "none",
            "front_face": "ccw",
            "depth_test": "disabled"
          },
          "texture_buffer": { "source": "input:textureData", "width": 64, "height": 64 }
        }
      ]
    })";

        const std::string jsonRejectedOutputPrefix = R"({
      "name": "render-pass-texture-buffer-reject",
      "version": "1.0.0",
      "gnus_spec_version": 1.0,
      "inputs": [
        {
          "name": "renderInput",
          "source_uri_param": "file://processing_dispatch/happy-path-vertex-data.raw",
          "type": "buffer",
          "dimensions": { "width": 1 }
        },
        {
          "name": "textureData",
          "source_uri_param": "file://processing_dispatch/texturing-source-image.raw",
          "type": "buffer",
          "dimensions": { "width": 1 }
        }
      ],
      "outputs": [
        {
          "name": "renderOutput",
          "source_uri_param": "file://processing_dispatch/happy-path-render-output.raw",
          "type": "image"
        }
      ],
      "passes": [
        {
          "name": "renderPass",
          "type": "render",
          "render_shader": {
            "stages": [
              { "stage": "vertex", "type": "glsl", "source": "file://processing_dispatch/scalar_position_vertex_shader.glsl", "entry_point": "main" },
              { "stage": "fragment", "type": "glsl", "source": "file://processing_dispatch/solid_red_fragment_shader.glsl", "entry_point": "main" }
            ]
          },
          "render_target": {
            "color_format": "RGBA8",
            "depth_format": "D32_SFLOAT",
            "width": 8,
            "height": 8,
            "clear_color": [0.0, 0.0, 0.0, 1.0],
            "clear_depth": 1.0
          },
          "vertex_buffer": { "source": "input:renderInput" },
          "vertex_layout": [
            { "name": "inPosition", "format": "FLOAT32", "offset": 0 }
          ],
          "pipeline_state": {
            "topology": "point_list",
            "cull_mode": "none",
            "front_face": "ccw",
            "depth_test": "disabled"
          },
          "texture_buffer": { "source": "output:textureData", "width": 64, "height": 64 }
        }
      ]
    })";

        auto acceptedResult = sgns::sgprocessing::ProcessingManager::Create( jsonAcceptedInputPrefix );
        ASSERT_TRUE( acceptedResult.has_value() )
            << "An input:-prefixed texture_buffer.source must be accepted by "
               "CheckProcessValidity() at Create() time";

        auto rejectedResult = sgns::sgprocessing::ProcessingManager::Create( jsonRejectedOutputPrefix );
        ASSERT_FALSE( rejectedResult.has_value() )
            << "An output:-prefixed texture_buffer.source must be rejected fail-closed by "
               "CheckProcessValidity() at Create() time -- no cross-pass output: dependency "
               "graph exists for texture_buffer, mirroring vertex_buffer/index_buffer's gate";
    }
}
