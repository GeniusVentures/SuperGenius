/**
 * @file       utxo_manager_test.cpp
 * @brief      Unit tests for UTXOManager lifecycle, persistence, and the D-01 preserve-clear.
 * @date       2026-10-23
 */
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

#include "base/blob.hpp" // for sgns::base::Hash256
#include "account/UTXOManager.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/TokenID.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crypto/hasher.hpp"
#include "storage/database_error.hpp"
#include "testutil/storage/base_rocksdb_test.hpp"

using namespace sgns;
using namespace sgns::base;

namespace sgns
{
    class UTXOManagerTestAccess
    {
    public:
        static void SetConsumeUTXOsBeforeStoreHook( UTXOManager &manager, std::function<void()> hook )
        {
            manager.SetConsumeUTXOsBeforeStoreHookForTest( std::move( hook ) );
        }

        static void SetPutUTXOBeforeStoreHook( UTXOManager &manager, std::function<void()> hook )
        {
            manager.SetPutUTXOBeforeStoreHookForTest( std::move( hook ) );
        }
    };
} // namespace sgns

// Test constants
static constexpr std::string_view PRIV_KEY = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
static const Hash256              DUMMY_HASH{};
static const TokenID              TOKEN_1 = TokenID::FromBytes( { 0x01 } );

std::vector<GeniusUTXO> BalanceUTXOs()
{
    return {
        GeniusUTXO( DUMMY_HASH, 0, 50, TOKEN_1 ),
        GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ),
        GeniusUTXO( DUMMY_HASH, 2, 20, TOKEN_1 ),
        GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ),
    };
}

namespace
{
    // Raw /utxo/<owner>/ prefix read straight off the fixture's RocksDB handle:
    // the anti-vacuous disk check for the preserve-clear suites — the consumed
    // record (and its spent_by_txid) must survive on disk, not just in memory.
    std::map<std::string, SGTransaction::UTXOEntryRecord> ReadRawUtxoRecords(
        const std::shared_ptr<storage::rocksdb> &db,
        const std::string                       &owner )
    {
        std::map<std::string, SGTransaction::UTXOEntryRecord> parsed;
        base::Buffer                                          prefix;
        prefix.put( "/utxo/" + owner + "/" );
        auto records = db->query( prefix );
        if ( records.has_error() )
        {
            // An absent prefix reads NOT_FOUND; anything louder is a real failure.
            EXPECT_EQ( records.error(), storage::DatabaseError::NOT_FOUND );
            return parsed;
        }
        for ( const auto &[key, value] : records.value() )
        {
            SGTransaction::UTXOEntryRecord record;
            if ( !record.ParseFromArray( value.data(), static_cast<int>( value.size() ) ) )
            {
                ADD_FAILURE() << "Malformed UTXO record at key " << key.toString();
                continue;
            }
            parsed[std::string( key.toString() )] = record;
        }
        return parsed;
    }
} // namespace

class UTXOManagerTest : public test::RocksDBFixture
{
public:
    UTXOManagerTest() : RocksDBFixture( "utxo_manager_test" )
    {
    }

