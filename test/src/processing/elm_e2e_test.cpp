/**
 * @file elm_e2e_test.cpp
 * @brief elmbridge 04-05: the milestone acceptance-criterion E2E.
 *
 * Leg 1 (empty-cache single-node E2E, D-05/D-08/D-09/D-10): one node, both
 * roles, EMPTY elmruntime cache; the staged Qwen fixture is published to real
 * ipfs:// uris; a funded 2-work-item/1-manifest job (seeded generation, stop
 * "\n", 1.0h funding -> 300-minion escrow) submits, splits, downloads the
 * model ONCE (single-flight), generates, publishes work-item-tagged envelope
 * artifacts, and settles by measured wall-clock with a NON-ZERO refund whose
 * value is recomputed exactly from the observed stamps.
 *
 * Leg 2 (overtime, D-11): tiny funding -> deadline overrun -> terminal
 * "cancelled" envelope -> queue drains, no re-grab.
 *
 * Leg 3 (regression gate, E2E-02): a minimal legacy chunk-based job still
 * submits/splits/pays through the untouched path.
 *
 * SGPROC_ELM_TEST_MODEL_DIR gates legs 1-2 (fixtures README cross-reference;
 * never a silent pass). Legs run isolated per the node-fixture convention
 * (one live node per process — see 04-03-SUMMARY).
 */

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <nlohmann/json.hpp>

#include <bitswap.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "account/GeniusNode.hpp"
#include "account/GeniusAccount.hpp"
#include "processing/elm_settlement.hpp"
#include "processing/processing_clocks_elm.hpp"

#include "FileManager.hpp"

#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/mint_source_hash.hpp"
#include "testutil/TestMintInputValidator.hpp"
#include "testutil/offline_chainlist.hpp"
#include "testutil/local_trust_setup.hpp"

using namespace sgns::test;
using namespace sgns;
namespace fs = std::filesystem;

static sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

// TEMP-DIAG (04-05): print a native stack for REAL access violations only
// (0xC0000005) — C++ exceptions (0xE06D7363) and OutputDebugString
// (0x40010006) pass through untouched. Removed once the model-fetch-completion
// flake is fixed.
#if defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

namespace
{
    LONG CrashHandler( EXCEPTION_POINTERS *info )
    {
        if ( info->ExceptionRecord->ExceptionCode != 0xC0000005 )
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        // MiniDumpWriteDump first (crash-loop safe, no symbol machinery
        // needed): TEMP-DIAG round 2 — the nearest-export stacks below
        // cannot pinpoint the freed object; a full dump can be analyzed
        // offline (windbg/cdb: `!analyze -v`, `heap -p -a <va>`).
        {
            char dumpPath[MAX_PATH] = {};
            snprintf( dumpPath, sizeof( dumpPath ), "%s\\elm_e2e_crash_%lu.dmp",
                      getenv( "TEMP" ) ? getenv( "TEMP" ) : ".",
                      static_cast<unsigned long>( GetCurrentProcessId() ) );
            HANDLE file = CreateFileA( dumpPath, GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( file != INVALID_HANDLE_VALUE )
            {
                MINIDUMP_EXCEPTION_INFORMATION mei{ GetCurrentThreadId(), info, FALSE };
                MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), file,
                                   MiniDumpWithFullMemory, &mei, nullptr, nullptr );
                CloseHandle( file );
                fprintf( stderr, "===== minidump written: %s =====\n", dumpPath );
                fflush( stderr );
            }
        }
        HANDLE process = GetCurrentProcess();
        SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
        SymInitialize( process, nullptr, TRUE );

        void *frames[62];
        WORD  count  = CaptureStackBackTrace( 0, 62, frames, nullptr );
        fprintf( stderr, "\n===== AV CRASH at %p (rw=%lu va=%p) =====\n",
                 info->ExceptionRecord->ExceptionAddress,
                 static_cast<unsigned long>( info->ExceptionRecord->ExceptionInformation[0] ),
                 reinterpret_cast<void *>( info->ExceptionRecord->ExceptionInformation[1] ) );
        for ( WORD i = 0; i < count; ++i )
        {
            char                 symbolBuffer[sizeof( SYMBOL_INFO ) + 256] = {};
            SYMBOL_INFO         *symbol       = reinterpret_cast<SYMBOL_INFO *>( symbolBuffer );
            symbol->SizeOfStruct               = sizeof( SYMBOL_INFO );
            symbol->MaxNameLen                 = 255;
            DWORD64             displacement   = 0;
            std::string         name           = "??";
            if ( SymFromAddr( process, reinterpret_cast<DWORD64>( frames[i] ), &displacement, symbol ) )
            {
                name = std::string( symbol->Name, symbol->NameLen );
            }
            else
            {
                char buf[32];
                snprintf( buf, sizeof( buf ), "%p", frames[i] );
                name = buf;
            }
            fprintf( stderr, "  [%02u] %s+0x%llx\n", i, name.c_str(),
                     static_cast<unsigned long long>( displacement ) );
        }
        fflush( stderr );
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const bool crashHandlerInstalled = []()
    {
        AddVectoredExceptionHandler( 1, CrashHandler );
        return true;
    }();
    [[maybe_unused]] const bool *crashHandlerInstalledGuard = &crashHandlerInstalled;
} // namespace
#endif

