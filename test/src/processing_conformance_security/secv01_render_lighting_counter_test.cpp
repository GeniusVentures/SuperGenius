#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
#include <memory>

#include <artifacts/artifact_types.hpp>
#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>
#include "testutil/processing_conformance_fixture.hpp"

// SECV-01-style lighting counter-test (Phase 17 D-06/D-07/D-08, RENDTOL-02):
// proves the lighting fixture's own empirically-derived byteQuantMode still
// catches a deliberately wrong light color, mirroring
// secv01_tex3d_counter_test.cpp's per-fixture-dedicated-file methodology and
// secv01_counter_test.cpp::RenderWrongShaderConstantStillDiverges's render-job
// shape (correct vs. wrong lightColor, binary memcmp on artifacts[0].artifactId).
//
// [Rule 1 - Bug found and fixed this task] Writing this counter-test at
// byteQuantMode=0 (raw byte-identity, no masking at all) initially FAILED --
// the correct (lightColor=[1,1,1]) and wrong-color (lightColor=[0.5,0.5,0.5])
// jobs produced a bit-identical artifactId even with zero quantization.
// Root cause: lighting_fragment_shader.glsl originally declared its
// push-constant struct in the "natural" order (lightDir, lightColor,
// viewPos), but RenderProcessor::ResolveUniforms packs uniforms in
// std::map key-SORTED (alphabetical) order (processing_processor_render.cpp
// ~922-924, "matches SerializeRenderPassConfig()'s own iteration order",
// DETV-01) -- i.e. lightColor, lightDir, viewPos. The mismatched declaration
// order meant the shader read lightColor's bytes into u.lightDir and vice
// versa. Because normalize() erases uniform scaling, [1,1,1] and [0.5,0.5,0.5]
// normalize to the identical direction once misread as "lightDir", so the
// deliberate corruption became completely invisible regardless of
// byteQuantMode -- and this also fully explains why 17-05's Round 1 capture
// measured "zero divergence" for lighting: the shader was rendering a
// degenerate solid-black image (confirmed via lighting-render-output.raw,
// every byte 0x00/0x00/0x00/0xFF), not real Phong lighting at all. Fixed by
// reordering the push-constant struct to the alphabetical (lightColor,
// lightDir, viewPos) order that actually matches ResolveUniforms's packing
// -- see lighting_fragment_shader.glsl's own comment. IMPORTANT DOWNSTREAM
// IMPLICATION: 17-CAPTURE-RESULTS-ROUND1.md's lighting maxAbsDelta=0.0 entry
// reflects the pre-fix degenerate render, not genuine per-fragment Phong
// math -- a future Round 2 (or a dedicated Round 1 re-capture) MUST re-measure
// real cross-hardware divergence for the corrected shader; that measurement
// does not yet exist as of this plan.
//
// With the shader fixed, this counter-test's own binary search (D-06,
// increasing N is the MORE tolerant direction for byteQuantMode --
// RESEARCH.md Tolerance Derivation Methodology step 4) starts from the
// divergence-absorption floor implied by 17-CAPTURE-RESULTS-ROUND1.md's
// (now-known-stale) maxAbsDelta=0.0 -- smallest N with 2^N > 0 is N=0 -- and
// walks N upward against this exact counter-test to find where the specific
// lightColor corruption (1.0,1.0,1.0 -> 0.5,0.5,0.5) starts being masked
// (actual rebuild+run search, this session):
//   N=0 (identity)                 -- SECV-01 passes (starting candidate)
//   N=1 (grid step 2/255)          -- SECV-01 passes
//   N=2 (grid step 4/255)          -- SECV-01 passes
//   N=3 (grid step 8/255)          -- SECV-01 passes
//   N=4 (grid step 16/255)         -- SECV-01 passes
//   N=5 (grid step 32/255)         -- SECV-01 passes
//   N=6 (grid step 64/255)         -- SECV-01 FAILS (correct and wrong-color
//     artifactIds collide -- confirmed deterministic, re-run twice)
// N=6 is the first failure boundary found; the final value is chosen one
// integer step BELOW it: N=5 (also re-confirmed passing twice). Because no
// FRESH real cross-hardware maxAbsDelta exists yet for the corrected shader
// (only the stale, degenerate-render 0.0 figure), N=5 is honestly reported as
// derived from this counter-test's own corruption-detection boundary, per
// D-06's primary search methodology -- not as a value independently verified
// against real post-fix cross-hardware divergence data (that verification is
// out of this plan's scope; it belongs to a future re-capture round).