    void SetUp() override
    {
        RocksDBFixture::SetUp();
        utxo_manager = std::make_shared<UTXOManager>(
            std::string( PRIV_KEY ),
            []( const std::vector<uint8_t> &data )
            {
                auto hashed = crypto::sha2_256( data );
                return std::vector( hashed.begin(), hashed.end() );
            },
            []( const std::string &_, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
            {
                auto hashed = crypto::sha2_256( data );
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
    ASSERT_TRUE( utxo_manager->SetUTXOs( { GeniusUTXO( crypto::sha2_256( {} ), 0, 420, TOKEN_1 ) } ).has_value() );
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

TEST_F( UTXOManagerTest, ConsumeSnapshotSerializesWithMintPersistenceAndReload )
{
    const std::array<uint8_t, 1> spend_seed{ 0x51 };
    const std::array<uint8_t, 1> mint_seed{ 0x52 };
    const auto                   spend_hash = crypto::sha2_256( gsl::span<const uint8_t>( spend_seed ) );
    const auto                   mint_hash  = crypto::sha2_256( gsl::span<const uint8_t>( mint_seed ) );
    const GeniusUTXO             spend_utxo( spend_hash, 0, 25, TOKEN_1 );
    const GeniusUTXO             mint_utxo( mint_hash, 0, 75, TOKEN_1 );

    ASSERT_TRUE( utxo_manager->PutUTXO( spend_utxo ).value() );

    std::mutex              barrier_mutex;
    std::condition_variable barrier_cv;
    bool                    consume_snapshot_captured = false;
    bool                    release_consumer          = false;
    UTXOManagerTestAccess::SetConsumeUTXOsBeforeStoreHook( *utxo_manager,
                                                           [&]
                                                           {
                                                               std::unique_lock barrier_lock( barrier_mutex );
                                                               consume_snapshot_captured = true;
                                                               barrier_cv.notify_all();
                                                               barrier_cv.wait( barrier_lock,
                                                                                [&] { return release_consumer; } );
                                                           } );

    std::promise<void> mint_store_reached;
    auto               mint_store_reached_future = mint_store_reached.get_future();
    UTXOManagerTestAccess::SetPutUTXOBeforeStoreHook( *utxo_manager, [&] { mint_store_reached.set_value(); } );

    InputUTXOInfo spend_input;
    spend_input.txid_hash_  = spend_hash;
    spend_input.output_idx_ = 0;

    bool        consumer_succeeded = false;
    bool        mint_succeeded     = false;
    std::thread consumer_thread(
        [&]
        {
            auto result        = utxo_manager->ConsumeUTXOs( { spend_input } );
            consumer_succeeded = result.has_value() && result.value();
        } );

    {
        std::unique_lock barrier_lock( barrier_mutex );
        barrier_cv.wait( barrier_lock, [&] { return consume_snapshot_captured; } );
    }

    std::thread mint_thread(
        [&]
        {
            auto result    = utxo_manager->PutUTXO( mint_utxo );
            mint_succeeded = result.has_value() && result.value();
        } );

    // The Consume snapshot is captured while its exclusive lock remains held,
    // so Mint cannot reach its persistence hook until the consumer commits.
    EXPECT_EQ( mint_store_reached_future.wait_for( std::chrono::milliseconds( 50 ) ), std::future_status::timeout );

    {
        std::lock_guard barrier_lock( barrier_mutex );
        release_consumer = true;
    }
    barrier_cv.notify_all();
    consumer_thread.join();
    mint_thread.join();

    EXPECT_TRUE( consumer_succeeded );
    EXPECT_TRUE( mint_succeeded );
    EXPECT_EQ( mint_store_reached_future.wait_for( std::chrono::milliseconds( 0 ) ), std::future_status::ready );

    UTXOManagerTestAccess::SetConsumeUTXOsBeforeStoreHook( *utxo_manager, {} );
    UTXOManagerTestAccess::SetPutUTXOBeforeStoreHook( *utxo_manager, {} );

    auto reloaded_manager = std::make_shared<UTXOManager>(
        std::string( PRIV_KEY ),
        []( const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return std::vector( hashed.begin(), hashed.end() );
        },
        []( const std::string &, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return signature == std::vector( hashed.begin(), hashed.end() );
        } );
    ASSERT_TRUE( reloaded_manager->LoadUTXOs( db_ ).has_value() );
    EXPECT_FALSE( reloaded_manager->GetUnconsumedUTXO( spend_hash, 0 ).has_value() );
    EXPECT_TRUE( reloaded_manager->GetUnconsumedUTXO( mint_hash, 0 ).has_value() );
}

TEST_F( UTXOManagerTest, MerkleRootDeterministicAcrossInsertionOrder )
{
    const std::array<uint8_t, 1> seed_a{ 0xA1 };
    const std::array<uint8_t, 1> seed_b{ 0xB2 };
    const auto                   hash_a = crypto::sha2_256( gsl::span<const uint8_t>( seed_a ) );
    const auto                   hash_b = crypto::sha2_256( gsl::span<const uint8_t>( seed_b ) );

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
    const auto                   hash_a = crypto::sha2_256( gsl::span<const uint8_t>( seed_a ) );
    const auto                   hash_b = crypto::sha2_256( gsl::span<const uint8_t>( seed_b ) );

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
    const auto                   tx_hash       = crypto::sha2_256( gsl::span<const uint8_t>( seed_tx ) );
    const auto                   registry_hash = crypto::sha2_256( gsl::span<const uint8_t>( seed_registry ) );

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

TEST_F( UTXOManagerTest, ReservedUTXORemainsVisibleToConsensusSnapshots )
{
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ) ).value() );

    InputUTXOInfo input;
    input.txid_hash_  = DUMMY_HASH;
    input.output_idx_ = 0;

    utxo_manager->ReserveUTXOs( { input }, "transfer-a" );

    const auto consensus_snapshot = utxo_manager->GetUnconsumedUTXOs( std::string( PRIV_KEY ) );
    ASSERT_EQ( consensus_snapshot.size(), 1u );
    EXPECT_EQ( consensus_snapshot[0].GetOutPoint(), GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ).GetOutPoint() );
}

TEST_F( UTXOManagerTest, ExactOutpointLookupFindsEscrowOwnedUTXO )
{
    const std::string lock_address = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ), lock_address ).value() );

    auto utxo = utxo_manager->GetUnconsumedUTXO( DUMMY_HASH, 0 );

    ASSERT_TRUE( utxo.has_value() );
    EXPECT_EQ( utxo->GetOwnerAddress(), lock_address );
    EXPECT_EQ( utxo->GetAmount(), 100u );
}

TEST_F( UTXOManagerTest, RollbackOnlyReleasesMatchingReservation )
{
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ) ).value() );

    InputUTXOInfo input;
    input.txid_hash_  = DUMMY_HASH;
    input.output_idx_ = 0;

    utxo_manager->ReserveUTXOs( { input }, "transfer-a" );
    utxo_manager->RollbackUTXOs( { input }, "transfer-b" );
    EXPECT_TRUE( utxo_manager->IsOutPointReserved( DUMMY_HASH, 0 ) );

    utxo_manager->RollbackUTXOs( { input }, "transfer-a" );
    EXPECT_FALSE( utxo_manager->IsOutPointReserved( DUMMY_HASH, 0 ) );
}