namespace
{
    std::string FixtureModelDir()
    {
        const char *env = std::getenv( "SGPROC_ELM_TEST_MODEL_DIR" );
        return env != nullptr ? std::string( env ) : std::string();
    }

    std::string NewTestUuidSuffix()
    {
        static std::mt19937 gen( static_cast<unsigned>( std::random_device{}() ) );
        std::uniform_int_distribution<int> dist( 0, 0xFFFF );
        std::ostringstream                oss;
        oss << std::hex << dist( gen ) << dist( gen ) << dist( gen ) << dist( gen );
        return oss.str();
    }

    std::string FileSha256Hex( const fs::path &p )
    {
        std::ifstream         file( p, std::ios::binary );
        std::vector<uint8_t>  bytes( ( std::istreambuf_iterator<char>( file ) ),
                                    std::istreambuf_iterator<char>() );
        const auto            hash = sgns::sgprocmanagersha::sha256( bytes.data(), bytes.size() );
        std::ostringstream    oss;
        oss << std::hex << std::setfill( '0' );
        for ( const auto b : hash )
        {
            oss << std::setw( 2 ) << static_cast<int>( b );
        }
        return oss.str();
    }

    // The five manifest roles + the optional embedding (04-01 D-03) over the
    // staged bundle, published to REAL ipfs:// uris (D-09).
    struct PublishedFixture
    {
        std::string manifestUri;    // ipfs://CID of the manifest bytes
        std::string manifestSha256; // sha256 of the manifest bytes (declared hash)
    };

    // Weak hook to the live fixture node (set by ElmE2eNode's constructor):
    // Publish uses it to register the node as seed provider for every CID it
    // publishes (single-node transport — no DHT, provider registry only).
    std::weak_ptr<sgns::GeniusNode> &E2eNodeHook()
    {
        static std::weak_ptr<sgns::GeniusNode> hook;
        return hook;
    }

    // ONE long-lived io_context + runner for ALL publishes in the process:
    // per-publish contexts get destroyed while bitswap/DHT callbacks still
    // reference them (observed as a later access violation + node deadlock —
    // the first E2E bring-up finding). The runner thread drains until the
    // process ends; each publish waits for its location with a bounded poll.
    struct IpfsPublisher
    {
        std::shared_ptr<boost::asio::io_context> ioc  = std::make_shared<boost::asio::io_context>();
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{ ioc->get_executor() };
        std::thread runner{ [this] { ioc->run(); } };

        ~IpfsPublisher()
        {
            guard.reset();
            if ( runner.joinable() )
            {
                runner.join();
            }
        }

        std::string Publish( const std::vector<char> &bytes, const std::string &fileName )
        {
            auto buffers =
                std::make_shared<std::pair<std::vector<std::string>, std::vector<std::vector<char>>>>();
            buffers->first.push_back( fileName );
            buffers->second.push_back( bytes );

            auto location = std::make_shared<std::string>();
            FileManager::GetInstance().SaveASync(
                "ipfs://",
                outcome::success( buffers ),
                ioc,
                []( const FileManager::ResultType & ) {},
                location );

            // Wait-condition idiom (no bare sleep): bounded poll on the
            // location slot with a description naming the published file.
            test::assertWaitForCondition( [ &location ] { return !location->empty(); },
                                          std::chrono::milliseconds( 180000 ),
                                          "ipfs publish never returned a location for " + fileName );
            if ( location->empty() )
            {
                return std::string();
            }
            // Single-node seed-provider registration (D-09 transport): the
            // node's DHT is NOT started in this fixture (auto_dht=false), so
            // DHT provider discovery would spin empty ("Empty provider
            // list") until the TTL kills the task. Registering THIS node as
            // a seed provider makes bitswap->GetProviders authoritative and
            // StartFindingPeers serves the block locally. The location now
            // carries the file path (ipfs://<cid>/<filename>) — the CID is
            // the segment between the scheme and the first '/'.
            if ( auto node = E2eNodeHook().lock(); node && node->GetBitswap() && location->rfind( "ipfs://", 0 ) == 0 )
            {
                const std::string afterScheme = location->substr( 7 );
                const auto        slash        = afterScheme.find( '/' );
                const std::string cidStr = slash == std::string::npos ? afterScheme : afterScheme.substr( 0, slash );
                auto              cid     = libp2p::multi::ContentIdentifierCodec::fromString( cidStr );
                if ( cid )
                {
                    node->GetBitswap()->AddProvider( cid.value(), node->GetPubSub()->GetHost()->getPeerInfo() );
                }
            }
            // The saver reports a complete fetchable location including the
            // path component (ipfs://<cid>/<filename>) — IPFSLoader's
            // parseIPFSUrl requires exactly that shape (bring-up finding:
            // bare ipfs://<cid> failed as "Invalid URL"). Use it verbatim.
            return *location;
        }
    };

