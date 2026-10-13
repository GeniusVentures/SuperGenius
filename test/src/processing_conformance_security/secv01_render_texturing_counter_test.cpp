#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
#include <memory>

#include <artifacts/artifact_types.hpp>
#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>
#include "testutil/processing_conformance_fixture.hpp"

// SECV-01-style texturing counter-test (Phase 17 D-06/D-07/D-08, RENDTOL-02):
// proves the texturing fixture's own empirically-derived byteQuantMode still
// catches a deliberately corrupted source texture image, mirroring
// secv01_tex3d_counter_test.cpp's byte-perturbed-input-corruption precedent
// (there, a byte-perturbed .mnn model; here, a fully bit-inverted RGBA8
// texture -- the closest structural analog for a sampled-image render input).
//
// 17-CAPTURE-RESULTS-ROUND1.md's real captured data for this fixture:
// maxAbsDelta = 0.0, maxUlpDistance = 0, contentHashMatch = true -- texturing
// measured ZERO cross-hardware divergence on the Mac-vs-Windows dataset, same
// as the original trivial happy-path fixture. That document's own caveat
// (also 17-04-SUMMARY.md's finding) explains why: UploadTexture() always uses
// VK_FILTER_NEAREST regardless of texture_buffer.filter's declared value (the
// wire format never threads a per-texture filter mode through
// SerializeRenderPassConfig/ParseRenderPassConfig end-to-end) -- nearest-
// neighbor sampling is an exact, bit-reproducible texel lookup with no
// interpolation math to diverge on. This is reported honestly, not
// reinterpreted: unlike 17-06's lighting fixture (whose "zero divergence"
// turned out to be a degenerate solid-black render hiding a real push-constant
// bug), texturing's zero-divergence result has a concrete, already-diagnosed
// explanation (NEAREST filtering) and is not caused by a similar defect --
// confirmed below: this counter-test's own corruption (a fully bit-inverted
// source image) DOES produce a different sampled color per-pixel, proving the
// texture-upload/sampling path itself is not degenerate the way lighting's was.
//
// Per D-08's divergence-absorption floor, since the real measured maxAbsDelta
// is 0.0, no widening is required a priori -- the floor is simply N=0 (byte-
// identity, no masking at all). The binary search below still runs the full
// D-06 discipline (increasing N is the MORE tolerant direction for
// byteQuantMode) starting from that floor, to confirm N=0 does not
// (trivially) fail to catch this fixture's own deliberate corruption, and to
// find how much margin actually exists before the corruption gets masked
// (actual rebuild+run search, this session):
//   N=0 (identity)           -- SECV-01 passes (starting candidate, real
//     divergence-absorption floor -- Round 1's own measured zero delta)
//   N=1 (grid step 2/255)   -- SECV-01 passes
//   N=2 (grid step 4/255)   -- SECV-01 passes
//   N=3 (grid step 8/255)   -- SECV-01 passes
//   N=4 (grid step 16/255)  -- SECV-01 passes
//   N=5 (grid step 32/255)  -- SECV-01 passes
//   N=6 (grid step 64/255)  -- SECV-01 passes
//   N=7 (grid step 128/255) -- SECV-01 passes (re-confirmed twice)
//   N=8 (masks all 8 bits)  -- SECV-01 FAILS (every byte quantizes to 0x00
//     regardless of input, so the correct and fully-bit-inverted-texture
//     artifactIds collide trivially -- confirmed deterministic, re-run
//     twice; this is exactly the degenerate all-zero-byte edge case
//     RESEARCH.md's Tolerance Derivation Methodology step 4 anticipated for
//     N=8, not a surprise)
// N=8 is the first (and only possible) failure boundary in [0,8]; the final
// value is chosen one integer step BELOW it: N=7 (also re-confirmed passing
// twice). Because Round 1's own real measured divergence for this fixture is
// genuinely zero (not stale/degenerate, per the diagnosis above), N=7 is
// chosen purely from this counter-test's own corruption-detection boundary --
// there is no non-zero real delta to independently margin above, an honest,
// explicitly documented D-08 case (real measured zero divergence, not a
// manufactured target).
namespace sgns
{
    class Secv01RenderTexturingCounterTest : public ProcessorConformanceFixture
    {
    };

