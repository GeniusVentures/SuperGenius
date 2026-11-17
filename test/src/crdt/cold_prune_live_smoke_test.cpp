/**
 * @file       cold_prune_live_smoke_test.cpp
 * @brief      D-08c dev-gated live-stack smoke (v1.2 Phase 14 plan
 *             14-03 Task 3): the REAL cold wire — a real MinIO community
 *             server child process, the PRODUCTION S3 client
 *             (OpenMinioS3Client) over the real TCP endpoint, real
 *             drainer spill -> flush evidence -> Accepted->Durable
 *             transitions — passes the cold-durability gate and the
 *             pruner erases (erased >= 1, cold counters zero, marker
 *             present).
 *
 *             ENV GATE (the v1.1 TEST-03 pattern,
 *             thirdparty/sgns-blockstore/docs/testing-tiers.md): the
 *             suite is dev-gated opt-in via SGNS_BLOCKSTORE_TEST_MINIO_BIN
 *             (a MinIO COMMUNITY build — NEVER the license-gated
 *             minio.exe). Without the gate the suite loud-skips with a
 *             labelled message; it NEVER runs in the Tier-1 CI lane
 *             (registered LABELS "Tier3", the cold_storage_wallclock_soak
 *             env-gated convention — Pitfall 6: no Tier1 label here).
 *
 *             A7 ruling (v1.1 Phase 11, the gc_minio_roundtrip_test
 *             precedent): real MinIO has no s3d admin listener, so the
 *             flush-evidence tier rides the InMemoryS3AdminClient fake
 *             while the S3 tier is the REAL production client over the
 *             live wire — the strongest live posture available without a
 *             real s3d deployment.
 *
 *             OPERATOR RUNBOOK (when the gate is set): the suite spawns
 *             the server itself on 127.0.0.1:19790 with credentials
 *             minio-dev/minio-dev-secret, data under
 *             test-data/cold-prune-live/. The bucket MUST exist before
 *             the live leg (the installed library surface has no
 *             MakeBucket): one-time setup per data dir —
 *               mc alias set prune-smoke http://127.0.0.1:19790 \
 *                  minio-dev minio-dev-secret
 *               mc mb --ignore-existing prune-smoke/prune-smoke
 *             (or the same via the MinIO console). A missing bucket is
 *             reported as a LOUD labelled skip, never a silent pass.
 *
 *             Posture honesty (the plan's D-08c letter): the deterministic
 *             drainer-transition stand-in is cold_prune_gate_test's LEG 1
 *             (real drainer + real AdvanceDurabilityOnce over injected
 *             clients — research A2's sanctioned stand-in, Tier-1). This
 *             smoke is the additional LIVE wire proof; when the
 *             environment cannot run it, the posture recorded is
 *             loud-skip + the deterministic leg — never silence.
 * @date       2026-11-16
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <soralog/impl/configurator_from_yaml.hpp>

#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3Client.hpp>

// Internal fake - relative-include-dir privilege (test-surface only).
#include <sgns-blockstore/InMemoryS3AdminClient.hpp>

#include "account/GeniusAccount.hpp"
#include "account/NodeType.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "blockchain/Blockchain.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/ColdStorageStack.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/wait_condition.hpp"
#include "transaction/PruneEligibility.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TxPruner.hpp"

using namespace sgns;

namespace
{
    /// The live lane's fixed topology (the runbook's contract).
    constexpr const char *kLiveHost        = "127.0.0.1";
    constexpr int          kLivePort        = 19790;
    constexpr const char *kLiveRootUser    = "minio-dev";
    constexpr const char *kLiveRootSecret  = "minio-dev-secret";
    constexpr const char *kLiveBucket      = "prune-smoke";

    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );
    constexpr char      kSeedTopic[] = "CRDT.Datastore.TEST.Channel";
    constexpr uint64_t  kWindow      = 2;
    constexpr uint64_t  kTestGeneration = 7;
    constexpr uint64_t  kSmallSettleSeconds = 1;

    /// Bounded poll loop with deadline (the suite-family discipline).
    bool PollUntil( const std::function<bool()> &predicate,
                    std::chrono::milliseconds       deadline,
                    std::chrono::milliseconds       step = std::chrono::milliseconds( 50 ) )
    {
        const auto end = std::chrono::steady_clock::now() + deadline;
        while ( std::chrono::steady_clock::now() < end )
        {
            if ( predicate() )
            {
                return true;
            }
            std::this_thread::sleep_for( step );
        }
        return predicate();
    }

    std::string LoggingConfig()
    {
        return R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: cold_prune_live_smoke_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )";
    }

    //
    // The MinIO child process (the MinioServerHarness shape, self-contained:
    // platform-split spawn/kill — the thirdparty harness TU is not
    // installed, and compiling it here would drag miniocpp includes that do
    // not propagate through the installed sgns-blockstore target).
    //
    class MinioChildProcess
    {
    public:
        ~MinioChildProcess()
        {
            Stop();
        }

        bool Start( const std::string &binary, const std::string &data_dir, std::string *error )
        {
            const std::string address = std::string( kLiveHost ) + ":" + std::to_string( kLivePort );
#ifdef _WIN32
            SetEnvironmentVariableA( "MINIO_ROOT_USER", kLiveRootUser );
            SetEnvironmentVariableA( "MINIO_ROOT_PASSWORD", kLiveRootSecret );
            std::string       command = "\"" + binary + "\" server \"" + data_dir + "\" --address " + address
                              + " --console-address :" + std::to_string( kLivePort + 1 );
            STARTUPINFOA     startup{};
            startup.cb = sizeof( startup );
            if ( !CreateProcessA( nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                  nullptr, &startup, &process_info_ ) )
            {
                if ( error )
                {
                    *error = "CreateProcess failed for the MinIO binary (GetLastError=" + std::to_string( GetLastError() )
                           + ")";
                }
                return false;
            }
            started_ = true;
            return true;
#else
            setenv( "MINIO_ROOT_USER", kLiveRootUser, 1 );
            setenv( "MINIO_ROOT_PASSWORD", kLiveRootSecret, 1 );
            pid_ = fork();
            if ( pid_ < 0 )
            {
                if ( error )
                {
                    *error = "fork failed";
                }
                return false;
            }
            if ( pid_ == 0 )
            {
                const std::string console = ":" + std::to_string( kLivePort + 1 );
                (void)signal( SIGTERM, SIG_DFL );
                (void)execl( binary.c_str(), binary.c_str(), "server", data_dir.c_str(), "--address", address.c_str(),
                             "--console-address", console.c_str(), static_cast<const char *>( nullptr ) );
                _exit( 127 ); // execl failed
            }
            started_ = true;
            return true;
#endif
        }

        void Stop()
        {
            if ( !started_ )
            {
                return;
            }
#ifdef _WIN32
            TerminateProcess( process_info_.hProcess, 1 );
            WaitForSingleObject( process_info_.hProcess, 5000 );
            CloseHandle( process_info_.hProcess );
            CloseHandle( process_info_.hThread );
#else
            kill( pid_, SIGTERM );
            int status = 0;
            waitpid( pid_, &status, 0 );
#endif
            started_ = false;
        }

    private:
        bool started_ = false;
#ifdef _WIN32
        PROCESS_INFORMATION process_info_{};
#else
        pid_t pid_ = -1;
#endif
    };

    /// Raw TCP connect probe (winsock / POSIX) — the harness's own
    /// readiness check shape, no HTTP client dependency.
    bool TcpPortAnswers( const std::string &host, int port )
    {
#ifdef _WIN32
        WSADATA wsa_data;
        if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa_data ) != 0 )
        {
            return false;
        }
        const SOCKET sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
        if ( sock == INVALID_SOCKET )
        {
            WSACleanup();
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port   = htons( static_cast<u_short>( port ) );
        inet_pton( AF_INET, host.c_str(), &address.sin_addr );
        const bool connected = connect( sock, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) == 0;
        closesocket( sock );
        WSACleanup();
        return connected;
#else
        const int fd = socket( AF_INET, SOCK_STREAM, 0 );
        if ( fd < 0 )
        {
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port   = htons( static_cast<uint16_t>( port ) );
        inet_pton( AF_INET, host.c_str(), &address.sin_addr );
        const bool connected = connect( fd, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) == 0;
        close( fd );
        return connected;
#endif
    }

    /**
     * @brief  The live smoke: one case, everything inside the env gate.
     */
    class ColdPruneLiveSmokeTest : public ::testing::Test
    {
    public:
        static void SetUpTestSuite()
        {
            auto log_system = std::make_shared<soralog::LoggingSystem>(
                std::make_shared<soralog::ConfiguratorFromYAML>(
                    std::make_shared<libp2p::log::Configurator>(), LoggingConfig() ) );
            (void)log_system->configure();
            libp2p::log::setLoggingSystem( log_system );
        }

        ~ColdPruneLiveSmokeTest() override
        {
            sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
            sgns::crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
            sgns::crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );
            if ( blockchain_ )
            {
                (void)blockchain_->Stop();
            }
            tm_.reset();
            blockchain_.reset();
            account_.reset();
            if ( db_ )
            {
                db_->ShutdownNow();
            }
            db_.reset();
            try
            {
                if ( pubs_ )
                {
                    pubs_->Stop();
                }
            }
            catch ( const std::exception &err )
            {
                std::cerr << "GossipPubSub::Stop() exception: " << err.what() << std::endl;
            }
            pubs_.reset();
            io_.reset();
            sgns::crdt::SetColdStorageStackClientsForTesting( nullptr, nullptr );
        }

    protected:
        void BuildLiveStack( const std::string &hot_dir )
        {
            io_ = std::make_shared<boost::asio::io_context>();
            keypair_path_ = "test-data/cold-prune-live/keypair";

            pubs_ = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
                crdt::KeyPairFileStorage( keypair_path_ ).GetKeyPair().value() );
            auto start_future = pubs_->Start( 0, {} );
            ASSERT_TRUE( PollUntil(
                [&] { return start_future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready; },
                std::chrono::milliseconds( 20000 ) ) );
            ASSERT_FALSE( start_future.get() );

            scheduler_ = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( pubs_->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            network_   = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(), scheduler_ );
            generator_ = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            // The LIVE wire: the PRODUCTION S3 client over the real MinIO
            // endpoint (the eager-validating factory constructs without
            // network I/O), injected through the TEST-ONLY override; the
            // admin tier is the hermetic fake (A7 — real MinIO has no s3d
            // admin listener, so flush evidence rides the fake exactly as
            // the v1.1 gc_minio_roundtrip_test rules).
            sgns::blockstore::S3ClientConfig client_config;
            client_config.endpoint  = kLiveHost;
            client_config.port      = static_cast<uint16_t>( kLivePort );
            client_config.region    = "us-east-1";
            client_config.pathStyle = true;
            client_config.bucket    = kLiveBucket;
            client_config.accessKey = kLiveRootUser;
            client_config.secretKey = kLiveRootSecret;
            auto opened_client      = sgns::blockstore::OpenMinioS3Client( client_config );
            ASSERT_TRUE( opened_client.has_value() ) << "production S3 client construction failed";
            auto live_s3 = std::shared_ptr<sgns::blockstore::S3Client>( std::move( opened_client.value() ) );

            // Bucket probe: a tiny Put succeeds only against an EXISTING
            // bucket (a missing bucket maps to TemporarilyUnavailable, and
            // Head cannot distinguish NoSuchBucket from NoSuchKey) — the
            // runbook's one-time `mc mb` step is the operator's half.
            {
                const std::string probe_key = "probe/live-smoke-ready";
                const std::vector<uint8_t> probe_bytes{ 'p', 'r', 'o', 'b', 'e' };
                auto probe = live_s3->Put( probe_key, probe_bytes, {} );
                ASSERT_TRUE( probe.has_value() )
                    << "[SGNS_COLD_PRUNE][LIVE-SMOKE] bucket '" << kLiveBucket
                    << "' not usable (create it once per data dir: mc alias set prune-smoke http://" << kLiveHost
                    << ":" << kLivePort << " " << kLiveRootUser << " " << kLiveRootSecret
                    << " && mc mb --ignore-existing prune-smoke/" << kLiveBucket << ")";
            }

            auto fake_admin = std::make_shared<sgns::blockstore::InMemoryS3AdminClient>();
            sgns::crdt::SetColdStorageStackClientsForTesting( live_s3, fake_admin );

            crdt::GlobalDB::ColdStorageOptions cold;
            cold.enabled   = true;
            cold.endpoint  = std::string( "http://" ) + kLiveHost + ":" + std::to_string( kLivePort );
            cold.bucket    = kLiveBucket;
            cold.accessKey = kLiveRootUser;
            cold.secretKey = kLiveRootSecret;
            cold.backpressure.drain_interval_ms = 100;
            cold.backpressure.warn_pending      = 100000;
            cold.backpressure.critical_pending  = 200000;
            cold.backpressure.backoff_cap_ms    = 1000;
            cold.durability.flush_min_interval_ms = 50;
            cold.durability.flush_max_interval_ms = 300;

            auto globaldb_ret = crdt::GlobalDB::New( io_,
                                                     hot_dir,
                                                     pubs_,
                                                     crdt::CrdtOptions::DefaultOptions(),
                                                     network_,
                                                     scheduler_,
                                                     generator_,
                                                     nullptr,
                                                     crdt::GlobalDB::BackupOptions{ false, 15, 12, true },
                                                     cold );
            ASSERT_TRUE( globaldb_ret.has_value() ) << "GlobalDB failed to construct over the live stack";
            db_ = std::move( globaldb_ret.value() );
            sgns::crdt::SetColdStorageStackClientsForTesting( nullptr, nullptr );

            db_->AddListenTopic( kSeedTopic );
            db_->AddBroadcastTopic( kSeedTopic );
            db_->Start();
            ASSERT_TRUE( db_->HasColdStorage() );

            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage> {
                    return std::make_shared<MemorySecureStorage>( identifier );
                } );

            account_ = sgns::GeniusAccount::New( kTestTokenId, boost::filesystem::path( hot_dir ) / "account" );
            auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
            ASSERT_TRUE( load_result.has_value() );

            blockchain_ = sgns::Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );

            constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
            constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );
            tm_ = sgns::TransactionManager::New( db_, io_, account_, blockchain_, sgns::NodeType::Light, 0,
                                                 kTimestampTolerance, kMutabilityWindow );
            ASSERT_NE( tm_, nullptr );
        }

        std::shared_ptr<TransferTransaction> MakeTransfer( uint64_t nonce, size_t output_count )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "transfer" );
            dag.set_source_addr( account_->GetAddress() );
            dag.set_nonce( nonce );
            dag.set_timestamp( static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() ) );
            std::vector<OutputDestInfo> destinations;
            for ( size_t i = 0; i < output_count; ++i )
            {
                destinations.push_back( { 100 + i, account_->GetAddress(), kTestTokenId } );
            }
            return std::make_shared<TransferTransaction>(
                TransferTransaction::New( {}, std::move( destinations ), std::move( dag ) ) );
        }

        void SeedTransaction( const std::shared_ptr<GeniusTransaction> &transaction )
        {
            const std::string tx_path = TransactionManager::GetTransactionPath( *transaction );
            crdt::GlobalDB::Buffer serialized;
            serialized.put( transaction->SerializeByteVector() );
            auto put_result = db_->Put( { tx_path }, serialized, { kSeedTopic } );
            ASSERT_TRUE( put_result.has_value() );
            crdt::GlobalDB::Buffer key_buffer;
            key_buffer.put( tx_path );
            crdt::GlobalDB::Buffer value_buffer;
            value_buffer.put( put_result.value().toString().value() );
            ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
        }

        void PutSeedValue( const std::string &key, const std::string &value )
        {
            crdt::GlobalDB::Buffer buffer;
            buffer.put( value );
            ASSERT_TRUE( db_->Put( { key }, buffer, { kSeedTopic } ).has_value() );
        }

        void TrackOutputs( const GeniusTransaction &transaction )
        {
            auto outputs = transaction.GetProducedUTXOs();
            ASSERT_TRUE( outputs.has_value() );
            for ( const auto &output : *outputs )
            {
                auto put = account_->GetUTXOManager().PutUTXO( output, output.GetOwnerAddress() );
                ASSERT_TRUE( put.has_value() );
                ASSERT_TRUE( put.value() );
            }
        }

        bool ConsumeOutput( const GeniusTransaction &transaction, uint32_t output_idx, const GeniusTransaction &spender )
        {
            auto txid = base::Hash256::fromReadableString( transaction.GetHash() );
            auto consumed = account_->GetUTXOManager().ConsumeUTXOs( { { txid.value(), output_idx, {} } },
                                                                     account_->GetAddress(),
                                                                     UTXOManager::UTXOType::UTXO_NORMAL,
                                                                     base::Hash256::fromReadableString( spender.GetHash() )
                                                                         .value() );
            return consumed.has_value() && consumed.value();
        }

        std::string SnapshotMapEntry( const std::string &tx_path )
        {
            auto entry = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( tx_path ) );
            return entry.has_value() ? std::string( entry.value().toString() ) : std::string( "absent" );
        }

        std::shared_ptr<boost::asio::io_context>                 io_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>         pubs_;
        std::shared_ptr<libp2p::basic::SchedulerImpl>            scheduler_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network> network_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        std::shared_ptr<crdt::GlobalDB>                          db_;
        std::string                                              keypair_path_;
        std::shared_ptr<GeniusAccount>                           account_;
        std::shared_ptr<Blockchain>                              blockchain_;
        std::shared_ptr<TransactionManager>                      tm_;
    };

    /**
     * The live leg: real MinIO server (child process) + the production S3
     * client + the real drainer/flush cadence -> the block reaches REAL
     * Durable over the wire -> the gate admits -> the pruner erases with
     * every cold counter zero and the /prune/ marker present.
     */
    TEST_F( ColdPruneLiveSmokeTest, LiveStack_RealDrainerTransitionsPassTheGate )
    {
        const char *binary = std::getenv( "SGNS_BLOCKSTORE_TEST_MINIO_BIN" );
        if ( binary == nullptr || std::string( binary ).empty() )
        {
            GTEST_SKIP() << "[SGNS_COLD_PRUNE][LIVE-SMOKE] MinIO binary not provided (set "
                            "SGNS_BLOCKSTORE_TEST_MINIO_BIN; COMMUNITY build — never the license-gated "
                            "minio.exe; runbook: thirdparty/sgns-blockstore/docs/testing-tiers.md). "
                            "Deterministic stand-in: cold_prune_gate_test LEG 1 (Tier-1, real drainer "
                            "transitions over injected clients).";
        }

        const std::string data_dir = "test-data/cold-prune-live/minio-data";
        std::error_code   ec;
        std::filesystem::create_directories( data_dir, ec );

        MinioChildProcess server;
        std::string       spawn_error;
        ASSERT_TRUE( server.Start( binary, data_dir, &spawn_error ) ) << spawn_error;

        ASSERT_TRUE( PollUntil( [&] { return TcpPortAnswers( kLiveHost, kLivePort ); },
                                std::chrono::milliseconds( 30000 ) ) )
            << "MinIO server never answered on " << kLiveHost << ":" << kLivePort;

        const std::string hot_dir = "test-data/cold-prune-live/hot";
        std::filesystem::remove_all( hot_dir, ec );
        BuildLiveStack( hot_dir );
        ASSERT_NE( db_, nullptr );

        // The RealPrune seeding shape (LEG 1's recipe, over the live wire).
        auto spender    = MakeTransfer( 1, 0 );
        auto eligible   = MakeTransfer( 2, 1 );
        auto unspent    = MakeTransfer( 3, 2 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/live/driver-1", "d1" );
        SeedTransaction( eligible );
        SeedTransaction( unspent );

        TrackOutputs( *eligible );
        TrackOutputs( *unspent );
        ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
        PutSeedValue( "/seed/live/driver-2", "d2" );

        const std::string tx_path = TransactionManager::GetTransactionPath( *eligible );
        const std::string cid_str = SnapshotMapEntry( tx_path );
        ASSERT_NE( cid_str, "absent" );
        auto target_cid = CID::fromString( cid_str );
        ASSERT_FALSE( target_cid.has_error() );

        // REAL transitions over the live wire: drainer Put -> real MinIO
        // (Accepted), fake-admin flush evidence (A7) + the fast cadence ->
        // Durable. Generous live deadline (first-boot MinIO can be slow).
        ASSERT_TRUE( PollUntil( [&] { return db_->GetColdUploadState( target_cid.value() ) ==
                                      crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 60000 ) ) )
            << "eligible block never reached Durable over the live wire (cid " << cid_str << ")";

        sgns::PruningConfig config;
        config.enabled              = true;
        config.dry_run              = false;
        config.finality_window      = kWindow;
        config.max_erases_per_cycle = 10;
        config.settle_seconds       = kSmallSettleSeconds;
        auto pruner                 = TxPruner::New( NodeType::Light, config );
        ASSERT_TRUE( pruner.has_value() );

        // Drive the tick gate and one settled cycle.
        {
            bool cycle_failed = false;
            bool ran          = false;
            PruneCycleStats   stats;
            ASSERT_WAIT_FOR_CONDITION(
                [&]
                {
                    auto result = pruner.value()->RunCycle( *db_, *tm_, account_->GetUTXOManager(), kTestGeneration );
                    if ( result.has_error() )
                    {
                        cycle_failed = true;
                        return true;
                    }
                    stats = result.value();
                    ran   = pruner.value()->GetPruneStatus().watermark.has_value();
                    return ran;
                },
                std::chrono::milliseconds( kSmallSettleSeconds * 1000 + 8000 ),
                "live tick gate settles and one prune cycle runs",
                nullptr );
            ASSERT_FALSE( cycle_failed );
            ASSERT_TRUE( ran );

            // The live contract: the REAL Durable evidence admits and
            // erases; every cold counter zero; no error; marker present.
            EXPECT_EQ( stats.erased, 1u );
            EXPECT_EQ( stats.cold_deferred_groups, 0u );
            EXPECT_EQ( stats.cold_unreadable_groups, 0u );
            EXPECT_EQ( stats.errors, 0u );
        }
        EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );

        server.Stop();
    }
} // namespace