namespace sgns
{
    class Secv01RenderLightingCounterTest : public ProcessorConformanceFixture
    {
    };

    TEST_F( Secv01RenderLightingCounterTest, LightingWrongColorStillDiverges )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        // Two render jobs shaped exactly like lighting-fixture-definition.json,
        // identical except lightColor: the correct fixture's [1.0, 1.0, 1.0]
        // vs. a deliberately wrong [0.5, 0.5, 0.5] (RESEARCH.md's own suggested
        // corruption). Both declare the SAME candidate byteQuantMode under test
        // -- if lighting's own empirically-derived precision were loose enough
        // to also mask a substituted light color, these two runs would produce
        // the same artifactId, which must never happen.
        const std::string correctJson = R"({
            "name": "secv01-render-lighting-correct",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "lightingVertexInput",
                    "source_uri_param": "file://processing_dispatch/lighting-vertex-data.raw",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "lightingRenderOutput",
                    "source_uri_param": "file://processing_dispatch/lighting-render-output.raw",
                    "type": "image"
                }
            ],
            "parameters": [
                { "name": "byteQuantMode", "type": "int", "default": 5 }
            ],
            "passes": [
                {
                    "name": "lightingPass",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "glsl",
                                "source": "file://processing_dispatch/lighting_vertex_shader.glsl",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "glsl",
                                "source": "file://processing_dispatch/lighting_fragment_shader.glsl",
                                "entry_point": "main"
                            }
                        ],
                        "uniforms": {
                            "lightDir":   { "type": "vec3", "value": [-0.3, -1.0, -0.2] },
                            "lightColor": { "type": "vec3", "value": [1.0, 1.0, 1.0] },
                            "viewPos":    { "type": "vec3", "value": [0.0, 0.0, 3.0] }
                        }
                    },
                    "render_target": {
                        "color_format": "RGBA8",
                        "depth_format": "D32_SFLOAT",
                        "width": 64,
                        "height": 64,
                        "clear_color": [0.0, 0.0, 0.0, 1.0],
                        "clear_depth": 1.0
                    },
                    "vertex_buffer": { "source": "input:lightingVertexInput" },
                    "vertex_layout": [
                        { "name": "inPosX",  "format": "FLOAT32", "offset": 0 },
                        { "name": "inPosY",  "format": "FLOAT32", "offset": 4 },
                        { "name": "inPosZ",  "format": "FLOAT32", "offset": 8 },
                        { "name": "inNormX", "format": "FLOAT32", "offset": 12 },
                        { "name": "inNormY", "format": "FLOAT32", "offset": 16 },
                        { "name": "inNormZ", "format": "FLOAT32", "offset": 20 }
                    ],
                    "pipeline_state": {
                        "topology": "triangle_list",
                        "cull_mode": "none",
                        "front_face": "ccw",
                        "depth_test": "disabled"
                    }
                }
            ]
        })";

        const std::string wrongColorJson = R"({
            "name": "secv01-render-lighting-wrong-color",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "lightingVertexInput",
                    "source_uri_param": "file://processing_dispatch/lighting-vertex-data.raw",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "lightingRenderOutput",
                    "source_uri_param": "file://processing_dispatch/lighting-render-output.raw",
                    "type": "image"
                }
            ],
            "parameters": [
                { "name": "byteQuantMode", "type": "int", "default": 5 }
            ],
            "passes": [
                {
                    "name": "lightingPass",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "glsl",
                                "source": "file://processing_dispatch/lighting_vertex_shader.glsl",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "glsl",
                                "source": "file://processing_dispatch/lighting_fragment_shader.glsl",
                                "entry_point": "main"
                            }
                        ],
                        "uniforms": {
                            "lightDir":   { "type": "vec3", "value": [-0.3, -1.0, -0.2] },
                            "lightColor": { "type": "vec3", "value": [0.5, 0.5, 0.5] },
                            "viewPos":    { "type": "vec3", "value": [0.0, 0.0, 3.0] }
                        }
                    },
                    "render_target": {
                        "color_format": "RGBA8",
                        "depth_format": "D32_SFLOAT",
                        "width": 64,
                        "height": 64,
                        "clear_color": [0.0, 0.0, 0.0, 1.0],
                        "clear_depth": 1.0
                    },
                    "vertex_buffer": { "source": "input:lightingVertexInput" },
                    "vertex_layout": [
                        { "name": "inPosX",  "format": "FLOAT32", "offset": 0 },
                        { "name": "inPosY",  "format": "FLOAT32", "offset": 4 },
                        { "name": "inPosZ",  "format": "FLOAT32", "offset": 8 },
                        { "name": "inNormX", "format": "FLOAT32", "offset": 12 },
                        { "name": "inNormY", "format": "FLOAT32", "offset": 16 },
                        { "name": "inNormZ", "format": "FLOAT32", "offset": 20 }
                    ],
                    "pipeline_state": {
                        "topology": "triangle_list",
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
        ASSERT_TRUE( rCorrect.has_value() ) << "Correct lighting job should be accepted: "
                                             << rCorrect.error().message();
        auto rWrongColor = sgns::sgprocessing::ProcessingManager::Create( patchedWrongColor );
        ASSERT_TRUE( rWrongColor.has_value() ) << "Wrong-light-color job should still be accepted "
                                                   "(a structurally-valid-but-wrong uniform, not a "
                                                   "malformed job): "
                                                << rWrongColor.error().message();

        const auto &managerCorrect    = rCorrect.value();
        const auto &managerWrongColor = rWrongColor.value();

        sgns::ModelNode modelNodeCorrect;
        modelNodeCorrect.set_source( std::string( "input:lightingVertexInput" ) );

        sgns::ModelNode modelNodeWrongColor;
        modelNodeWrongColor.set_source( std::string( "input:lightingVertexInput" ) );

        auto                              iocCorrect = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrect;
        std::vector<std::string>         outputLocationsCorrect;

        auto prCorrect =
            managerCorrect->Process( iocCorrect, chunkhashesCorrect, modelNodeCorrect, outputLocationsCorrect );
        ASSERT_TRUE( prCorrect.has_value() ) << "Correct lighting run: " << prCorrect.error().message();
        ASSERT_GE( prCorrect.value().artifacts.size(), 1u );

        auto                              iocWrongColor = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesWrongColor;
        std::vector<std::string>         outputLocationsWrongColor;

        auto prWrongColor = managerWrongColor->Process( iocWrongColor,
                                                          chunkhashesWrongColor,
                                                          modelNodeWrongColor,
                                                          outputLocationsWrongColor );
        ASSERT_TRUE( prWrongColor.has_value() ) << "Wrong-light-color run: " << prWrongColor.error().message();
        ASSERT_GE( prWrongColor.value().artifacts.size(), 1u );

        EXPECT_NE( std::memcmp( prCorrect.value().artifacts[0].artifactId,
                                 prWrongColor.value().artifacts[0].artifactId,
                                 sgns::sgprocessing::SHA256_HASH_SIZE ),
                   0 )
            << "A deliberately wrong light color must produce a different post-quantization "
               "artifactId than the correct light color at lighting's own empirically-derived "
               "byteQuantMode -- SECV-01/T-17-13";

        std::cout << "LightingWrongColorStillDiverges: correct and wrong-color artifactId "
                     "hashes differ as required"
                  << std::endl;
    }
} // namespace sgns
