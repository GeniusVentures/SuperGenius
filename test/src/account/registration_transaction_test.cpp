/**
 * @file       registration_transaction_test.cpp
 * @brief      Unit tests for RegistrationTransaction — factory, serialization round-trip, topics,
 *             filter gates, and end-to-end registration flow.
 * @date       2026-07-15
 * @author     (Phase 4)
 */
#include <gtest/gtest.h>

#include <boost/filesystem/operations.hpp>
#include <cassert>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include "account/GeniusAccount.hpp"
#include "account/RegistrationTransaction.hpp"
#include "account/TransactionManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "blockchain/Consensus.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"

namespace
{
    using namespace sgns;

    /// Test token identifier.
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

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

// ===================================================================
// E2E Integration Tests (CRDT-backed fixture)
// ===================================================================

namespace sgns
{
    /**
     * @brief Friend accessor for private TransactionManager methods needed by E2E tests.
     */
    class RegistrationE2ETestAccess
    {
    public:
        static std::shared_ptr<GeniusTransaction> GetTransactionByHash(
            TransactionManager &tm,
            const std::string  &hash )
        {
            return tm.GetTransactionByHash( hash );
        }

        static std::optional<std::vector<crdt::pb::Element>> FilterRegistration(
            TransactionManager      &tm,
            const crdt::pb::Element &element )
        {
            return tm.FilterRegistration( element );
        }
    };
} // namespace sgns

namespace
{
    using namespace sgns;

    /**
     * @brief CRDT-backed fixture for end-to-end registration transaction tests.
     *
     * Creates a GeniusAccount, Blockchain, and TransactionManager using the
     * CRDT test infrastructure (no GeniusNode, no network sync).
     */
    class RegistrationTransactionE2ETest : public test::CRDTFixture
    {
    public:
        RegistrationTransactionE2ETest()
            : CRDTFixture( "reg_tx_e2e_test" )
        {
            GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                { return std::make_shared<MemorySecureStorage>( identifier ); } );

            account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
            assert( account_ != nullptr );

            auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
            assert( load_result.has_value() );

            blockchain_ = Blockchain::New(
                db_, account_, pubs_, []( outcome::result<void> ) {} );
            assert( blockchain_ != nullptr );

            constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
            constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

            tm_ = TransactionManager::New(
                db_, io_, account_,
                blockchain_,
                true,  // full_node — enables isolated boot in CheckNonce() when FetchNetworkNonce fails
                0,     // subnet_id
                kTimestampTolerance,
                kMutabilityWindow );
            assert( tm_ != nullptr );

            // Run the io_context on a worker thread so TickOnce() (self-reposting) drives the
            // TransactionManager state machine through INITIALIZING → READY.
            work_guard_.emplace( boost::asio::make_work_guard( *io_ ) );
            io_thread_  = std::thread( [this]() { io_->run(); } );
        }

        void TearDown() override
        {
            if ( tm_ )
                tm_->Stop();
            work_guard_.reset();
            if ( io_thread_.joinable() )
                io_thread_.join();
            GeniusAccount::SetSecureStorageFactory( nullptr );
        }

        std::shared_ptr<GeniusAccount>      account_;
        std::shared_ptr<Blockchain>         blockchain_;
        std::shared_ptr<TransactionManager> tm_;

        std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;
        std::thread io_thread_;
    };

} // namespace