TEST_F( UTXOManagerTest, RestoreConsumedUTXOsRejectsInvalidBatchAtomically )
{
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ) ).value() );
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 100, TOKEN_1 ) ).value() );

    InputUTXOInfo consumed_input;
    consumed_input.txid_hash_  = DUMMY_HASH;
    consumed_input.output_idx_ = 0;
    InputUTXOInfo ready_input;
    ready_input.txid_hash_  = DUMMY_HASH;
    ready_input.output_idx_ = 1;
    ASSERT_TRUE( utxo_manager->ConsumeUTXOs( { consumed_input } ).value() );
    ASSERT_TRUE( utxo_manager->IsOutPointConsumed( DUMMY_HASH, 0 ) );

    const auto restore_result = utxo_manager->RestoreConsumedUTXOs( { consumed_input, ready_input },
                                                                    std::string( PRIV_KEY ) );

    ASSERT_TRUE( restore_result.has_error() );
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( DUMMY_HASH, 0 ) );
    EXPECT_FALSE( utxo_manager->GetUnconsumedUTXO( DUMMY_HASH, 0 ).has_value() );
    EXPECT_TRUE( utxo_manager->GetUnconsumedUTXO( DUMMY_HASH, 1 ).has_value() );
    EXPECT_EQ( utxo_manager->GetBalance(), 100U );
}

TEST_F( UTXOManagerTest, WinningPeerConsumesLocallyOwnedReservedBridgeBurn )
{
    const std::string local_detector = "peer-b";
    const std::string winning_minter = "peer-a";
    ASSERT_TRUE(
        utxo_manager
            ->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ), local_detector, UTXOManager::UTXOType::UTXO_BRIDGE )
            .value() );

    InputUTXOInfo input;
    input.txid_hash_  = DUMMY_HASH;
    input.output_idx_ = 0;
    utxo_manager->ReserveUTXOs( { input }, "peer-b-proposal", UTXOManager::UTXOType::UTXO_BRIDGE );

    auto consumed = utxo_manager->ConsumeUTXOs( { input }, winning_minter, UTXOManager::UTXOType::UTXO_BRIDGE );

    ASSERT_TRUE( consumed.has_value() );
    EXPECT_TRUE( consumed.value() );
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( DUMMY_HASH, 0 ) );

    utxo_manager->RollbackUTXOs( { input }, "peer-b-proposal", UTXOManager::UTXOType::UTXO_BRIDGE );
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( DUMMY_HASH, 0 ) );
}

TEST_F( UTXOManagerTest, PutUTXOWithBridgeType )
{
    // Insert a UTXO with UTXO_BRIDGE type for a foreign address
    const std::string foreign = "bridge_test_address";
    ASSERT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 10, 500, TOKEN_1 ), foreign, UTXOManager::UTXOType::UTXO_BRIDGE )
            .value() );
    // Verify the UTXO appears in GetUTXOs for that address
    auto utxos = utxo_manager->GetUTXOs( foreign );
    EXPECT_EQ( utxos.size(), 1u );
    EXPECT_EQ( utxos[0].GetAmount(), 500u );

    // Also verify we can insert with default UTXO_NORMAL type
    ASSERT_TRUE(
        utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 11, 300, TOKEN_1 ), foreign, UTXOManager::UTXOType::UTXO_NORMAL )
            .value() );
    EXPECT_EQ( utxo_manager->GetUTXOs( foreign ).size(), 2u );
}

