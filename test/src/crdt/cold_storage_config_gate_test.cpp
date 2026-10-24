/**
 * @file       cold_storage_config_gate_test.cpp
 * @brief      Hermetic tri-state matrix for the strict "blockstore" config
 *             gate (INTG-02 / TEST-01, v1.1 Phase 7 plan 07-03).
 *
 * Every case passes its fixture as an in-memory JSON string and its env
 * values through the INJECTED ColdStorageEnvLookup - zero file I/O, zero
 * process-env mutation, zero network (parallel-safe under ctest -j).
 *
 * Errors are asserted via the outcome result's message CONTENT (field/key
 * names), never via exceptions: the std::runtime_error throw-through-
 * constructor surface is GeniusNode's and stays out of this binary by
 * design (07-RESEARCH A7: seam-scoped proof).
 *
 * Registered UNCONDITIONALLY (NOT under SGNS_COLD_STORAGE) so the Tier-1
 * lane is non-empty in default OFF builds - ctest -L Tier1 --no-tests=error
 * cannot pass vacuously (Pitfall 6).
 */

#include <gtest/gtest.h>

#include <string>

#include "crdt/globaldb/globaldb.hpp"

namespace
{
    using ColdStorageOptions     = sgns::crdt::GlobalDB::ColdStorageOptions;
    using ColdStorageEnvLookup   = sgns::crdt::GlobalDB::ColdStorageEnvLookup;
    using ColdStorageParseResult = sgns::crdt::GlobalDB::ColdStorageParseResult;

    /// Empty lookup = no env overrides (unset env), for every case that is
    /// not explicitly an env case.
    ColdStorageEnvLookup NoEnv()
    {
        return ColdStorageEnvLookup{};
    }

    ColdStorageParseResult Parse( const std::string &json, const ColdStorageEnvLookup &env = NoEnv() )
    {
        return sgns::crdt::GlobalDB::ParseColdStorageConfig( json, env );
    }
} // namespace

// ---------------------------------------------------------------------------
// D-03: absent / empty-object / optional-fields-only => disabled (not error)
// ---------------------------------------------------------------------------

TEST( ColdStorageConfigGate, AbsentSectionDisables )
{
    auto result = Parse( R"({"backup_enabled": true})", NoEnv() );
    ASSERT_FALSE( result.has_error() );
    EXPECT_FALSE( result.value().enabled );
}

TEST( ColdStorageConfigGate, EmptyObjectSectionDisables )
{
    auto result = Parse( R"({"blockstore": {}})", NoEnv() );
    ASSERT_FALSE( result.has_error() );
    EXPECT_FALSE( result.value().enabled );
}

TEST( ColdStorageConfigGate, OptionalOnlySectionDisables )
{
    // prefix set, all four required fields blank => disabled (D-03 optional-only)
    auto result = Parse( R"({"blockstore": {"prefix": "v1/blocks"}})", NoEnv() );
    ASSERT_FALSE( result.has_error() );
    EXPECT_FALSE( result.value().enabled );
}

// ---------------------------------------------------------------------------
// D-04: each single-required-field-filled fixture => error naming the
//       missing required field(s)
// ---------------------------------------------------------------------------