// ---------------------------------------------------------------------------
// ChildRegistrationEndToEnd — verifies the full RegisterChild flow
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, ChildRegistrationEndToEnd )
{
    // Start the TransactionManager and poll for READY state.
    // TickOnce() (self-reposting on the io_context worker thread) drives the state machine
    // through INITIALIZING → InitTransactions → READY. CheckNonce() blocks ~5s on network
    // nonce fetch, so allow generous timeout.
    tm_->Start();

    TransactionManager::State state = tm_->GetState();
    auto                      start = std::chrono::steady_clock::now();
    constexpr auto            kReadyTimeout = std::chrono::seconds( 60 );

    while ( state != TransactionManager::State::READY )
    {
        if ( std::chrono::steady_clock::now() - start > kReadyTimeout )
        {
            break;
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        state = tm_->GetState();
    }

    ASSERT_EQ( state, TransactionManager::State::READY )
        << "TransactionManager did not reach READY state within "
        << kReadyTimeout.count() << "s";

    // Registration data
    std::string                        main_address( 128, 'a' ); // 128-hex main pubkey
    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "e2e_test_game" );

    // Register child via TransactionManager
    auto result = tm_->RegisterChild( main_address, metadata, 1 );
    ASSERT_TRUE( result.has_value() );
    std::string tx_hash = result.value();
    EXPECT_FALSE( tx_hash.empty() );

    // Query the transaction by hash
    auto tx = RegistrationE2ETestAccess::GetTransactionByHash( *tm_, tx_hash );
    ASSERT_NE( tx, nullptr );

    // Verify status is SENDING (processed asynchronously by TickOnce READY branch — poll briefly)
    {
        auto           status = tm_->GetTransactionStatusByTxId( tx_hash );
        auto           tstart = std::chrono::steady_clock::now();
        constexpr auto kStatusTimeout = std::chrono::seconds( 10 );

        while ( status != TransactionManager::TransactionStatus::SENDING )
        {
            if ( std::chrono::steady_clock::now() - tstart > kStatusTimeout )
            {
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
            status = tm_->GetTransactionStatusByTxId( tx_hash );
        }
        EXPECT_EQ( status, TransactionManager::TransactionStatus::SENDING );
    }

    // Downcast to RegistrationTransaction
    auto reg_tx = std::dynamic_pointer_cast<RegistrationTransaction>( tx );
    ASSERT_NE( reg_tx, nullptr );

    // Verify transaction fields
    EXPECT_EQ( reg_tx->GetType(), "registration" );
    EXPECT_EQ( reg_tx->GetMainAddress(), main_address );
    EXPECT_EQ( reg_tx->GetSequence(), 1 );
    EXPECT_EQ( reg_tx->GetMetadata().game_id(), "e2e_test_game" );

    // Verify DAG struct fields
    // Nonce starts at 0 for a fresh account with no prior transactions.
    EXPECT_EQ( reg_tx->GetNonce(), 0ULL );
    EXPECT_FALSE( reg_tx->GetSrcAddress().empty() );
    EXPECT_EQ( reg_tx->GetSrcAddress(), account_->GetAddress() );
}

// ---------------------------------------------------------------------------
// ChildRegistrationTamperedSignatureRejected — per D-45: tampered sig fails
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionTest, ChildRegistrationTamperedSignatureRejected )
{
    // Create GeniusAccount (real keypair)
    auto account = GeniusAccount::New( kTestTokenId, path_ / "sig_acct" );
    ASSERT_NE( account, nullptr );

    // Build a valid RegistrationTx with the account's actual address as source_addr
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "sig_test" );

    std::string main_address( 128, 'm' );

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );

    // MakeSignature modifies dag_st in-place with the signature.
    // After signing, dag_st and the internal proto are updated.
    tx.MakeSignature( *account );

    // Verify the signature is currently valid
    EXPECT_TRUE( tx.CheckSignature() )
        << "Valid signature should pass CheckSignature";

    // Now tamper the DAG struct's signature by modifying it post-signing.
    // Serialize to get a clean snapshot, then modify the signature in the
    // proto payload.
    auto serialized = tx.SerializeByteVector();

    // Deserialize and modify the DAG signature directly in the proto struct
    SGTransaction::RegistrationTx tx_struct;
    ASSERT_TRUE( tx_struct.ParseFromArray( serialized.data(), serialized.size() ) )
        << "Should parse valid serialized RegistrationTx";

    // Tamper the DAG signature — flip the last byte of the signature
    auto *dag_mutable = tx_struct.mutable_dag_struct();
    std::string sig = dag_mutable->signature();
    ASSERT_FALSE( sig.empty() ) << "Signature should be non-empty after signing";
    sig[sig.size() - 1] ^= 0xFF;
    dag_mutable->set_signature( sig );

    // Re-serialize the tampered proto
    size_t size = tx_struct.ByteSizeLong();
    std::vector<uint8_t> tampered_bytes( size );
    ASSERT_TRUE( tx_struct.SerializeToArray( tampered_bytes.data(), tampered_bytes.size() ) );

    // Deserialize the tampered bytes
    auto deserialized = RegistrationTransaction::DeSerializeByteVector( tampered_bytes );
    ASSERT_NE( deserialized, nullptr )
        << "Tampered protobuf should still parse (only signature field modified)";

    // CheckSignature should detect the tampered signature
    EXPECT_FALSE( deserialized->CheckSignature() )
        << "Tampered signature should fail CheckSignature verification";
}

