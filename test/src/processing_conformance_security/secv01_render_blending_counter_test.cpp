#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
#include <memory>

#include <artifacts/artifact_types.hpp>
#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>
#include "testutil/processing_conformance_fixture.hpp"

// SECV-01-style blending counter-test (Phase 17 D-06/D-07/D-08, RENDTOL-02):
// proves the blending fixture's own empirically-derived byteQuantMode still
// catches a deliberately wrong blend factor pair, mirroring
// secv01_tex3d_counter_test.cpp's per-fixture-dedicated-file methodology and
// secv01_counter_test.cpp::RenderWrongShaderConstantStillDiverges's render-job
// shape (correct vs. wrong blend_src_factor/blend_dst_factor, binary memcmp on
// artifacts[0].artifactId).
//
// 17-CAPTURE-RESULTS-ROUND1.md's real captured data for this fixture:
// maxAbsDelta = 1.0, maxUlpDistance = 1, contentHashMatch = false -- blending
// is the one fixture that measured REAL cross-hardware divergence on the
// Mac-vs-Windows dataset. Per D-08's divergence-absorption floor, the smallest
// N with 2^N > 1.0 is N=1 (grid step 2/255) -- the starting candidate below.
//
// Binary search of N upward from that floor against this exact counter-test
// (D-06 methodology, increasing N is the MORE tolerant direction for
// byteQuantMode -- RESEARCH.md Tolerance Derivation Methodology step 4), to
// find where the blend-factor corruption (src_alpha/one_minus_src_alpha ->
// one/zero, which disables the destination clear color's contribution
// entirely) starts being masked (actual rebuild+run search, this session):
//   N=1 (grid step 2/255)   -- SECV-01 passes (starting candidate, real
//     divergence-absorption floor)
//   N=2 (grid step 4/255)   -- SECV-01 passes
//   N=3 (grid step 8/255)   -- SECV-01 passes
//   N=4 (grid step 16/255)  -- SECV-01 passes
//   N=5 (grid step 32/255)  -- SECV-01 passes
//   N=6 (grid step 64/255)  -- SECV-01 passes
//   N=7 (grid step 128/255) -- SECV-01 FAILS (correct and wrong-blend-factor
//     artifactIds collide -- confirmed deterministic, re-run twice)
// N=7 is the first failure boundary found; the final value is chosen one
// integer step BELOW it: N=6 (also re-confirmed passing twice). N=6's grid
// step (64/255, ~25% of the uint8 range) comfortably exceeds the real
// captured maxAbsDelta=1.0 (a ~64x margin), while remaining empirically
// confirmed to still catch this fixture's own deliberate blend-factor
// substitution.
namespace sgns
{
    class Secv01RenderBlendingCounterTest : public ProcessorConformanceFixture
    {
    };