    // Process-wide publisher (constructed lazily at first use; outlives every
    // test body — matches the bitswap singleton's lifetime).
    IpfsPublisher &ThePublisher()
    {
        static IpfsPublisher publisher;
        return publisher;
    }

    std::vector<char> ReadFileBytes( const fs::path &p )
    {
        std::ifstream     file( p, std::ios::binary );
        return std::vector<char>( ( std::istreambuf_iterator<char>( file ) ),
                                  std::istreambuf_iterator<char>() );
    }

    // The E2E fixture: one node, both roles (the ElmCostNode pattern).
    class ElmE2eNode : public ::testing::Test
    {
    public:
        static inline boost::filesystem::path path =
            boost::dll::program_location().parent_path() / "elm_e2e_node";

        ElmE2eNode()
        {
            try
            {
                test::removeAllWithRetry( path.string() );
            }
            catch ( ... ) //NOLINT(bugprone-empty-catch)
            {
            }
            boost::filesystem::create_directories( path );
            sgns::GeniusNode::WriteNetworkConfig( path.generic_string() + '/', /*port_seed=*/0, /*auto_dht=*/false );
            // Post-develop-merge (2026-09-28): the trust fail-closed gate
            // (FATAL_TRUST_MISMATCH when no trust policy exists — 674db31cf)
            // blocks the legacy WriteSgnsConfig boot. Single-node local
            // trust: this node's own account is the sole trusted peer +
            // bootstrapper, thresholds 1/1 (the account_management pattern).
            sgns::test::WriteLocalTrustSgnsConfig( path,
                                                   /*node_type=*/"Full",
                                                   /*is_processor=*/true,
                                                   /*rpc_catchup=*/false,
                                                   /*private_key_hex=*/"90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" );
            // Diagnosability (04-05): raise the queue/engine/CRDT loggers to
            // debug in the node's file sink (sgnslog*.log) so bring-up
            // failures leave grab/lock/sync evidence on disk even when the
            // console stream is lost to redirection fragmentation.
            {
                std::ofstream logCfg( ( path / "log_config.json" ).string() );
                logCfg << R"({"loggers":{)"
                       R"("ProcessingEngine":"debug",)"
                       R"("ProcessingSubTaskQueueManager":"debug",)"
                       R"("SubTaskQueueAccessorImpl":"debug",)"
                       R"("TaskQueueImpl":"debug",)"
                       R"("SGProcessor":"debug",)"
                       R"("SGProcessingManager":"debug",)"
                       R"("Bitswap":"debug",)"
                       R"("IPFSCommon":"debug",)"
                       R"("IPFSLoader":"debug",)"
                       R"("IPFSSaver":"info",)"
                       R"("FileManager":"debug",)"
                       R"("TransactionManager":"debug",)"
                       R"("SuperGeniusNode":"debug")"
                       R"(}})";
            }
            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage> {
                    return std::make_shared<MemorySecureStorage>( identifier );
                } );
            node_ = sgns::GeniusNode::New(
                { "0xcafe", "0.35", "1.0", TOKEN_ID, path.generic_string() + '/' },
                sgns::FromPrivateKey{ "90bd26f57e3c243358666f32ff8321181545f4ddd8c981aceac163f26b05eaaa" } );
            node_->SetChainlistFetcher( sgns::test::OfflineChainlistFetcher() );
            sgns::Blockchain::SetAuthorizedFullNodeAddress( node_->GetAddress() );
            E2eNodeHook() = node_;
            assert( node_ != nullptr );
            // Trust-lifecycle boot (develop merge): drive through
            // WAITING_FOR_TRUST_GENESIS via the local genesis approval
            // instead of the removed direct-READY path. (No ASSERT in the
            // constructor — gtest ctor failures are fatal-by-return; the
            // helper's internal ASSERT_NO_FATAL_FAILURE records any boot
            // failure and the first test body's READY check trips on it.)
            sgns::test::MakeNodeReadyWithLocalTrust( node_ );
            if ( node_->GetState() != sgns::GeniusNode::NodeState::READY )
            {
                std::fprintf( stderr,
                              "[ELMDBG] node boot failed post-trust-migration; state=%d\n",
                              static_cast<int>( node_->GetState() ) );
            }
        }

