#include <gtest/gtest.h>

#include <array>

#include "base/blob.hpp" // for sgns::base::Hash256
#include "account/UTXOManager.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/TokenID.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "testutil/storage/base_crdt_test.hpp"

using namespace sgns;
using namespace sgns::base;

// Test constants
static constexpr std::string_view PRIV_KEY = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
static const Hash256              DUMMY_HASH{};
static const TokenID              TOKEN_1 = TokenID::FromBytes( { 0x01 } );
static crypto::HasherImpl         HASHER;

class UTXOManagerTest : public test::CRDTFixture
{
public:
    UTXOManagerTest() : CRDTFixture( "utxo_crdt_test" )
    {
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
        auto result = utxo_manager->LoadUTXOs( db_->GetDataStore() );
        if ( result.has_error() )
        {
            logger->error( "Error when loading UTXOs" );
            throw result.error();
        }
        if ( result.value() )
        {
            logger->error( "DB already contained UTXOs" );
            throw "DB already contained UTXOs";
        }
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
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, TOKEN_1 ) ).value() );
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( utxo_manager->GetBalance(), 140ull );
}

TEST_F( UTXOManagerTest, BalanceByToken )
{
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, TOKEN_1 ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ).value() );
    EXPECT_EQ( utxo_manager->GetBalance( TOKEN_1 ), 70ull );
    EXPECT_EQ( utxo_manager->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 30ull );
    EXPECT_EQ( utxo_manager->GetBalance( sgns::TokenID::FromBytes( { 0x03 } ) ), 40ull );
}

TEST_F( UTXOManagerTest, BalanceByTokenNonexistent )
{
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, TOKEN_1 ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ).value() );
    EXPECT_EQ( utxo_manager->GetBalance( sgns::TokenID::FromBytes( { 0xFF } ) ), 0ull );
}

TEST_F( UTXOManagerTest, StringTemplateBalance )
{
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 50, sgns::TokenID::FromBytes( { 0x02 } ) ) ).value() );
    std::string s = std::to_string( utxo_manager->GetBalance() );
    EXPECT_EQ( s, std::to_string( utxo_manager->GetBalance() ) );
}

TEST_F( UTXOManagerTest, RefreshNoUTXOsLeavesAll )
{
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ).value() );
    size_t before = utxo_manager->GetUTXOs().size();
    utxo_manager->ConsumeUTXOs( {} );
    EXPECT_EQ( utxo_manager->GetUTXOs().size(), before );
}

TEST_F( UTXOManagerTest, RefreshPartialUTXOsRemovesOnlySpecified )
{
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ).value() );
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, TOKEN_1 ) ).value() );
    InputUTXOInfo info;
    info.txid_hash_  = DUMMY_HASH;
    info.output_idx_ = 1; // remove idx 1
    utxo_manager->ConsumeUTXOs( { info } );
    EXPECT_EQ( utxo_manager->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 0ull );
    EXPECT_EQ( utxo_manager->GetBalance( TOKEN_1 ), 70ull );
}

TEST_F( UTXOManagerTest, RefreshAllUTXOsRemovesAll )
{
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ) ).value() );
    EXPECT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ).value() );
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
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( HASHER.sha2_256( {} ), 0, 420, TOKEN_1 ) ).value() );
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
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 420, TOKEN_1 ) ).value() );
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 420, TOKEN_1 ) ).value() );

    auto res = utxo_manager->LoadUTXOs( db_->GetDataStore() );
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

    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( hash_a, 0, 55, TOKEN_1 ) ) );
    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( hash_b, 1, 77, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );

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
    const auto                   tx_hash = HASHER.sha2_256( gsl::span<const uint8_t>( seed_tx ) );
    const auto                   registry_hash = HASHER.sha2_256( gsl::span<const uint8_t>( seed_registry ) );

    EXPECT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( tx_hash, 0, 123, TOKEN_1 ) ) );

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

// ── Phase 5: Startup Wiring + Mock RPC Transport tests ─────────────────────

TEST_F( UTXOManagerTest, IsOutPointReservedRejectsReadyState )
{
    // Insert a UTXO (always enters READY state via PutUTXO)
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ) ).value() );
    // IsOutPointReserved should return false for READY entries
    EXPECT_FALSE( utxo_manager->IsOutPointReserved( DUMMY_HASH, 0 ) );
}