TEST_F( UTXOManagerTest, ForeignAddressPutUTXO )
{
    // Insert a UTXO for an address that is NOT the node's own address
    const std::string foreign = "0xdeadbeef_foreign_address";
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( DUMMY_HASH, 20, 777, TOKEN_1 ), foreign ).value() );
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
    ASSERT_TRUE(
        utxo_manager
            ->PutUTXO( GeniusUTXO( DUMMY_HASH, 30, 1000, TOKEN_1 ), bridge_addr, UTXOManager::UTXOType::UTXO_BRIDGE )
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

TEST_F( UTXOManagerTest, TryReserveOutpointClaimsOnceAndReportsCollisions )
{
    /**
     * The duplicate-burn serialization point. ReserveUTXOs is silent when the
     * outpoint is already reserved under the SAME id — and the burn hash is the
     * reservation id, so that silence is exactly the concurrent-duplicate case.
     * TryReserveOutpoint must claim exactly once and report every collision
     * distinctly: same-id, different-id, consumed, and not-claimable.
     */
    const auto burn = base::Hash256::fromReadableString( std::string( 64, 'd' ) ).value();
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( burn, 0, 100, sgns::TokenID::FromBytes( { 0x00 } ) ),
                                "claim-owner",
                                UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );

    // First claim wins.
    EXPECT_EQ( utxo_manager->TryReserveOutpoint( burn, 0, "burn-id", UTXOManager::UTXOType::UTXO_BRIDGE ),
               UTXOManager::OutpointClaim::kClaimed );
    // Same-id re-claim (the duplicate burn) is reported, not silently accepted.
    EXPECT_EQ( utxo_manager->TryReserveOutpoint( burn, 0, "burn-id", UTXOManager::UTXOType::UTXO_BRIDGE ),
               UTXOManager::OutpointClaim::kAlreadyReserved );
    // Different holder is also reported.
    EXPECT_EQ( utxo_manager->TryReserveOutpoint( burn, 0, "other-id", UTXOManager::UTXOType::UTXO_BRIDGE ),
               UTXOManager::OutpointClaim::kAlreadyReserved );
    // Type mismatch against a READY outpoint is not claimable.
    const auto other_burn = base::Hash256::fromReadableString( std::string( 64, 'c' ) ).value();
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( other_burn, 0, 50, sgns::TokenID::FromBytes( { 0x00 } ) ),
                                "claim-owner",
                                UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );
    EXPECT_EQ( utxo_manager->TryReserveOutpoint( other_burn, 0, "burn-id", UTXOManager::UTXOType::UTXO_NORMAL ),
               UTXOManager::OutpointClaim::kNotClaimable );
    // Unknown outpoint is not claimable.
    const auto unknown = base::Hash256::fromReadableString( std::string( 64, 'e' ) ).value();
    EXPECT_EQ( utxo_manager->TryReserveOutpoint( unknown, 0, "burn-id", UTXOManager::UTXOType::UTXO_BRIDGE ),
               UTXOManager::OutpointClaim::kNotClaimable );

    // After consumption the claim reports spent.
    InputUTXOInfo consumed_input;
    consumed_input.txid_hash_  = burn;
    consumed_input.output_idx_ = 0;
    ASSERT_TRUE( utxo_manager->ConsumeUTXOs( { consumed_input }, "claim-owner", UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );
    EXPECT_EQ( utxo_manager->TryReserveOutpoint( burn, 0, "burn-id", UTXOManager::UTXOType::UTXO_BRIDGE ),
               UTXOManager::OutpointClaim::kAlreadyConsumed );
}

TEST_F( UTXOManagerTest, ConsumeUTXOsPersistsConsumedStateUnderStoredOwner )
{
    /**
     * Cross-address consumption durability: ConsumeUTXOs stored its snapshot
     * under the SPENDER's address, but StoreUTXOsLocked snapshots only entries
     * owned by its address argument — so consuming an outpoint owned by another
     * address (delegated escrow, foreign bridge outpoint) left the CONSUMED
     * state durable under NEITHER address. After a reload the outpoint came
     * back READY: a resurrected spend / double-spend window.
     */
    const auto        outpoint_hash = base::Hash256::fromReadableString( std::string( 64, '5' ) ).value();
    const std::string stored_owner  = "owner-a";
    const std::string spender       = "spender-b";
    // UTXO_BRIDGE is the shape where the hole bites: bridge entries match any
    // spender (owner_matches is unconditionally true for the type), so the
    // in-memory CONSUMED flip lands on the ORIGINAL owner's entry while the
    // store runs under the spender's bucket. (A NORMAL-type mismatch instead
    // synthesizes a spender-owned tombstone, which does persist.)
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( outpoint_hash, 0, 40, sgns::TokenID::FromBytes( { 0x00 } ) ),
                                stored_owner,
                                UTXOManager::UTXOType::UTXO_BRIDGE )
                     .has_value() );

    InputUTXOInfo input;
    input.txid_hash_  = outpoint_hash;
    input.output_idx_ = 0;
    // Bridge consume, as MintFunds performs it: the entry is FOUND (bridge
    // matches any spender AND the type must match), so the CONSUMED flip lands
    // on the original owner's entry — the exact shape the store must persist
    // under the stored owner.
    const auto consumed = utxo_manager->ConsumeUTXOs( { input }, spender, UTXOManager::UTXOType::UTXO_BRIDGE );
    ASSERT_TRUE( consumed.has_value() );

    // Reload from storage into a fresh manager: the consumed state must
    // survive — pre-fix the snapshot was written under the spender's bucket,
    // which contains no entry for this outpoint.
    auto reloaded = std::make_shared<UTXOManager>(
        std::string( PRIV_KEY ),
        []( const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return std::vector( hashed.begin(), hashed.end() );
        },
        []( const std::string &_, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return signature == std::vector( hashed.begin(), hashed.end() );
        } );
    ASSERT_TRUE( reloaded->LoadUTXOs( db_ ).has_value() );
    EXPECT_TRUE( reloaded->IsOutPointConsumed( outpoint_hash, 0 ) );
}

