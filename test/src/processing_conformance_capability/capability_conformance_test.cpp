#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <set>

#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>
#include "testutil/processing_conformance_fixture.hpp"

namespace sgns
{
    class CapabilityConformanceTest : public ProcessorConformanceFixture
    {
    protected:
        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "fixtures/";
        }
    };

    TEST_F( CapabilityConformanceTest, AcceptValidInferenceJob )
    {
        // A valid INFERENCE job with MNN model should be accepted
        const std::string json_str = R"({
            "name": "cap-check-inference",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "testInput",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "float",
                    "dimensions": {
                        "width": 16,
                        "block_len": 16,
                        "chunk_stride": 16
                    },
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "testOutput",
                    "source_uri_param": "file://processing_datatypes/float_output.raw",
                    "type": "tensor"
                }
            ],
            "passes": [
                {
                    "name": "cap_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_datatypes/float_model.mnn",
                        "format": "MNN",
                        "batch_size": 1,
                        "input_nodes": [
                            {
                                "name": "input",
                                "type": "tensor",
                                "source": "input:testInput",
                                "shape": [1, 16]
                            }
                        ],
                        "output_nodes": [
                            {
                                "name": "output",
                                "type": "tensor",
                                "target": "output:testOutput",
                                "shape": [1, 16]
                            }
                        ]
                    }
                }
            ]
        })";

        auto patched = PatchJsonUrisToAbsolute( json_str, BinPath() );
        auto r = sgns::sgprocessing::ProcessingManager::Create( patched );
        ASSERT_TRUE( r.has_value() ) << "Valid INFERENCE job should be accepted: "
                                     << r.error().message();

        std::cout << "AcceptValidInferenceJob: accepted" << std::endl;
    }

    TEST_F( CapabilityConformanceTest, AcceptValidRenderJob )
    {
        // A valid RENDER job with SPIR-V shaders should be accepted
        const std::string json_str = R"({
            "name": "cap-check-render",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "vertexData",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "renderTarget",
                    "source_uri_param": "file://processing_datatypes/render_output.raw",
                    "type": "image"
                }
            ],
            "passes": [
                {
                    "name": "cap_render",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "spirv",
                                "source": "file://processing_conformance_regression/fixtures/passthrough.vert.spv",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "spirv",
                                "source": "file://processing_conformance_regression/fixtures/passthrough.frag.spv",
                                "entry_point": "main"
                            }
                        ]
                    },
                    "render_target": {
                        "color_format": "RGBA8",
                        "depth_format": "D32_SFLOAT",
                        "width": 64,
                        "height": 64,
                        "clear_color": [0.0, 0.0, 0.0, 1.0],
                        "clear_depth": 1.0
                    },
                    "vertex_buffer": { "source": "input:vertexData" },
                    "vertex_layout": [
                        { "name": "inPosition", "format": "FLOAT32", "offset": 0 }
                    ]
                }
            ]
        })";

        auto patched = PatchJsonUrisToAbsolute( json_str, BinPath() );
        auto r = sgns::sgprocessing::ProcessingManager::Create( patched );
        ASSERT_TRUE( r.has_value() ) << "Valid RENDER job should be accepted: "
                                     << r.error().message();

        std::cout << "AcceptValidRenderJob: accepted" << std::endl;
    }

    // NOTE: This case (and RejectUnsupportedPassType/RejectionReasonsAreDistinct below) tests
    // the pre-CanExecute() Create()/schema validation gate (correctly fixed by Plan 09-09's
    // Gap 2/4/5) -- a distinct, earlier defense layer from the CanExecute()/capability gate
    // exercised by AcceptValidRenderJobViaCanExecute/RejectUnsupportedVulkanFeature/
    // InferenceCanExecuteReflectsPassTypeRegistryGap below.
    TEST_F( CapabilityConformanceTest, RejectUnsupportedModelFormat )
    {
        // A job with an unsupported model format should be rejected
        const std::string json_str = R"({
            "name": "bad-format-job",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [],
            "outputs": [],
            "passes": [
                {
                    "name": "bad_format",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_datatypes/float_model.mnn",
                        "format": "ONNX",
                        "batch_size": 1,
                        "input_nodes": [],
                        "output_nodes": []
                    }
                }
            ]
        })";

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( r.has_value() ) << "ONNX model format should be rejected";

        std::string err = r.error().message();
        ASSERT_TRUE( err.find( "ONNX" ) != std::string::npos ||
                     err.find( "format" ) != std::string::npos ||
                     err.find( "Format" ) != std::string::npos )
            << "Error should mention unsupported format: " << err;

        std::cout << "RejectUnsupportedModelFormat: correctly rejected — " << err << std::endl;
    }

    // NOTE: This case tests the pre-CanExecute() Create()/schema validation gate -- a distinct,
    // earlier defense layer from the CanExecute()/capability gate (see NOTE above
    // RejectUnsupportedModelFormat).
    TEST_F( CapabilityConformanceTest, RejectUnsupportedPassType )
    {
        // A job with an unregistered pass type should be rejected
        const std::string json_str = R"({
            "name": "bad-passtype-job",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [],
            "outputs": [],
            "passes": [
                {
                    "name": "unsupported",
                    "type": "UNREGISTERED_CAP_TYPE"
                }
            ]
        })";

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( r.has_value() ) << "Unregistered pass type should be rejected";

        std::string err = r.error().message();
        ASSERT_TRUE( err.find( "UNREGISTERED_CAP_TYPE" ) != std::string::npos ||
                     err.find( "type" ) != std::string::npos ||
                     err.find( "Type" ) != std::string::npos )
            << "Error should name the unsupported type: " << err;

        std::cout << "RejectUnsupportedPassType: correctly rejected — " << err << std::endl;
    }

    // NOTE: This case tests the pre-CanExecute() Create()/schema validation gate -- a distinct,
    // earlier defense layer from the CanExecute()/capability gate (see NOTE above
    // RejectUnsupportedModelFormat).
    TEST_F( CapabilityConformanceTest, RejectionReasonsAreDistinct )
    {
        // Verify that rejection messages for different failure modes are distinct
        std::set<std::string> errors;

        // Bad model format
        {
            const std::string json_str = R"({
                "name": "distinct-1",
                "version": "1.0.0",
                "gnus_spec_version": 1.0,
                "inputs": [],
                "outputs": [],
                "passes": [{
                    "name": "bad",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_datatypes/float_model.mnn",
                        "format": "ONNX",
                        "batch_size": 1,
                        "input_nodes": [],
                        "output_nodes": []
                    }
                }]
            })";
            auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
            if ( !r.has_value() ) errors.insert( r.error().message() );
        }

        // Missing model
        {
            const std::string json_str = R"({
                "name": "distinct-2",
                "version": "1.0.0",
                "gnus_spec_version": 1.0,
                "inputs": [],
                "outputs": [],
                "passes": [{
                    "name": "no_model",
                    "type": "inference"
                }]
            })";
            auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
            if ( !r.has_value() ) errors.insert( r.error().message() );
        }

        // Unknown pass type
        {
            const std::string json_str = R"({
                "name": "distinct-3",
                "version": "1.0.0",
                "gnus_spec_version": 1.0,
                "inputs": [],
                "outputs": [],
                "passes": [{
                    "name": "bad_type",
                    "type": "DISTINCT_UNKNOWN_TYPE"
                }]
            })";
            auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
            if ( !r.has_value() ) errors.insert( r.error().message() );
        }

        // All rejection messages should be unique
        ASSERT_EQ( errors.size(), 3u )
            << "All 3 rejection categories should produce distinct messages. Got " << errors.size();

        std::cout << "RejectionReasonsAreDistinct: all " << errors.size()
                  << " rejection messages are unique" << std::endl;
    }

    TEST_F( CapabilityConformanceTest, RejectMissingExecutor )
    {
        // A pass type that exists but has no executor factory registered
        const std::string json_str = R"({
            "name": "missing-executor-job",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [],
            "outputs": [],
            "passes": [
                {
                    "name": "orphan",
                    "type": "inference"
                }
            ]
        })";

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        // Without a model, this should either fail or succeed at schema level
        // The test verifies the system doesn't crash
        if ( r.has_value() )
        {
            std::cout << "RejectMissingExecutor: accepted at schema level (no model required for schema check)"
                      << std::endl;
        }
        else
        {
            std::string err = r.error().message();
            ASSERT_TRUE( err.find( "executor" ) != std::string::npos ||
                         err.find( "model" ) != std::string::npos ||
                         err.find( "factory" ) != std::string::npos ||
                         err.find( "not found" ) != std::string::npos )
                << "Error should mention executor/factory gap: " << err;
            std::cout << "RejectMissingExecutor: correctly rejected — " << err << std::endl;
        }
    }

    // =======================================================================
    // CanExecute()-based capability conformance (TEST-08 gap closure) —
    // the three cases below call ProcessingManager::CanExecute() directly,
    // exercising the capability gate (ProcessingManager.cpp:1873, delegating
    // to CapabilityValidator::CanExecute) rather than the Create()/schema gate
    // exercised by the six cases above.
    // =======================================================================

    TEST_F( CapabilityConformanceTest, AcceptValidRenderJobViaCanExecute )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        // A valid RENDER job with SPIR-V shaders — identical to AcceptValidRenderJob's
        // JSON literal (64x64 render target, generous device limits on any real GPU).
        const std::string json_str = R"({
            "name": "cap-check-render",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "vertexData",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "renderTarget",
                    "source_uri_param": "file://processing_datatypes/render_output.raw",
                    "type": "image"
                }
            ],
            "passes": [
                {
                    "name": "cap_render",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "spirv",
                                "source": "file://processing_conformance_regression/fixtures/passthrough.vert.spv",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "spirv",
                                "source": "file://processing_conformance_regression/fixtures/passthrough.frag.spv",
                                "entry_point": "main"
                            }
                        ]
                    },
                    "render_target": {
                        "color_format": "RGBA8",
                        "depth_format": "D32_SFLOAT",
                        "width": 64,
                        "height": 64,
                        "clear_color": [0.0, 0.0, 0.0, 1.0],
                        "clear_depth": 1.0
                    },
                    "vertex_buffer": { "source": "input:vertexData" },
                    "vertex_layout": [
                        { "name": "inPosition", "format": "FLOAT32", "offset": 0 }
                    ]
                }
            ]
        })";

        auto patched = PatchJsonUrisToAbsolute( json_str, BinPath() );
        auto r = sgns::sgprocessing::ProcessingManager::Create( patched );
        ASSERT_TRUE( r.has_value() ) << "Valid RENDER job should be accepted at schema level: "
                                     << r.error().message();

        const auto &manager = r.value();
        auto        pass    = manager->GetProcessingData().get_passes()[0];

        bool                                       called = false;
        sgns::sgprocessing::CanExecuteResult       result;
        manager->CanExecute( pass,
                              [&]( sgns::sgprocessing::CanExecuteResult r2 )
                              {
                                  called = true;
                                  result = std::move( r2 );
                              } );

        ASSERT_TRUE( called ) << "CanExecute callback must be invoked synchronously";
        EXPECT_TRUE( result.executable )
            << "A 64x64 render target is executable on any real Vulkan-capable GPU with "
               "generous device limits";
        EXPECT_FALSE( result.executorId.empty() ) << "executorId must be populated when executable";
        EXPECT_TRUE( result.unmet.empty() ) << "No unmet requirements expected on acceptance";

        std::cout << "AcceptValidRenderJobViaCanExecute: CanExecute() accepted, executorId="
                  << result.executorId << std::endl;
    }

    TEST_F( CapabilityConformanceTest, RejectUnsupportedVulkanFeature )
    {
        // No HasUsableVulkanDevice() guard needed — this works identically with or without
        // a real GPU. Duplicate of AcceptValidRenderJob's JSON literal, but with
        // render_target width/height set to a value that exceeds any real Vulkan device's
        // maxImageDimension2D (and also exceeds the zero-valued default
        // CapabilitySnapshot.vulkanProps.limits used when no GPU is present).
        // CheckProcessValidity()'s RENDER case only checks presence of
        // render_shader/render_target/vertex_buffer/vertex_layout, never bounds-checks
        // width/height, so Create() still succeeds with this value.
        const std::string json_str = R"({
            "name": "cap-check-render-oversized",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "vertexData",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "renderTarget",
                    "source_uri_param": "file://processing_datatypes/render_output.raw",
                    "type": "image"
                }
            ],
            "passes": [
                {
                    "name": "cap_render_oversized",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "spirv",
                                "source": "file://processing_conformance_regression/fixtures/passthrough.vert.spv",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "spirv",
                                "source": "file://processing_conformance_regression/fixtures/passthrough.frag.spv",
                                "entry_point": "main"
                            }
                        ]
                    },
                    "render_target": {
                        "color_format": "RGBA8",
                        "depth_format": "D32_SFLOAT",
                        "width": 999999999,
                        "height": 999999999,
                        "clear_color": [0.0, 0.0, 0.0, 1.0],
                        "clear_depth": 1.0
                    },
                    "vertex_buffer": { "source": "input:vertexData" },
                    "vertex_layout": [
                        { "name": "inPosition", "format": "FLOAT32", "offset": 0 }
                    ]
                }
            ]
        })";

        auto patched = PatchJsonUrisToAbsolute( json_str, BinPath() );
        auto r = sgns::sgprocessing::ProcessingManager::Create( patched );
        ASSERT_TRUE( r.has_value() )
            << "Oversized render target must still pass Create()'s schema-level check "
               "(width/height bounds are only enforced by CanExecute()'s capability gate): "
            << r.error().message();

        const auto &manager = r.value();
        auto        pass    = manager->GetProcessingData().get_passes()[0];

        bool                                       called = false;
        sgns::sgprocessing::CanExecuteResult       result;
        manager->CanExecute( pass,
                              [&]( sgns::sgprocessing::CanExecuteResult r2 )
                              {
                                  called = true;
                                  result = std::move( r2 );
                              } );

        ASSERT_TRUE( called ) << "CanExecute callback must be invoked synchronously";
        ASSERT_FALSE( result.executable )
            << "999999999x999999999 render target must be rejected regardless of host hardware";

        bool foundVulkanDimensionUnmet = false;
        for ( const auto &u : result.unmet )
        {
            if ( u.category == sgns::sgprocessing::UnmetRequirementCategory::VULKAN &&
                 u.detail.find( "maxImageDimension2D" ) != std::string::npos )
            {
                foundVulkanDimensionUnmet = true;
                break;
            }
        }
        EXPECT_TRUE( foundVulkanDimensionUnmet )
            << "Expected a VULKAN-category unmet requirement mentioning maxImageDimension2D";

        std::cout << "RejectUnsupportedVulkanFeature: correctly rejected via CanExecute() — "
                   << result.unmet.size() << " unmet requirement(s)" << std::endl;
    }

    TEST_F( CapabilityConformanceTest, InferenceCanExecuteReflectsPassTypeRegistryGap )
    {
        // Build the manager from the exact AcceptValidInferenceJob JSON literal (a
        // schema-valid MNN inference pass).
        const std::string json_str = R"({
            "name": "cap-check-inference",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "testInput",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "float",
                    "dimensions": {
                        "width": 16,
                        "block_len": 16,
                        "chunk_stride": 16
                    },
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "testOutput",
                    "source_uri_param": "file://processing_datatypes/float_output.raw",
                    "type": "tensor"
                }
            ],
            "passes": [
                {
                    "name": "cap_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_datatypes/float_model.mnn",
                        "format": "MNN",
                        "batch_size": 1,
                        "input_nodes": [
                            {
                                "name": "input",
                                "type": "tensor",
                                "source": "input:testInput",
                                "shape": [1, 16]
                            }
                        ],
                        "output_nodes": [
                            {
                                "name": "output",
                                "type": "tensor",
                                "target": "output:testOutput",
                                "shape": [1, 16]
                            }
                        ]
                    }
                }
            ]
        })";

        auto patched = PatchJsonUrisToAbsolute( json_str, BinPath() );
        auto r = sgns::sgprocessing::ProcessingManager::Create( patched );
        ASSERT_TRUE( r.has_value() ) << "Valid INFERENCE job should be accepted at schema level: "
                                     << r.error().message();

        const auto &manager = r.value();
        auto        pass    = manager->GetProcessingData().get_passes()[0];

        bool                                       called = false;
        sgns::sgprocessing::CanExecuteResult       result;
        manager->CanExecute( pass,
                              [&]( sgns::sgprocessing::CanExecuteResult r2 )
                              {
                                  called = true;
                                  result = std::move( r2 );
                              } );

        ASSERT_TRUE( called ) << "CanExecute callback must be invoked synchronously";

        // WHY this is expected to be false today: ProcessingManager::Init()
        // (ProcessingManager.cpp line ~414) only ever calls
        // RegisterPassProcessorFactory( PassType::RENDER, ... ). INFERENCE passes dispatch
        // through the separate DataType-keyed m_processorFactories map and are never entered
        // into the PassType-keyed capability registry that CapabilityValidator::BuildSnapshot()
        // reads (CollectExecutorCapabilities() only iterates m_passFactories, which is
        // PassType-keyed and RENDER-only). This test honestly documents today's real, verified
        // behavior (INFERENCE passes are schema-valid but not yet capability-registered); it is
        // not a bug this task fixes — registering INFERENCE for capability purposes would be a
        // separate, larger cross-phase production change, out of this task's authorized scope.
        // Discovered follow-up item: consider registering INFERENCE (and RETRAIN) PassTypes into
        // the capability registry in a future phase so CanExecute() can meaningfully validate
        // MNN model-format/quantization requirements ahead of execution, not just RENDER passes.
        ASSERT_FALSE( result.executable )
            << "INFERENCE passes are not yet capability-registered (see comment above)";
        ASSERT_FALSE( result.unmet.empty() );
        EXPECT_EQ( result.unmet[0].category, sgns::sgprocessing::UnmetRequirementCategory::PASS_TYPE );
        EXPECT_TRUE( result.unmet[0].detail.find( "PassType" ) != std::string::npos ||
                     result.unmet[0].detail.find( "registered" ) != std::string::npos )
            << "Unmet detail should reference PassType registration: " << result.unmet[0].detail;

        std::cout << "InferenceCanExecuteReflectsPassTypeRegistryGap: CanExecute() correctly "
                     "reflects that INFERENCE is not capability-registered — "
                  << result.unmet[0].detail << std::endl;
    }

} // namespace sgns
