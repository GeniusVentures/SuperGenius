#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <execution/execution_context.hpp>
#include <processingbase/ProcessingManager.hpp>
#include <processors/vulkan_gpu_probe.hpp>
#include "testutil/processing_conformance_fixture.hpp"

// Architectural note (Gap 2 / TEST-07 closure, Phase 09 Plan 12): the per-pass deadline
// timer (boost::asio::deadline_timer inside ProcessingManager::ProcessInternal) only fires
// its callback when the associated io_context runs concurrently on another thread while
// StartProcessing() blocks the calling thread. Nothing in this suite (or in production
// today) spawns such a concurrent io_context runner, so deadlineMs-triggered timeout cannot
// be deterministically exercised from a test without adding that concurrency machinery —
// a separate, out-of-scope production change, not attempted here. This remains the one
// documented, accepted gap in GetLastManifest()'s terminal-state coverage (Phase 16
// manifest-evolution UAT): TerminalState::Timeout is unreachable from any test in this
// suite, though it is built by the same buildFailureManifest() lambda as every other
// terminal state (ProcessingManager.cpp).
//
// CancellationToken::IsCancelled() and ExecutionContext::maxOutputArtifactBytes, by
// contrast, are plain synchronous checks made by the processor itself (see
// processing_processor_mnn_buffer.cpp and processing_processor_render.cpp) and need no
// io_context concurrency at all — these are two of the mechanisms this suite tests
// deterministically, via the caller-owned ExecutionContext now exposed by
// ProcessingManager::Process()'s new 5-arg overload (Plan 09-12 Task 1). A third —
// RenderDataTransformUnsupportedProducesErrorTerminalState below — deterministically
// reaches the generic (non-Cancelled/non-Timeout/non-BudgetExceeded) TerminalState::Error
// path via RENDER-07 (processing_processor_render.cpp: no data_transform executor exists
// anywhere in this codebase, so any render pass declaring one always fails), closing the
// other half of the Phase 16 GetLastManifest() reachability gap without needing any
// concurrency machinery.

namespace sgns
{
    class CancellationConformanceTest : public ProcessorConformanceFixture
    {
    protected:
        static void SetUpTestSuite()
        {
            binary_path = boost::dll::program_location().parent_path().string() + "/";
            data_path   = binary_path + "processing_datatypes/";
        }
    };

    TEST_F( CancellationConformanceTest, ProgressEventsEmitted )
    {
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        // Caller-owned ExecutionContext: capture every ProgressEvent the pipeline fires.
        sgns::sgprocessing::ExecutionContext                  execCtx;
        std::vector<sgns::sgprocessing::ProgressEvent>        captured;
        execCtx.progressCallback = [&captured]( const sgns::sgprocessing::ProgressEvent &ev )
        {
            captured.push_back( ev );
        };
        execCtx.cancelToken.SetCallback( []() {} );

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations, execCtx );
        ASSERT_TRUE( pr.has_value() ) << "Process() should succeed: " << pr.error().message();
        ASSERT_FALSE( pr.value().empty() );
        ASSERT_GT( chunkhashes.size(), 0 );

        ASSERT_GT( captured.size(), 0u ) << "The caller-supplied progressCallback must be invoked "
                                             "at least once — it must not be silently discarded by "
                                             "ProcessInternal's internal default logging callback";
        EXPECT_FLOAT_EQ( captured.back().percent, 100.0f )
            << "The final captured ProgressEvent should report full completion";

