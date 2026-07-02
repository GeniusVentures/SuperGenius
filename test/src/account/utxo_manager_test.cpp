#include <gtest/gtest.h>

#include <array>

#include "base/blob.hpp" // for sgns::base::Hash256
#include "account/UTXOManager.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/TokenID.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "testutil/storage/base_rocksdb_test.hpp"

using namespace sgns;
using namespace sgns::base;

// Test constants
static constexpr std::string_view PRIV_KEY = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
static const Hash256              DUMMY_HASH{};
static const TokenID              TOKEN_1 = TokenID::FromBytes( { 0x01 } );
static crypto::HasherImpl         HASHER;

std::vector<GeniusUTXO> BalanceUTXOs()
{
    return {
        GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ),
        GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ),
        GeniusUTXO( DUMMY_HASH, 2, 20, TOKEN_1 ),
        GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ),
    };
}

class UTXOManagerTest : public test::RocksDBFixture
{
public:
    UTXOManagerTest() : RocksDBFixture( "utxo_manager_test" ) {}

    void SetUp() override
    {
        RocksDBFixture::SetUp();
        utxo_manager = std::make_shared<UTXOManager>(
            true,
            std::string( PRIV_KEY ),
            []( const std::vector<uint8_t> &data )
            {
                auto hashed = HASHER.sha2_256( data );
                return std::vector( hashed.begin(), hashed.end() );
            },
            []( const std::string &_, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
            {
                auto hashed = HASHER.sha2_256( data );
                return signature == std::vector( hashed.begin(), hashed.end() );
            } );

        auto result = utxo_manager->LoadUTXOs( db_ );
        ASSERT_TRUE( result.has_value() ) << result.error().message();
        ASSERT_FALSE( result.value() ) << "DB already contained UTXOs";
    }

    ~UTXOManagerTest() override = default;

    std::shared_ptr<UTXOManager> utxo_manager;
};

TEST_F( UTXOManagerTest, InitialUTXOCount )
{
    // Insert four unique UTXOs
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, sgns::TokenID::FromBytes( { 0x03 } ) ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x04 } ) ) ).value() );
    // Duplicate should be ignored
    EXPECT_FALSE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_EQ( utxo_manager->GetUTXOs().size(), 4u );
}

TEST_F( UTXOManagerTest, TotalBalance )
{
    ASSERT_TRUE( utxo_manager->SetUTXOs( BalanceUTXOs() ).has_value() );
    EXPECT_EQ( utxo_manager->GetBalance(), 140ull );
}

TEST_F( UTXOManagerTest, BalanceByToken )
{
    ASSERT_TRUE( utxo_manager->SetUTXOs( BalanceUTXOs() ).has_value() );
    EXPECT_EQ( utxo_manager->GetBalance( TOKEN_1 ), 70ull );
    EXPECT_EQ( utxo_manager->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 30ull );
    EXPECT_EQ( utxo_manager->GetBalance( sgns::TokenID::FromBytes( { 0x03 } ) ), 40ull );
}

TEST_F( UTXOManagerTest, BalanceByTokenNonexistent )
{
    ASSERT_TRUE( utxo_manager->SetUTXOs( BalanceUTXOs() ).has_value() );
    EXPECT_EQ( utxo_manager->GetBalance( sgns::TokenID::FromBytes( { 0xFF } ) ), 0ull );
}

TEST_F( UTXOManagerTest, StringTemplateBalance )
{
    ASSERT_TRUE( utxo_manager
                     ->SetUTXOs( {
                         GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ),
                         GeniusUTXO( DUMMY_HASH, 1, 50, sgns::TokenID::FromBytes( { 0x02 } ) ),
                     } )
                     .has_value() );
    std::string s = std::to_string( utxo_manager->GetBalance() );
    EXPECT_EQ( s, std::to_string( utxo_manager->GetBalance() ) );
}

TEST_F( UTXOManagerTest, RefreshNoUTXOsLeavesAll )
{
    ASSERT_TRUE( utxo_manager
                     ->SetUTXOs( {
                         GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ),
                         GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ),
                     } )
                     .has_value() );
    size_t before = utxo_manager->GetUTXOs().size();
    utxo_manager->ConsumeUTXOs( {} );
    EXPECT_EQ( utxo_manager->GetUTXOs().size(), before );
}

