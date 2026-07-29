#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "base/blob.hpp" // for sgns::base::Hash256
#include "account/UTXOManager.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/TokenID.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "crypto/hasher.hpp"
#include "testutil/storage/base_rocksdb_test.hpp"

using namespace sgns;
using namespace sgns::base;

namespace sgns
{
    class UTXOManagerTestAccess
    {
    public:
        using Stage = UTXOManager::FaultStage;
        using Result = UTXOManager::AtomicMintEffectResult;

        static void SetFault( UTXOManager &manager, UTXOManager::FaultCallback callback )
        {
            manager.fault_callback_ = std::move( callback );
        }

        static void ResetFault( UTXOManager &manager )
        {
            manager.ResetFaultCallback();
        }

        static outcome::result<UTXOManager::AtomicMintEffectResult> Apply(
            UTXOManager &manager,
            const base::Hash256 &winner,
            const std::string &chain,
            const base::Hash256 &burn,
            uint32_t index,
            const std::vector<GeniusUTXO> &outputs,
            const std::string &owner )
        {
            UTXOManager::AtomicMintEffectRequest request;
            request.winning_transaction_hash = winner;
            request.chain_id = chain;
            request.burn_hash = burn;
            request.receipt_log_index = index;
            request.produced_outputs = outputs;
            request.bridge_input = InputUTXOInfo{ burn, index, {} };
            request.bridge_input_owner = owner;
            request.bridge_input_type = UTXOManager::UTXOType::UTXO_BRIDGE;
            request.certified_bridge_input = GeniusUTXO(
                burn, index, outputs.front().GetAmount(), outputs.front().GetTokenID(), owner );
            return manager.ApplyMintEffectsAtomically( request );
        }

        static outcome::result<std::optional<UTXOManager::BridgeApplication>> Application(
            const UTXOManager &manager,
            const std::string &chain,
            const base::Hash256 &burn,
            uint32_t index )
        {
            return manager.GetBridgeApplication( chain, burn, index );
        }

        static outcome::result<Result> ApplyFinalized(
            UTXOManager &manager,
            const std::shared_ptr<ConsensusStateStore> &store,
            const ConsensusStateStore::FinalizedReservationIdentity &identity,
            const base::Hash256 &winner,
            const std::vector<GeniusUTXO> &outputs,
            const std::string &owner,
            uint64_t bridge_amount,
            const TokenID &bridge_token )
        {
            UTXOManager::AtomicMintEffectRequest request;
            request.winning_transaction_hash = winner;
            request.chain_id = identity.outpoint.source_chain;
            auto burn = base::Hash256::fromReadableString( identity.outpoint.burn_hash );
            if ( !burn ) return outcome::failure( std::errc::invalid_argument );
            request.burn_hash = burn.value();
            request.receipt_log_index = identity.outpoint.receipt_log_index;
            request.produced_outputs = outputs;
            request.bridge_input = InputUTXOInfo{
                burn.value(), identity.outpoint.receipt_log_index, {} };
            request.bridge_input_owner = owner;
            request.bridge_input_type = UTXOManager::UTXOType::UTXO_BRIDGE;
            request.certified_bridge_input = GeniusUTXO(
                burn.value(), identity.outpoint.receipt_log_index,
                bridge_amount, bridge_token, owner );
            request.slot_id = identity.slot_id;
            request.reservation_generation = identity.generation;
            request.certificate_digest = identity.certificate_digest;
            request.proposal_id = identity.proposal_id;
            request.winner_id = identity.winner_id;
            Result result = Result::Applied;
            auto applied = store->ApplyFinalizedReservationBatch(
                identity, manager.AcquireStorage(),
                [&]( storage::BufferBatch &batch,
                     const ConsensusStateStore::BurnReservationRecord &reservation )
                    -> outcome::result<void>
                {
                    BOOST_OUTCOME_TRY( auto effect,
                                       manager.ApplyMintEffectsAtomically(
                                           request, &batch, &reservation ) );
                    result = effect;
                    return outcome::success();
                } );
            if ( !applied ) return outcome::failure( applied.error() );
            return result;
        }

        static outcome::result<void> RemoveApplication(
            UTXOManager &manager,
            const std::string &chain,
            const base::Hash256 &burn,
            uint32_t index )
        {
            base::Buffer key;
            key.put( UTXOManager::MakeBridgeApplicationKey( chain, burn, index ) );
            return manager.db_->remove( key );
        }