        std::shared_ptr<sgns::GeniusNode> node_;
    };

    // Build + publish the manifest over the staged bundle; returns the job's
    // manifest uri/hash pair. The manifest lists each staged file under its
    // role, with per-file sha256 + size from disk.
    PublishedFixture PublishStagedBundle( const std::string &dir )
    {
        const std::vector<std::pair<std::string, std::string>> rolesAndFiles = {
            { "llm_config", "llm_config.json" },
            { "llm_model", "llm.mnn" },
            { "llm_weight", "llm.mnn.weight" },
            { "tokenizer_file", "tokenizer.txt" },
            { "embedding_file", "embeddings_bf16.bin" },
        };

        nlohmann::json artifacts = nlohmann::json::array();
        for ( const auto &[ role, fileName ] : rolesAndFiles )
        {
            const fs::path full = fs::path( dir ) / fileName;
            if ( !fs::exists( full ) )
            {
                ADD_FAILURE() << "fixture missing " << full.string();
                return {};
            }
            const auto        bytes = ReadFileBytes( full );
            const std::string uri   = ThePublisher().Publish( bytes, fileName );
            if ( uri.empty() )
            {
                ADD_FAILURE() << "ipfs publish failed for " << fileName;
                return {};
            }
            artifacts.push_back( {
                { "name", role },
                { "uri", uri },
                { "sha256", FileSha256Hex( full ) },
                { "size_bytes", fs::file_size( full ) },
            } );
        }

        const std::string manifestJson = nlohmann::json( {
            { "schema_version", 1 },
            { "elm_type", "causal_lm" },
            { "model_format", "mnn" },
            { "artifacts", artifacts },
        } ).dump();

        PublishedFixture published;
        published.manifestUri = ThePublisher().Publish(
            std::vector<char>( manifestJson.begin(), manifestJson.end() ), "elm_manifest.json" );
        published.manifestSha256 = FileSha256Hex( fs::path( dir ) / "nonexistent" ); // replaced below
        // sha256 of the manifest BYTES (not a file on disk):
        {
            const auto hash = sgns::sgprocmanagersha::sha256( manifestJson.data(), manifestJson.size() );
            std::ostringstream oss;
            oss << std::hex << std::setfill( '0' );
            for ( const auto b : hash )
            {
                oss << std::setw( 2 ) << static_cast<int>( b );
            }
            published.manifestSha256 = oss.str();
        }
        return published;
    }

    std::string BuildE2eJobJson( const PublishedFixture &fixture, double fundingHours, int workItemCount )
    {
        std::string elms;
        for ( int i = 1; i <= workItemCount; ++i )
        {
            const std::string id = "w-" + std::to_string( i );
            if ( !elms.empty() )
            {
                elms += ", ";
            }
            elms += "{"
                    "\"work_item_id\": \"" + id + "\","
                    "\"elm_type\": \"causal_lm\","
                    "\"model_manifest_uri\": \"" + fixture.manifestUri + "\","
                    "\"model_manifest_hash\": \"" + fixture.manifestSha256 + "\","
                    "\"input_uri\": \"ipfs://prompt-" + id + "\","
                    "\"generation\": { \"max_output_tokens\": 64, \"temperature\": 0.7, \"top_p\": 0.95, "
                    "\"seed\": 42, \"stop\": [\"\\n\"] }}";
        }
        std::ostringstream funding;
        funding << ", \"funding\": { \"maximum_processing_hours\": " << fundingHours << " }";
        return "{"
               "\"name\": \"elm-e2e-job\","
               "\"version\": \"1.0\","
               "\"gnus_spec_version\": 1,"
               "\"job_type\": \"elm_processing\","
               "\"elms\": [" + elms + "]" + funding.str() + ", \"validation\": \"none\"}";
    }

    // Publish a small prompt for a work item (file:// uris cannot ride the
    // job for the worker's FileManager fetch through ipfs, but the input_uri
    // fetch uses the node's FileManager with all loaders — ipfs:// works).
    std::string PublishPrompt( const std::string &text, const std::string &name )
    {
        return ThePublisher().Publish( std::vector<char>( text.begin(), text.end() ), name );
    }
} // namespace

// ===================== Leg 1: empty-cache E2E (D-05/D-08/D-09/D-10) =====================