TEST( ColdStorageConfigGate, EndpointOnlyFailsNamingMissingField )
{
    auto result = Parse( R"({"blockstore": {"endpoint": "127.0.0.1"}})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "bucket" ), std::string::npos );
    EXPECT_NE( result.error().find( "access_key" ), std::string::npos );
    EXPECT_NE( result.error().find( "secret_key" ), std::string::npos );
}

TEST( ColdStorageConfigGate, BucketOnlyFailsNamingMissingField )
{
    auto result = Parse( R"({"blockstore": {"bucket": "cold"}})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "endpoint" ), std::string::npos );
    EXPECT_NE( result.error().find( "secret_key" ), std::string::npos );
}

TEST( ColdStorageConfigGate, AccessKeyOnlyFailsNamingMissingField )
{
    auto result = Parse( R"({"blockstore": {"access_key": "a"}})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "endpoint" ), std::string::npos );
    EXPECT_NE( result.error().find( "bucket" ), std::string::npos );
}

TEST( ColdStorageConfigGate, SecretKeyOnlyFailsNamingMissingField )
{
    auto result = Parse( R"({"blockstore": {"secret_key": "s"}})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "endpoint" ), std::string::npos );
    EXPECT_NE( result.error().find( "bucket" ), std::string::npos );
}

// ---------------------------------------------------------------------------
// D-05: unknown key / wrong-typed key => error naming the key
// ---------------------------------------------------------------------------

TEST( ColdStorageConfigGate, UnknownKeyFails )
{
    // the "acess_key" typo - caught at startup, not as a confusing auth
    // failure at first drain (D-05 rationale)
    auto result = Parse( R"({"blockstore": {"acess_key": "typo"}})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "acess_key" ), std::string::npos );
}

TEST( ColdStorageConfigGate, WrongTypeFails )
{
    auto result = Parse( R"({"blockstore": {"endpoint": 9001}})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "endpoint" ), std::string::npos );
}

// (c) present-but-not-an-object is a detectable malformation: loud (07-REVIEW IN-02)
TEST( ColdStorageConfigGate, SectionNotAnObjectFails )
{
    auto result = Parse( R"({"blockstore": "yes"})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "blockstore" ), std::string::npos );
}

// ---------------------------------------------------------------------------
// D-06: whole-file corruption stays lenient => disabled, not an error
// ---------------------------------------------------------------------------

TEST( ColdStorageConfigGate, FileCorruptJsonDisables )
{
    auto result = Parse( R"(not json at all {{{)", NoEnv() );
    ASSERT_FALSE( result.has_error() );
    EXPECT_FALSE( result.value().enabled );
}

// ---------------------------------------------------------------------------
// D-07 / D-08: env overrides credential VALUES only, never activates; the
// required-field check runs AFTER env resolution
// ---------------------------------------------------------------------------

TEST( ColdStorageConfigGate, EnvCredentialsSatisfyWhenIntentExists )
{
    // File supplies endpoint+bucket+file-credential VALUES; env credentials
    // WIN over the file values (value override, D-07).
    ColdStorageEnvLookup env;
    env.access_key = "env-access";
    env.secret_key = "env-secret";
    auto result = Parse(
        R"({"blockstore": {"endpoint": "127.0.0.1", "bucket": "cold", "access_key": "file-access", "secret_key": "file-secret"}})",
        env );
    ASSERT_FALSE( result.has_error() );
    EXPECT_TRUE( result.value().enabled );
    EXPECT_EQ( result.value().accessKey, std::string( "env-access" ) );
    EXPECT_EQ( result.value().secretKey, std::string( "env-secret" ) );
}

TEST( ColdStorageConfigGate, EnvNeverActivates )
{
    // All four required fields blank in the file + env credentials set:
    // disabled regardless of environment (D-08 file-intent gate).
    ColdStorageEnvLookup env;
    env.access_key = "env-access";
    env.secret_key = "env-secret";
    auto result = Parse(
        R"({"blockstore": {"endpoint": "", "bucket": "", "access_key": "", "secret_key": ""}})",
        env );
    ASSERT_FALSE( result.has_error() );
    EXPECT_FALSE( result.value().enabled );
}

TEST( ColdStorageConfigGate, EnvOnlyCredentialsWithFileIntent )
{
    // The flagship D-07 server posture (07-REVIEW WR-01): file carries ONLY
    // endpoint+bucket; credentials come EXCLUSIVELY from the environment -
    // enabled, with the env values resolved.
    ColdStorageEnvLookup env;
    env.access_key = "env-access";
    env.secret_key = "env-secret";
    auto result = Parse( R"({"blockstore": {"endpoint": "10.1.2.3", "bucket": "cold"}})", env );
    ASSERT_FALSE( result.has_error() );
    EXPECT_TRUE( result.value().enabled );
    EXPECT_EQ( result.value().accessKey, std::string( "env-access" ) );
    EXPECT_EQ( result.value().secretKey, std::string( "env-secret" ) );
}

TEST( ColdStorageConfigGate, EnvEmptyStringIsUnset )
{
    // Empty-string env value = unset: falls back to the file credential.
    ColdStorageEnvLookup env;
    env.access_key = "";
    env.secret_key = "";
    auto result = Parse(
        R"({"blockstore": {"endpoint": "127.0.0.1", "bucket": "cold", "access_key": "file-access", "secret_key": "file-secret"}})",
        env );
    ASSERT_FALSE( result.has_error() );
    EXPECT_TRUE( result.value().enabled );
    EXPECT_EQ( result.value().accessKey, std::string( "file-access" ) );
    EXPECT_EQ( result.value().secretKey, std::string( "file-secret" ) );
}

// ---------------------------------------------------------------------------
// T-7-03: LogSummary redaction by omission
// ---------------------------------------------------------------------------

TEST( ColdStorageConfigGate, LogSummaryContainsEndpointBucketAndNoCredentials )
{
    ColdStorageOptions options;
    options.endpoint  = "sum-endpoint";
    options.bucket    = "sum-bucket";
    options.accessKey = "SECRET-ACCESS-VALUE";
    options.secretKey = "SECRET-SECRET-VALUE";
    options.prefix    = "sum-prefix";
    const auto summary = options.LogSummary();
    EXPECT_NE( summary.find( "sum-endpoint" ), std::string::npos );
    EXPECT_NE( summary.find( "sum-bucket" ), std::string::npos );
    EXPECT_EQ( summary.find( "SECRET-ACCESS-VALUE" ), std::string::npos );
    EXPECT_EQ( summary.find( "SECRET-SECRET-VALUE" ), std::string::npos );
}

// ---------------------------------------------------------------------------
// D-02: the seven fields map onto the struct
// ---------------------------------------------------------------------------

TEST( ColdStorageConfigGate, EnabledOptionsFieldMapping )
{
    ColdStorageEnvLookup env;
    env.access_key = "env-access";
    auto result = Parse(
        R"({"blockstore": {"endpoint": "10.0.0.1", "bucket": "b1", "access_key": "ignored-file-access", "secret_key": "file-secret", "prefix": "v1/blocks", "path_style": false, "tls": true}})",
        env );
    ASSERT_FALSE( result.has_error() );
    const auto &options = result.value();
    EXPECT_TRUE( options.enabled );
    EXPECT_EQ( options.endpoint, std::string( "10.0.0.1" ) );
    EXPECT_EQ( options.bucket, std::string( "b1" ) );
    EXPECT_EQ( options.accessKey, std::string( "env-access" ) );
    EXPECT_EQ( options.secretKey, std::string( "file-secret" ) );
    EXPECT_EQ( options.prefix, std::string( "v1/blocks" ) );
    EXPECT_FALSE( options.pathStyle );
    EXPECT_TRUE( options.tls );
}

// ---------------------------------------------------------------------------
// blockstore.backpressure (D-05/D-06/D-07, v1.1 Phase 8 plan 08-02):
// absent sub-object / blank object / 0 knobs => built-in defaults; unknown
// key / wrong type / warn >= critical => loud failure naming the offender;
// backpressure-only sections stay DISABLED (Phase-7 (e) inheritance).
// ---------------------------------------------------------------------------

/// A fully-enabled config fixture WITHOUT the blockstore/outer closers -
/// the backpressure cases inject their sub-object inside "blockstore"
/// then append the closers themselves.
constexpr const char *kEnabledConfigBody =
    R"({"blockstore": {"endpoint": "10.0.0.1", "bucket": "b1", "access_key": "a", "secret_key": "s")";

TEST( ColdStorageConfigGate, BackpressureAbsentSubObjectGivesDefaults )
{
    auto result = Parse( std::string( kEnabledConfigBody ) + "}}", NoEnv() );
    ASSERT_FALSE( result.has_error() );
    EXPECT_TRUE( result.value().enabled );
    const auto &bp = result.value().backpressure;
    EXPECT_EQ( bp.drain_interval_ms, 5000u );
    EXPECT_EQ( bp.backoff_cap_ms, 60000u );
    EXPECT_EQ( bp.warn_pending, 10000u );
    EXPECT_EQ( bp.critical_pending, 100000u );
}

TEST( ColdStorageConfigGate, BackpressureBlankObjectGivesDefaults )
{
    auto result = Parse( std::string( kEnabledConfigBody ) + R"(, "backpressure": {}}})", NoEnv() );
    ASSERT_FALSE( result.has_error() );
    EXPECT_TRUE( result.value().enabled );
    const auto &bp = result.value().backpressure;
    EXPECT_EQ( bp.drain_interval_ms, 5000u );
    EXPECT_EQ( bp.backoff_cap_ms, 60000u );
    EXPECT_EQ( bp.warn_pending, 10000u );
    EXPECT_EQ( bp.critical_pending, 100000u );
}

TEST( ColdStorageConfigGate, BackpressureZeroKnobTakesOnlyItsDefault )
{
    // drain_interval_ms=0 takes its default while the explicit others keep
    // their values (D-07 0-default semantics, per-knob).
    auto result = Parse( std::string( kEnabledConfigBody )
                         + R"(, "backpressure": {"drain_interval_ms": 0, "warn_pending": 50, "critical_pending": 500, "backoff_cap_ms": 10000}}})",
                         NoEnv() );
    ASSERT_FALSE( result.has_error() );
    const auto &bp = result.value().backpressure;
    EXPECT_EQ( bp.drain_interval_ms, 5000u );
    EXPECT_EQ( bp.warn_pending, 50u );
    EXPECT_EQ( bp.critical_pending, 500u );
    EXPECT_EQ( bp.backoff_cap_ms, 10000u );
}

TEST( ColdStorageConfigGate, BackpressureUnknownKeyFails )
{
    auto result = Parse( std::string( kEnabledConfigBody ) + R"(, "backpressure": {"bogus": 1}}})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "unknown key 'blockstore.backpressure.bogus'" ), std::string::npos );
}

TEST( ColdStorageConfigGate, BackpressureWrongTypeFails )
{
    auto result = Parse( std::string( kEnabledConfigBody )
                         + R"(, "backpressure": {"drain_interval_ms": "fast"}}})",
                         NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "blockstore.backpressure.drain_interval_ms" ), std::string::npos );
    EXPECT_NE( result.error().find( "uint" ), std::string::npos );
}

TEST( ColdStorageConfigGate, BackpressureNotAnObjectFails )
{
    auto result = Parse( std::string( kEnabledConfigBody ) + R"(, "backpressure": 5}})", NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "'blockstore.backpressure' must be an object" ), std::string::npos );
}

TEST( ColdStorageConfigGate, BackpressureWarnAtOrAboveCriticalFails )
{
    auto result = Parse( std::string( kEnabledConfigBody )
                         + R"(, "backpressure": {"warn_pending": 500, "critical_pending": 500}}})",
                         NoEnv() );
    ASSERT_TRUE( result.has_error() );
    EXPECT_NE( result.error().find( "warn_pending < critical_pending" ), std::string::npos );
    EXPECT_NE( result.error().find( "500" ), std::string::npos );
}

TEST( ColdStorageConfigGate, BackpressureOnlySectionDisables )
{
    // Phase-7 (e) inheritance: optional-fields-only (backpressure included)
    // => disabled config, NOT an error - the wallet blank form relies on it.
    auto result = Parse( R"({"blockstore": {"backpressure": {"drain_interval_ms": 250}}})", NoEnv() );
    ASSERT_FALSE( result.has_error() );
    EXPECT_FALSE( result.value().enabled );
}

TEST( ColdStorageConfigGate, BackpressureValidExplicitValuesPropagate )
{
    auto result = Parse( std::string( kEnabledConfigBody )
                         + R"(, "backpressure": {"drain_interval_ms": 250, "warn_pending": 50, "critical_pending": 500, "backoff_cap_ms": 10000}}})",
                         NoEnv() );
    ASSERT_FALSE( result.has_error() );
    EXPECT_TRUE( result.value().enabled );
    const auto &bp = result.value().backpressure;
    EXPECT_EQ( bp.drain_interval_ms, 250u );
    EXPECT_EQ( bp.warn_pending, 50u );
    EXPECT_EQ( bp.critical_pending, 500u );
    EXPECT_EQ( bp.backoff_cap_ms, 10000u );
}