        static outcome::result<void> SwapStoredApplicationOutputs(
            UTXOManager &manager,
            const std::string &chain,
            const base::Hash256 &burn,
            uint32_t index )
        {
            base::Buffer key;
            key.put( UTXOManager::MakeBridgeApplicationKey( chain, burn, index ) );
            BOOST_OUTCOME_TRY( auto raw, manager.db_->get( key ) );
            SGTransaction::BridgeApplicationRecord record;
            if ( !record.ParseFromArray( raw.data(), raw.size() ) || record.produced_outputs_size() != 2 )
                return outcome::failure( std::errc::bad_message );
            record.mutable_produced_outputs()->SwapElements( 0, 1 );
            base::Buffer changed;
            changed.put( record.SerializeAsString() );
            return manager.db_->put( key, changed );
        }

        static void RemoveLiveOutpoint( UTXOManager &manager, const OutPoint &outpoint )
        {
            std::unique_lock lock( manager.utxos_mutex_ );
            manager.utxo_outpoints_.erase( outpoint );
        }

        static void MarkLiveOutpointReady( UTXOManager &manager, const OutPoint &outpoint )
        {
            std::unique_lock lock( manager.utxos_mutex_ );
            auto it = manager.utxo_outpoints_.find( outpoint );
            if ( it != manager.utxo_outpoints_.end() ) it->second.state = UTXOManager::UTXOState::UTXO_READY;
        }
    };
}

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

class UTXOManagerTest : public test::RocksDBFixture
{
public:
    UTXOManagerTest() : RocksDBFixture( "utxo_manager_test" ) {}

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

    const auto restore_result =
        utxo_manager->RestoreConsumedUTXOs( { consumed_input, ready_input }, std::string( PRIV_KEY ) );

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
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 100, TOKEN_1 ),
                                local_detector,
                                UTXOManager::UTXOType::UTXO_BRIDGE )
                     .value() );

    InputUTXOInfo input;
    input.txid_hash_  = DUMMY_HASH;
    input.output_idx_ = 0;
    utxo_manager->ReserveUTXOs( { input }, "peer-b-proposal", UTXOManager::UTXOType::UTXO_BRIDGE );

    auto consumed = utxo_manager->ConsumeUTXOs(
        { input }, winning_minter, UTXOManager::UTXOType::UTXO_BRIDGE );

    ASSERT_TRUE( consumed.has_value() );
    EXPECT_TRUE( consumed.value() );
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( DUMMY_HASH, 0 ) );

    utxo_manager->RollbackUTXOs(
        { input }, "peer-b-proposal", UTXOManager::UTXOType::UTXO_BRIDGE );
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( DUMMY_HASH, 0 ) );
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