TEST_F( UTXOManagerTest, GenuineConsumeRecordsSpender )
{
    /**
     * D-02 Candidate-B carrier: a genuine ConsumeUTXOs call records the
     * consuming transaction's hash on the consumed entry, readable through
     * GetOutPointSpender alongside the genuine-spend discriminator.
     */
    const auto        outpoint_hash = base::Hash256::fromReadableString( std::string( 64, 'a' ) ).value();
    const auto        spender       = base::Hash256::fromReadableString( std::string( 64, 'b' ) ).value();
    const std::string owner         = "spender-owner";

    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( outpoint_hash, 0, 100, TOKEN_1 ), owner ).value() );

    InputUTXOInfo input;
    input.txid_hash_    = outpoint_hash;
    input.output_idx_   = 0;
    const auto consumed = utxo_manager->ConsumeUTXOs( { input }, owner, UTXOManager::UTXOType::UTXO_NORMAL, spender );
    ASSERT_TRUE( consumed.has_value() );
    EXPECT_TRUE( consumed.value() );

    const auto recorded = utxo_manager->GetOutPointSpender( outpoint_hash, 0 );
    ASSERT_TRUE( recorded.has_value() );
    EXPECT_EQ( recorded->toReadableString(), spender.toReadableString() );
    EXPECT_TRUE( utxo_manager->IsOutPointGenuinelyConsumed( outpoint_hash, 0 ) );
}

TEST_F( UTXOManagerTest, SynthesizedTombstoneCarriesSpenderButNotGenuine )
{
    /**
     * Uniform population: an unknown outpoint synthesizes a zero-amount
     * CONSUMED tombstone that still carries the spender — but the D-C2
     * discriminator excludes it from genuine-spend (eligibility) evidence.
     */
    const auto unknown = base::Hash256::fromReadableString( std::string( 64, 'c' ) ).value();
    const auto spender = base::Hash256::fromReadableString( std::string( 64, 'd' ) ).value();

    InputUTXOInfo input;
    input.txid_hash_    = unknown;
    input.output_idx_   = 7;
    const auto consumed = utxo_manager->ConsumeUTXOs( { input },
                                                      "tombstone-spender",
                                                      UTXOManager::UTXOType::UTXO_NORMAL,
                                                      spender );
    ASSERT_TRUE( consumed.has_value() );
    EXPECT_FALSE( consumed.value() ); // unknown outpoint: not every input was consumed

    const auto recorded = utxo_manager->GetOutPointSpender( unknown, 7 );
    ASSERT_TRUE( recorded.has_value() );
    EXPECT_EQ( recorded->toReadableString(), spender.toReadableString() );
    EXPECT_FALSE( utxo_manager->IsOutPointGenuinelyConsumed( unknown, 7 ) );
}

TEST_F( UTXOManagerTest, RestoreConsumedUTXOsResetsSpender )
{
    /**
     * The revert path keeps the carrier symmetric: restoring a consumed
     * input clears spent_by_txid and makes the outpoint spendable again.
     */
    const auto        outpoint_hash = base::Hash256::fromReadableString( std::string( 64, 'e' ) ).value();
    const auto        spender       = base::Hash256::fromReadableString( std::string( 64, 'f' ) ).value();
    const std::string owner         = "restore-owner";

    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( outpoint_hash, 0, 25, TOKEN_1 ), owner ).value() );

    InputUTXOInfo input;
    input.txid_hash_  = outpoint_hash;
    input.output_idx_ = 0;
    ASSERT_TRUE(
        utxo_manager->ConsumeUTXOs( { input }, owner, UTXOManager::UTXOType::UTXO_NORMAL, spender ).has_value() );
    ASSERT_TRUE( utxo_manager->GetOutPointSpender( outpoint_hash, 0 ).has_value() );

    ASSERT_TRUE(
        utxo_manager->RestoreConsumedUTXOs( { input }, owner, UTXOManager::UTXOType::UTXO_NORMAL ).has_value() );

    EXPECT_FALSE( utxo_manager->GetOutPointSpender( outpoint_hash, 0 ).has_value() );
    EXPECT_EQ( utxo_manager->GetOutPointState( outpoint_hash, 0 ).value(), UTXOManager::UTXOState::UTXO_READY );
}

