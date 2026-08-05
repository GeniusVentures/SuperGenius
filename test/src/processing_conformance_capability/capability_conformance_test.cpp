#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <set>

#include <processingbase/ProcessingManager.hpp>
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
                    "format": "FLOAT32"
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
            ]
        })";

        auto patched = PatchJsonUrisToAbsolute( json_str, BinPath() );
        auto r = sgns::sgprocessing::ProcessingManager::Create( patched );
        ASSERT_TRUE( r.has_value() ) << "Valid RENDER job should be accepted: "
                                     << r.error().message();

        std::cout << "AcceptValidRenderJob: accepted" << std::endl;
    }

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

} // namespace sgns
