/**
 * @file       registration_transaction_test.cpp
 * @brief      Unit tests for RegistrationTransaction — factory, serialization round-trip, topics.
 * @date       2026-07-15
 * @author     (Phase 4)
 */
#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>

#include "account/GeniusAccount.hpp"
#include "account/RegistrationTransaction.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

namespace
{
    using namespace sgns;

    /// Fixture that provides MemorySecureStorage so GeniusAccount can be created without
    /// interacting with the OS keychain.
    class RegistrationTransactionTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );
            path_ = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        }

        void TearDown() override
        {
            GeniusAccount::SetSecureStorageFactory( nullptr );
            boost::filesystem::remove_all( path_ );
        }

        boost::filesystem::path path_;
    };

} // namespace

// ---------------------------------------------------------------------------
// RoundTripSerialization — SerializeByteVector → DeSerializeByteVector
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, RoundTripSerialization )
{
    // Build a well-formed DAG struct with a 128-hex source address.
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( std::string( 128, 'a' ) );
    dag.set_nonce( 0 );
    dag.set_timestamp( 1721000000 );

    // Build metadata with all optional fields populated.
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "test_game" );
    metadata.set_publisher_id( "test_publisher" );
    metadata.set_dev_wallet( std::string( 128, 'd' ) );
    metadata.set_peers_cut( 5000 );

    std::string main_address( 128, 'b' ); // 128-hex main pubkey

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );

    // Serialize and deserialize.
    auto  serialized = tx.SerializeByteVector();
    auto  deserialized = RegistrationTransaction::DeSerializeByteVector( serialized );
    ASSERT_NE( deserialized, nullptr );

    EXPECT_EQ( deserialized->GetMainAddress(), main_address );
    EXPECT_EQ( deserialized->GetSequence(), 1 );
    EXPECT_EQ( deserialized->GetMetadata().game_id(), "test_game" );
    EXPECT_EQ( deserialized->GetMetadata().publisher_id(), "test_publisher" );
    EXPECT_EQ( deserialized->GetMetadata().dev_wallet(), std::string( 128, 'd' ) );
    EXPECT_EQ( deserialized->GetMetadata().peers_cut(), 5000 );
}

// ---------------------------------------------------------------------------
// FactoryFillHash — New() calls FillHash; GetHash is non-empty and GetType is "registration"
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, FactoryFillHash )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( std::string( 128, 'c' ) );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "factory_test" );

    auto tx = RegistrationTransaction::New( std::string( 128, 'm' ), 42, metadata, dag );

    EXPECT_FALSE( tx.GetHash().empty() );
    EXPECT_EQ( tx.GetType(), "registration" );
}

// ---------------------------------------------------------------------------
// SerializeToEmbeddedTransaction — oneof registration() set correctly
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, SerializeToEmbeddedTransaction )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( std::string( 128, 'e' ) );
    dag.set_nonce( 7 );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "embedded_test" );

    std::string main_address( 128, 'f' );
    auto        tx = RegistrationTransaction::New( main_address, 99, metadata, dag );

    auto embedded = tx.SerializeToEmbeddedTransaction();

    // The registration oneof arm should be set.
    EXPECT_EQ( embedded.transaction_case(), EmbeddedTransaction::kRegistration );
    EXPECT_EQ( embedded.registration().main_address(), main_address );
    EXPECT_EQ( embedded.registration().sequence(), 99 );
    EXPECT_EQ( embedded.registration().metadata().game_id(), "embedded_test" );
}

// ---------------------------------------------------------------------------
// GetTopics — includes main_address_
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, GetTopics )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( std::string( 128, 'g' ) );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    std::string                        main_address( 128, 'h' );

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );

    auto topics = tx.GetTopics();
    EXPECT_NE( topics.find( main_address ), topics.end() );
}