TEST_F( UTXOManagerTest, SpenderSurvivesBucketRewriteAndReload )
{
    /**
     * Durability of the carrier: a later write to the SAME address bucket
     * forces StoreUTXOSnapshot's whole-bucket remove+reput, and a fresh
     * manager LoadUTXOs-ing the same DB still reads the original spender.
     */
    const auto        outpoint_hash = base::Hash256::fromReadableString( std::string( 64, '6' ) ).value();
    const auto        later_hash    = base::Hash256::fromReadableString( std::string( 64, '7' ) ).value();
    const auto        spender       = base::Hash256::fromReadableString( std::string( 64, '8' ) ).value();
    const std::string owner         = "rewrite-owner";

    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( outpoint_hash, 0, 40, TOKEN_1 ), owner ).value() );

    InputUTXOInfo first;
    first.txid_hash_  = outpoint_hash;
    first.output_idx_ = 0;
    ASSERT_TRUE(
        utxo_manager->ConsumeUTXOs( { first }, owner, UTXOManager::UTXOType::UTXO_NORMAL, spender ).has_value() );

    // Second lifecycle on the same bucket: PutUTXO re-stores the whole owner
    // bucket (remove + reput), then a second consume rewrites it again.
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( later_hash, 1, 60, TOKEN_1 ), owner ).value() );
    InputUTXOInfo second;
    second.txid_hash_  = later_hash;
    second.output_idx_ = 1;
    ASSERT_TRUE(
        utxo_manager->ConsumeUTXOs( { second }, owner, UTXOManager::UTXOType::UTXO_NORMAL, spender ).has_value() );

    utxo_manager.reset();
    auto reloaded = std::make_shared<UTXOManager>(
        std::string( PRIV_KEY ),
        []( const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return std::vector( hashed.begin(), hashed.end() );
        },
        []( const std::string &_, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return signature == std::vector( hashed.begin(), hashed.end() );
        } );
    ASSERT_TRUE( reloaded->LoadUTXOs( db_ ).has_value() );

    const auto recorded = reloaded->GetOutPointSpender( outpoint_hash, 0 );
    ASSERT_TRUE( recorded.has_value() );
    EXPECT_EQ( recorded->toReadableString(), spender.toReadableString() );
    EXPECT_TRUE( reloaded->IsOutPointGenuinelyConsumed( outpoint_hash, 0 ) );
}

TEST_F( UTXOManagerTest, ClearUnspentKeepConsumedPreservesEvidenceInMemoryAndOnDisk )
{
    /**
     * D-01 preserve-clear: READY/RESERVED entries and their reservations are
     * dropped, but CONSUMED spend evidence (spent_by_txid) survives — in memory
     * AND on the raw /utxo/ prefix. The reload leg exercises the post-restart
     * shape where LoadUTXOs has the CONSUMED entry resident in the address
     * index — the exact state InitializeUTXOs's checkpoint-mismatch clear runs
     * against.
     */
    const auto        ready_hash    = base::Hash256::fromReadableString( std::string( 64, '1' ) ).value();
    const auto        reserved_hash = base::Hash256::fromReadableString( std::string( 64, '2' ) ).value();
    const auto        spent_hash    = base::Hash256::fromReadableString( std::string( 64, '3' ) ).value();
    const auto        spender       = base::Hash256::fromReadableString( std::string( 64, '9' ) ).value();
    const std::string owner         = "keep-consumed-owner";
    const std::string ready_key     = "/utxo/" + owner + "/" + ready_hash.toReadableString() + ":0";
    const std::string reserved_key  = "/utxo/" + owner + "/" + reserved_hash.toReadableString() + ":1";
    const std::string spent_key     = "/utxo/" + owner + "/" + spent_hash.toReadableString() + ":2";

    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( ready_hash, 0, 100, TOKEN_1 ), owner ).value() );
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( reserved_hash, 1, 50, TOKEN_1 ), owner ).value() );
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( spent_hash, 2, 70, TOKEN_1 ), owner ).value() );

    InputUTXOInfo reserved_input;
    reserved_input.txid_hash_  = reserved_hash;
    reserved_input.output_idx_ = 1;
    utxo_manager->ReserveUTXOs( { reserved_input }, "res-1" );
    ASSERT_TRUE( utxo_manager->IsOutPointReserved( reserved_hash, 1 ) );
    // The reservation is live before the clear: a second claimant is refused.
    EXPECT_EQ( utxo_manager->TryReserveOutpoint( reserved_hash, 1, "res-2" ),
               UTXOManager::OutpointClaim::kAlreadyReserved );

    InputUTXOInfo spent_input;
    spent_input.txid_hash_  = spent_hash;
    spent_input.output_idx_ = 2;
    ASSERT_TRUE(
        utxo_manager->ConsumeUTXOs( { spent_input }, owner, UTXOManager::UTXOType::UTXO_NORMAL, spender ).value() );

    // Present-before (disk): all three outpoints carry records under the prefix.
    EXPECT_EQ( ReadRawUtxoRecords( db_, owner ).size(), 3u );

    ASSERT_TRUE( utxo_manager->ClearUnspentKeepConsumed( owner ).has_value() );

    // Memory: unspent entries are gone and their reservation was dropped with
    // them (the outpoint is unknown — not merely held by someone else).
    EXPECT_FALSE( utxo_manager->GetOutPointState( ready_hash, 0 ).has_value() );
    EXPECT_FALSE( utxo_manager->GetOutPointState( reserved_hash, 1 ).has_value() );
    EXPECT_TRUE( utxo_manager->GetUTXOs( owner ).empty() );
    EXPECT_EQ( utxo_manager->TryReserveOutpoint( reserved_hash, 1, "res-2" ),
               UTXOManager::OutpointClaim::kNotClaimable );

    // Memory: consumed evidence survives with its spender binding.
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( spent_hash, 2 ) );
    const auto spent_by_txid = utxo_manager->GetOutPointSpender( spent_hash, 2 );
    ASSERT_TRUE( spent_by_txid.has_value() );
    EXPECT_EQ( spent_by_txid->toReadableString(), spender.toReadableString() );
    EXPECT_TRUE( utxo_manager->IsOutPointGenuinelyConsumed( spent_hash, 2 ) );

    // Disk: the raw /utxo/ prefix carries exactly the consumed record with its
    // spent_by_txid bytes, and the cleared unspent keys are gone.
    const auto records = ReadRawUtxoRecords( db_, owner );
    ASSERT_EQ( records.size(), 1u );
    EXPECT_EQ( records.count( ready_key ), 0u );
    EXPECT_EQ( records.count( reserved_key ), 0u );
    const auto spent_it = records.find( spent_key );
    ASSERT_NE( spent_it, records.end() );
    EXPECT_EQ( spent_it->second.state(), SGTransaction::UTXO_ENTRY_CONSUMED );
    EXPECT_EQ( spent_it->second.utxo().amount(), 70u );
    ASSERT_TRUE( spent_it->second.has_spent_by_txid() );
    EXPECT_EQ( spent_it->second.spent_by_txid(), spender.toString() );

    // Reload leg: LoadUTXOs indexes every persisted record — including CONSUMED
    // entries — into the address index. The clear must keep a CONSUMED survivor
    // that is index-resident (the post-restart mismatch-clear shape).
    auto reloaded = std::make_shared<UTXOManager>(
        std::string( PRIV_KEY ),
        []( const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return std::vector( hashed.begin(), hashed.end() );
        },
        []( const std::string &, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return signature == std::vector( hashed.begin(), hashed.end() );
        } );
    ASSERT_TRUE( reloaded->LoadUTXOs( db_ ).has_value() );
    ASSERT_TRUE( reloaded->IsOutPointConsumed( spent_hash, 2 ) );

    ASSERT_TRUE( reloaded->ClearUnspentKeepConsumed( owner ).has_value() );

    EXPECT_TRUE( reloaded->IsOutPointConsumed( spent_hash, 2 ) );
    const auto reloaded_spender = reloaded->GetOutPointSpender( spent_hash, 2 );
    ASSERT_TRUE( reloaded_spender.has_value() );
    EXPECT_EQ( reloaded_spender->toReadableString(), spender.toReadableString() );
    const auto reloaded_records = ReadRawUtxoRecords( db_, owner );
    ASSERT_EQ( reloaded_records.size(), 1u );
    const auto reloaded_spent_it = reloaded_records.find( spent_key );
    ASSERT_NE( reloaded_spent_it, reloaded_records.end() );
    EXPECT_EQ( reloaded_spent_it->second.state(), SGTransaction::UTXO_ENTRY_CONSUMED );
    EXPECT_EQ( reloaded_spent_it->second.spent_by_txid(), spender.toString() );
}

