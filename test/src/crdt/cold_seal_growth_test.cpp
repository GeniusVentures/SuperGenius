/**
 * @file       cold_seal_growth_test.cpp
 * @brief      Seal-growth probe harness (SEAL-01 / SC1-SC3, v1.2 Phase 17
 *             plan 17-01 Task 1) - the phase's measurement instrument
 *             (D-08): minimal-stack hot store, sanctioned seal seeding at
 *             knobbed scale, raw-iterator suffix-filter enumeration with
 *             full per-row bytes/timing capture, and flat JSON evidence
 *             emission (D-09).
 *
 *             This suite runs the bounded 10^6 Tier-1 floor leg UNGATED
 *             (D-07): it is a first-class member of the fail-closed
 *             ctest -L Tier1 lane. The dev-gated big legs (10^6 -> 10^7
 *             growing dataset, cold-cache halves, fidelity/density
 *             fixtures) arrive with plan 17-02 and will be env-gated
 *             with a loud labelled skip - NEVER in this lane.
 *
 *             The seal row measured here is the vendored
 *             RocksdbDatastore::seal row (an EMPTY-valued
 *             <cid>+"sealed" text key in the hot store's shared default
 *             column family). GATE-03/D-C1 discipline: the seal-suffix
 *             literal's ONLY occurrence in this file is inside the
 *             enumeration suffix comparison below - no key is ever
 *             built by appending the suffix, and seeding goes ONLY
 *             through the vendored seal() surface (the
 *             IPLDNodeImpl::createFromString ghost-CID precedent: real
 *             content-addressed CIDs whose bytes exist nowhere).
 *
 *             Posture (governing invariant 3): read/count/time only -
 *             no row deletion, no compaction call, no key re-shaping on
 *             any measured store. The per-run floor fixture root is
 *             wiped BEFORE creation (Tier-1 determinism - setup, never
 *             a store operation); the persistent growing root is
 *             created empty and NEVER wiped (17-02's D-02 dataset).
 *
 *             Evidence-is-stdout: status lines printed during each leg
 *             are the human-readable evidence; the machine-readable
 *             flat JSON is written to evidence-floor.json under the
 *             floor root AND echoed to stdout beside them.
 *
 * Env gates (NAMES only - never values in committed captures; integers
 * only, the V5 EnvOr posture):
 *   SGNS_SEAL_SCALE    - seals to seed in the floor leg (default 1000000)
 *   SGNS_SEAL_REPEATS  - enumeration passes (default 3; variance input
 *                        for 17-03's D-12 band)
 *   SGNS_SEAL_SEED     - dataset seed baked into every content string
 *                        (default 20261118)
 *   SGNS_SEAL_DEPTH    - deep-chain depth (default 64; also the fidelity
 *                        walk-chain length - 17-02)
 *   SGNS_SEAL_FANOUT   - wide fan-out breadth (default 8 - 17-02)
 *   SGNS_SEAL_MIX      - mixed-shape chain percent (default 50 - 17-02)
 *
 * Hermetic: no S3, no node, no network beyond the loopback pubsub host
 * the fidelity fixture's GlobalDB stack needs (17-02); the volume legs
 * are one hot RocksDB fixture plus nothing.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <soralog/impl/configurator_from_yaml.hpp>

#include "crdt/crdt_datastore.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "transaction/PruneEligibility.hpp"

#include "storage/rocksdb/rocksdb.hpp"

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>

namespace
{
    using sgns::storage::rocksdb;

    /// The link/destination name tying fixture DAG links together (any
    /// stable string; the fixtures are never broadcast or walked via
    /// topic routing - only the CRDT chain is).
    constexpr const char *kFidelityTopic  = "seal-growth-fidelity";
    constexpr const char *kFidelityLink   = "seal-growth-link";
    constexpr const char *kFloorRoot      = "test-data/cold-seal-growth/floor";
    constexpr const char *kGrowRoot       = "test-data/cold-seal-growth/grow";
    constexpr const char *kFidelityHotRoot  = "test-data/cold-seal-growth/fidelity-hot";
    constexpr const char *kFidelityTwinRoot = "test-data/cold-seal-growth/fidelity-twin";
    constexpr const char *kFidelityKeyRoot  = "test-data/cold-seal-growth/fidelity-keypair";
    constexpr const char *kDensityRoot      = "test-data/cold-seal-growth/density";

    /// Seals to seed in the floor leg (D-08 knob; bounded for Tier-1).
    uint64_t EnvOr( const char *name, uint64_t fallback )
    {
        const char *raw = std::getenv( name );
        return raw == nullptr || *raw == '\0'
                   ? fallback
                   : static_cast<uint64_t>( std::strtoull( raw, nullptr, 10 ) );
    }

    /// Boolean env-gate companion (the big legs arriving in 17-02 gate
    /// on presence; carried now so the knob surface is complete).
    [[maybe_unused]] bool EnvSet( const char *name )
    {
        const char *raw = std::getenv( name );
        return raw != nullptr && *raw != '\0';
    }

    /// Fixed fixture roots under the suite's test-data base (NOT
    /// FreshDir): the floor root is wiped per run (determinism); the
    /// grow root is the persistent 17-02 D-02 dataset - created empty,
    /// never wiped, never enumerated by this plan; the fidelity roots
    /// are per-run comparison fixtures (both wiped; the twin exists so
    /// the byte-identity comparison spans two structurally distinct
    /// stores). The constants live in the header block above.

    /// The sanctioned bounded-poll helper (the soak/wall-clock shape).
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

    /// Soralog config for the GlobalDB stack (the cold-kit YAML shape).
    std::string LoggingConfigYaml()
    {
        return R"(
sinks:
  - name: console
    type: console
    capacity: 4294967295
    buffer: 16384
    lazy_flush: true
groups:
  - name: main
    sink: console
    level: error
    children:
      - name: libp2p
        level: error
      - name: Gossip
        level: error
# ----------------
  )";
    }

    /// One-shot soralog bootstrap (the cold-kit SetUpTestSuite shape).
    bool EnsureLoggingSystem()
    {
        static bool configured = []
        {
            auto log_system = std::make_shared<soralog::LoggingSystem>(
                std::make_shared<soralog::ConfiguratorFromYAML>(
                    std::make_shared<libp2p::log::Configurator>(), LoggingConfigYaml() ) );
            (void)log_system->configure();
            libp2p::log::setLoggingSystem( log_system );
            return true;
        }();
        return configured;
    }

    /// Wipes (when @p wipe) and creates a fixture directory.
    void PrepareDir( const std::string &dir, bool wipe )
    {
        if ( wipe )
        {
            std::error_code ec;
            std::filesystem::remove_all( dir, ec );
        }
        std::error_code mk;
        std::filesystem::create_directories( dir, mk );
    }

    /// The concrete DAG syncer behind the GlobalDB's broadcaster (the
    /// cold_evidence_matrix_test syncerOf accessor idiom) - the surface
    /// whose markResolved is the walk's own sealing surface (GATE-03's
    /// merkle-dag alias; the W3 precedent's seeding path).
    std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> SyncerOf( const std::shared_ptr<sgns::crdt::GlobalDB> &db )
    {
        return std::static_pointer_cast<sgns::crdt::GraphsyncDAGSyncer>( db->GetBroadcaster()->GetDagSyncer() );
    }

    /// Hermetic GlobalDB stack (cold DISABLED - the fidelity/density
    /// legs need no cold tier), the Phase-14 kit's construction reduced
    /// to the CRDT core: loopback pubsub host + scheduler + graphsync
    /// network + GlobalDB. NO account, NO blockchain, NO transaction
    /// manager - puts and walks are pure CRDT-surface operations.
    struct GlobalDbStack
    {
        std::shared_ptr<boost::asio::io_context>                           io;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                   pubs;
        std::shared_ptr<libp2p::basic::SchedulerImpl>                      scheduler;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>         network;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator;
        std::shared_ptr<sgns::crdt::GlobalDB>                              db;
    };

    /// Teardown order (the CRDTFixture invariant): the GlobalDB is
    /// released BEFORE the pubsub host stops, so ~BasicHost never
    /// deregisters connections from a db-backed handle.
    void TeardownGlobalDbStack( GlobalDbStack &stack )
    {
        if ( stack.db )
        {
            stack.db->ShutdownNow();
        }
        stack.db.reset();
        stack.generator.reset();
        stack.network.reset();
        stack.scheduler.reset();
        try
        {
            if ( stack.pubs )
            {
                stack.pubs->Stop();
            }
        }
        catch ( const std::exception &err )
        {
            std::cerr << "GossipPubSub::Stop() exception: " << err.what() << std::endl;
        }
        stack.pubs.reset();
        stack.io.reset();
    }

    bool BuildGlobalDbStack( const std::string &hot_dir, GlobalDbStack &out )
    {
        EnsureLoggingSystem();
        PrepareDir( hot_dir, /*wipe=*/true );
        PrepareDir( kFidelityKeyRoot, /*wipe=*/true );
        // The key FILE path (nonexistent - GetKeyPair then generates and
        // writes it; a pre-existing DIRECTORY here would send the reader
        // down the load path against a directory and fail).
        const std::string keypair_file = std::string( kFidelityKeyRoot ) + "/key";

        out.io   = std::make_shared<boost::asio::io_context>();
        out.pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
            sgns::crdt::KeyPairFileStorage( keypair_file ).GetKeyPair().value() );
        auto start_future = out.pubs->Start( 0, {} );
        if ( !PollUntil( [&] { return start_future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready; },
                          std::chrono::milliseconds( 20000 ) ) )
        {
            return false;
        }
        if ( start_future.get() )
        {
            return false;
        }
        out.scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( out.pubs->GetAsioContext() ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
        out.network   = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( out.pubs->GetHost(), out.scheduler );
        out.generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

        // Cold DISABLED (default options) - no tier, no fakes, no seam.
        sgns::crdt::GlobalDB::ColdStorageOptions cold;
        auto globaldb_ret = sgns::crdt::GlobalDB::New( out.io,
                                                       hot_dir,
                                                       out.pubs,
                                                       sgns::crdt::CrdtOptions::DefaultOptions(),
                                                       out.network,
                                                       out.scheduler,
                                                       out.generator,
                                                       nullptr,
                                                       sgns::crdt::GlobalDB::BackupOptions{ false, 15, 12, true },
                                                       cold );
        if ( !globaldb_ret.has_value() )
        {
            return false;
        }
        out.db = std::move( globaldb_ret.value() );
        out.db->AddListenTopic( kFidelityTopic );
        out.db->AddBroadcastTopic( kFidelityTopic );
        (void)out.db->Start();
        return out.db->HasColdStorage() == false;
    }

    /// One knobbed DAG-spectrum fixture (D-05; new code by design - no
    /// in-repo analog, 17-PATTERNS). Three named shapes, deterministic
    /// from the seed (content strings carry shape+seed+index; the W3
    /// ghost-CID precedent - bytes exist nowhere). Single root per
    /// shape; ancestry is every node (known exactly by construction -
    /// the D-06/Pitfall-7 denominator).
    struct SpectrumFixture
    {
        std::string              shape;
        std::vector<sgns::CID>   cids;             // emission order (the deterministic selection order)
        std::vector<std::string> cid_strings;
        std::vector<uint64_t>    node_generation;  // per-node depth from the root (the D-06 generation tag)
        uint64_t                 ancestry_size = 0;
        uint64_t                 max_depth     = 0;
        uint64_t                 max_breadth   = 0;
    };

    std::shared_ptr<sgns::ipfs_lite::ipld::IPLDNode> MakeFixtureNode( const std::string &shape,
                                                                      uint64_t           seed,
                                                                      uint64_t           idx )
    {
        std::ostringstream content;
        content << "seal-growth spectrum shape=" << shape << " seed=" << seed << " idx=" << idx;
        return sgns::ipfs_lite::ipld::IPLDNodeImpl::createFromString( content.str() );
    }

    SpectrumFixture GenerateSpectrumFixture( const std::string &shape,
                                             uint64_t           depth,
                                             uint64_t           fanout,
                                             uint64_t           mix_pct,
                                             uint64_t           seed )
    {
        SpectrumFixture fixture;
        fixture.shape = shape;
        std::vector<std::shared_ptr<sgns::ipfs_lite::ipld::IPLDNode>> nodes;

        auto append_node = [&]( uint64_t generation )
        {
            auto node = MakeFixtureNode( shape, seed, nodes.size() );
            nodes.push_back( node );
            fixture.node_generation.push_back( generation );
            if ( generation > fixture.max_depth )
            {
                fixture.max_depth = generation;
            }
        };

        if ( shape == "deep" )
        {
            // A chain of @p depth nodes: node 0 is the root, node i
            // links to node i-1 (single head at node depth-1).
            append_node( 0 );
            for ( uint64_t i = 1; i < depth; ++i )
            {
                append_node( i );
                const auto parent_cid = nodes[i - 1]->getCID();
                auto parent_bytes     = parent_cid.toBytes();
                nodes[i]->addLink( sgns::ipfs_lite::ipld::IPLDLink( parent_cid, kFidelityLink, parent_bytes.value().size() ) );
            }
            fixture.max_breadth = 1;
        }
        else if ( shape == "wide" )
        {
            // Root with @p fanout branches, each with @p fanout
            // children (depth 2, breadth fanout).
            append_node( 0 );
            const auto root_cid = nodes[0]->getCID();
            auto root_bytes     = root_cid.toBytes();
            for ( uint64_t j = 0; j < fanout; ++j )
            {
                append_node( 1 );
                nodes.back()->addLink( sgns::ipfs_lite::ipld::IPLDLink( root_cid, kFidelityLink, root_bytes.value().size() ) );
                const auto branch_cid = nodes.back()->getCID();
                auto branch_bytes     = branch_cid.toBytes();
                for ( uint64_t k = 0; k < fanout; ++k )
                {
                    append_node( 2 );
                    nodes.back()->addLink( sgns::ipfs_lite::ipld::IPLDLink( branch_cid, kFidelityLink, branch_bytes.value().size() ) );
                }
            }
            fixture.max_breadth = fanout;
        }
        else // mixed
        {
            // Root carrying BOTH a deep chain (mix% of depth links) and
            // a wide fan (@p fanout direct branches): the mix knob is
            // the chain fraction of the deep half.
            append_node( 0 );
            const auto root_cid = nodes[0]->getCID();
            auto root_bytes     = root_cid.toBytes();
            const uint64_t chain_len = std::max<uint64_t>( 1, depth * mix_pct / 100 );
            for ( uint64_t i = 0; i < chain_len; ++i )
            {
                append_node( i + 1 );
                const auto parent_cid = i == 0 ? root_cid : nodes[nodes.size() - 2]->getCID();
                auto parent_bytes     = parent_cid.toBytes();
                nodes.back()->addLink( sgns::ipfs_lite::ipld::IPLDLink( parent_cid, kFidelityLink, parent_bytes.value().size() ) );
            }
            for ( uint64_t j = 0; j < fanout; ++j )
            {
                append_node( 1 );
                nodes.back()->addLink( sgns::ipfs_lite::ipld::IPLDLink( root_cid, kFidelityLink, root_bytes.value().size() ) );
            }
            fixture.max_breadth = fanout + 1;
        }

        fixture.ancestry_size = nodes.size();
        fixture.cids.reserve( nodes.size() );
        fixture.cid_strings.reserve( nodes.size() );
        for ( const auto &node : nodes )
        {
            fixture.cids.push_back( node->getCID() );
            fixture.cid_strings.push_back( node->getCID().toString().value() );
        }
        return fixture;
    }

    /// Suffix-scan of one hot store's seal rows (the SAME sanctioned
    /// comparison the enumeration leg uses): returns the exact key-byte
    /// set and sets @p all_values_empty false if ANY seal row carries a
    /// non-empty value (values are empty by the vendored row shape).
    std::set<std::string> ScanSealKeys( const std::shared_ptr<rocksdb> &hot, bool *all_values_empty )
    {
        std::set<std::string> keys;
        *all_values_empty = true;
        auto db_handle = hot->getDB();
        std::unique_ptr<ROCKSDB_NAMESPACE::Iterator> it(
            db_handle->NewIterator( ROCKSDB_NAMESPACE::ReadOptions() ) );
        for ( it->SeekToFirst(); it->Valid(); it->Next() )
        {
            const auto k = it->key().ToString();
            if ( k.size() > 6 && k.compare( k.size() - 6, 6, "sealed" ) == 0 )
            {
                keys.insert( k );
                if ( it->value().size() != 0 )
                {
                    *all_values_empty = false;
                }
            }
        }
        return keys;
    }

    /// Minimal hot-store stack (D-08): plain storage::rocksdb ->
    /// vendored IpfsRocksDb::create(getDB()) -> vendored
    /// RocksdbDatastore. NO GlobalDB, NO node - the growing-DB posture
    /// mirrors the production construction (globaldb.cpp:815-822) and
    /// the seven production RocksDB options (globaldb.cpp:690-697) are
    /// set verbatim so the measured store shape matches a real node.
    struct MinimalSealStack
    {
        std::shared_ptr<rocksdb>                              hot;
        std::shared_ptr<sgns::ipfs_lite::ipfs::RocksdbDatastore> datastore;
    };

    bool BuildMinimalStack( const std::string &dir, bool wipe, MinimalSealStack &out )
    {
        if ( wipe )
        {
            std::error_code ec;
            std::filesystem::remove_all( dir, ec );
        }
        std::error_code mk;
        std::filesystem::create_directories( dir, mk );

        rocksdb::Options options;
        // The seven production options (globaldb.cpp:690-697, verbatim):
        options.create_if_missing                    = true;
        options.target_file_size_base                = 32 * 1024 * 1024;
        options.max_compaction_bytes                 = 32 * 1024 * 1024;
        options.write_buffer_size                    = 32 * 1024 * 1024;
        options.level0_file_num_compaction_trigger   = 1;
        options.target_file_size_multiplier          = 1;
        options.level_compaction_dynamic_level_bytes = false;

        auto opened_hot = rocksdb::create( dir, options );
        if ( !opened_hot.has_value() )
        {
            return false;
        }
        out.hot = std::move( opened_hot.value() );

        auto ipfs_db = sgns::ipfs_lite::rocksdb::create( out.hot->getDB() );
        if ( !ipfs_db.has_value() )
        {
            return false;
        }
        out.datastore = std::make_shared<sgns::ipfs_lite::ipfs::RocksdbDatastore>( ipfs_db.value() );
        return true;
    }

    /// One warm-cache enumeration pass over the shared default CF with
    /// the six-byte seal-suffix comparison - the ONLY sanctioned home
    /// of the suffix literal in this file. The wrapper's query(prefix)
    /// materializes a std::map and is unusable at this scale, so the
    /// raw getDB() iterator is the streaming surface (Pitfall 1: seals
    /// share the CF with block keys and CRDT rows; the suffix scan IS
    /// the cost a maintenance sweep would pay today).
    struct EnumPass
    {
        uint64_t seals             = 0;
        uint64_t others            = 0;
        uint64_t seal_bytes_total  = 0;
        double   wall_ms           = 0.0;
        double   rows_per_sec      = 0.0;
        double   per_row_bytes     = 0.0;
    };

    EnumPass RunEnumerationPass( const std::shared_ptr<rocksdb> &hot )
    {
        EnumPass pass;
        auto    db_handle = hot->getDB();
        std::unique_ptr<ROCKSDB_NAMESPACE::Iterator> it(
            db_handle->NewIterator( ROCKSDB_NAMESPACE::ReadOptions() ) );
        const auto t0 = std::chrono::steady_clock::now();
        for ( it->SeekToFirst(); it->Valid(); it->Next() )
        {
            const auto k = it->key().ToString();
            if ( k.size() > 6 && k.compare( k.size() - 6, 6, "sealed" ) == 0 )
            {
                ++pass.seals;
                pass.seal_bytes_total += k.size() + it->value().size();
            }
            else
            {
                ++pass.others;
            }
        }
        const auto elapsed = std::chrono::steady_clock::now() - t0;
        pass.wall_ms = std::chrono::duration<double, std::milli>( elapsed ).count();
        const double seconds = pass.wall_ms / 1000.0;
        pass.rows_per_sec    = seconds > 0.0 ? static_cast<double>( pass.seals + pass.others ) / seconds : 0.0;
        pass.per_row_bytes   = pass.seals > 0 ? static_cast<double>( pass.seal_bytes_total ) / static_cast<double>( pass.seals ) : 0.0;
        return pass;
    }

    /// Hand-rolled flat JSON writer (D-09; research A5 fallback - no
    /// third-party JSON library): dotted-key flat pairs, strings
    /// escaped, numbers rendered without locale decoration.
    class FlatJsonEvidence
    {
    public:
        void Add( const std::string &key, const std::string &value )
        {
            entries_.emplace_back( key, "\"" + Escape( value ) + "\"" );
        }

        void Add( const std::string &key, const char *value )
        {
            Add( key, std::string( value ) );
        }

        void Add( const std::string &key, uint64_t value )
        {
            entries_.emplace_back( key, std::to_string( value ) );
        }

        void Add( const std::string &key, double value )
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision( 3 ) << value;
            entries_.emplace_back( key, oss.str() );
        }

        std::string ToString() const
        {
            std::ostringstream oss;
            oss << "{";
            for ( size_t i = 0; i < entries_.size(); ++i )
            {
                if ( i > 0 )
                {
                    oss << ", ";
                }
                oss << "\"" << entries_[i].first << "\": " << entries_[i].second;
            }
            oss << "}";
            return oss.str();
        }

    private:
        static std::string Escape( const std::string &s )
        {
            std::string out;
            out.reserve( s.size() );
            for ( const char c : s )
            {
                switch ( c )
                {
                    case '"':
                        out += "\\\"";
                        break;
                    case '\\':
                        out += "\\\\";
                        break;
                    case '\n':
                        out += "\\n";
                        break;
                    case '\r':
                        out += "\\r";
                        break;
                    case '\t':
                        out += "\\t";
                        break;
                    default:
                        if ( static_cast<unsigned char>( c ) < 0x20 )
                        {
                            std::ostringstream oss;
                            oss << "\\u" << std::hex << std::setw( 4 ) << std::setfill( '0' )
                                << static_cast<int>( static_cast<unsigned char>( c ) );
                            out += oss.str();
                        }
                        else
                        {
                            out += c;
                        }
                        break;
                }
            }
            return out;
        }

        std::vector<std::pair<std::string, std::string>> entries_;
    };

    std::string HostTag()
    {
        const char *n = std::getenv( "COMPUTERNAME" );
        if ( n != nullptr && *n != '\0' )
        {
            return n;
        }
        n = std::getenv( "HOSTNAME" );
        if ( n != nullptr && *n != '\0' )
        {
            return n;
        }
        return "unknown";
    }

    std::string OsTag()
    {
#if defined( _WIN32 )
        return "windows";
#elif defined( __APPLE__ )
        return "macos";
#elif defined( __linux__ )
        return "linux";
#else
        return "other";
#endif
    }

    std::string BuildConfigTag()
    {
#if defined( NDEBUG )
        return "release";
#else
        return "debug";
#endif
    }

    std::string GeneratedUtc()
    {
        const auto now = std::chrono::system_clock::now();
        const auto tt  = std::chrono::system_clock::to_time_t( now );
        tm       parts{};
#ifdef _WIN32
        gmtime_s( &parts, &tt );
#else
        gmtime_r( &tt, &parts );
#endif
        char buf[32];
        strftime( buf, sizeof( buf ), "%Y-%m-%dT%H:%M:%SZ", &parts );
        return buf;
    }

    /// The bounded Tier-1 floor leg (D-07): seed SGNS_SEAL_SCALE seals
    /// through the sanctioned surface on a freshly-wiped floor root,
    /// enumerate SGNS_SEAL_REPEATS times via the raw suffix-filter
    /// iterator, assert the exact-count edge (seals == N, others == 0 -
    /// the PROBE-LIFT empty-intersection edge of the shared-CF scan),
    /// and emit the evidence JSON.
    TEST( Floor, VolumeEnumeration10e6 )
    {
        const uint64_t scale   = EnvOr( "SGNS_SEAL_SCALE", 1000000 );
        const uint64_t repeats = EnvOr( "SGNS_SEAL_REPEATS", 3 );
        const uint64_t seed    = EnvOr( "SGNS_SEAL_SEED", 20261118 );
        ASSERT_GT( scale, 0 ) << "SGNS_SEAL_SCALE must be positive";
        ASSERT_GE( repeats, 1 ) << "SGNS_SEAL_REPEATS must be at least 1";

        // The persistent grow root: create-if-missing, NEVER wiped,
        // never opened by this leg (17-02's D-02 growing dataset).
        std::error_code mk_grow;
        std::filesystem::create_directories( kGrowRoot, mk_grow );
        ASSERT_TRUE( std::filesystem::exists( kGrowRoot ) ) << "grow root must exist";

        MinimalSealStack stack;
        ASSERT_TRUE( BuildMinimalStack( kFloorRoot, /*wipe=*/true, stack ) )
            << "minimal hot-store stack failed to build";

        // ---- Seeding leg (sanctioned path only, per GATE-03/D-C1) ----
        std::string first_cid;
        std::string last_cid;
        std::cout << "[seal-growth] seeding " << scale << " seals (seed=" << seed
                  << ") through the vendored seal surface..." << std::endl;
        const auto seed_t0 = std::chrono::steady_clock::now();
        for ( uint64_t i = 0; i < scale; ++i )
        {
            std::ostringstream content;
            content << "seal-growth probe seed=" << seed << " idx=" << i;
            auto node = sgns::ipfs_lite::ipld::IPLDNodeImpl::createFromString( content.str() );
            ASSERT_TRUE( node != nullptr ) << "CID generation failed at idx=" << i;
            const auto cid = node->getCID();
            if ( i == 0 )
            {
                first_cid = cid.toString().value();
            }
            else if ( i == scale - 1 )
            {
                last_cid = cid.toString().value();
            }
            auto sealed = stack.datastore->seal( cid );
            ASSERT_TRUE( sealed.has_value() ) << "seal() failed at idx=" << i;
        }
        const auto seed_elapsed = std::chrono::steady_clock::now() - seed_t0;
        const double  seed_ms = std::chrono::duration<double, std::milli>( seed_elapsed ).count();
        const double  seed_per_sec = seed_ms > 0.0 ? static_cast<double>( scale ) / ( seed_ms / 1000.0 ) : 0.0;
        std::cout << "[seal-growth] seeded " << scale << " seals in " << std::fixed
                  << std::setprecision( 1 ) << seed_ms << " ms (" << std::setprecision( 0 )
                  << seed_per_sec << " seals/sec; rate is generation-inclusive - a "
                  << "conservative floor of the pure seal rate)" << std::endl;

        // ---- Enumeration leg (D-03 warm half, repeated for variance) ----
        std::vector<EnumPass> passes;
        double                rows_per_sec_sum = 0.0;
        for ( uint64_t r = 0; r < repeats; ++r )
        {
            EnumPass pass = RunEnumerationPass( stack.hot );
            std::cout << "[seal-growth] enum pass " << ( r + 1 ) << "/" << repeats
                      << ": seals=" << pass.seals << " others=" << pass.others
                      << " wall_ms=" << std::fixed << std::setprecision( 1 ) << pass.wall_ms
                      << " rows/sec=" << std::setprecision( 0 ) << pass.rows_per_sec
                      << " per_row_bytes=" << std::setprecision( 2 ) << pass.per_row_bytes
                      << std::endl;
            // Exact-count contract on the clean floor root: the PROBE-LIFT
            // empty-intersection edge (SC1's shared-CF denominator is zero
            // here; the fidelity/density fixtures of 17-02 populate it).
            ASSERT_EQ( pass.seals, scale ) << "seal count mismatch on pass " << ( r + 1 );
            ASSERT_EQ( pass.others, 0 ) << "non-seal rows in a clean floor root on pass " << ( r + 1 );
            rows_per_sec_sum += pass.rows_per_sec;
            passes.push_back( pass );
        }
        const double rows_per_sec_mean = rows_per_sec_sum / static_cast<double>( repeats );
        double       max_deviation_pct = 0.0;
        for ( const auto &pass : passes )
        {
            const double dev = rows_per_sec_mean > 0.0
                                   ? std::abs( pass.rows_per_sec - rows_per_sec_mean ) / rows_per_sec_mean * 100.0
                                   : 0.0;
            if ( dev > max_deviation_pct )
            {
                max_deviation_pct = dev;
            }
        }

        // D-07 Tier-1 floors (derived 2026-11-19 from this plan's own
        // measured 10^6 run, default knobs, host MOFU / windows /
        // release build: warm enumeration mean 4,831,882 rows/sec over
        // 3 passes, max deviation 0.447%; per-row bytes measured
        // 52.000 - a 46-char CIDv0 text key plus the 6-char suffix).
        // Hardware caveat: the measured numbers transfer only with the
        // evidence fingerprint (meta.* in evidence-floor.json); the
        // FLOORS transfer with a full order of magnitude of margin so
        // slow Phase 19 runners stay green (Pitfall 3 - a generous
        // lower bound, never a target; exact timings live only in the
        // dev-side JSON).
        const double per_row_bytes = passes.front().per_row_bytes;
        // rows/sec floor = measured mean / 10, rounded down for margin;
        // held by the first (coldest warm) pass AND the repeat mean.
        EXPECT_GE( passes.front().rows_per_sec, 480000.0 );
        EXPECT_GE( rows_per_sec_mean, 480000.0 );
        // per-row bytes band = [mean - 8, mean + 8] = [44, 60], clamped
        // to stay inside the A1 shape envelope [46, 90].
        EXPECT_GE( per_row_bytes, 46.0 );
        EXPECT_LE( per_row_bytes, 60.0 );

        // ---- Evidence emission (D-09) ----
        FlatJsonEvidence json;
        json.Add( "meta.harness", "cold_seal_growth_test" );
        json.Add( "meta.phase_plan", "17-01" );
        json.Add( "meta.host", HostTag() );
        json.Add( "meta.os", OsTag() );
        json.Add( "meta.build_config", BuildConfigTag() );
        json.Add( "meta.generated_utc", GeneratedUtc() );
        json.Add( "meta.dataset_fingerprint",
                  "seed=" + std::to_string( seed ) + ";scale=" + std::to_string( scale )
                      + ";first=" + first_cid + ";last=" + last_cid );
        json.Add( "options.target_file_size_base", static_cast<uint64_t>( 32 * 1024 * 1024 ) );
        json.Add( "options.max_compaction_bytes", static_cast<uint64_t>( 32 * 1024 * 1024 ) );
        json.Add( "options.write_buffer_size", static_cast<uint64_t>( 32 * 1024 * 1024 ) );
        json.Add( "options.level0_file_num_compaction_trigger", static_cast<uint64_t>( 1 ) );
        json.Add( "options.target_file_size_multiplier", static_cast<uint64_t>( 1 ) );
        json.Add( "options.level_compaction_dynamic_level_bytes", "false" );
        json.Add( "options.create_if_missing", "true" );
        json.Add( "options.wal_posture", "default (WAL on)" );
        json.Add( "leg.volume_10e6.seeded_count", scale );
        json.Add( "leg.volume_10e6.seed_elapsed_ms", seed_ms );
        json.Add( "leg.volume_10e6.seeding_rate_per_sec", seed_per_sec );
        json.Add( "leg.volume_10e6.enum_repeats", repeats );
        json.Add( "leg.volume_10e6.seal_rows", passes.front().seals );
        json.Add( "leg.volume_10e6.other_rows", passes.front().others );
        json.Add( "leg.volume_10e6.seal_bytes_total", passes.front().seal_bytes_total );
        json.Add( "leg.volume_10e6.per_row_bytes_mean", per_row_bytes );
        for ( size_t r = 0; r < passes.size(); ++r )
        {
            const std::string prefix = "leg.volume_10e6.enum_pass_" + std::to_string( r + 1 );
            json.Add( prefix + ".wall_ms", passes[r].wall_ms );
            json.Add( prefix + ".rows_per_sec", passes[r].rows_per_sec );
        }
        json.Add( "leg.volume_10e6.warm_rows_per_sec_mean", rows_per_sec_mean );
        json.Add( "leg.volume_10e6.enum_max_deviation_pct", max_deviation_pct );

        const std::string json_text = json.ToString();
        std::cout << "[evidence-json] " << json_text << std::endl;
        const std::filesystem::path evidence_path =
            std::filesystem::path( kFloorRoot ) / "evidence-floor.json";
        std::ofstream out( evidence_path );
        ASSERT_TRUE( out.is_open() ) << "cannot open " << evidence_path.string();
        out << json_text << std::endl;
        out.close();
        ASSERT_TRUE( out.good() ) << "failed writing " << evidence_path.string();

        // ---- Growing-root untouched re-assert (invariant 3 posture) ----
        // The grow root was created empty by this leg and never opened:
        // it must still exist and still be empty (17-02 owns its first
        // real population).
        ASSERT_TRUE( std::filesystem::exists( kGrowRoot ) ) << "grow root vanished";
        size_t grow_entries = 0;
        std::error_code ec;
        for ( const auto &entry : std::filesystem::directory_iterator( kGrowRoot, ec ) )
        {
            (void)entry;
            ++grow_entries;
        }
        ASSERT_EQ( grow_entries, 0 ) << "grow root was touched by the floor leg";
    }

    /// The D-04 fidelity leg: a REAL walk-driven CRDT chain (real
    /// GlobalDB puts through the fidelity topic, real DAG jobs, the
    /// walk's own sealing path - UpdateCRDTHeads -> markResolved ->
    /// vendored seal) plus the three spectrum-shape CID sets seeded
    /// through the syncer's public markResolved (the W3 ghost-CID
    /// surface, the walk's sealing surface per GATE-03), compared
    /// BYTE-FOR-BYTE against a twin minimal-stack store seeded directly
    /// through the vendored seal() surface - the proof that makes
    /// direct volume seeding admissible evidence.
    ///
    /// Topology ruling (recorded in the emitted evidence): the walk
    /// chain is driven DATASTORE-LEVEL (GlobalDB RequestForcedWalk +
    /// the GetForcedWalkStatus poll, the cold_evidence_matrix drive
    /// idiom); the per-shape sets use the W3 syncer-alias surface.
    /// Fidelity is topology-agnostic - both paths converge on the one
    /// vendored seal home.
    TEST( Fidelity, RealWalkByteIdentity )
    {
        const uint64_t depth   = EnvOr( "SGNS_SEAL_DEPTH", 64 );
        const uint64_t fanout  = EnvOr( "SGNS_SEAL_FANOUT", 8 );
        const uint64_t mix_pct = EnvOr( "SGNS_SEAL_MIX", 50 );
        const uint64_t seed    = EnvOr( "SGNS_SEAL_SEED", 20261118 );
        ASSERT_GE( depth, 2 );
        ASSERT_GE( fanout, 1 );
        ASSERT_LE( mix_pct, 100 );

        // ---- Store A: the GlobalDB-stack fixture (walk-produced seals) ----
        GlobalDbStack stack;
        ASSERT_TRUE( BuildGlobalDbStack( kFidelityHotRoot, stack ) ) << "GlobalDB fixture failed to build";
        auto syncer = SyncerOf( stack.db );

        // The real CRDT chain: N puts through the topic; each Put
        // enqueues a DAG root whose job success seals the root and the
        // replaced previous head (UpdateCRDTHeads -> markResolved).
        std::vector<sgns::CID> chain_cids;
        chain_cids.reserve( depth );
        for ( uint64_t i = 0; i < depth; ++i )
        {
            std::ostringstream key_oss;
            key_oss << "/seal-growth/fidelity/" << i;
            std::ostringstream payload;
            payload << "fidelity walk chain seed=" << seed << " idx=" << i;
            sgns::crdt::GlobalDB::Buffer buffer;
            buffer.put( payload.str() );
            auto put = stack.db->Put( sgns::crdt::HierarchicalKey( key_oss.str() ), buffer, { kFidelityTopic } );
            ASSERT_TRUE( put.has_value() ) << "Put failed at idx=" << i;
            chain_cids.push_back( put.value() );
        }
        // Wait for every job to complete (the sanctioned presence check
        // - isResolved per CID; jobs drain on the worker tick).
        ASSERT_TRUE( PollUntil(
            [&]
            {
                for ( const auto &cid : chain_cids )
                {
                    auto resolved = syncer->isResolved( cid );
                    if ( resolved.has_error() || !resolved.value() )
                    {
                        return false;
                    }
                }
                return true;
            },
            std::chrono::milliseconds( 60000 ) ) )
            << "DAG jobs never sealed the put chain";

        // ---- The walk (the public drive + status poll) ----
        auto requested = stack.db->RequestForcedWalk(
            { kFidelityTopic },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            depth * 4 );
        ASSERT_TRUE( requested.has_value() ) << requested.error().message();
        using sgns::crdt::CrdtDatastore;
        ASSERT_TRUE( PollUntil(
            [&]
            {
                const auto state = stack.db->GetForcedWalkStatus().state;
                return state == CrdtDatastore::ForcedWalkState::Completed
                       || state == CrdtDatastore::ForcedWalkState::Incomplete;
            },
            std::chrono::milliseconds( 60000 ) ) )
            << "fidelity walk never reached a terminal state";
        const auto walk_status = stack.db->GetForcedWalkStatus();
        EXPECT_EQ( walk_status.state, CrdtDatastore::ForcedWalkState::Completed );
        EXPECT_GT( walk_status.visited, 0u ) << "the walk must revisit the chain's ancestry";
        EXPECT_EQ( walk_status.walk_gaps, 0u );
        EXPECT_EQ( walk_status.errors, 0u );

        // ---- Spectrum fixtures + W3-alias sealing into store A ----
        const std::vector<std::string> shapes = { "deep", "wide", "mixed" };
        std::vector<SpectrumFixture>   fixtures;
        fixtures.reserve( shapes.size() );
        for ( const auto &shape : shapes )
        {
            fixtures.push_back( GenerateSpectrumFixture( shape, depth, fanout, mix_pct, seed ) );
            for ( const auto &cid : fixtures.back().cids )
            {
                ASSERT_TRUE( syncer->markResolved( cid ).has_value() );
            }
        }

        // ---- Store B: the twin minimal stack (directly-seeded seals) ----
        MinimalSealStack twin;
        ASSERT_TRUE( BuildMinimalStack( kFidelityTwinRoot, /*wipe=*/true, twin ) ) << "twin store failed to build";
        for ( const auto &cid : chain_cids )
        {
            ASSERT_TRUE( twin.datastore->seal( cid ).has_value() );
        }
        for ( const auto &fixture : fixtures )
        {
            for ( const auto &cid : fixture.cids )
            {
                ASSERT_TRUE( twin.datastore->seal( cid ).has_value() );
            }
        }

        // ---- The byte-identity comparison (prefix-spy discipline:
        // exact key bytes, sorted-set equality, values empty by shape) ----
        bool values_a_empty = false;
        bool values_b_empty = false;
        const auto keys_a = ScanSealKeys( stack.db->GetDataStore(), &values_a_empty );
        const auto keys_b = ScanSealKeys( twin.hot, &values_b_empty );
        EXPECT_TRUE( values_a_empty ) << "walk-produced seal rows carry values";
        EXPECT_TRUE( values_b_empty ) << "directly-seeded seal rows carry values";

        size_t expected_seals = chain_cids.size();
        for ( const auto &fixture : fixtures )
        {
            expected_seals += fixture.ancestry_size;
        }
        EXPECT_EQ( keys_b.size(), expected_seals ) << "twin store seal count";
        EXPECT_EQ( keys_a.size(), expected_seals ) << "walk-fixture seal count (extra seals would break identity)";
        EXPECT_EQ( keys_a, keys_b ) << "seeded and walk-produced seal rows are NOT byte-identical";
        const bool identity_equal = ( keys_a == keys_b ) && values_a_empty && values_b_empty
                                 && keys_a.size() == expected_seals;
        EXPECT_TRUE( identity_equal );

        // ---- Evidence emission (D-09) ----
        FlatJsonEvidence json;
        json.Add( "meta.harness", "cold_seal_growth_test" );
        json.Add( "meta.phase_plan", "17-02" );
        json.Add( "meta.host", HostTag() );
        json.Add( "meta.os", OsTag() );
        json.Add( "meta.build_config", BuildConfigTag() );
        json.Add( "meta.generated_utc", GeneratedUtc() );
        json.Add( "meta.dataset_fingerprint",
                  "seed=" + std::to_string( seed ) + ";depth=" + std::to_string( depth )
                      + ";fanout=" + std::to_string( fanout ) + ";mix=" + std::to_string( mix_pct ) );
        json.Add( "leg.fidelity.topology",
                  "datastore-level (GlobalDB RequestForcedWalk) + W3 syncer-alias for spectrum shape sets" );
        json.Add( "leg.fidelity.chain_puts", static_cast<uint64_t>( chain_cids.size() ) );
        json.Add( "leg.fidelity.walk.state",
                  walk_status.state == CrdtDatastore::ForcedWalkState::Completed ? "Completed" : "other" );
        json.Add( "leg.fidelity.walk.visited", static_cast<uint64_t>( walk_status.visited ) );
        json.Add( "leg.fidelity.walk.fetched", static_cast<uint64_t>( walk_status.fetched ) );
        json.Add( "leg.fidelity.walk.restored", static_cast<uint64_t>( walk_status.restored ) );
        json.Add( "leg.fidelity.walk.skipped_pruned", static_cast<uint64_t>( walk_status.skipped_pruned ) );
        json.Add( "leg.fidelity.walk.skipped_existing", static_cast<uint64_t>( walk_status.skipped_existing ) );
        json.Add( "leg.fidelity.walk.walk_gaps", static_cast<uint64_t>( walk_status.walk_gaps ) );
        json.Add( "leg.fidelity.walk.deferred", static_cast<uint64_t>( walk_status.deferred ) );
        json.Add( "leg.fidelity.walk.errors", static_cast<uint64_t>( walk_status.errors ) );
        json.Add( "leg.fidelity.identity_equal", identity_equal ? "true" : "false" );
        json.Add( "leg.fidelity.seal_keys_compared", static_cast<uint64_t>( expected_seals ) );
        for ( const auto &fixture : fixtures )
        {
            const std::string prefix = "leg.fidelity." + fixture.shape;
            json.Add( prefix + ".ancestry_size", fixture.ancestry_size );
            json.Add( prefix + ".max_depth", fixture.max_depth );
            json.Add( prefix + ".max_breadth", fixture.max_breadth );
            json.Add( prefix + ".identity_equal", identity_equal ? "true" : "false" );
        }
        const std::string json_text = json.ToString();
        std::cout << "[evidence-json] " << json_text << std::endl;
        const std::filesystem::path evidence_path =
            std::filesystem::path( kFidelityHotRoot ) / "evidence-fidelity.json";
        std::ofstream out( evidence_path );
        ASSERT_TRUE( out.is_open() ) << "cannot open " << evidence_path.string();
        out << json_text << std::endl;
        out.close();

        TeardownGlobalDbStack( stack );
    }

    /// A deterministic 64-hex-char tx hash for the density fixtures
    /// (FNV-1a over seed|shape|fraction, widened to 64 chars - the
    /// record's back-reference value, never a real transaction).
    std::string DeterministicTxHash( uint64_t seed, const std::string &shape, uint64_t frac_num, uint64_t frac_den )
    {
        uint64_t h = 14695981039346656037ull ^ seed;
        for ( const char c : shape + std::to_string( frac_num ) + std::to_string( frac_den ) )
        {
            h ^= static_cast<uint8_t>( c );
            h *= 1099511628211ull;
        }
        std::ostringstream oss;
        for ( int i = 0; i < 16; ++i )
        {
            h ^= static_cast<uint8_t>( i );
            h *= 1099511628211ull;
            oss << std::hex << std::setw( 4 ) << std::setfill( '0' ) << static_cast<uint16_t>( h >> 48 );
        }
        return oss.str();
    }

    /// A small real-topic put chain (the density store's seals + CRDT
    /// rows); returns the delta CIDs (all sealed by the job path).
    std::vector<sgns::CID> PutFidelityChain( const std::shared_ptr<sgns::crdt::GlobalDB> &db,
                                             uint64_t                                    count,
                                             uint64_t                                    seed,
                                             const std::function<void()> &fail )
    {
        std::vector<sgns::CID> cids;
        cids.reserve( count );
        for ( uint64_t i = 0; i < count; ++i )
        {
            std::ostringstream key_oss;
            key_oss << "/seal-growth/density/" << i;
            std::ostringstream payload;
            payload << "density chain seed=" << seed << " idx=" << i;
            sgns::crdt::GlobalDB::Buffer buffer;
            buffer.put( payload.str() );
            auto put = db->Put( sgns::crdt::HierarchicalKey( key_oss.str() ), buffer, { kFidelityTopic } );
            if ( !put.has_value() )
            {
                fail();
                return cids;
            }
            cids.push_back( put.value() );
        }
        return cids;
    }

    /// The D-06 density legs: the /prune/blk back-reference spectrum
    /// matrix (3 shapes x 3 deterministic erase fractions) through the
    /// ONE marker home - writes via PruneEligibility::
    /// WritePrunedBlockEntryForTest, reads via LookupPrunedBlockEntry
    /// (never a hand-built /prune/* key) - with the exact-denominator
    /// discipline (the generator's known ancestry, Pitfall 7) and the
    /// shared-CF mix context row the capacity math cites.
    TEST( Density, PruneBlockBackReferenceSpectrum )
    {
        const uint64_t depth   = EnvOr( "SGNS_SEAL_DEPTH", 64 );
        const uint64_t fanout  = EnvOr( "SGNS_SEAL_FANOUT", 8 );
        const uint64_t mix_pct = EnvOr( "SGNS_SEAL_MIX", 50 );
        const uint64_t seed    = EnvOr( "SGNS_SEAL_SEED", 20261118 );

        GlobalDbStack stack;
        ASSERT_TRUE( BuildGlobalDbStack( kDensityRoot, stack ) ) << "GlobalDB fixture failed to build";
        auto syncer = SyncerOf( stack.db );

        // A small real put chain populates the store with production
        // row shapes (seals + CRDT delta blocks + heads).
        constexpr uint64_t kDensityChainLen = 8;
        auto chain = PutFidelityChain(
            stack.db, kDensityChainLen, seed,
            []() { FAIL() << "density chain Put failed"; } );
        ASSERT_EQ( chain.size(), kDensityChainLen );
        ASSERT_TRUE( PollUntil(
            [&]
            {
                for ( const auto &cid : chain )
                {
                    auto resolved = syncer->isResolved( cid );
                    if ( resolved.has_error() || !resolved.value() )
                    {
                        return false;
                    }
                }
                return true;
            },
            std::chrono::milliseconds( 60000 ) ) )
            << "DAG jobs never sealed the density chain";

        const std::vector<std::string> shapes = { "deep", "wide", "mixed" };
        std::vector<SpectrumFixture>   fixtures;
        for ( const auto &shape : shapes )
        {
            fixtures.push_back( GenerateSpectrumFixture( shape, depth, fanout, mix_pct, seed ) );
        }

        FlatJsonEvidence json;
        json.Add( "meta.harness", "cold_seal_growth_test" );
        json.Add( "meta.phase_plan", "17-02" );
        json.Add( "meta.host", HostTag() );
        json.Add( "meta.os", OsTag() );
        json.Add( "meta.build_config", BuildConfigTag() );
        json.Add( "meta.generated_utc", GeneratedUtc() );
        json.Add( "meta.dataset_fingerprint",
                  "seed=" + std::to_string( seed ) + ";depth=" + std::to_string( depth )
                      + ";fanout=" + std::to_string( fanout ) + ";mix=" + std::to_string( mix_pct ) );

        struct Fraction
        {
            uint64_t num;
            uint64_t den;
        };
        const std::vector<Fraction> fractions = { { 1, 4 }, { 1, 2 }, { 1, 1 } };

        for ( const auto &fixture : fixtures )
        {
            for ( const auto &frac : fractions )
            {
                // Deterministic selection by seeded (emission) order -
                // nearest-integer marking, no RNG beyond the seed.
                const uint64_t total = fixture.ancestry_size;
                const uint64_t marked = ( total * frac.num + frac.den / 2 ) / frac.den;
                const std::string tx_hash = DeterministicTxHash( seed, fixture.shape, frac.num, frac.den );

                for ( uint64_t i = 0; i < marked; ++i )
                {
                    auto written = sgns::PruneEligibility::WritePrunedBlockEntryForTest(
                        *stack.db, fixture.cid_strings[i], tx_hash, fixture.node_generation[i] );
                    ASSERT_TRUE( written.has_value() ) << "marker write failed (" << fixture.shape
                                                       << " idx=" << i << "): " << written.error().message();
                }

                // Read back the WHOLE ancestry through the one-home
                // tri-state reader: present-for-marked (with the exact
                // record round-trip), absent-for-unmarked.
                bool roundtrip_ok = true;
                for ( uint64_t i = 0; i < total; ++i )
                {
                    auto lookup = sgns::PruneEligibility::LookupPrunedBlockEntry(
                        *stack.db, fixture.cid_strings[i] );
                    ASSERT_TRUE( lookup.has_value() )
                        << "lookup errored (" << fixture.shape << " idx=" << i << ")";
                    const auto &[verdict, record] = lookup.value();
                    if ( i < marked )
                    {
                        EXPECT_EQ( verdict, sgns::PrunedBlockLookup::PRUNED )
                            << fixture.shape << " idx=" << i << " must be present";
                        if ( verdict == sgns::PrunedBlockLookup::PRUNED && record.has_value() )
                        {
                            if ( record->pruning_tx_hash() != tx_hash
                                 || !record->has_erased_at_generation()
                                 || record->erased_at_generation() != fixture.node_generation[i] )
                            {
                                roundtrip_ok = false;
                            }
                        }
                        else
                        {
                            roundtrip_ok = false;
                        }
                    }
                    else
                    {
                        EXPECT_EQ( verdict, sgns::PrunedBlockLookup::NOT_FOUND )
                            << fixture.shape << " idx=" << i << " must be absent";
                    }
                }
                EXPECT_TRUE( roundtrip_ok ) << "record round-trip broken (" << fixture.shape << ")";

                // Pitfall 7: the denominator is the generator's exactly-
                // known ancestry; density never exceeds 1 and tracks the
                // fraction within the nearest-integer rounding slack.
                const double density = static_cast<double>( marked ) / static_cast<double>( total );
                const double target = static_cast<double>( frac.num ) / static_cast<double>( frac.den );
                EXPECT_LE( density, 1.0 );
                EXPECT_LE( std::abs( density - target ), 0.5 / static_cast<double>( total ) + 1e-12 );

                const std::string prefix = "leg.density." + fixture.shape + ".frac_"
                                         + std::to_string( frac.num ) + "_" + std::to_string( frac.den );
                json.Add( prefix + ".marked", marked );
                json.Add( prefix + ".total", total );
                json.Add( prefix + ".density", density );
                json.Add( prefix + ".roundtrip_ok", roundtrip_ok ? "true" : "false" );
            }
        }

        // The shared-CF context row (Pitfall 1 / D-11 denominator
        // context): one full suffix-scan pass over the populated store -
        // seals (the chain's), non-seal others (CRDT rows + heads +
        // every /prune/blk marker written above).
        const EnumPass context = RunEnumerationPass( stack.db->GetDataStore() );
        EXPECT_EQ( context.seals, kDensityChainLen );
        EXPECT_GT( context.others, 0u );
        json.Add( "leg.density.shared_cf.seals", context.seals );
        json.Add( "leg.density.shared_cf.others", context.others );
        json.Add( "leg.density.shared_cf.per_row_bytes", context.per_row_bytes );
        std::cout << "[seal-growth] density shared-CF context: seals=" << context.seals
                  << " others=" << context.others << std::endl;

        const std::string json_text = json.ToString();
        std::cout << "[evidence-json] " << json_text << std::endl;
        const std::filesystem::path evidence_path =
            std::filesystem::path( kDensityRoot ) / "evidence-density.json";
        std::ofstream out( evidence_path );
        ASSERT_TRUE( out.is_open() ) << "cannot open " << evidence_path.string();
        out << json_text << std::endl;
        out.close();

        TeardownGlobalDbStack( stack );
    }
} // namespace