TEST_F( UTXOManagerTest, AtomicMintApplicationSerializesOverlappingOrdinaryStore )
{
    using Stage = UTXOManagerTestAccess::Stage;
    const auto burn = crypto::sha2_256( std::vector<uint8_t>{ 0x41 } );
    const auto winner = crypto::sha2_256( std::vector<uint8_t>{ 0x42 } );
    const auto unrelated = crypto::sha2_256( std::vector<uint8_t>{ 0x43 } );
    const std::string owner = "atomic-owner";
    const std::string destination = "mint-destination";
    ASSERT_TRUE( utxo_manager
                     ->PutUTXO( GeniusUTXO( burn, 7, 500, TOKEN_1 ),
                                owner,
                                UTXOManager::UTXOType::UTXO_BRIDGE )
                     .value() );
    ASSERT_TRUE( utxo_manager->PutUTXO( GeniusUTXO( unrelated, 0, 9, TOKEN_1 ), owner ).value() );
    const std::vector<GeniusUTXO> outputs{
        GeniusUTXO( winner, 0, 500, TOKEN_1, destination )
    };

    std::mutex mutex;
    std::condition_variable cv;
    bool ordinary_paused = false;
    bool atomic_waiting = false;
    bool release_ordinary = false;
    bool atomic_acquired = false;
    UTXOManagerTestAccess::SetFault(
        *utxo_manager,
        [&]( Stage stage ) -> outcome::result<void>
        {
            std::unique_lock lock( mutex );
            if ( stage == Stage::OrdinaryStoreSnapshotReadyBeforeCommit )
            {
                ordinary_paused = true;
                cv.notify_all();
                cv.wait( lock, [&] { return release_ordinary; } );
            }
            else if ( stage == Stage::AtomicMintWaitingForPersistenceGate )
            {
                atomic_waiting = true;
                cv.notify_all();
            }
            else if ( stage == Stage::AtomicMintPersistenceGateAcquired )
            {
                atomic_acquired = true;
                cv.notify_all();
            }
            return outcome::success();
        } );

    outcome::result<void> ordinary_result = outcome::success();
    outcome::result<UTXOManagerTestAccess::Result> atomic_result =
        outcome::failure( std::errc::operation_canceled );
    std::thread ordinary_thread( [&] { ordinary_result = utxo_manager->StoreUTXOs( owner ); } );
    {
        std::unique_lock lock( mutex );
        ASSERT_TRUE( cv.wait_for( lock, std::chrono::seconds( 5 ), [&] { return ordinary_paused; } ) );
    }
    std::thread atomic_thread(
        [&]
        {
            atomic_result = UTXOManagerTestAccess::Apply(
                *utxo_manager, winner, "11155111", burn, 7, outputs, owner );
        } );
    {
        std::unique_lock lock( mutex );
        ASSERT_TRUE( cv.wait_for( lock, std::chrono::seconds( 5 ), [&] { return atomic_waiting; } ) );
        EXPECT_FALSE( atomic_acquired );
        release_ordinary = true;
        cv.notify_all();
    }
    ordinary_thread.join();
    atomic_thread.join();
    ASSERT_TRUE( ordinary_result.has_value() );
    ASSERT_TRUE( atomic_result.has_value() );

    UTXOManagerTestAccess::ResetFault( *utxo_manager );
    utxo_manager->ReleaseStorage();
    auto reloaded = std::make_shared<UTXOManager>(
        std::string( PRIV_KEY ),
        []( const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return std::vector( hashed.begin(), hashed.end() );
        },
        []( const std::string &, const std::vector<uint8_t> &signature,
            const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return signature == std::vector( hashed.begin(), hashed.end() );
        } );
    ASSERT_TRUE( reloaded->LoadUTXOs( db_ ).has_value() );
    auto application = UTXOManagerTestAccess::Application( *reloaded, "11155111", burn, 7 );
    ASSERT_TRUE( application.has_value() );
    ASSERT_TRUE( application.value().has_value() );
    EXPECT_TRUE( reloaded->IsOutPointConsumed( burn, 7 ) );
    ASSERT_TRUE( reloaded->GetUnconsumedUTXO( winner, 0 ).has_value() );
    ASSERT_TRUE( reloaded->GetUnconsumedUTXO( unrelated, 0 ).has_value() );

    const auto burn2 = crypto::sha2_256( std::vector<uint8_t>{ 0x51 } );
    const auto winner2 = crypto::sha2_256( std::vector<uint8_t>{ 0x52 } );
    ASSERT_TRUE( reloaded
                     ->PutUTXO( GeniusUTXO( burn2, 8, 600, TOKEN_1 ),
                                owner,
                                UTXOManager::UTXOType::UTXO_BRIDGE )
                     .value() );
    const std::vector<GeniusUTXO> outputs2{
        GeniusUTXO( winner2, 0, 600, TOKEN_1, destination )
    };
    bool atomic_paused = false;
    bool ordinary_waiting = false;
    bool release_atomic = false;
    bool ordinary_acquired = false;
    UTXOManagerTestAccess::SetFault(
        *reloaded,
        [&]( Stage stage ) -> outcome::result<void>
        {
            std::unique_lock lock( mutex );
            if ( stage == Stage::AtomicMintBeforeBatchCommit )
            {
                atomic_paused = true;
                cv.notify_all();
                cv.wait( lock, [&] { return release_atomic; } );
            }
            else if ( stage == Stage::OrdinaryStoreWaitingForPersistenceGate )
            {
                ordinary_waiting = true;
                cv.notify_all();
            }
            else if ( stage == Stage::OrdinaryStorePersistenceGateAcquired )
            {
                ordinary_acquired = true;
                cv.notify_all();
            }
            return outcome::success();
        } );
    std::thread atomic_first(
        [&]
        {
            atomic_result = UTXOManagerTestAccess::Apply(
                *reloaded, winner2, "11155111", burn2, 8, outputs2, owner );
        } );
    {
        std::unique_lock lock( mutex );
        ASSERT_TRUE( cv.wait_for( lock, std::chrono::seconds( 5 ), [&] { return atomic_paused; } ) );
    }
    std::thread ordinary_second( [&] { ordinary_result = reloaded->StoreUTXOs( owner ); } );
    {
        std::unique_lock lock( mutex );
        ASSERT_TRUE( cv.wait_for( lock, std::chrono::seconds( 5 ), [&] { return ordinary_waiting; } ) );
        EXPECT_FALSE( ordinary_acquired );
        release_atomic = true;
        cv.notify_all();
    }
    atomic_first.join();
    ordinary_second.join();
    ASSERT_TRUE( atomic_result.has_value() );
    ASSERT_TRUE( ordinary_result.has_value() );
    UTXOManagerTestAccess::ResetFault( *reloaded );
    reloaded->ReleaseStorage();

    auto final_reload = std::make_shared<UTXOManager>(
        std::string( PRIV_KEY ),
        []( const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return std::vector( hashed.begin(), hashed.end() );
        },
        []( const std::string &, const std::vector<uint8_t> &signature,
            const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return signature == std::vector( hashed.begin(), hashed.end() );
        } );
    ASSERT_TRUE( final_reload->LoadUTXOs( db_ ).has_value() );
    auto application2 = UTXOManagerTestAccess::Application(
        *final_reload, "11155111", burn2, 8 );
    ASSERT_TRUE( application2.has_value() );
    ASSERT_TRUE( application2.value().has_value() );
    EXPECT_TRUE( final_reload->IsOutPointConsumed( burn2, 8 ) );
    EXPECT_TRUE( final_reload->GetUnconsumedUTXO( winner2, 0 ).has_value() );
    EXPECT_TRUE( final_reload->GetUnconsumedUTXO( unrelated, 0 ).has_value() );
}