// ---------------------------------------------------------------------------
// FilterRegistrationAcceptsValid — valid RegistrationTx passes filter
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationAcceptsValid )
{
    // Build a well-formed RegistrationTx
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    metadata.set_game_id( "filter_test" );

    std::string main_address( 128, 'v' ); // 128-hex (valid)

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );
    tx.MakeSignature( *account_ );

    // Serialize the tx
    auto serialized = tx.SerializeByteVector();

    // Construct a CRDT element in the reg/ namespace
    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized.begin(), serialized.end() ) );

    // Call FilterRegistration via accessor
    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Valid registration should be accepted (nullopt)
    EXPECT_FALSE( result.has_value() )
        << "Valid RegistrationTx should be accepted (std::nullopt)";
}

// ---------------------------------------------------------------------------
// FilterRegistrationRejectsBadMainAddress — malformed main_address gets tombstone
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsBadMainAddress )
{
    // Build a RegistrationTx with a malformed main_address (not 128 hex chars)
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;

    // main_address is only 64 chars — not a valid 128-hex public key
    std::string bad_main_address( 64, 'b' );

    auto tx = RegistrationTransaction::New( bad_main_address, 1, metadata, dag );
    tx.MakeSignature( *account_ );

    // Serialize
    auto serialized = tx.SerializeByteVector();

    // Construct CRDT element
    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized.begin(), serialized.end() ) );

    // Call FilterRegistration via accessor
    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Malformed main_address should be rejected (tombstone)
    EXPECT_TRUE( result.has_value() )
        << "RegistrationTx with malformed main_address should be tombstoned";
}

// ---------------------------------------------------------------------------
// FilterRegistrationRejectsTamperedSignature — tampered sig gets tombstone
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsTamperedSignature )
{
    // Build a valid RegistrationTx
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    std::string                        main_address( 128, 't' );

    auto tx = RegistrationTransaction::New( main_address, 1, metadata, dag );
    tx.MakeSignature( *account_ );

    // Serialize and tamper
    auto serialized = tx.SerializeByteVector();
    if ( serialized.size() > 10 )
    {
        serialized[serialized.size() - 5] ^= 0xFF;
    }

    // Construct CRDT element
    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized.begin(), serialized.end() ) );

    // Call FilterRegistration via accessor — should reject with tombstone
    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    EXPECT_TRUE( result.has_value() )
        << "RegistrationTx with tampered signature should be tombstoned";
}

// ---------------------------------------------------------------------------
// Gate (d): FilterRegistrationRejectsZeroSequence
// sequence == 0 is always rejected as a well-formed check
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsZeroSequence )
{
    SGTransaction::DAGStruct dag;
    dag.set_type( "registration" );
    dag.set_source_addr( account_->GetAddress() );
    dag.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata;
    std::string main_address( 128, 'z' ); // 128-hex

    auto tx = RegistrationTransaction::New( main_address, 0, metadata, dag );
    tx.MakeSignature( *account_ );

    auto serialized = tx.SerializeByteVector();

    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized.begin(), serialized.end() ) );

    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Zero sequence should always be rejected (tombstone)
    EXPECT_TRUE( result.has_value() )
        << "RegistrationTx with sequence=0 should be tombstoned";
}

