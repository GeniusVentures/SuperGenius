#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
#include <memory>

#include <artifacts/artifact_types.hpp>
#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>
#include "testutil/processing_conformance_fixture.hpp"

// SECV-01-style tex3d/spleen_ct_seg counter-test (Phase 14 D-09/D-10, QUANT-CFG-03):
// the existing secv01_counter_test.cpp only exercises the small MNN float
// fixture at S=2^15 -- it does not prove tex3d/spleen_ct_seg's own, much
// coarser empirically-derived quantScale still catches a genuinely corrupted
// model. This mirrors that suite's exact methodology (binary std::memcmp on
// output.artifacts[0].artifactId, never .combinedHash) applied to the
// texture3d-processing-definition.json job shape instead.
//
// STATE.md's "New evidence for QUANT-CFG-01" paragraph: real captured
// cross-machine divergence for spleen_ct_seg via processing_processor_mnn_volume.cpp
// -- all 25 chunk hashes mismatch, delta 0.005126953125 = 168x the (then-)
// current S=2^15 grid step (3.0517578125e-05).
//
// A local binary search over power-of-two quantScale values against this
// exact test (D-10, mirroring 13-04-PLAN.md's process for the small model's
// S=2^15) was run before this value was hardcoded:
//   S=256 (2^8, grid step 0.00390625) -- SECV-01 passes
//   S=128 (2^7, grid step 0.0078125)  -- SECV-01 passes
//   S=64  (2^6, grid step 0.015625)   -- SECV-01 passes
//   S=2   (2^1, grid step 0.5)        -- SECV-01 passes
//   S=1   (2^0, grid step 1.0, the floor of the valid positive-power-of-two
//          domain IsPositivePowerOfTwo() accepts) -- SECV-01 still passes
// Unlike Phase 13's small-model search (which found a genuine S=2^14 failure
// boundary), no SECV-01 failure boundary exists anywhere in the valid
// quantScale domain for this offset's corruption: the single-byte weight-tensor
// flip at offset 15000000 in this real ~19MB segmentation model propagates
// through the network into a divergence large enough that even the coarsest
// possible integer grid (S=1) still distinguishes the corrupted artifactId
// from the correct one. This is an honest, empirically-confirmed finding, not
// an assumption -- see 14-03-SUMMARY.md's Deviations section.
//
// Because no SECV-01 failure boundary exists to margin above, the final
// value below (S=128, 2^7) is instead chosen by the other empirical
// constraint D-10 cites: it must be coarse enough to actually absorb the
// real measured cross-hardware divergence (delta 0.005126953125). S=128's
// grid step (0.0078125) is the largest power-of-two grid step that still
// exceeds that delta (~1.53x margin); S=256's grid step (0.00390625) is
// smaller than the delta and would not reliably absorb it. S=128 is also
// empirically confirmed above to still pass this exact counter-test.

namespace sgns
{
    class Secv01Tex3dCounterTest : public ProcessorConformanceFixture
    {
    };