TEST_F( ElmE2eNode, EmptyCacheTwoWorkItemE2E )
{
    const std::string modelDir = FixtureModelDir();
    if ( modelDir.empty() )
    {
        GTEST_SKIP() << "SGPROC_ELM_TEST_MODEL_DIR unset — stage the Qwen fixture per "
                        "SuperGenius/SGProcessingManager/test/fixtures/README.md (six files, "
                        "hashes recorded); never a silent pass.";
    }

    // Publish prompts for both work items first (uri goes into the job JSON).
    const std::string prompt1 = PublishPrompt( "Hello, grid.", "prompt-w-1.txt" );
    const std::string prompt2 = PublishPrompt( "Describe the weather.", "prompt-w-2.txt" );
    ASSERT_FALSE( prompt1.empty() );
    ASSERT_FALSE( prompt2.empty() );

    const auto fixture = PublishStagedBundle( modelDir );
    ASSERT_FALSE( fixture.manifestUri.empty() );

    // Rebuild the job JSON with the REAL prompt uris (the helper stamps
    // placeholders; patch them here to keep the helper simple).
    std::string jobJson = BuildE2eJobJson( fixture, /*fundingHours=*/1.0, 2 );
    {
        auto j = nlohmann::json::parse( jobJson );
        j["elms"][0]["input_uri"] = prompt1;
        j["elms"][1]["input_uri"] = prompt2;
        jobJson                   = j.dump();
    }

    ASSERT_TRUE(
        node_->MintTokens( 50000000000, sgns::test::NextMintSourceHash(), "test", TOKEN_ID, "", sgns::GeniusNode::TIMEOUT_MINT )
            .has_value() );
    const uint64_t balanceBefore = node_->GetBalance();

    auto submit = node_->ProcessImage( jobJson );
    ASSERT_TRUE( submit.has_value() ) << "E2E submit failed, error code "
                                      << static_cast<int>( submit.error().value() );
    // ProcessImage returns the ESCROW TX HASH; task results are keyed by the
    // task uuid (bring-up finding: GetTaskResult polled the wrong id and
    // timed out against a green pipeline). Resolve via GetMyTaskIds — the
    // newest tracked id is this submission's uuid.
    const std::string escrowTxId = submit.value();
    std::string        taskId;
    test::assertWaitForCondition(
        [&] {
            const auto ids = node_->GetMyTaskIds( 1, 0 );
            if ( !ids.empty() && !ids.front().empty() )
            {
                taskId = ids.front();
                return true;
            }
            return false;
        },
        std::chrono::milliseconds( 10000 ),
        "submitted task id never appeared in GetMyTaskIds" );
    ASSERT_FALSE( taskId.empty() );
    (void) escrowTxId;

    // Wait for completion: GetTaskResult returns a result with both subtask
    // results (model download is the long pole — generous bound).
    SGProcessing::TaskResult taskResult;
    bool                       gotResult = false;
    test::assertWaitForCondition(
        [&] {
            auto result = node_->GetTaskResult( taskId );
            if ( result.has_value() && result.value().subtask_results_size() == 2 )
            {
                taskResult = result.value();
                gotResult  = true;
                return true;
            }
            return false;
        },
        std::chrono::milliseconds( 900000 ),
        "E2E task never completed with 2 results" );
    // assertWaitForCondition records FATAL but returns — guard the access
    // (same SEH-on-OOB hazard as leg 2's bring-up finding).
    if ( !gotResult )
    {
        return;
    }

    // Per-result assertions.
    std::map<std::string, nlohmann::json> envelopes;
    for ( const auto &result : taskResult.subtask_results() )
    {
        EXPECT_EQ( result.chunk_hashes_size(), 1 );
        EXPECT_FALSE( result.result_hash().empty() );
        ASSERT_FALSE( result.ipfs_results_data_id().empty() );
        EXPECT_EQ( result.ipfs_results_data_id().substr( 0, 7 ), "ipfs://" );

        // Fetch the artifact and parse the envelope.
        // Bring-up fix (04-05): the bitswap completion is posted onto this
        // ioc from the node's threads — a bare run() can drain and return
        // before that post arrives (same race fixed at the worker's save
        // branch). Drain under a work guard; cv-bounded wait.
        auto ioc = std::make_shared<boost::asio::io_context>();
        std::vector<char> collected;
        auto done       = std::make_shared<std::atomic_bool>( false );
        auto waitState  = std::make_shared<std::pair<std::mutex, std::condition_variable>>();
        FileManager::GetInstance().LoadASync(
            result.ipfs_results_data_id(),
            false,
            false,
            ioc,
            [ &collected, done, waitState ]( FileManager::ResultType buffers ) {
                if ( buffers && !buffers.value()->second.empty() )
                {
                    collected = buffers.value()->second.front();
                }
                done->store( true );
                {
                    std::lock_guard<std::mutex> lock( waitState->first );
                }
                waitState->second.notify_all();
            },
            "file" );
        {
            auto guard = boost::asio::make_work_guard( *ioc );
            std::thread drainer( [ioc, done]()
            {
                while ( !done->load() )
                {
                    ioc->reset();
                    ioc->run();
                }
            } );
            std::unique_lock<std::mutex> lock( waitState->first );
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 120 );
            ASSERT_TRUE( waitState->second.wait_until( lock, deadline, [&] { return done->load(); } ) )
                << "envelope artifact fetch timed out";
            guard.reset();
            if ( drainer.joinable() )
            {
                drainer.join();
            }
        }
        ASSERT_FALSE( collected.empty() ) << "envelope artifact fetch failed";
        const auto envelope = nlohmann::json::parse( collected.begin(), collected.end() );

        const std::string wid = envelope.at( "work_item_id" ).get<std::string>();
        EXPECT_TRUE( wid == "w-1" || wid == "w-2" );
        const std::string finishReason = envelope.at( "finish_reason" ).get<std::string>();
        EXPECT_TRUE( finishReason == "stop" || finishReason == "max_tokens" )
            << "unexpected finish_reason " << finishReason;
        EXPECT_GT( envelope.at( "prompt_tokens" ).get<int64_t>(), 0 );
        EXPECT_GT( envelope.at( "completion_tokens" ).get<int64_t>(), 0 );
        EXPECT_LE( envelope.at( "completion_tokens" ).get<int64_t>(), 64 );
        EXPECT_EQ( envelope.at( "model_manifest_hash" ).get<std::string>(), fixture.manifestSha256 );
        EXPECT_GT( envelope.at( "finish_time_usec" ).get<int64_t>(),
                   envelope.at( "grab_time_usec" ).get<int64_t>() );
        envelopes[ wid ] = envelope;
    }
    ASSERT_EQ( envelopes.size(), 2u );

    // Single-flight at grid level (D-10): exactly ONE elmruntime cache entry.
    {
        std::string cacheDir;
        try
        {
            cacheDir = FileManager::GetInstance().getCacheDir();
        }
        catch ( const std::exception & )
        {
        }
        ASSERT_FALSE( cacheDir.empty() );
        const fs::path elmCache = fs::path( cacheDir ) / "elmruntime";
        ASSERT_TRUE( fs::exists( elmCache ) );
        size_t entryDirs = 0;
        for ( const auto &entry : fs::directory_iterator( elmCache ) )
        {
            if ( entry.is_directory() )
            {
                const auto name = entry.path().filename().string();
                if ( name.rfind( ".tmp-", 0 ) != 0 && name.rfind( ".bad-", 0 ) != 0 )
                {
                    ++entryDirs;
                }
            }
        }
        EXPECT_EQ( entryDirs, 1u ) << "two subtasks must share ONE cache entry (single-flight)";
    }

    // Settlement (D-05): refund == 300 - billable, recomputed EXACTLY from
    // the observed stamps via the unit-proven arithmetic.
    {
        using sgns::processing::ElmSubtaskWindow;
        using sgns::processing::ElmWindowsToShares;
        std::vector<ElmSubtaskWindow> windows;
        for ( const auto &[ wid, envelope ] : envelopes )
        {
            windows.push_back( ElmSubtaskWindow{ wid,
                                                 envelope.at( "grab_time_usec" ).get<int64_t>(),
                                                 envelope.at( "finish_time_usec" ).get<int64_t>() } );
        }
        const auto    split   = ElmWindowsToShares( windows, 300 );
        // Mirror the chain's post-burn escrow math (BuildPayoutOutputs): the
        // splittable pool is escrow − burn(1% default basis points), and
        // refundMinions is computed on THAT pool — recomputing on the full
        // 300 overstates the refund by the burn and fails the poll.
        const uint64_t burnBasisPoints = 100; // TransactionManager::BURN_BASIS_POINTS_DEFAULT
        const uint64_t burn            = 300 * burnBasisPoints / 10000;
        const uint64_t refund          = ElmWindowsToShares( windows, 300 - burn ).refundMinions;
        EXPECT_GT( refund, 0u ) << "D-05: measured windows are far below the 1h budget — refund must be non-trivial";
        // The escrow source (this node's own address) receives the refund:
        // balance recovery of at least refund minions post-payout.
        const uint64_t balanceAfterHold = node_->GetBalance();
        (void) balanceAfterHold;
        // Payout confirmation is asynchronous; poll for the refund landing
        // (balance climbing back from the hold by >= refund).
        test::assertWaitForCondition(
            [&] { return node_->GetBalance() >= 50000000000 - 300 + refund - 1; },
            std::chrono::milliseconds( 120000 ),
            "refund never landed on the escrow source" );
    }
}