TEST_F( UTXOManagerTest, RefreshPartialUTXOsRemovesOnlySpecified )
{
    ASSERT_TRUE( utxo_manager
                     ->SetUTXOs( {
                         GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ),
                         GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ),
                         GeniusUTXO( DUMMY_HASH, 2, 20, TOKEN_1 ),
                     } )
                     .has_value() );
    InputUTXOInfo info;
    info.txid_hash_  = DUMMY_HASH;
    info.output_idx_ = 1; // remove idx 1
    utxo_manager->ConsumeUTXOs( { info } );
    EXPECT_EQ( utxo_manager->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 0ull );
    EXPECT_EQ( utxo_manager->GetBalance( TOKEN_1 ), 70ull );
}

TEST_F( UTXOManagerTest, RefreshAllUTXOsRemovesAll )
{
    ASSERT_TRUE( utxo_manager
                     ->SetUTXOs( {
                         GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ),
                         GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ),
                     } )
                     .has_value() );
    std::vector<InputUTXOInfo> infos;
    for ( const auto &utxo : utxo_manager->GetUTXOs() )
    {
        InputUTXOInfo i;
        i.txid_hash_  = utxo.GetTxID();
        i.output_idx_ = utxo.GetOutputIdx();
        infos.push_back( i );
    }
    ASSERT_FALSE( utxo_manager->ConsumeUTXOs( infos ).has_error() );
    EXPECT_TRUE( utxo_manager->GetUTXOs().empty() );
    EXPECT_EQ( utxo_manager->GetBalance(), 0ull );
}

TEST_F( UTXOManagerTest, VerifyParameters )
{
    ASSERT_TRUE( utxo_manager->SetUTXOs( { GeniusUTXO( HASHER.sha2_256( {} ), 0, 420, TOKEN_1 ) } ).has_value() );
    auto tx = utxo_manager->CreateTxParameter( 69, "foobar", TOKEN_1 );
    EXPECT_TRUE( tx.has_value() );
    EXPECT_TRUE( utxo_manager->VerifyParameters( tx.value() ) );

    // Fails because amount is incorrect
    tx.value().second[0].encrypted_amount = 420;
    EXPECT_FALSE( utxo_manager->VerifyParameters( tx.value() ) );
    tx.value().second[0].encrypted_amount = 69;

    // Fails because signature does not match
    tx.value().first[0].signature_ = { 1, 2, 3 };
    EXPECT_FALSE( utxo_manager->VerifyParameters( tx.value() ) );
}

TEST_F( UTXOManagerTest, Storage )
{
    ASSERT_TRUE( utxo_manager
                     ->SetUTXOs( {
                         GeniusUTXO( DUMMY_HASH, 0, 420, TOKEN_1 ),
                         GeniusUTXO( DUMMY_HASH, 1, 420, TOKEN_1 ),
                     } )
                     .has_value() );

    auto res = utxo_manager->LoadUTXOs( db_ );
    EXPECT_TRUE( res.has_value() );
    EXPECT_TRUE( res.value() );

    auto utxos = utxo_manager->GetUTXOs();
    EXPECT_EQ( utxos.size(), 2 );
}

TEST_F( UTXOManagerTest, MerkleRootDeterministicAcrossInsertionOrder )
{
    const std::array<uint8_t, 1> seed_a{ 0xA1 };
    const std::array<uint8_t, 1> seed_b{ 0xB2 };
    const auto                   hash_a = HASHER.sha2_256( gsl::span<const uint8_t>( seed_a ) );
    const auto                   hash_b = HASHER.sha2_256( gsl::span<const uint8_t>( seed_b ) );

    std::vector<GeniusUTXO> ordered_a{
        GeniusUTXO( hash_a, 0, 100, TOKEN_1 ),
        GeniusUTXO( hash_b, 1, 200, sgns::TokenID::FromBytes( { 0x02 } ) ),
    };

    std::vector<GeniusUTXO> ordered_b{
        GeniusUTXO( hash_b, 1, 200, sgns::TokenID::FromBytes( { 0x02 } ) ),
        GeniusUTXO( hash_a, 0, 100, TOKEN_1 ),
    };

    ASSERT_TRUE( utxo_manager->SetUTXOs( ordered_a ).has_value() );
    auto root_a = utxo_manager->ComputeUTXOMerkleRoot();

    ASSERT_TRUE( utxo_manager->SetUTXOs( ordered_b ).has_value() );
    auto root_b = utxo_manager->ComputeUTXOMerkleRoot();

    EXPECT_EQ( root_a, root_b );
}

