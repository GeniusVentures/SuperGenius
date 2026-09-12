#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
#include <memory>

#include <artifacts/artifact_types.hpp>
#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>
#include "testutil/processing_conformance_fixture.hpp"

// SECV-01 (Phase 12 Plan 02): the "wrong-result-still-diverges" counter-test
// that research and 12-CONTEXT.md flag as this milestone's central,
// non-optional risk. Plan 12-01 replaced Phase 10's identity-stub
// quantization with real IEEE-754 canonicalization + fixed-point rounding
// tolerant enough to absorb genuine cross-machine floating-point noise
// (Phase 11's empirical deltas). This suite proves that tolerance is not
// ALSO loose enough to make a substituted/corrupted MNN model or a
// substituted render shader constant hash-indistinguishable from the
// correct result -- the other half of the proof Phase 13's empirical
// cross-machine match test alone cannot provide.
//
// Both assertions are a binary std::memcmp != 0 on
// output.artifacts[0].artifactId (ComputeArtifactIdentity's D-01-compliant
// content-addressable hash) with no secondary magnitude/divergence-threshold
// check (D-13) -- never on ExecutionManifest::manifestHash/combinedHash.

namespace sgns
{
    class Secv01CounterTest : public ProcessorConformanceFixture
    {
    };

    TEST_F( Secv01CounterTest, MnnCorruptedModelStillDiverges )
    {
        // Two float-processing-definition.json-shaped inline jobs, identical
        // in every field except which .mnn file the model parameter points
        // at: the real float_model.mnn (correct) vs Task 1's
        // byte-perturbed copy (corrupted, T-12-06). If Plan 12-01's real
        // quantization tolerance were loose enough to also mask a
        // substituted model, these two runs would produce the same
        // artifactId -- which must never happen.
        const std::string correctJson = R"({
            "name": "secv01-mnn-correct",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "inputFloat",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "float",
                    "dimensions": { "width": 512, "block_len": 64, "chunk_stride": 32 },
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "floatOutput",
                    "source_uri_param": "file://processing_datatypes/float_output.raw",
                    "type": "tensor"
                }
            ],
            "parameters": [
                {
                    "name": "modelUri",
                    "type": "uri",
                    "default": "file://processing_datatypes/float_model.mnn"
                }
            ],
            "passes": [
                {
                    "name": "float_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_datatypes/float_model.mnn",
                        "format": "MNN",
                        "batch_size": 1,
                        "input_nodes": [
                            { "name": "input", "type": "tensor", "source": "input:inputFloat", "shape": [1, 64] }
                        ],
                        "output_nodes": [
                            { "name": "output", "type": "tensor", "target": "output:floatOutput", "shape": [1, 64] }
                        ]
                    }
                }
            ]
        })";

        const std::string corruptedJson = R"({
            "name": "secv01-mnn-corrupted",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "inputFloat",
                    "source_uri_param": "file://processing_datatypes/float_input.bin",
                    "type": "float",
                    "dimensions": { "width": 512, "block_len": 64, "chunk_stride": 32 },
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "floatOutput",
                    "source_uri_param": "file://processing_datatypes/float_output.raw",
                    "type": "tensor"
                }
            ],
            "parameters": [
                {
                    "name": "modelUri",
                    "type": "uri",
                    "default": "file://processing_conformance_security/fixtures/secv01-corrupted-float_model.mnn"
                }
            ],
            "passes": [
                {
                    "name": "float_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_conformance_security/fixtures/secv01-corrupted-float_model.mnn",
                        "format": "MNN",
                        "batch_size": 1,
                        "input_nodes": [
                            { "name": "input", "type": "tensor", "source": "input:inputFloat", "shape": [1, 64] }
                        ],
                        "output_nodes": [
                            { "name": "output", "type": "tensor", "target": "output:floatOutput", "shape": [1, 64] }
                        ]
                    }
                }
            ]
        })";

        auto patchedCorrect   = PatchJsonUrisToAbsolute( correctJson, BinPath() );
        auto patchedCorrupted = PatchJsonUrisToAbsolute( corruptedJson, BinPath() );

        auto rCorrect = sgns::sgprocessing::ProcessingManager::Create( patchedCorrect );
        ASSERT_TRUE( rCorrect.has_value() ) << "Correct MNN job should be accepted: "
                                             << rCorrect.error().message();
        auto rCorrupted = sgns::sgprocessing::ProcessingManager::Create( patchedCorrupted );
        ASSERT_TRUE( rCorrupted.has_value() ) << "Corrupted-model MNN job should still be accepted "
                                                  "(a structurally-valid-but-wrong model, not a "
                                                  "malformed job): "
                                               << rCorrupted.error().message();

        const auto &managerCorrect   = rCorrect.value();
        const auto &managerCorrupted = rCorrupted.value();

        auto        pCorrect       = managerCorrect->GetProcessingData();
        const auto &passesCorrect  = pCorrect.get_passes();
        ASSERT_EQ( passesCorrect.size(), 1 );
        ASSERT_TRUE( passesCorrect[0].get_model().has_value() );
        const auto modelCorrect      = passesCorrect[0].get_model().value();
        const auto inputNodesCorrect = modelCorrect.get_input_nodes();
        ASSERT_GE( inputNodesCorrect.size(), 1 );

        auto        pCorrupted      = managerCorrupted->GetProcessingData();
        const auto &passesCorrupted = pCorrupted.get_passes();
        ASSERT_EQ( passesCorrupted.size(), 1 );
        ASSERT_TRUE( passesCorrupted[0].get_model().has_value() );
        const auto modelCorrupted      = passesCorrupted[0].get_model().value();
        const auto inputNodesCorrupted = modelCorrupted.get_input_nodes();
        ASSERT_GE( inputNodesCorrupted.size(), 1 );

        auto                              iocCorrect = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrect;
        std::vector<std::string>         outputLocationsCorrect;
        sgns::ModelNode                   modelNodeCorrect = inputNodesCorrect[0];

        auto prCorrect =
            managerCorrect->Process( iocCorrect, chunkhashesCorrect, modelNodeCorrect, outputLocationsCorrect );
        ASSERT_TRUE( prCorrect.has_value() ) << "Correct MNN run: " << prCorrect.error().message();
        ASSERT_GE( prCorrect.value().artifacts.size(), 1u );

        auto                              iocCorrupted = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrupted;
        std::vector<std::string>         outputLocationsCorrupted;
        sgns::ModelNode                   modelNodeCorrupted = inputNodesCorrupted[0];

        auto prCorrupted = managerCorrupted->Process( iocCorrupted,
                                                        chunkhashesCorrupted,
                                                        modelNodeCorrupted,
                                                        outputLocationsCorrupted );
        ASSERT_TRUE( prCorrupted.has_value() ) << "Corrupted-model MNN run: " << prCorrupted.error().message();
        ASSERT_GE( prCorrupted.value().artifacts.size(), 1u );

        EXPECT_NE( std::memcmp( prCorrect.value().artifacts[0].artifactId,
                                 prCorrupted.value().artifacts[0].artifactId,
                                 sgns::sgprocessing::SHA256_HASH_SIZE ),
                   0 )
            << "A deliberately corrupted MNN model must produce a different "
               "post-quantization artifactId than the correct model -- SECV-01/T-12-06";

        std::cout << "MnnCorruptedModelStillDiverges: correct and corrupted-model artifactId "
                     "hashes differ as required"
                  << std::endl;
    }

    TEST_F( Secv01CounterTest, RenderWrongShaderConstantStillDiverges )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        // Two render jobs shaped exactly like cancellation_conformance_test.cpp's
        // RenderCancelBeforeStartProducesNoSuccessfulResult job, identical except
        // for the fragment stage: the correct precompiled passthrough.frag.spv
        // (solid white) vs Task 1's secv01-wrong-color.frag GLSL source
        // (solid mid-gray, compiled+validated at runtime by ShaderCompiler,
        // T-12-07). If the real quantization tolerance were loose enough to
        // also mask a substituted shader constant, these two runs would
        // produce the same artifactId -- which must never happen.
        const std::string correctJson = R"({
            "name": "secv01-render-correct",
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
                    "name": "secv01_render",
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
                    ],
                    "pipeline_state": {
                        "topology": "point_list",
                        "cull_mode": "none",
                        "front_face": "ccw",
                        "depth_test": "disabled"
                    }
                }
            ]
        })";

        const std::string wrongColorJson = R"({
            "name": "secv01-render-wrong-color",
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
                    "name": "secv01_render",
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
                                "type": "glsl",
                                "source": "file://processing_conformance_security/fixtures/secv01-wrong-color.frag",
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
                    ],
                    "pipeline_state": {
                        "topology": "point_list",
                        "cull_mode": "none",
                        "front_face": "ccw",
                        "depth_test": "disabled"
                    }
                }
            ]
        })";

        auto patchedCorrect    = PatchJsonUrisToAbsolute( correctJson, BinPath() );
        auto patchedWrongColor = PatchJsonUrisToAbsolute( wrongColorJson, BinPath() );

        auto rCorrect = sgns::sgprocessing::ProcessingManager::Create( patchedCorrect );
        ASSERT_TRUE( rCorrect.has_value() ) << "Correct render job should be accepted: "
                                             << rCorrect.error().message();
        auto rWrongColor = sgns::sgprocessing::ProcessingManager::Create( patchedWrongColor );
        ASSERT_TRUE( rWrongColor.has_value() ) << "Wrong-color render job should still be accepted "
                                                   "(a structurally-valid-but-wrong shader, not a "
                                                   "malformed job): "
                                                << rWrongColor.error().message();

        const auto &managerCorrect    = rCorrect.value();
        const auto &managerWrongColor = rWrongColor.value();

        sgns::ModelNode modelNodeCorrect;
        modelNodeCorrect.set_source( std::string( "input:vertexData" ) );

        sgns::ModelNode modelNodeWrongColor;
        modelNodeWrongColor.set_source( std::string( "input:vertexData" ) );

        auto                              iocCorrect = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrect;
        std::vector<std::string>         outputLocationsCorrect;

        auto prCorrect =
            managerCorrect->Process( iocCorrect, chunkhashesCorrect, modelNodeCorrect, outputLocationsCorrect );
        ASSERT_TRUE( prCorrect.has_value() ) << "Correct render run: " << prCorrect.error().message();
        ASSERT_GE( prCorrect.value().artifacts.size(), 1u );

        auto                              iocWrongColor = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesWrongColor;
        std::vector<std::string>         outputLocationsWrongColor;

        auto prWrongColor = managerWrongColor->Process( iocWrongColor,
                                                          chunkhashesWrongColor,
                                                          modelNodeWrongColor,
                                                          outputLocationsWrongColor );
        ASSERT_TRUE( prWrongColor.has_value() ) << "Wrong-color render run: " << prWrongColor.error().message();
        ASSERT_GE( prWrongColor.value().artifacts.size(), 1u );

        EXPECT_NE( std::memcmp( prCorrect.value().artifacts[0].artifactId,
                                 prWrongColor.value().artifacts[0].artifactId,
                                 sgns::sgprocessing::SHA256_HASH_SIZE ),
                   0 )
            << "A deliberately wrong render shader constant must produce a different "
               "post-quantization artifactId than the correct shader -- SECV-01/T-12-07";

        std::cout << "RenderWrongShaderConstantStillDiverges: correct and wrong-color artifactId "
                     "hashes differ as required"
                  << std::endl;
    }
} // namespace sgns