TEST_F( UTXOManagerTest, AtomicFinalizedMintFailureRetryAndRestartReplayAreExact )
{
    using Stage = UTXOManagerTestAccess::Stage;
    using Result = UTXOManagerTestAccess::Result;
    const auto burn = crypto::sha2_256( std::vector<uint8_t>{ 0x61 } );
    const auto winner = crypto::sha2_256( std::vector<uint8_t>{ 0x62 } );
    const std::string chain = "11155111";
    const uint32_t index = 17;
    const std::string owner = "certified-bridge-owner";
    const std::string destination = "certified-destination";
    const std::vector<GeniusUTXO> outputs{
        GeniusUTXO( winner, 0, 700, TOKEN_1, destination )
    };
    auto store = std::make_shared<ConsensusStateStore>( db_ );
    ConsensusStateStore::BurnOutpoint outpoint{ chain, burn.toReadableString(), index };
    const auto preimage = fmt::format( "mint-v2:{}:{}:{}", chain, burn.toReadableString(), index );
    const auto slot = crypto::sha2_256(
        std::vector<uint8_t>( preimage.begin(), preimage.end() ) );
    auto created = store->CreateOrJoinBurnReservation(
        slot.toReadableString(), outpoint, 10'000, 1 );
    ASSERT_TRUE( created );
    auto finalized = store->FinalizeBurnReservation(
        slot.toReadableString(), outpoint, std::string( 64, 'a' ),
        std::string( 64, 'b' ), winner.toReadableString(), 2 );
    ASSERT_TRUE( finalized );
    ConsensusStateStore::FinalizedReservationIdentity identity{
        slot.toReadableString(), outpoint, finalized.value().generation(),
        finalized.value().certificate_digest(), finalized.value().proposal_id(),
        finalized.value().winner_id() };

    UTXOManagerTestAccess::SetFault(
        *utxo_manager,
        []( Stage stage ) -> outcome::result<void>
        {
            if ( stage == Stage::AtomicMintBeforeBatchCommit )
                return outcome::failure( std::errc::operation_canceled );
            return outcome::success();
        } );
    auto failed = UTXOManagerTestAccess::ApplyFinalized(
        *utxo_manager, store, identity, winner, outputs, owner, 700, TOKEN_1 );
    UTXOManagerTestAccess::ResetFault( *utxo_manager );
    ASSERT_TRUE( failed.has_error() );
    auto pending = store->GetBurnReservation( identity.slot_id );
    ASSERT_TRUE( pending && pending.value() );
    EXPECT_EQ( pending.value()->state(),
               ConsensusStateStore::BurnReservationRecord::FINALIZED_PENDING_APPLICATION );
    EXPECT_FALSE( UTXOManagerTestAccess::Application(
        *utxo_manager, chain, burn, index ).value().has_value() );
    EXPECT_FALSE( utxo_manager->GetUnconsumedUTXO( winner, 0 ).has_value() );
    EXPECT_FALSE( utxo_manager->IsOutPointConsumed( burn, index ) );

    auto applied = UTXOManagerTestAccess::ApplyFinalized(
        *utxo_manager, store, identity, winner, outputs, owner, 700, TOKEN_1 );
    ASSERT_TRUE( applied ) << applied.error().message();
    EXPECT_EQ( applied.value(), Result::Applied );
    auto consumed = store->GetBurnReservation( identity.slot_id );
    ASSERT_TRUE( consumed && consumed.value() );
    EXPECT_EQ( consumed.value()->state(),
               ConsensusStateStore::BurnReservationRecord::CONSUMED );
    EXPECT_TRUE( UTXOManagerTestAccess::Application(
        *utxo_manager, chain, burn, index ).value().has_value() );
    EXPECT_TRUE( utxo_manager->IsOutPointConsumed( burn, index ) );
    EXPECT_TRUE( utxo_manager->GetUnconsumedUTXO( winner, 0 ).has_value() );

    utxo_manager->ReleaseStorage();
    auto reloaded = std::make_shared<UTXOManager>(
        std::string( PRIV_KEY ),
        []( const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return std::vector( hashed.begin(), hashed.end() );
        },
        []( const std::string &, const std::vector<uint8_t> &signature,
            const std::vector<uint8_t> &data )
        {
            auto hashed = crypto::sha2_256( data );
            return signature == std::vector( hashed.begin(), hashed.end() );
        } );
    ASSERT_TRUE( reloaded->LoadUTXOs( db_ ) );
    auto replay = UTXOManagerTestAccess::ApplyFinalized(
        *reloaded, store, identity, winner, outputs, owner, 700, TOKEN_1 );
    ASSERT_TRUE( replay );
    EXPECT_EQ( replay.value(), Result::AlreadyApplied );
    EXPECT_EQ( reloaded->GetUTXOs( destination ).size(), 1U );
}

TEST_F( UTXOManagerTest, ConsumedApplicationRejectsDifferentWinnerIdentityAndArtifacts )
{
    const auto burn = crypto::sha2_256( std::vector<uint8_t>{ 0x71 } );
    const auto winner = crypto::sha2_256( std::vector<uint8_t>{ 0x72 } );
    const auto other_winner = crypto::sha2_256( std::vector<uint8_t>{ 0x73 } );
    const std::string chain = "11155111";
    const uint32_t index = 18;
    const std::string owner = "identity-owner";
    const std::vector<GeniusUTXO> outputs{
        GeniusUTXO( winner, 0, 800, TOKEN_1, "identity-destination" )
    };
    const auto preimage = fmt::format( "mint-v2:{}:{}:{}", chain, burn.toReadableString(), index );
    const auto slot = crypto::sha2_256( std::vector<uint8_t>( preimage.begin(), preimage.end() ) );
    auto store = std::make_shared<ConsensusStateStore>( db_ );
    ConsensusStateStore::BurnOutpoint outpoint{ chain, burn.toReadableString(), index };
    ASSERT_TRUE( store->CreateOrJoinBurnReservation(
        slot.toReadableString(), outpoint, 10'000, 1 ) );
    auto finalized = store->FinalizeBurnReservation(
        slot.toReadableString(), outpoint, std::string( 64, 'c' ),
        std::string( 64, 'd' ), winner.toReadableString(), 2 );
    ASSERT_TRUE( finalized );
    ConsensusStateStore::FinalizedReservationIdentity identity{
        slot.toReadableString(), outpoint, finalized.value().generation(),
        finalized.value().certificate_digest(), finalized.value().proposal_id(),
        finalized.value().winner_id() };
    auto first_apply = UTXOManagerTestAccess::ApplyFinalized(
        *utxo_manager, store, identity, winner, outputs, owner, 800, TOKEN_1 );
    ASSERT_TRUE( first_apply ) << first_apply.error().message();

    auto wrong_identity = identity;
    wrong_identity.winner_id = other_winner.toReadableString();
    auto identity_error = UTXOManagerTestAccess::ApplyFinalized(
        *utxo_manager, store, wrong_identity, other_winner,
        { GeniusUTXO( other_winner, 0, 800, TOKEN_1, "identity-destination" ) },
        owner, 800, TOKEN_1 );
    EXPECT_TRUE( identity_error.has_error() );

    auto changed_outputs = outputs;
    changed_outputs.front() = GeniusUTXO(
        winner, 0, 801, TOKEN_1, "identity-destination" );
    auto artifact_error = UTXOManagerTestAccess::ApplyFinalized(
        *utxo_manager, store, identity, winner, changed_outputs, owner, 800, TOKEN_1 );
    ASSERT_TRUE( artifact_error.has_error() );
    EXPECT_EQ( artifact_error.error(),
               std::make_error_code( std::errc::state_not_recoverable ) );
    EXPECT_EQ( utxo_manager->GetUnconsumedUTXO( winner, 0 )->GetAmount(), 800U );

    enum class Corruption : uint8_t
    {
        MissingApplication,
        MissingWinnerOutput,
        ConflictingOrderedOutput,
        ConsumedInputInconsistency,
    };
    const auto run_corruption = [&]( Corruption corruption, uint8_t seed, uint32_t case_index )
    {
        const auto case_burn = crypto::sha2_256( std::vector<uint8_t>{ seed } );
        const auto case_winner = crypto::sha2_256( std::vector<uint8_t>{ static_cast<uint8_t>( seed + 1 ) } );
        const std::vector<GeniusUTXO> case_outputs{
            GeniusUTXO( case_winner, 0, 810, TOKEN_1, "artifact-destination" ),
            GeniusUTXO( case_winner, 1, 190, TOKEN_1, "artifact-destination" )
        };
        const auto case_preimage = fmt::format(
            "mint-v2:{}:{}:{}", chain, case_burn.toReadableString(), case_index );
        const auto case_slot = crypto::sha2_256(
            std::vector<uint8_t>( case_preimage.begin(), case_preimage.end() ) );
        ConsensusStateStore::BurnOutpoint case_outpoint{
            chain, case_burn.toReadableString(), case_index };
        ASSERT_TRUE( store->CreateOrJoinBurnReservation(
            case_slot.toReadableString(), case_outpoint, 10'000, 1 ) );
        auto case_finalized = store->FinalizeBurnReservation(
            case_slot.toReadableString(), case_outpoint, std::string( 64, 'e' ),
            std::string( 64, 'f' ), case_winner.toReadableString(), 2 );
        ASSERT_TRUE( case_finalized );
        ConsensusStateStore::FinalizedReservationIdentity case_identity{
            case_slot.toReadableString(), case_outpoint, case_finalized.value().generation(),
            case_finalized.value().certificate_digest(), case_finalized.value().proposal_id(),
            case_finalized.value().winner_id() };
        ASSERT_TRUE( UTXOManagerTestAccess::ApplyFinalized(
            *utxo_manager, store, case_identity, case_winner, case_outputs,
            owner, 1'000, TOKEN_1 ) );

        switch ( corruption )
        {
            case Corruption::MissingApplication:
                ASSERT_TRUE( UTXOManagerTestAccess::RemoveApplication(
                    *utxo_manager, chain, case_burn, case_index ) );
                break;
            case Corruption::MissingWinnerOutput:
                UTXOManagerTestAccess::RemoveLiveOutpoint(
                    *utxo_manager, case_outputs.front().GetOutPoint() );
                break;
            case Corruption::ConflictingOrderedOutput:
                ASSERT_TRUE( UTXOManagerTestAccess::SwapStoredApplicationOutputs(
                    *utxo_manager, chain, case_burn, case_index ) );
                break;
            case Corruption::ConsumedInputInconsistency:
                UTXOManagerTestAccess::MarkLiveOutpointReady(
                    *utxo_manager, OutPoint{ case_burn, case_index } );
                break;
        }

        auto replay_error = UTXOManagerTestAccess::ApplyFinalized(
            *utxo_manager, store, case_identity, case_winner, case_outputs,
            owner, 1'000, TOKEN_1 );
        ASSERT_TRUE( replay_error.has_error() );
        EXPECT_EQ( replay_error.error(), std::make_error_code( std::errc::state_not_recoverable ) );
        auto protected_reservation = store->GetBurnReservation( case_identity.slot_id );
        ASSERT_TRUE( protected_reservation && protected_reservation.value() );
        EXPECT_EQ( protected_reservation.value()->state(),
                   ConsensusStateStore::BurnReservationRecord::CONSUMED );
    };

    run_corruption( Corruption::MissingApplication, 0x81, 21 );
    run_corruption( Corruption::MissingWinnerOutput, 0x83, 22 );
    run_corruption( Corruption::ConflictingOrderedOutput, 0x85, 23 );
    run_corruption( Corruption::ConsumedInputInconsistency, 0x87, 24 );
}