// ---------------------------------------------------------------------------
// Gate (d): FilterRegistrationRejectsNonMonotonicSequence
// Pre-populate CRDT with sequence=4, then test sequence=3 → tombstone
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationRejectsNonMonotonicSequence )
{
    // Build a RegistrationTx with sequence=4 to pre-populate CRDT
    SGTransaction::DAGStruct dag4;
    dag4.set_type( "registration" );
    dag4.set_source_addr( account_->GetAddress() );
    dag4.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata4;
    std::string main_address( 128, 'm' );

    auto existing_tx = RegistrationTransaction::New( main_address, 4, metadata4, dag4 );
    existing_tx.MakeSignature( *account_ );

    // Write directly to GlobalDB at the reg/ key
    std::string reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    auto serialized_existing = existing_tx.SerializeByteVector();
    base::Buffer existing_buffer( std::vector<uint8_t>( serialized_existing.begin(), serialized_existing.end() ) );
    crdt::HierarchicalKey hk( reg_key );
    auto put_result = db_->Put( hk, existing_buffer, {} );
    ASSERT_TRUE( put_result.has_value() ) << "Pre-population Put should succeed";

    // Now create a new element with sequence=3 (lower than stored 4)
    SGTransaction::DAGStruct dag3;
    dag3.set_type( "registration" );
    dag3.set_source_addr( account_->GetAddress() );
    dag3.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata3;
    auto new_tx = RegistrationTransaction::New( main_address, 3, metadata3, dag3 );
    new_tx.MakeSignature( *account_ );

    auto serialized_new = new_tx.SerializeByteVector();

    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized_new.begin(), serialized_new.end() ) );

    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Non-monotonic sequence should be rejected (tombstone)
    EXPECT_TRUE( result.has_value() )
        << "RegistrationTx with sequence=3 <= stored=4 should be tombstoned";
}

// ---------------------------------------------------------------------------
// Gate (d): FilterRegistrationAcceptsHigherSequence
// Pre-populate CRDT with sequence=4, then test sequence=5 → accepted
// ---------------------------------------------------------------------------
TEST_F( RegistrationTransactionE2ETest, FilterRegistrationAcceptsHigherSequence )
{
    // Build a RegistrationTx with sequence=4 to pre-populate CRDT
    SGTransaction::DAGStruct dag4;
    dag4.set_type( "registration" );
    dag4.set_source_addr( account_->GetAddress() );
    dag4.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata4;
    std::string main_address( 128, 'h' );

    auto existing_tx = RegistrationTransaction::New( main_address, 4, metadata4, dag4 );
    existing_tx.MakeSignature( *account_ );

    // Write directly to GlobalDB at the reg/ key
    std::string reg_key = TransactionManager::GetBlockChainBase() + "reg/" + account_->GetAddress();
    auto serialized_existing = existing_tx.SerializeByteVector();
    base::Buffer existing_buffer( std::vector<uint8_t>( serialized_existing.begin(), serialized_existing.end() ) );
    crdt::HierarchicalKey hk( reg_key );
    auto put_result = db_->Put( hk, existing_buffer, {} );
    ASSERT_TRUE( put_result.has_value() ) << "Pre-population Put should succeed";

    // Now create a new element with sequence=5 (higher than stored 4)
    SGTransaction::DAGStruct dag5;
    dag5.set_type( "registration" );
    dag5.set_source_addr( account_->GetAddress() );
    dag5.set_nonce( 0 );

    SGTransaction::RegistrationMetadata metadata5;
    auto new_tx = RegistrationTransaction::New( main_address, 5, metadata5, dag5 );
    new_tx.MakeSignature( *account_ );

    auto serialized_new = new_tx.SerializeByteVector();

    crdt::pb::Element element;
    element.set_key( "/bc/999/reg/" + account_->GetAddress() );
    element.set_value( std::string( serialized_new.begin(), serialized_new.end() ) );

    auto result = RegistrationE2ETestAccess::FilterRegistration( *tm_, element );

    // Higher sequence should be accepted (nullopt)
    EXPECT_FALSE( result.has_value() )
        << "RegistrationTx with sequence=5 > stored=4 should be accepted";
}
