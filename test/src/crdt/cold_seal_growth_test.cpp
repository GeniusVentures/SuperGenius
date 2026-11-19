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
 *
 * Hermetic: no S3, no node, no network - one hot RocksDB fixture.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>

#include "storage/rocksdb/rocksdb.hpp"

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>

namespace
{
    using sgns::storage::rocksdb;

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
    /// never wiped, never enumerated by this plan.
    constexpr const char *kFloorRoot = "test-data/cold-seal-growth/floor";
    constexpr const char *kGrowRoot  = "test-data/cold-seal-growth/grow";

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

    EnumPass RunEnumerationPass( const MinimalSealStack &stack )
    {
        EnumPass pass;
        auto    db_handle = stack.hot->getDB();
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
            EnumPass pass = RunEnumerationPass( stack );
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
} // namespace