    TEST_F( Secv01RenderBlendingCounterTest, BlendingWrongFactorStillDiverges )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        // Two render jobs shaped exactly like blending-fixture-definition.json,
        // identical except pipeline_state's blend_src_factor/blend_dst_factor:
        // the correct fixture's src_alpha/one_minus_src_alpha (standard "over"
        // alpha compositing) vs. a deliberately wrong one/zero (disables the
        // destination clear color's contribution entirely -- RESEARCH.md's own
        // suggested corruption). Both declare the SAME candidate byteQuantMode
        // under test -- if blending's own empirically-derived precision were
        // loose enough to also mask a substituted blend factor, these two runs
        // would produce the same artifactId, which must never happen.
        const std::string correctJson = R"({
            "name": "secv01-render-blending-correct",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "blendingVertexInput",
                    "source_uri_param": "file://processing_dispatch/blending-vertex-data.raw",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "blendingRenderOutput",
                    "source_uri_param": "file://processing_dispatch/blending-render-output.raw",
                    "type": "image"
                }
            ],
            "parameters": [
                { "name": "byteQuantMode", "type": "int", "default": 6 }
            ],
            "passes": [
                {
                    "name": "blendingPass",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "glsl",
                                "source": "file://processing_dispatch/blending_vertex_shader.glsl",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "glsl",
                                "source": "file://processing_dispatch/blending_fragment_shader.glsl",
                                "entry_point": "main"
                            }
                        ]
                    },
                    "render_target": {
                        "color_format": "RGBA8",
                        "depth_format": "D32_SFLOAT",
                        "width": 64,
                        "height": 64,
                        "clear_color": [0.2, 0.4, 0.6, 1.0],
                        "clear_depth": 1.0
                    },
                    "vertex_buffer": { "source": "input:blendingVertexInput" },
                    "vertex_layout": [
                        { "name": "inPosX", "format": "FLOAT32", "offset": 0 },
                        { "name": "inPosY", "format": "FLOAT32", "offset": 4 }
                    ],
                    "pipeline_state": {
                        "topology": "triangle_list",
                        "cull_mode": "none",
                        "front_face": "ccw",
                        "depth_test": "disabled",
                        "blend_enable": true,
                        "blend_src_factor": "src_alpha",
                        "blend_dst_factor": "one_minus_src_alpha"
                    }
                }
            ]
        })";

        const std::string wrongFactorJson = R"({
            "name": "secv01-render-blending-wrong-factor",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "blendingVertexInput",
                    "source_uri_param": "file://processing_dispatch/blending-vertex-data.raw",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "blendingRenderOutput",
                    "source_uri_param": "file://processing_dispatch/blending-render-output.raw",
                    "type": "image"
                }
            ],
            "parameters": [
                { "name": "byteQuantMode", "type": "int", "default": 6 }
            ],
            "passes": [
                {
                    "name": "blendingPass",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "glsl",
                                "source": "file://processing_dispatch/blending_vertex_shader.glsl",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "glsl",
                                "source": "file://processing_dispatch/blending_fragment_shader.glsl",
                                "entry_point": "main"
                            }
                        ]
                    },
                    "render_target": {
                        "color_format": "RGBA8",
                        "depth_format": "D32_SFLOAT",
                        "width": 64,
                        "height": 64,
                        "clear_color": [0.2, 0.4, 0.6, 1.0],
                        "clear_depth": 1.0
                    },
                    "vertex_buffer": { "source": "input:blendingVertexInput" },
                    "vertex_layout": [
                        { "name": "inPosX", "format": "FLOAT32", "offset": 0 },
                        { "name": "inPosY", "format": "FLOAT32", "offset": 4 }
                    ],
                    "pipeline_state": {
                        "topology": "triangle_list",
                        "cull_mode": "none",
                        "front_face": "ccw",
                        "depth_test": "disabled",
                        "blend_enable": true,
                        "blend_src_factor": "one",
                        "blend_dst_factor": "zero"
                    }
                }
            ]
        })";

        auto patchedCorrect     = PatchJsonUrisToAbsolute( correctJson, BinPath() );
        auto patchedWrongFactor = PatchJsonUrisToAbsolute( wrongFactorJson, BinPath() );

        auto rCorrect = sgns::sgprocessing::ProcessingManager::Create( patchedCorrect );
        ASSERT_TRUE( rCorrect.has_value() ) << "Correct blending job should be accepted: "
                                             << rCorrect.error().message();
        auto rWrongFactor = sgns::sgprocessing::ProcessingManager::Create( patchedWrongFactor );
        ASSERT_TRUE( rWrongFactor.has_value() ) << "Wrong-blend-factor job should still be accepted "
                                                    "(a structurally-valid-but-wrong pipeline_state, "
                                                    "not a malformed job): "
                                                 << rWrongFactor.error().message();

        const auto &managerCorrect     = rCorrect.value();
        const auto &managerWrongFactor = rWrongFactor.value();

        sgns::ModelNode modelNodeCorrect;
        modelNodeCorrect.set_source( std::string( "input:blendingVertexInput" ) );

        sgns::ModelNode modelNodeWrongFactor;
        modelNodeWrongFactor.set_source( std::string( "input:blendingVertexInput" ) );

        auto                              iocCorrect = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrect;
        std::vector<std::string>         outputLocationsCorrect;

        auto prCorrect =
            managerCorrect->Process( iocCorrect, chunkhashesCorrect, modelNodeCorrect, outputLocationsCorrect );
        ASSERT_TRUE( prCorrect.has_value() ) << "Correct blending run: " << prCorrect.error().message();
        ASSERT_GE( prCorrect.value().artifacts.size(), 1u );

        auto                              iocWrongFactor = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesWrongFactor;
        std::vector<std::string>         outputLocationsWrongFactor;

        auto prWrongFactor = managerWrongFactor->Process( iocWrongFactor,
                                                            chunkhashesWrongFactor,
                                                            modelNodeWrongFactor,
                                                            outputLocationsWrongFactor );
        ASSERT_TRUE( prWrongFactor.has_value() ) << "Wrong-blend-factor run: " << prWrongFactor.error().message();
        ASSERT_GE( prWrongFactor.value().artifacts.size(), 1u );

        EXPECT_NE( std::memcmp( prCorrect.value().artifacts[0].artifactId,
                                 prWrongFactor.value().artifacts[0].artifactId,
                                 sgns::sgprocessing::SHA256_HASH_SIZE ),
                   0 )
            << "A deliberately wrong blend factor must produce a different post-quantization "
               "artifactId than the correct blend factor at blending's own empirically-derived "
               "byteQuantMode -- SECV-01/T-17-13";

        std::cout << "BlendingWrongFactorStillDiverges: correct and wrong-factor artifactId "
                     "hashes differ as required"
                  << std::endl;
    }
} // namespace sgns