TEST_F( UTXOManagerTest, MerkleRootChangesWhenUTXOSetChanges )
{
    const std::array<uint8_t, 1> seed_a{ 0xC3 };
    const std::array<uint8_t, 1> seed_b{ 0xD4 };
    const auto                   hash_a = HASHER.sha2_256( gsl::span<const uint8_t>( seed_a ) );
    const auto                   hash_b = HASHER.sha2_256( gsl::span<const uint8_t>( seed_b ) );

    ASSERT_TRUE( utxo_manager
                     ->SetUTXOs( {
                         GeniusUTXO( hash_a, 0, 55, TOKEN_1 ),
                         GeniusUTXO( hash_b, 1, 77, sgns::TokenID::FromBytes( { 0x03 } ) ),
                     } )
                     .has_value() );

    const auto root_before = utxo_manager->ComputeUTXOMerkleRoot();

    InputUTXOInfo spent;
    spent.txid_hash_  = hash_a;
    spent.output_idx_ = 0;
    utxo_manager->ConsumeUTXOs( { spent } );

    const auto root_after = utxo_manager->ComputeUTXOMerkleRoot();
    EXPECT_NE( root_before, root_after );
}

TEST_F( UTXOManagerTest, CheckpointRoundtrip )
{
    const std::array<uint8_t, 1> seed_tx{ 0x11 };
    const std::array<uint8_t, 1> seed_registry{ 0x22 };
    const auto                   tx_hash       = HASHER.sha2_256( gsl::span<const uint8_t>( seed_tx ) );
    const auto                   registry_hash = HASHER.sha2_256( gsl::span<const uint8_t>( seed_registry ) );

    ASSERT_TRUE( utxo_manager->SetUTXOs( { GeniusUTXO( tx_hash, 0, 123, TOKEN_1 ) } ).has_value() );

    ASSERT_TRUE( utxo_manager->CreateCheckpoint( 7, tx_hash, registry_hash ).has_value() );
    auto checkpoint_res = utxo_manager->LoadLatestCheckpoint();
    ASSERT_TRUE( checkpoint_res.has_value() );
    ASSERT_TRUE( checkpoint_res.value().has_value() );

    const auto &checkpoint = checkpoint_res.value().value();
    EXPECT_EQ( checkpoint.owner_address, std::string( PRIV_KEY ) );
    EXPECT_EQ( checkpoint.epoch, 7u );
    EXPECT_EQ( checkpoint.last_finalized_tx, tx_hash );
    EXPECT_EQ( checkpoint.registry_hash, registry_hash );
    EXPECT_EQ( checkpoint.utxo_count, 1u );
    EXPECT_GT( checkpoint.created_at_ms, 0u );
}

TEST( GeniusUTXO, PropertyAccessors )
{
    uint32_t   idx = 5;
    uint64_t   amt = 12345;
    auto       tok = TokenID::FromBytes( { 0x01, 0x02 } );
    GeniusUTXO utxo( DUMMY_HASH, idx, amt, tok );
    EXPECT_EQ( utxo.GetTxID(), DUMMY_HASH );
    EXPECT_EQ( utxo.GetOutputIdx(), idx );
    EXPECT_EQ( utxo.GetAmount(), amt );
    EXPECT_EQ( utxo.GetTokenID(), tok );
    EXPECT_TRUE( utxo.GetOwnerAddress().empty() );
}

TEST( InputUTXOInfo, FieldAssignment )
{
    InputUTXOInfo info;
    info.txid_hash_  = DUMMY_HASH;
    info.output_idx_ = 2;
    EXPECT_EQ( info.txid_hash_, DUMMY_HASH );
    EXPECT_EQ( info.output_idx_, 2u );
}