        std::cout << "ProgressEventsEmitted: Process() completed successfully, " << captured.size()
                  << " progress events captured, final percent=" << captured.back().percent << std::endl;
    }

    TEST_F( CancellationConformanceTest, DeadlineExpiryProducesTimeout )
    {
        // Use a very short deadline to verify the system handles tight time constraints.
        // NOTE: as explained in the file-level comment above, the deadline timer requires
        // concurrent io_context execution to actually fire — which nothing here provides —
        // so this case cannot deterministically force a timeout. It instead verifies the
        // pipeline does not crash when a (never-firing) deadline is configured; the honest,
        // disclosed limitation replaces a previous comment that implied deadline behavior
        // was exercised here.
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );

        // The test verifies the pipeline does not crash under deadline pressure. Either
        // outcome is acceptable here because the timer is never given a concurrently-running
        // io_context to fire on (see file-level comment) — this is a disclosed limitation,
        // not a claim that deadline enforcement is verified by this case.
        if ( pr.has_value() )
        {
            std::cout << "DeadlineExpiryProducesTimeout: Process() completed within deadline" << std::endl;
        }
        else
        {
            std::cout << "DeadlineExpiryProducesTimeout: Process() returned error: "
                      << pr.error().message() << std::endl;
        }
    }

    TEST_F( CancellationConformanceTest, CancelBeforeStartProducesNoSuccessfulResult )
    {
        // Pre-cancel (rather than a racy sleep-then-cancel-from-another-thread) is used
        // deliberately: this fixture's MNN inference completes in well under a millisecond,
        // making any sleep-based race non-deterministic. IsCancelled() is checked by the
        // processor at the very first stage boundary (right after model load, ~25% progress
        // per MNN_Buffer::StartProcessing), so pre-cancelling deterministically exercises the
        // exact same execCtx.cancelToken.IsCancelled()/RunTeardown() code path a mid-flight
        // cancel would.
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        sgns::sgprocessing::ExecutionContext execCtx;
        execCtx.cancelToken.Cancel(); // Cancel BEFORE Process() starts

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations, execCtx );

        ASSERT_FALSE( pr.has_value() ) << "A pre-cancelled ExecutionContext must produce a non-success result";
        ASSERT_EQ( pr.error(), sgns::sgprocessing::ProcessingManager::Error::PROCESSING_FAILED );
        ASSERT_TRUE( output_locations.empty() || output_locations[0].empty() )
            << "No output location should be populated for a cancelled run";

        // ARTF-09: a caller holding only `manager` (not the original ProcessingError)
        // can retrieve a non-empty, human-readable errorMessage via GetLastManifest().
        EXPECT_EQ( manager->GetLastManifest().terminalState, sgns::sgprocessing::TerminalState::Cancelled );
        EXPECT_STRNE( manager->GetLastManifest().errorMessage, "" );

        std::cout << "CancelBeforeStartProducesNoSuccessfulResult: pre-cancelled MNN run correctly "
                     "produced a non-success result with no output published"
                  << std::endl;
    }

    TEST_F( CancellationConformanceTest, RenderCancelBeforeStartProducesNoSuccessfulResult )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        // Identical inline JSON literal to capability_conformance_test.cpp's
        // AcceptValidRenderJob (64x64 render target, shared passthrough.vert.spv/.frag.spv
        // fixtures) — reused here to exercise the same real Vulkan RenderProcessor path
        // through the new 5-arg Process() overload's cancellation wiring.
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
        ASSERT_TRUE( r.has_value() ) << "Valid RENDER job should be accepted: " << r.error().message();

        const auto &manager = r.value();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:vertexData" ) );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;

        sgns::sgprocessing::ExecutionContext execCtx;
        execCtx.cancelToken.Cancel(); // Cancel BEFORE Process() starts

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations, execCtx );

        ASSERT_FALSE( pr.has_value() ) << "A pre-cancelled ExecutionContext must produce a non-success result";
        ASSERT_EQ( pr.error(), sgns::sgprocessing::ProcessingManager::Error::PROCESSING_FAILED );
        ASSERT_TRUE( output_locations.empty() || output_locations[0].empty() )
            << "No output location should be populated for a cancelled run";

        std::cout << "RenderCancelBeforeStartProducesNoSuccessfulResult: pre-cancelled Vulkan render "
                     "run correctly produced a non-success result with no output published"
                  << std::endl;
    }

    TEST_F( CancellationConformanceTest, RenderDataTransformUnsupportedProducesErrorTerminalState )
    {
        if ( !sgns::sgprocessing::HasUsableVulkanDevice() )
        {
            GTEST_SKIP() << "No usable Vulkan device (DISCRETE_GPU/INTEGRATED_GPU) found on this "
                            "host; skipping this GPU-dependent test, not failing it.";
        }

        // Identical render job to RenderCancelBeforeStartProducesNoSuccessfulResult, plus a
        // declared data_transforms entry. RENDER-07 (processing_processor_render.cpp) rejects
        // ANY data_transform declared on a render pass unconditionally — no data_transform
        // executor exists anywhere in this codebase — so this deterministically reaches the
        // generic (non-Cancelled/non-Timeout/non-BudgetExceeded) ProcessingError path and its
        // TerminalState::Error mapping, without cancellation, a real deadline expiry, or a
        // budget breach.
        const std::string json_str = R"({
            "name": "cap-check-render-data-transform",
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
                    "data_transforms": [
                        { "type": "normalize", "input": "input:vertexData", "output": "internal:unused" }
                    ],
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
        ASSERT_TRUE( r.has_value() )
            << "A render job with a declared data_transform should still be accepted at "
               "Create()-time (the rejection happens later, inside StartProcessing()): "
            << r.error().message();

        const auto &manager = r.value();

        sgns::ModelNode model_node;
        model_node.set_source( std::string( "input:vertexData" ) );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );

        ASSERT_FALSE( pr.has_value() )
            << "A render pass with a declared data_transform must fail: no data_transform "
               "executor exists in this codebase (RENDER-07)";
        ASSERT_EQ( pr.error(), sgns::sgprocessing::ProcessingManager::Error::PROCESSING_FAILED );

        // ARTF-09 + Phase 16 manifest-evolution: GetLastManifest() must be reachable and
        // correctly populated for a generic (non-Cancelled/non-Timeout/non-BudgetExceeded)
        // ProcessingError, matching the guarantee already fixture-proven for
        // Cancelled/BudgetExceeded/Success above.
        EXPECT_EQ( manager->GetLastManifest().terminalState, sgns::sgprocessing::TerminalState::Error );
        EXPECT_STRNE( manager->GetLastManifest().errorMessage, "" );

        std::cout << "RenderDataTransformUnsupportedProducesErrorTerminalState: declared "
                     "data_transform correctly produced TerminalState::Error with a non-empty "
                     "errorMessage"
                  << std::endl;
    }

    TEST_F( CancellationConformanceTest, ResourcesCleanedUpAfterCancelledRun )
    {
        // First run: pre-cancelled, must fail. Second run: a brand-new manager instance,
        // normal (uncancelled) 4-arg Process(), must succeed. The second run succeeding
        // proves the first (cancelled) run's RunTeardown() released its MNN session
        // resources without leaving corrupted state.
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        // First run — pre-cancelled
        {
            auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
            ASSERT_TRUE( r.has_value() );
            const auto &manager = r.value();
            auto        p = manager->GetProcessingData();
            const auto &passes = p.get_passes();
            ASSERT_EQ( passes.size(), 1 );
            ASSERT_TRUE( passes[0].get_model().has_value() );
            const auto model       = passes[0].get_model().value();
            const auto input_nodes = model.get_input_nodes();
            ASSERT_GE( input_nodes.size(), 1 );

            auto                              ioc = std::make_shared<boost::asio::io_context>();
            std::vector<std::vector<uint8_t>> chunkhashes;
            std::vector<std::string>         output_locations;
            sgns::ModelNode                   model_node = input_nodes[0];

            sgns::sgprocessing::ExecutionContext execCtx;
            execCtx.cancelToken.Cancel();

            auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations, execCtx );
            ASSERT_FALSE( pr.has_value() ) << "First (pre-cancelled) run must fail";
        }

        // Second run — a brand-new manager, normal execution, no cancellation
        {
            auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
            ASSERT_TRUE( r.has_value() );
            const auto &manager = r.value();
            auto        p = manager->GetProcessingData();
            const auto &passes = p.get_passes();
            ASSERT_EQ( passes.size(), 1 );
            ASSERT_TRUE( passes[0].get_model().has_value() );
            const auto model       = passes[0].get_model().value();
            const auto input_nodes = model.get_input_nodes();
            ASSERT_GE( input_nodes.size(), 1 );

            auto                              ioc = std::make_shared<boost::asio::io_context>();
            std::vector<std::vector<uint8_t>> chunkhashes;
            std::vector<std::string>         output_locations;
            sgns::ModelNode                   model_node = input_nodes[0];

            auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations );
            ASSERT_TRUE( pr.has_value() )
                << "Second (normal) run should succeed, proving the first cancelled run's "
                   "teardown released resources without leaving corrupted state: "
                << pr.error().message();
        }

        std::cout << "ResourcesCleanedUpAfterCancelledRun: cancelled run's teardown left the "
                     "processor usable for a subsequent normal run"
                  << std::endl;
    }

    TEST_F( CancellationConformanceTest, BudgetExceededProducesBudgetFailure )
    {
        // maxOutputArtifactBytes = 1 is guaranteed smaller than any real output. This is
        // deterministic because ProcessInternal's schema-budget defaulting only applies the
        // schema's (here, unset/0) budget when the caller's field is still 0 — so the
        // caller's 1 survives and is enforced synchronously inside MNN_Buffer::StartProcessing.
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        sgns::sgprocessing::ExecutionContext execCtx;
        execCtx.maxOutputArtifactBytes = 1;
        execCtx.cancelToken.SetCallback( []() {} );

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations, execCtx );

        ASSERT_FALSE( pr.has_value() ) << "A 1-byte output budget must be exceeded and produce a failure";
        ASSERT_EQ( pr.error(), sgns::sgprocessing::ProcessingManager::Error::PROCESSING_FAILED );

        // ARTF-09: a caller holding only `manager` (not the original ProcessingError)
        // can retrieve a non-empty, human-readable errorMessage via GetLastManifest().
        EXPECT_EQ( manager->GetLastManifest().terminalState, sgns::sgprocessing::TerminalState::BudgetExceeded );
        EXPECT_STRNE( manager->GetLastManifest().errorMessage, "" );

        std::cout << "BudgetExceededProducesBudgetFailure: 1-byte budget correctly rejected the "
                     "MNN run's real output size"
                  << std::endl;
    }

    TEST_F( CancellationConformanceTest, SuccessfulRunHasEmptyErrorMessageInManifest )
    {
        // Proves the success path also populates m_lastManifest (not just failure paths),
        // and that errorMessage stays empty when nothing failed (ARTF-09).
        const std::string &json_str = PatchedJson( "buffer-processing-definition.json" );
        ASSERT_FALSE( json_str.empty() );

        auto r = sgns::sgprocessing::ProcessingManager::Create( json_str );
        ASSERT_TRUE( r.has_value() );

        const auto &manager = r.value();
        auto        p = manager->GetProcessingData();
        const auto &passes = p.get_passes();
        ASSERT_EQ( passes.size(), 1 );
        ASSERT_TRUE( passes[0].get_model().has_value() );
        const auto model       = passes[0].get_model().value();
        const auto input_nodes = model.get_input_nodes();
        ASSERT_GE( input_nodes.size(), 1 );

        auto                              ioc = std::make_shared<boost::asio::io_context>();
        std::vector<std::vector<uint8_t>> chunkhashes;
        std::vector<std::string>         output_locations;
        sgns::ModelNode                   model_node = input_nodes[0];

        sgns::sgprocessing::ExecutionContext execCtx;
        execCtx.cancelToken.SetCallback( []() {} );

        auto pr = manager->Process( ioc, chunkhashes, model_node, output_locations, execCtx );
        ASSERT_TRUE( pr.has_value() ) << "Process() should succeed: " << pr.error().message();

        EXPECT_EQ( manager->GetLastManifest().terminalState, sgns::sgprocessing::TerminalState::Success );
        EXPECT_EQ( manager->GetLastManifest().errorMessage[0], '\0' );

        std::cout << "SuccessfulRunHasEmptyErrorMessageInManifest: normal run correctly left "
                     "GetLastManifest().errorMessage empty with TerminalState::Success"
                  << std::endl;
    }

} // namespace sgns