TEST_F( UTXOManagerTest, ClearUnspentKeepConsumedKeepsTombstoneAndBlocksRecreation )
{
    /**
     * Both consumed classes survive the clear: a genuine spend (amount != 0)
     * and a synthesized zero-amount tombstone. Genuineness discrimination stays
     * with IsOutPointGenuinelyConsumed; for blocking purposes ANY CONSUMED
     * entry suffices — PutUTXO's existing-entry check refuses re-creation of
     * both, which is the by-construction resurrection blocker.
     */
    const auto        genuine_hash = base::Hash256::fromReadableString( std::string( 64, '4' ) ).value();
    const auto        unknown_hash = base::Hash256::fromReadableString( std::string( 64, '5' ) ).value();
    const auto        spender      = base::Hash256::fromReadableString( std::string( 64, '6' ) ).value();
    const std::string owner        = "tombstone-keep-owner";

    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( genuine_hash, 0, 100, TOKEN_1 ), owner ).value() );
    InputUTXOInfo genuine_input;
    genuine_input.txid_hash_  = genuine_hash;
    genuine_input.output_idx_ = 0;
    ASSERT_TRUE(
        utxo_manager->ConsumeUTXOs( { genuine_input }, owner, UTXOManager::UTXOType::UTXO_NORMAL, spender ).value() );

    InputUTXOInfo unknown_input;
    unknown_input.txid_hash_  = unknown_hash;
    unknown_input.output_idx_ = 7;
    const auto tombstoned     = utxo_manager->ConsumeUTXOs( { unknown_input },
                                                            owner,
                                                            UTXOManager::UTXOType::UTXO_NORMAL,
                                                            spender );
    ASSERT_TRUE( tombstoned.has_value() );
    EXPECT_FALSE( tombstoned.value() ); // unknown outpoint: not every input was consumed

    ASSERT_TRUE( utxo_manager->ClearUnspentKeepConsumed( owner ).has_value() );

    // Genuine spend: survives with full evidence quality.
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( genuine_hash, 0 ) );
    EXPECT_TRUE( utxo_manager->IsOutPointGenuinelyConsumed( genuine_hash, 0 ) );
    const auto genuine_spender = utxo_manager->GetOutPointSpender( genuine_hash, 0 );
    ASSERT_TRUE( genuine_spender.has_value() );
    EXPECT_EQ( genuine_spender->toReadableString(), spender.toReadableString() );

    // Synthetic tombstone: survives (blocks re-creation) but is not genuine.
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( unknown_hash, 7 ) );
    EXPECT_FALSE( utxo_manager->IsOutPointGenuinelyConsumed( unknown_hash, 7 ) );
    const auto tombstone_spender = utxo_manager->GetOutPointSpender( unknown_hash, 7 );
    ASSERT_TRUE( tombstone_spender.has_value() );
    EXPECT_EQ( tombstone_spender->toReadableString(), spender.toReadableString() );

    // Any surviving CONSUMED entry refuses re-creation — no resurrection.
    EXPECT_FALSE( utxo_manager->PutUTXO( GeniusUTXO( genuine_hash, 0, 100, TOKEN_1 ), owner ).value() );
    EXPECT_FALSE( utxo_manager->PutUTXO( GeniusUTXO( unknown_hash, 7, 100, TOKEN_1 ), owner ).value() );

    // Disk: both consumed records survive the clear.
    const auto records = ReadRawUtxoRecords( db_, owner );
    ASSERT_EQ( records.size(), 2u );
    const auto genuine_it = records.find( "/utxo/" + owner + "/" + genuine_hash.toReadableString() + ":0" );
    ASSERT_NE( genuine_it, records.end() );
    EXPECT_EQ( genuine_it->second.state(), SGTransaction::UTXO_ENTRY_CONSUMED );
    EXPECT_EQ( genuine_it->second.spent_by_txid(), spender.toString() );
    const auto tombstone_it = records.find( "/utxo/" + owner + "/" + unknown_hash.toReadableString() + ":7" );
    ASSERT_NE( tombstone_it, records.end() );
    EXPECT_EQ( tombstone_it->second.state(), SGTransaction::UTXO_ENTRY_CONSUMED );
    EXPECT_EQ( tombstone_it->second.utxo().amount(), 0u );
    EXPECT_EQ( tombstone_it->second.spent_by_txid(), spender.toString() );
}