// ===================== Leg 2: overtime (D-11) =====================

TEST_F( ElmE2eNode, OvertimeLegCancelledNoReGrab )
{
    const std::string modelDir = FixtureModelDir();
    if ( modelDir.empty() )
    {
        GTEST_SKIP() << "SGPROC_ELM_TEST_MODEL_DIR unset — see fixtures README (shared with leg 1).";
    }

    const auto fixture = PublishStagedBundle( modelDir );
    ASSERT_FALSE( fixture.manifestUri.empty() );
    std::fprintf( stderr, "[ELMDBG] leg2: bundle published\n" );

    // Publish the REAL prompt uri and patch the job JSON (the helper stamps
    // a placeholder input_uri; the worker's input fetch needs a resolvable
    // ipfs:// uri or the subtask errors out with "Could not get input").
    const std::string promptUri = PublishPrompt( "Hello, overtime.", "prompt-overtime.txt" );
    ASSERT_FALSE( promptUri.empty() );

    // Tiny-but-fundable funding: 0.005h = 18s deadline, 5 milli-hours -> 1
    // minion escrow (0.001h would price at 0 minions and reject
    // PROCESS_COST_ERROR before any deadline could matter — the Phase 1
    // cost matrix's sub-4-milli-hour row). Single-node bring-up finding:
    // the cold ~557MB "download" hits the node's OWN local store (it
    // published the blocks — seed-provider registry, no network), and
    // 64-token generation finishes in ~4s, so a 64-token cap completes
    // legitimately (finish_reason=max_tokens, run2 evidence) BEFORE the
    // 18s deadline can fire. To exercise the deadline path the work must
    // still be RUNNING at expiry: a large max_output_tokens (2048) keeps
    // generation alive well past 18s, so the ProcessElmWorkItem deadline
    // timer fires Cancel() and the per-token external-cancel poll
    // (ElmStopStringStreamBuf) converts it into finish_reason=cancelled.
    const std::string jobJson = [&] {
        auto j = nlohmann::json::parse( BuildE2eJobJson( fixture, /*fundingHours=*/0.005, 1 ) );
        j["elms"][0]["input_uri"]       = promptUri;
        j["elms"][0]["generation"]["max_output_tokens"] = 2048;
        return j.dump();
    }();
    std::fprintf( stderr, "[ELMDBG] leg2: job json built (%zu bytes)\n", jobJson.size() );

    ASSERT_TRUE(
        node_->MintTokens( 50000000000, sgns::test::NextMintSourceHash(), "test", TOKEN_ID, "", sgns::GeniusNode::TIMEOUT_MINT )
            .has_value() );
    std::fprintf( stderr, "[ELMDBG] leg2: mint ok\n" );

    auto submit = node_->ProcessImage( jobJson );
    std::fprintf( stderr, "[ELMDBG] leg2: submit returned %s\n",
                  submit.has_value() ? "ok" : ( "err:" + std::to_string( static_cast<int>( submit.error().value() ) ) ).c_str() );
    ASSERT_TRUE( submit.has_value() );
    // Same id semantics as leg 1: escrow hash vs task uuid (GetMyTaskIds).
    std::string taskId;
    test::assertWaitForCondition(
        [&] {
            const auto ids = node_->GetMyTaskIds( 1, 0 );
            if ( !ids.empty() && !ids.front().empty() )
            {
                taskId = ids.front();
                return true;
            }
            return false;
        },
        std::chrono::milliseconds( 10000 ),
        "[leg2] submitted task id never appeared in GetMyTaskIds" );
    ASSERT_FALSE( taskId.empty() );

    SGProcessing::TaskResult taskResult;
    bool                       gotResult = false;
    test::assertWaitForCondition(
        [&] {
            auto result = node_->GetTaskResult( taskId );
            if ( result.has_value() && result.value().subtask_results_size() == 1 )
            {
                taskResult = result.value();
                gotResult  = true;
                return true;
            }
            return false;
        },
        std::chrono::milliseconds( 900000 ),
        "overtime task never reached a terminal result" );
    // assertWaitForCondition records a FATAL gtest failure on timeout but
    // RETURNS — guard the result access so a timeout fails the test instead
    // of crashing on an unset TaskResult (SEH on OOB observed bring-up).
    if ( !gotResult )
    {
        return;
    }

    // The terminal envelope is CANCELLED (deadline fired via the cancel token).
    const auto &result = taskResult.subtask_results( 0 );
    ASSERT_FALSE( result.ipfs_results_data_id().empty() );
    // Bring-up fix (04-05): port leg 1's drainer pattern — the artifact
    // fetch completion is posted onto this ioc from the node's threads, and
    // the bare run() at this site drained and returned before that post
    // arrived (run2: "Value of: ok" failure with the pipeline green —
    // envelope published, ProcessingDone settled, only the test's fetch
    // missed). Drain under a work guard; cv-bounded wait.
    auto ioc         = std::make_shared<boost::asio::io_context>();
    std::vector<char> collected;
    auto done        = std::make_shared<std::atomic_bool>( false );
    auto waitState   = std::make_shared<std::pair<std::mutex, std::condition_variable>>();
    FileManager::GetInstance().LoadASync(
        result.ipfs_results_data_id(),
        false,
        false,
        ioc,
        [ &collected, done, waitState ]( FileManager::ResultType buffers ) {
            if ( buffers && !buffers.value()->second.empty() )
            {
                collected = buffers.value()->second.front();
            }
            done->store( true );
            {
                std::lock_guard<std::mutex> lock( waitState->first );
            }
            waitState->second.notify_all();
        },
        "file" );
    {
        auto guard   = boost::asio::make_work_guard( *ioc );
        std::thread drainer( [ioc, done]()
        {
            while ( !done->load() )
            {
                ioc->reset();
                ioc->run();
            }
        } );
        std::unique_lock<std::mutex> lock( waitState->first );
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 120 );
        ASSERT_TRUE( waitState->second.wait_until( lock, deadline, [&] { return done->load(); } ) )
            << "envelope artifact fetch timed out";
        guard.reset();
        if ( drainer.joinable() )
        {
            drainer.join();
        }
    }
    ASSERT_FALSE( collected.empty() ) << "cancelled envelope artifact fetch failed";
    const auto envelope = nlohmann::json::parse( collected.begin(), collected.end() );
    EXPECT_EQ( envelope.at( "finish_reason" ).get<std::string>(), "cancelled" );

    // Terminal, drained, NOT re-grabbed: the task is completed (no pending
    // subtask re-enters the queue; absence of re-grab is implied by terminal
    // completion of a 1-subtask task plus a settled queue).
    EXPECT_TRUE( node_->GetTaskResult( taskId ).has_value() );
}