TEST_F( UTXOManagerTest, IsOutPointReservedRejectsConsumedState )
{
    // Insert a UTXO, then consume it
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 200, TOKEN_1 ) ).value() );
    InputUTXOInfo info;
    info.txid_hash_  = DUMMY_HASH;
    info.output_idx_ = 1;
    utxo_manager->ConsumeUTXOs( { info } );
    // IsOutPointReserved should return false for CONSUMED entries
    EXPECT_FALSE( utxo_manager->IsOutPointReserved( DUMMY_HASH, 1 ) );
}

TEST_F( UTXOManagerTest, IsOutPointReservedRejectsNonexistent )
{
    // Non-existent outpoint should return false (not crash)
    EXPECT_FALSE( utxo_manager->IsOutPointReserved( DUMMY_HASH, 999 ) );
}

TEST_F( UTXOManagerTest, PutUTXOWithBridgeType )
{
    // Insert a UTXO with UTXO_BRIDGE type for a foreign address
    const std::string foreign = "bridge_test_address";
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( DUMMY_HASH, 10, 500, TOKEN_1 ),
                                foreign,
                                UTXOManager::UTXOType::UTXO_BRIDGE )
                     .value() );
    // Verify the UTXO appears in GetUTXOs for that address
    auto utxos = utxo_manager->GetUTXOs( foreign );
    EXPECT_EQ( utxos.size(), 1u );
    EXPECT_EQ( utxos[0].GetAmount(), 500u );

    // Also verify we can insert with default UTXO_NORMAL type
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( DUMMY_HASH, 11, 300, TOKEN_1 ),
                                foreign,
                                UTXOManager::UTXOType::UTXO_NORMAL )
                     .value() );
    EXPECT_EQ( utxo_manager->GetUTXOs( foreign ).size(), 2u );
}

TEST_F( UTXOManagerTest, ForeignAddressPutUTXO )
{
    // Insert a UTXO for an address that is NOT the node's own address
    const std::string foreign = "0xdeadbeef_foreign_address";
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( DUMMY_HASH, 20, 777, TOKEN_1 ), foreign )
                     .value() );
    // Verify the UTXO is tracked under the foreign address
    auto foreign_utxos = utxo_manager->GetUTXOs( foreign );
    EXPECT_EQ( foreign_utxos.size(), 1u );
    EXPECT_EQ( foreign_utxos[0].GetAmount(), 777u );
    EXPECT_EQ( foreign_utxos[0].GetOutputIdx(), 20u );

    // Verify the node's own UTXOs are not affected
    EXPECT_EQ( utxo_manager->GetUTXOs().size(), 0u );
}

TEST_F( UTXOManagerTest, ConsumedUTXOsExcludedFromBalance )
{
    // Insert UTXOs and consume one; verify balance excludes consumed entries
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ) ).value() );
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 200, TOKEN_1 ) ).value() );
    EXPECT_EQ( utxo_manager->GetBalance(), 300u );

    InputUTXOInfo consumed;
    consumed.txid_hash_  = DUMMY_HASH;
    consumed.output_idx_ = 0;
    utxo_manager->ConsumeUTXOs( { consumed } );

    // Balance should now exclude the consumed UTXO
    EXPECT_EQ( utxo_manager->GetBalance(), 200u );
    // GetUTXOs should also exclude it (state != UTXO_READY)
    EXPECT_EQ( utxo_manager->GetUTXOs().size(), 1u );
}

TEST_F( UTXOManagerTest, GetAllUTXOsIncludesBridgeTypeEntries )
{
    // Insert a BRIDGE-type UTXO and verify GetAllUTXOs tracks it
    const std::string bridge_addr = "bridge_catchup_address";
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( DUMMY_HASH, 30, 1000, TOKEN_1 ),
                                bridge_addr,
                                UTXOManager::UTXOType::UTXO_BRIDGE )
                     .value() );

    auto all = utxo_manager->GetAllUTXOs();
    // Verify the bridge address exists in the map
    auto it = all.find( bridge_addr );
    ASSERT_NE( it, all.end() );
    EXPECT_EQ( it->second.size(), 1u );
    // Verify the state is READY (PutUTXO always sets READY)
    EXPECT_EQ( it->second[0].first, UTXOManager::UTXOState::UTXO_READY );
    EXPECT_EQ( it->second[0].second.GetAmount(), 1000u );
}