TEST_F( UTXOManagerTest, KeepConsumedNegativeControlSetUTXOsStillWipesEverything )
{
    /**
     * Negative control: SetUTXOs({}, address) keeps its wipe-everything
     * semantics — CONSUMED records and their spent_by_txid are destroyed, in
     * memory and on disk. The preserve-clear is purely additive.
     *
     * Seeded through the real-world wipe path: ConsumeUTXOs drops the outpoint
     * from the address index, so only after a reload (LoadUTXOs indexes every
     * persisted record, CONSUMED included) is the spent entry index-resident —
     * the exact shape InitializeUTXOs's mismatch clear runs against.
     */
    const auto        spent_hash = base::Hash256::fromReadableString( std::string( 64, '7' ) ).value();
    const auto        ready_hash = base::Hash256::fromReadableString( std::string( 64, '8' ) ).value();
    const auto        spender    = base::Hash256::fromReadableString( std::string( 64, '0' ) ).value();
    const std::string owner      = "wipe-control-owner";

    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( ready_hash, 0, 60, TOKEN_1 ), owner ).value() );
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( spent_hash, 1, 80, TOKEN_1 ), owner ).value() );
    InputUTXOInfo spent_input;
    spent_input.txid_hash_  = spent_hash;
    spent_input.output_idx_ = 1;
    ASSERT_TRUE(
        utxo_manager->ConsumeUTXOs( { spent_input }, owner, UTXOManager::UTXOType::UTXO_NORMAL, spender ).value() );
    ASSERT_TRUE( utxo_manager->IsOutPointConsumed( spent_hash, 1 ) );
    EXPECT_EQ( ReadRawUtxoRecords( db_, owner ).size(), 2u );

    // Reload so the CONSUMED entry is index-resident (the post-restart shape).
    auto reloaded = std::make_shared<UTXOManager>(
        std::string( PRIV_KEY ),
        []( const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return std::vector( hashed.begin(), hashed.end() );
        },
        []( const std::string &, const std::vector<uint8_t> &signature, const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return signature == std::vector( hashed.begin(), hashed.end() );
        } );
    ASSERT_TRUE( reloaded->LoadUTXOs( db_ ).has_value() );
    ASSERT_TRUE( reloaded->IsOutPointConsumed( spent_hash, 1 ) );
    ASSERT_TRUE( reloaded->GetOutPointSpender( spent_hash, 1 ).has_value() );

    ASSERT_TRUE( reloaded->SetUTXOs( {}, owner ).has_value() );

    EXPECT_FALSE( reloaded->IsOutPointConsumed( spent_hash, 1 ) );
    EXPECT_FALSE( reloaded->GetOutPointSpender( spent_hash, 1 ).has_value() );
    EXPECT_FALSE( reloaded->GetOutPointState( ready_hash, 0 ).has_value() );
    EXPECT_TRUE( reloaded->GetUTXOs( owner ).empty() );
    EXPECT_TRUE( ReadRawUtxoRecords( db_, owner ).empty() );
}