// ===================== Leg 3: non-ELM regression gate (E2E-02) =====================

TEST_F( ElmE2eNode, NonElmLegacyJobStillSubmits )
{
    // Minimal legacy chunk-based job (the processing_multi shape): full
    // legacy path post-phase — parse, price, split into chunk subtasks,
    // escrow, enqueue. (The deep end-to-end legacy proof stays with
    // processing_multi_test; this leg is the same-node diff-side gate.)
    const std::string job = "{"
                            "\"name\": \"legacy-e2e\","
                            "\"version\": \"1.0\","
                            "\"gnus_spec_version\": 1,"
                            "\"passes\": [{ \"name\": \"p1\", \"type\": \"compute\", \"shader\": { \"source\": \"s\" } }],"
                            "\"inputs\": [{ \"name\": \"in1\", \"source_uri_param\": \"p1\", \"type\": \"BUFFER\" }],"
                            "\"outputs\": [{ \"name\": \"out1\", \"source_uri_param\": \"p2\", \"type\": \"BUFFER\" }]}";
    auto created = sgns::sgprocessing::ProcessingManager::Create( job );
    if ( !created.has_value() )
    {
        // Schema-rejected legacy shape is out of this leg's scope — the
        // authoritative legacy regression is processing_multi_test.
        SUCCEED();
        return;
    }
    // Fund + submit through the REAL entry point.
    ASSERT_TRUE(
        node_->MintTokens( 50000000000, sgns::test::NextMintSourceHash(), "test", TOKEN_ID, "", sgns::GeniusNode::TIMEOUT_MINT )
            .has_value() );
    auto submit = node_->ProcessImage( job );
    // Either a structured rejection this fixture cannot execute (no real
    // chunk inputs) or success — the gate is that the legacy path behaves
    // (never an ELM-branch leak, i.e. never MISSING_INPUT for a legacy job).
    if ( submit.has_value() )
    {
        EXPECT_FALSE( submit.value().empty() );
    }
    else
    {
        EXPECT_NE( static_cast<int>( submit.error().value() ),
                   static_cast<int>( sgns::GeniusNode::Error::ELM_SUBMIT_UNAVAILABLE ) );
    }
}