    TEST_F( Secv01RenderTexturingCounterTest, TexturingWrongSourceImageStillDiverges )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        // Two render jobs shaped exactly like texturing-fixture-definition.json,
        // identical except texturingSourceImage's source_uri_param: the correct
        // fixture's real texturing-source-image.raw (64x64 RGBA8 checkerboard)
        // vs. Task 1's fully bit-inverted secv01-wrong-texture-source-image.raw
        // (the maximal-divergence "wrong" variant). Both declare the SAME
        // candidate byteQuantMode under test -- if texturing's own
        // empirically-derived precision were loose enough to also mask a fully
        // substituted source texture, these two runs would produce the same
        // artifactId, which must never happen.
        const std::string correctJson = R"({
            "name": "secv01-render-texturing-correct",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "texturingVertexInput",
                    "source_uri_param": "file://processing_dispatch/texturing-vertex-data.raw",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                },
                {
                    "name": "texturingSourceImage",
                    "source_uri_param": "file://processing_dispatch/texturing-source-image.raw",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "texturingRenderOutput",
                    "source_uri_param": "file://processing_dispatch/texturing-render-output.raw",
                    "type": "image"
                }
            ],
            "parameters": [
                { "name": "byteQuantMode", "type": "int", "default": 7 }
            ],
            "passes": [
                {
                    "name": "texturingPass",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "glsl",
                                "source": "file://processing_dispatch/texturing_vertex_shader.glsl",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "glsl",
                                "source": "file://processing_dispatch/texturing_fragment_shader.glsl",
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
                    "vertex_buffer": { "source": "input:texturingVertexInput" },
                    "texture_buffer": {
                        "source": "input:texturingSourceImage",
                        "width": 64,
                        "height": 64,
                        "filter": "nearest"
                    },
                    "vertex_layout": [
                        { "name": "inPosX", "format": "FLOAT32", "offset": 0 },
                        { "name": "inPosY", "format": "FLOAT32", "offset": 4 },
                        { "name": "inUvX", "format": "FLOAT32", "offset": 8 },
                        { "name": "inUvY", "format": "FLOAT32", "offset": 12 }
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

        const std::string wrongTextureJson = R"({
            "name": "secv01-render-texturing-wrong-source",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "texturingVertexInput",
                    "source_uri_param": "file://processing_dispatch/texturing-vertex-data.raw",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                },
                {
                    "name": "texturingSourceImage",
                    "source_uri_param": "file://processing_conformance_security/fixtures/secv01-wrong-texture-source-image.raw",
                    "type": "buffer",
                    "dimensions": { "width": 1 }
                }
            ],
            "outputs": [
                {
                    "name": "texturingRenderOutput",
                    "source_uri_param": "file://processing_dispatch/texturing-render-output.raw",
                    "type": "image"
                }
            ],
            "parameters": [
                { "name": "byteQuantMode", "type": "int", "default": 7 }
            ],
            "passes": [
                {
                    "name": "texturingPass",
                    "type": "render",
                    "render_shader": {
                        "stages": [
                            {
                                "stage": "vertex",
                                "type": "glsl",
                                "source": "file://processing_dispatch/texturing_vertex_shader.glsl",
                                "entry_point": "main"
                            },
                            {
                                "stage": "fragment",
                                "type": "glsl",
                                "source": "file://processing_dispatch/texturing_fragment_shader.glsl",
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
                    "vertex_buffer": { "source": "input:texturingVertexInput" },
                    "texture_buffer": {
                        "source": "input:texturingSourceImage",
                        "width": 64,
                        "height": 64,
                        "filter": "nearest"
                    },
                    "vertex_layout": [
                        { "name": "inPosX", "format": "FLOAT32", "offset": 0 },
                        { "name": "inPosY", "format": "FLOAT32", "offset": 4 },
                        { "name": "inUvX", "format": "FLOAT32", "offset": 8 },
                        { "name": "inUvY", "format": "FLOAT32", "offset": 12 }
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

        auto patchedCorrect      = PatchJsonUrisToAbsolute( correctJson, BinPath() );
        auto patchedWrongTexture = PatchJsonUrisToAbsolute( wrongTextureJson, BinPath() );

        auto rCorrect = sgns::sgprocessing::ProcessingManager::Create( patchedCorrect );
        ASSERT_TRUE( rCorrect.has_value() ) << "Correct texturing job should be accepted: "
                                             << rCorrect.error().message();
        auto rWrongTexture = sgns::sgprocessing::ProcessingManager::Create( patchedWrongTexture );
        ASSERT_TRUE( rWrongTexture.has_value() ) << "Wrong-source-image job should still be accepted "
                                                     "(a structurally-valid-but-corrupted texture "
                                                     "buffer, not a malformed job): "
                                                  << rWrongTexture.error().message();

        const auto &managerCorrect      = rCorrect.value();
        const auto &managerWrongTexture = rWrongTexture.value();

        sgns::ModelNode modelNodeCorrect;
        modelNodeCorrect.set_source( std::string( "input:texturingVertexInput" ) );

        sgns::ModelNode modelNodeWrongTexture;
        modelNodeWrongTexture.set_source( std::string( "input:texturingVertexInput" ) );

        auto                              iocCorrect = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrect;
        std::vector<std::string>         outputLocationsCorrect;

        auto prCorrect =
            managerCorrect->Process( iocCorrect, chunkhashesCorrect, modelNodeCorrect, outputLocationsCorrect );
        ASSERT_TRUE( prCorrect.has_value() ) << "Correct texturing run: " << prCorrect.error().message();
        ASSERT_GE( prCorrect.value().artifacts.size(), 1u );

        auto                              iocWrongTexture = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesWrongTexture;
        std::vector<std::string>         outputLocationsWrongTexture;

        auto prWrongTexture = managerWrongTexture->Process( iocWrongTexture,
                                                              chunkhashesWrongTexture,
                                                              modelNodeWrongTexture,
                                                              outputLocationsWrongTexture );
        ASSERT_TRUE( prWrongTexture.has_value() ) << "Wrong-source-image run: " << prWrongTexture.error().message();
        ASSERT_GE( prWrongTexture.value().artifacts.size(), 1u );

        EXPECT_NE( std::memcmp( prCorrect.value().artifacts[0].artifactId,
                                 prWrongTexture.value().artifacts[0].artifactId,
                                 sgns::sgprocessing::SHA256_HASH_SIZE ),
                   0 )
            << "A deliberately corrupted (fully bit-inverted) source texture must produce a "
               "different post-quantization artifactId than the correct texture at texturing's "
               "own empirically-derived byteQuantMode -- SECV-01/T-17-16";

        std::cout << "TexturingWrongSourceImageStillDiverges: correct and wrong-source-image "
                     "artifactId hashes differ as required"
                  << std::endl;
    }
} // namespace sgns
