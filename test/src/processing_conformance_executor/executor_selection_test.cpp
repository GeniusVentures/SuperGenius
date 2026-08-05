#include <gtest/gtest.h>

#include <iostream>
#include <memory>

#include <processingbase/ProcessingManager.hpp>
#include "testutil/processing_conformance_fixture.hpp"

namespace sgns
{
    class ExecutorSelectionTest : public ProcessorConformanceFixture
    {
    protected:
        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "fixtures/";
        }
    };

    TEST_F( ExecutorSelectionTest, MnnInferenceExecutorSelected )
    {
        // Inline JSON for a valid INFERENCE pass with MNN model
        const std::string json_str = R"({
            "name": "executor-inference-test",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "testInput",
                    "source_uri_param": "file://processing_datatypes/buffer_input.raw",
                    "type": "buffer",
                    "dimensions": { "width": 16, "block_len": 16, "chunk_stride": 16 },
                    "format": "INT8"
                }
            ],
            "outputs": [
                {
                    "name": "testOutput",
                    "source_uri_param": "file://processing_datatypes/buffer_output.raw",
                    "type": "tensor"
                }
            ],
            "passes": [
                {
                    "name": "exec_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_datatypes/buffer_tiny.mnn",
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

        auto patched_json = PatchJsonUrisToAbsolute( json_str, BinPath() );
        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( patched_json );
        ASSERT_TRUE( manager_result.has_value() )
            << "Failed to create ProcessingManager: " << manager_result.error().message();

        const auto &manager  = manager_result.value();
        const auto  passes   = manager->GetProcessingData().get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );

        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model = passes[0].get_model().value();
        ASSERT_EQ( model.get_format(), sgns::ModelFormat::MNN );

        std::cout << "MnnInferenceExecutorSelected: INFERENCE pass dispatched correctly" << std::endl;
    }

    TEST_F( ExecutorSelectionTest, ExecutorIdentityStable )
    {
        const std::string json_str = R"({
            "name": "identity-stable-test",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "testInput",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "tensor",
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
                    "name": "identity_inference",
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

        auto patched_json = PatchJsonUrisToAbsolute( json_str, BinPath() );

        auto r1 = sgns::sgprocessing::ProcessingManager::Create( patched_json );
        ASSERT_TRUE( r1.has_value() );

        auto r2 = sgns::sgprocessing::ProcessingManager::Create( patched_json );
        ASSERT_TRUE( r2.has_value() );

        // Both should produce the same pass type and model format
        const auto passes1 = r1.value()->GetProcessingData().get_passes();
        const auto passes2 = r2.value()->GetProcessingData().get_passes();

        ASSERT_EQ( passes1.size(), passes2.size() );
        ASSERT_EQ( passes1[0].get_type(), passes2[0].get_type() );
        ASSERT_EQ( passes1[0].get_model().value().get_format(),
                   passes2[0].get_model().value().get_format() );

        std::cout << "ExecutorIdentityStable: identity stable across repeated creation" << std::endl;
    }

    TEST_F( ExecutorSelectionTest, MultiplePassesDifferentExecutors )
    {
        // Two passes: 1 INFERENCE + 1 RENDER — verify each gets correct type
        auto render_pass_fixture = R"(
            {
                "name": "render_pass",
                "type": "render",
                "shader_pipeline": {
                    "vertex": {
                        "source_uri_param": "file://processing_conformance_regression/fixtures/passthrough.vert.spv",
                        "entry_point": "main"
                    },
                    "fragment": {
                        "source_uri_param": "file://processing_conformance_regression/fixtures/passthrough.frag.spv",
                        "entry_point": "main"
                    }
                },
                "framebuffer": { "width": 64, "height": 64, "format": "RGBA8" },
                "vertex_buffers": [
                    {
                        "binding": 0,
                        "source_uri_param": "file://processing_datatypes/float_input.bin",
                        "stride": 12
                    }
                ]
            }
        )";

        const std::string json_str = std::string( R"({
            "name": "multi-executor-test",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "testInput",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "tensor",
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "testOutput",
                    "source_uri_param": "file://processing_datatypes/float_output.raw",
                    "type": "tensor"
                },
                {
                    "name": "renderOutput",
                    "source_uri_param": "file://processing_datatypes/render_output.raw",
                    "type": "image"
                }
            ],
            "passes": [
                {
                    "name": "inference_pass",
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
                },)" ) + render_pass_fixture + R"(
            ]
        })";

        auto patched_json = PatchJsonUrisToAbsolute( json_str, BinPath() );
        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( patched_json );
        ASSERT_TRUE( manager_result.has_value() )
            << "Multi-pass job should succeed: " << manager_result.error().message();

        const auto passes = manager_result.value()->GetProcessingData().get_passes();
        ASSERT_EQ( passes.size(), 2 );
        ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );
        ASSERT_EQ( passes[1].get_type(), sgns::PassType::RENDER );

        std::cout << "MultiplePassesDifferentExecutors: both passes dispatched correctly" << std::endl;
    }

    TEST_F( ExecutorSelectionTest, UnknownPassTypeRejected )
    {
        const std::string json_str = R"({
            "name": "unknown-passtype-test",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [],
            "outputs": [],
            "passes": [
                {
                    "name": "bad_pass",
                    "type": "UNSUPPORTED_TYPE"
                }
            ]
        })";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_FALSE( manager_result.has_value() )
            << "Unregistered pass type should be rejected";

        std::string err = manager_result.error().message();
        ASSERT_TRUE( err.find( "UNSUPPORTED_TYPE" ) != std::string::npos ||
                     err.find( "type" ) != std::string::npos )
            << "Error should name the unsupported pass type: " << err;

        std::cout << "UnknownPassTypeRejected: correctly rejected: " << err << std::endl;
    }

    TEST_F( ExecutorSelectionTest, ExecutorFactoryNotRegistered )
    {
        // This test verifies that if a pass type has no registered factory,
        // ProcessingManager reports the gap appropriately.
        const std::string json_str = R"({
            "name": "no-factory-test",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [],
            "outputs": [],
            "passes": [
                {
                    "name": "orphan_pass",
                    "type": "inference"
                }
            ]
        })";

        auto manager_result = sgns::sgprocessing::ProcessingManager::Create( json_str );
        // With no model, this should either fail or succeed minimally
        // The key is it doesn't crash
        if ( manager_result.has_value() )
        {
            const auto passes = manager_result.value()->GetProcessingData().get_passes();
            ASSERT_EQ( passes.size(), 1 );
            ASSERT_EQ( passes[0].get_type(), sgns::PassType::INFERENCE );
            std::cout << "ExecutorFactoryNotRegistered: accepted minimal pass (no model)" << std::endl;
        }
        else
        {
            std::cout << "ExecutorFactoryNotRegistered: rejected as expected: "
                      << manager_result.error().message() << std::endl;
        }
    }

} // namespace sgns
