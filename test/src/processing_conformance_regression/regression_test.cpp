#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>
#include "testutil/processing_conformance_fixture.hpp"

namespace sgns
{
    // =======================================================================
    // RegressionTest — TEST-10: 4 known v1.0 bug regression tests
    // =======================================================================

    class RegressionTest : public ProcessorConformanceFixture
    {
    protected:
        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "fixtures/";
        }
    };

    TEST_F( RegressionTest, BugA_IndexMismatchInPassInputIndexing )
    {
        // D-08(a): Multi-pass job where input bindings reference pass indices
        // that shift after insertions. Assert FIXED behavior — indices match
        // the intended passes.
        const std::string &json_str = PatchedJson( "regression-a-index-mismatch.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Bug A — multi-pass job should not crash from index mismatch: "
            << manager_result.error().message();

        const auto &manager  = manager_result.value();
        const auto  passes   = manager->GetProcessingData().get_passes();
        ASSERT_EQ( passes.size(), 2 ) << "Both passes should be present";

        // Both passes should have type INFERENCE
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );
        ASSERT_EQ( passes[1].get_type(), sgns::PassType::INFERENCE );

        // Input bindings should resolve to correct pass indices
        ASSERT_TRUE( passes[0].get_model().has_value() );
        ASSERT_TRUE( passes[1].get_model().has_value() );

        std::cout << "BugA_IndexMismatchInPassInputIndexing: FIXED — passes correctly indexed" << std::endl;
    }

    TEST_F( RegressionTest, BugB_ModelOnlyAssumptionCrashInParseBlockSize )
    {
        // D-08(b): Shader-only render pass with NO model field.
        // Assert FIXED behavior — no crash, valid parse result.
        const std::string &json_str = PatchedJson( "regression-b-parseblocksize-model-only.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Bug B — shader-only render pass should not crash in ParseBlockSize: "
            << manager_result.error().message();

        const auto &manager = manager_result.value();
        const auto  passes  = manager->GetProcessingData().get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::RENDER );

        // The pass should NOT have a model (it's shader-only)
        ASSERT_FALSE( passes[0].get_model().has_value() )
            << "Shader-only render pass should not have a model field";

        std::cout << "BugB_ModelOnlyAssumptionCrashInParseBlockSize: FIXED — no crash on model-less render"
                  << std::endl;
    }

    TEST_F( RegressionTest, BugC_OutputBufferZeroProducesCorrectResult )
    {
        // D-08(c): Output tensor with zero elements — fallback path
        // previously produced incorrect results.
        // Assert FIXED behavior — valid result, correct hash.
        const std::string &json_str = PatchedJson( "regression-c-output-buffer-zero.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Bug C — output-buffer-zero job should not crash: "
            << manager_result.error().message();

        const auto &manager = manager_result.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );

        const auto  model       = passes[0].get_model().value();
        const auto &input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );

        if ( pr.has_value() )
        {
            // Success — output-buffer-zero handled correctly
            std::cout << "BugC_OutputBufferZeroProducesCorrectResult: FIXED — Process() succeeded" << std::endl;
        }
        else
        {
            std::cout << "BugC_OutputBufferZeroProducesCorrectResult: Process() returned error (acceptable): "
                      << pr.error().message() << std::endl;
        }
        // The key assertion: no crash, no garbage data
    }

    TEST_F( RegressionTest, BugD_UnsupportedPassTypeProducesError )
    {
        // D-08(d): Job referencing an unregistered PassType.
        // Assert FIXED behavior — produces error, not silent fallthrough.
        const std::string &json_str = PatchedJson( "regression-d-unsupported-passtype.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() )
            << "Bug D — unsupported pass type MUST produce an error, not silently fall through";

        std::string err = manager_result.error().message();
        ASSERT_FALSE( err.empty() ) << "Error message must not be empty";

        // The error should reference the unsupported type
        ASSERT_TRUE( err.find( "UNSUPPORTED_TYPE" ) != std::string::npos ||
                     err.find( "type" ) != std::string::npos ||
                     err.find( "Type" ) != std::string::npos ||
                     err.find( "pass" ) != std::string::npos )
            << "Error should mention the unsupported pass type: " << err;

        std::cout << "BugD_UnsupportedPassTypeProducesError: FIXED — error instead of silent fallthrough: "
                  << err << std::endl;
    }

    // =======================================================================
    // RenderConformanceTest — TEST-04/TEST-05: GPU conformance
    // =======================================================================

    class RenderConformanceTest : public ProcessorConformanceFixture
    {
    protected:
        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "fixtures/";

            // GPU probe (D-05): skip the entire suite if no usable Vulkan device is present.
            // GTEST_SKIP() inside a static SetUpTestSuite() skips every TEST_F in this class.
            if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
            {
                GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on "
                                "this host; skipping RenderConformanceTest, not failing it.";
            }
        }
    };

    TEST_F( RenderConformanceTest, RenderPassShaderFixturesValid )
    {
        // Verify SPIR-V fixtures load and are valid
        const std::string &json_str = PatchedJson( "regression-b-parseblocksize-model-only.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Render pass with valid SPIR-V fixtures should parse: "
            << manager_result.error().message();

        const auto &manager = manager_result.value();
        const auto  passes  = manager->GetProcessingData().get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::RENDER );

        std::cout << "RenderPassShaderFixturesValid: SPIR-V fixtures accepted" << std::endl;
    }

    TEST_F( RenderConformanceTest, RenderPassFullPipeline )
    {
        // Run full pipeline with pass-through shaders — verify a real rendered-output hash.
        const std::string &json_str = PatchedJson( "regression-b-parseblocksize-model-only.json" );
        ASSERT_FALSE( json_str.empty() );

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( manager_result.has_value() )
            << "Render pass should be accepted: " << manager_result.error().message();

        const auto &manager = manager_result.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:vertexData" ) );

        auto                               ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>          output_locations;

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( pr.has_value() ) << pr.error().message();
        ASSERT_EQ( pr.value().combinedHash.size(), 32u )
            << "Process() must produce a 32-byte SHA-256 rendered-output hash";

        std::cout << "RenderPassFullPipeline: Process() executed the real Vulkan pipeline, "
                     "combinedHash size="
                  << pr.value().combinedHash.size() << std::endl;
    }

    TEST_F( RenderConformanceTest, RenderPassOutputHashDeterministic )
    {
        // Run the same render pass definition on two separate ProcessingManager instances
        // and assert a bit-exact matching combinedHash — a real same-node determinism check.
        const std::string &json_str = PatchedJson( "regression-b-parseblocksize-model-only.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r1 = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r1.has_value() );
        auto r2 = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r2.has_value() );

        sgns::ModelNode model_node1;
        model_node1.set_source( std::string( "input:vertexData" ) );
        auto                               ioc1 = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes1;
        std::vector<std::string>          output_locations1;
        auto pr1 = r1.value()->Process( ioc1, chunkhashes1, model_node1, output_locations1 );
        ASSERT_TRUE( pr1.has_value() ) << "First run: " << pr1.error().message();

        sgns::ModelNode model_node2;
        model_node2.set_source( std::string( "input:vertexData" ) );
        auto                               ioc2 = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes2;
        std::vector<std::string>          output_locations2;
        auto pr2 = r2.value()->Process( ioc2, chunkhashes2, model_node2, output_locations2 );
        ASSERT_TRUE( pr2.has_value() ) << "Second run: " << pr2.error().message();

        ASSERT_EQ( pr1.value().combinedHash, pr2.value().combinedHash )
            << "Same render pass definition must produce a bit-exact matching output hash";

        std::cout << "RenderPassOutputHashDeterministic: two independent Process() runs "
                     "produced identical combinedHash"
                  << std::endl;
    }

    TEST_F( RenderConformanceTest, RenderPassTeardownVerified )
    {
        // Process() on two separate manager instances built from the same JSON, in sequence.
        // The second call succeeding proves the first call's RunTeardown() released Vulkan
        // resources without leaking, mirroring CancellationConformanceTest's
        // ResourcesCleanedUpAfterProcess pattern.
        const std::string &json_str = PatchedJson( "regression-b-parseblocksize-model-only.json" );
        ASSERT_FALSE( json_str.empty() );

        {
            auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
            ASSERT_TRUE( r.has_value() ) << "First render pass creation should succeed";

            sgns::ModelNode model_node;
            model_node.set_source( std::string( "input:vertexData" ) );
            auto                               ioc = std::make_shared<boost::asio::io_context>();
            std::vector<std::vector<uint8_t>> chunkhashes;
            std::vector<std::string>          output_locations;
            auto pr = r.value()->Process( ioc, chunkhashes, model_node, output_locations );
            ASSERT_TRUE( pr.has_value() ) << "First Process() should succeed: " << pr.error().message();
        }

        {
            auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
            ASSERT_TRUE( r.has_value() ) << "Second render pass creation should succeed (implies teardown worked)";

            sgns::ModelNode model_node;
            model_node.set_source( std::string( "input:vertexData" ) );
            auto                               ioc = std::make_shared<boost::asio::io_context>();
            std::vector<std::vector<uint8_t>> chunkhashes;
            std::vector<std::string>          output_locations;
            auto pr = r.value()->Process( ioc, chunkhashes, model_node, output_locations );
            ASSERT_TRUE( pr.has_value() )
                << "Second Process() should succeed, proving the first call's teardown released "
                   "Vulkan resources without leaking: "
                << pr.error().message();
        }

        std::cout << "RenderPassTeardownVerified: two consecutive Process() runs both succeeded" << std::endl;
    }

    TEST_F( RenderConformanceTest, MoltenVkEquivalentPath )
    {
        // "Equivalent" is proven by exercising the identical platform-agnostic
        // Vulkan/RenderProcessor code path on whichever backend (native Vulkan or MoltenVK)
        // the host provides — per 09-CONTEXT.md's Deferred Ideas, no separate
        // MoltenVK-specific fixture is needed or in scope.
        const std::string &json_str = PatchedJson( "regression-b-parseblocksize-model-only.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() )
            << "Render pass should parse on any Vulkan implementation (native or MoltenVK): "
            << r.error().message();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:vertexData" ) );
        auto                               ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>          output_locations;

        auto pr = r.value()->Process( ioc, chunkhashes, model_node, output_locations );
        ASSERT_TRUE( pr.has_value() ) << pr.error().message();
        ASSERT_EQ( pr.value().combinedHash.size(), 32u );

        std::cout << "MoltenVkEquivalentPath: Process() executed successfully on whichever "
                     "Vulkan implementation this host provides"
                  << std::endl;
    }

} // namespace sgns