    TEST_F( Secv01Tex3dCounterTest, MnnCorruptedSpleenCtSegModelStillDiverges )
    {
        // Two texture3d-processing-definition.json-shaped inline jobs,
        // identical in every field except which .mnn file the model
        // parameter points at: the real spleen_ct_seg.mnn (correct) vs
        // Task 1's byte-perturbed copy (corrupted, T-14-07). Both declare
        // the same candidate quantScale under test -- if tex3d's own
        // (much coarser) precision were loose enough to also mask a
        // substituted model, these two runs would produce the same
        // artifactId, which must never happen.
        const std::string correctJson = R"({
            "name": "secv01-tex3d-correct",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "inputVolume",
                    "source_uri_param": "file://processing_datatypes/spleen_15.raw",
                    "type": "texture3D",
                    "dimensions": {
                        "width": 253,
                        "height": 253,
                        "chunk_count": 94,
                        "chunk_subchunk_width": 96,
                        "chunk_subchunk_height": 96,
                        "block_len": 96,
                        "chunk_stride": 48,
                        "chunk_line_stride": 48,
                        "block_stride": 48
                    },
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "segmentationOutput",
                    "source_uri_param": "file://processing_datatypes/stitched_logits.raw",
                    "type": "tensor"
                }
            ],
            "parameters": [
                {
                    "name": "modelUri",
                    "type": "uri",
                    "default": "file://processing_datatypes/spleen_ct_seg.mnn"
                },
                {
                    "name": "quantScale",
                    "type": "float",
                    "default": 128.0
                }
            ],
            "passes": [
                {
                    "name": "volume_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_datatypes/spleen_ct_seg.mnn",
                        "format": "MNN",
                        "batch_size": 1,
                        "input_nodes": [
                            { "name": "input", "type": "tensor", "source": "input:inputVolume", "shape": [1, 1, 96, 96, 96] }
                        ],
                        "output_nodes": [
                            { "name": "output", "type": "tensor", "target": "output:segmentationOutput", "shape": [1, 2, 96, 96, 96] }
                        ]
                    }
                }
            ]
        })";

        const std::string corruptedJson = R"({
            "name": "secv01-tex3d-corrupted",
            "version": "1.0.0",
            "gnus_spec_version": 1.0,
            "inputs": [
                {
                    "name": "inputVolume",
                    "source_uri_param": "file://processing_datatypes/spleen_15.raw",
                    "type": "texture3D",
                    "dimensions": {
                        "width": 253,
                        "height": 253,
                        "chunk_count": 94,
                        "chunk_subchunk_width": 96,
                        "chunk_subchunk_height": 96,
                        "block_len": 96,
                        "chunk_stride": 48,
                        "chunk_line_stride": 48,
                        "block_stride": 48
                    },
                    "format": "FLOAT32"
                }
            ],
            "outputs": [
                {
                    "name": "segmentationOutput",
                    "source_uri_param": "file://processing_datatypes/stitched_logits.raw",
                    "type": "tensor"
                }
            ],
            "parameters": [
                {
                    "name": "modelUri",
                    "type": "uri",
                    "default": "file://processing_conformance_security/fixtures/secv01-corrupted-spleen_ct_seg.mnn"
                },
                {
                    "name": "quantScale",
                    "type": "float",
                    "default": 128.0
                }
            ],
            "passes": [
                {
                    "name": "volume_inference",
                    "type": "inference",
                    "model": {
                        "source_uri_param": "file://processing_conformance_security/fixtures/secv01-corrupted-spleen_ct_seg.mnn",
                        "format": "MNN",
                        "batch_size": 1,
                        "input_nodes": [
                            { "name": "input", "type": "tensor", "source": "input:inputVolume", "shape": [1, 1, 96, 96, 96] }
                        ],
                        "output_nodes": [
                            { "name": "output", "type": "tensor", "target": "output:segmentationOutput", "shape": [1, 2, 96, 96, 96] }
                        ]
                    }
                }
            ]
        })";

        auto patchedCorrect   = PatchJsonUrisToAbsolute( correctJson, BinPath() );
        auto patchedCorrupted = PatchJsonUrisToAbsolute( corruptedJson, BinPath() );

        auto rCorrect = sgns::sgprocessing::ProcessingManager::Create( patchedCorrect );
        ASSERT_TRUE( rCorrect.has_value() ) << "Correct tex3d job should be accepted: "
                                             << rCorrect.error().message();
        auto rCorrupted = sgns::sgprocessing::ProcessingManager::Create( patchedCorrupted );
        ASSERT_TRUE( rCorrupted.has_value() ) << "Corrupted-model tex3d job should still be accepted "
                                                  "(a structurally-valid-but-wrong model, not a "
                                                  "malformed job): "
                                               << rCorrupted.error().message();

        const auto &managerCorrect   = rCorrect.value();
        const auto &managerCorrupted = rCorrupted.value();

        auto        pCorrect      = managerCorrect->GetProcessingData();
        const auto &passesCorrect = pCorrect.get_passes();
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
        ASSERT_TRUE( prCorrect.has_value() ) << "Correct tex3d run: " << prCorrect.error().message();
        ASSERT_GE( prCorrect.value().artifacts.size(), 1u );

        auto                              iocCorrupted = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashesCorrupted;
        std::vector<std::string>         outputLocationsCorrupted;
        sgns::ModelNode                   modelNodeCorrupted = inputNodesCorrupted[0];

        auto prCorrupted = managerCorrupted->Process( iocCorrupted,
                                                        chunkhashesCorrupted,
                                                        modelNodeCorrupted,
                                                        outputLocationsCorrupted );
        ASSERT_TRUE( prCorrupted.has_value() ) << "Corrupted-model tex3d run: " << prCorrupted.error().message();
        ASSERT_GE( prCorrupted.value().artifacts.size(), 1u );

        EXPECT_NE( std::memcmp( prCorrect.value().artifacts[0].artifactId,
                                 prCorrupted.value().artifacts[0].artifactId,
                                 sgns::sgprocessing::SHA256_HASH_SIZE ),
                   0 )
            << "A deliberately corrupted MNN spleen_ct_seg model must produce a different "
               "post-quantization artifactId than the correct model at tex3d's own "
               "empirically-derived quantScale -- T-14-07/QUANT-CFG-03";

        std::cout << "MnnCorruptedSpleenCtSegModelStillDiverges: correct and corrupted-model "
                     "artifactId hashes differ as required"
                  << std::endl;
    }
} // namespace sgns
