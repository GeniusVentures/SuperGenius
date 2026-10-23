/**
 * @file       cold_storage_link_proof_test.cpp
 * @brief      One-binary link proof for the gated sgns-blockstore consumption
 *             block (INTG-01, v1.1 Phase 7).
 *
 * Hermetic by construction: this TU includes ONLY installed sgns-blockstore
 * headers (never miniocpp or httplib directly - SC2 containment) and performs
 * ZERO network I/O. OpenMinioS3Client construction is eagerly validating and
 * connects lazily per operation (Phase-6 LIB-02 / D-06 contract), so building
 * against a non-listening endpoint proves the configure -> find -> link ->
 * construct chain without any live dependency.
 */

#include <gtest/gtest.h>

#include <string>

#include <sgns-blockstore/S3Client.hpp>

namespace
{
    sgns::blockstore::S3ClientConfig MakeValidConfig()
    {
        sgns::blockstore::S3ClientConfig config;
        config.endpoint = "127.0.0.1";
        config.bucket   = "cold-storage-link-proof";
        return config;
    }
} // namespace

/**
 * @brief Construct the production client against a non-listening endpoint.
 *
 * Eager validation + zero network I/O at construction (Phase-6 D-06): the
 * call must succeed without any listener on the endpoint. Guard the result
 * with has_value() before .value() - the library-local result alias uses
 * policy::terminate.
 */
TEST( ColdStorageLinkProof, OpenMinioS3ClientConstructsWithEagerValidation )
{
    auto client = sgns::blockstore::OpenMinioS3Client( MakeValidConfig() );
    ASSERT_TRUE( client.has_value() );
    EXPECT_NE( client.value(), nullptr );
}

/** @brief Empty endpoint is a programmer error: loud invalid_argument. */
TEST( ColdStorageLinkProof, EmptyEndpointThrowsInvalidArgument )
{
    auto config       = MakeValidConfig();
    config.endpoint   = "";
    // static_cast<void> honors the [[nodiscard]] result alias inside the macro
    EXPECT_THROW( static_cast<void>( sgns::blockstore::OpenMinioS3Client( config ) ),
                  std::invalid_argument );
}

/**
 * @brief The consumed S3ClientConfig field subset compiles and defaults
 *        resolve.
 *
 * Exercises the Phase-7 consumed fields (endpoint, bucket, accessKey,
 * secretKey, pathStyle, useTls) and pins the remaining defaults: port 0
 * (scheme default), region "us-east-1" (pinned - avoids a bucket-location
 * roundtrip), tlsVerify true (NEVER defaults off, T-3-02). The blockstore
 * config-gate's optional "prefix" knob (D-02) lives in the v1.1
 * ColdStorageOptions layer (Phase 7 plan 07-02), not on S3ClientConfig.
 */
TEST( ColdStorageLinkProof, ConfigFieldSubsetAndDefaultsResolve )
{
    auto config       = MakeValidConfig();
    config.accessKey  = "test-access-key";
    config.secretKey  = "test-secret-key";
    config.pathStyle  = false;
    config.useTls     = true;

    EXPECT_EQ( config.endpoint, std::string( "127.0.0.1" ) );
    EXPECT_EQ( config.bucket, std::string( "cold-storage-link-proof" ) );
    EXPECT_EQ( config.accessKey, std::string( "test-access-key" ) );
    EXPECT_EQ( config.secretKey, std::string( "test-secret-key" ) );
    EXPECT_FALSE( config.pathStyle );
    EXPECT_TRUE( config.useTls );

    // Defaults that must hold without being written:
    EXPECT_EQ( config.port, 0 );
    EXPECT_EQ( config.region, std::string( "us-east-1" ) );
    EXPECT_TRUE( config.tlsVerify );
}
