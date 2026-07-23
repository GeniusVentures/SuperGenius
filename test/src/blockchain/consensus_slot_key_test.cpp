/**
 * @file       consensus_slot_key_test.cpp
 * @brief      Tests strict canonical consensus slot derivation.
 * @date       2026-07-23
 */
#include <gtest/gtest.h>

#include "account/TransactionManager.hpp"
#include "account/TransferTransaction.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "blockchain/Consensus.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

using namespace sgns;

namespace
{
    const std::string kAddressA( 128, 'a' );
    const std::string kAddressB( 128, 'b' );
    const std::string kBurnHashA( 64, 'a' );
    const std::string kBurnHashB( 64, 'b' );

    bool IsLowerHexSlot( const std::string &slot )
    {
        return slot.size() == 64 &&
               std::all_of(
                   slot.begin(),
                   slot.end(),
                   []( unsigned char c )
                   {
                       return std::isdigit( c ) != 0 || ( c >= 'a' && c <= 'f' );
                   } );
    }

    NonceSubject MakeMintV2NonceSubject( const std::string &chain_id,
                                         const std::string &token_id,
                                         uint64_t           amount,
                                         const std::string &dest_addr,
                                         const std::string &burn_tx_hash,
                                         uint32_t           receipt_index,
                                         std::size_t        input_count = 1,
                                         uint64_t           nonce       = 1,
                                         const std::string &tx_hash     = std::string( 64, 'c' ) )
    {
        NonceSubject subject;
        subject.set_nonce( nonce );
        subject.set_tx_hash( tx_hash );

        auto *mint = subject.mutable_transaction()->mutable_mint_v2();
        mint->set_chain_id( chain_id );
        mint->set_token_id( token_id );
        mint->set_amount( amount );
        mint->mutable_dag_struct()->set_source_addr( kAddressA );
        mint->mutable_dag_struct()->set_nonce( nonce );

        auto *output = mint->mutable_utxo_params()->add_outputs();
        output->set_dest_addr( dest_addr );
        output->set_encrypted_amount( amount );
        output->set_token_id( token_id );

        for ( std::size_t i = 0; i < input_count; ++i )
        {
            auto *input = mint->mutable_utxo_params()->add_inputs();
            input->set_tx_id_hash( burn_tx_hash );
            input->set_output_index( receipt_index + static_cast<uint32_t>( i ) );
        }

        return subject;
    }

    outcome::result<std::string> GetEmbeddedSlotPreimage( const NonceSubject &subject )
    {
        auto tx = TransactionManager::DeSerializeEmbeddedTransaction( subject.transaction() );
        if ( tx.has_error() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return tx.value()->GetSlotPreimage();
    }

    outcome::result<std::string> GetEmbeddedSlotId( const NonceSubject &subject )
    {
        auto tx = TransactionManager::DeSerializeEmbeddedTransaction( subject.transaction() );
        if ( tx.has_error() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return tx.value()->GetSlotID();
    }
} // namespace

TEST( CanonicalTransactionSlotTest, NormalTransactionHashesCanonicalAddressAndNonce )
{
    SGTransaction::DAGStruct dag;
    dag.set_source_addr( kAddressA );
    dag.set_nonce( 42 );
    auto tx = TransferTransaction::New( {}, {}, dag );

    auto preimage = tx.GetSlotPreimage();
    auto slot_id  = tx.GetSlotID();

    ASSERT_TRUE( preimage.has_value() );
    ASSERT_TRUE( slot_id.has_value() );
    EXPECT_EQ( preimage.value(), kAddressA + ":42" );
    EXPECT_EQ( slot_id.value(), "73dfd5a346a5f9101d43bc0e93bfe84d341b70a6e37a00fa28a445ef950a6824" );
    EXPECT_TRUE( IsLowerHexSlot( slot_id.value() ) );
}

TEST( CanonicalTransactionSlotTest, NormalTransactionNonceChangesSlot )
{
    auto slot_7 = GeniusTransaction::MakeNonceSlotPreimage( kAddressA, 7 );
    auto slot_8 = GeniusTransaction::MakeNonceSlotPreimage( kAddressA, 8 );
    ASSERT_TRUE( slot_7.has_value() );
    ASSERT_TRUE( slot_8.has_value() );

    EXPECT_NE( GeniusTransaction::HashSlotPreimage( slot_7.value() ),
               GeniusTransaction::HashSlotPreimage( slot_8.value() ) );
}

TEST( CanonicalTransactionSlotTest, NormalTransactionRejectsNoncanonicalAddress )
{
    EXPECT_TRUE( GeniusTransaction::MakeNonceSlotPreimage( "", 1 ).has_error() );
    EXPECT_TRUE( GeniusTransaction::MakeNonceSlotPreimage( std::string( 128, 'A' ), 1 ).has_error() );
    EXPECT_TRUE( GeniusTransaction::MakeNonceSlotPreimage( "0x" + std::string( 128, 'a' ), 1 ).has_error() );
    EXPECT_TRUE( GeniusTransaction::MakeNonceSlotPreimage( std::string( 127, 'a' ), 1 ).has_error() );
}

TEST( CanonicalTransactionSlotTest, MintUsesExactBurnIdentityPreimageAndDigest )
{
    auto subject  = MakeMintV2NonceSubject( "1", "token-a", 100, kAddressA, kBurnHashA, 7 );
    auto preimage = GetEmbeddedSlotPreimage( subject );
    auto slot_id  = GetEmbeddedSlotId( subject );

    ASSERT_TRUE( preimage.has_value() );
    ASSERT_TRUE( slot_id.has_value() );
    EXPECT_EQ( preimage.value(), "mint-v2:1:" + kBurnHashA + ":7" );
    EXPECT_EQ( slot_id.value(), "415d6d3898bfa18c50079f062da1082baeba67ab003fa2654bb5e28a652215df" );
    EXPECT_TRUE( IsLowerHexSlot( slot_id.value() ) );
    EXPECT_EQ( slot_id.value().find( "mint-v2:" ), std::string::npos );
}

TEST( CanonicalTransactionSlotTest, MintCandidateControlledFieldsCannotSplitBurnSlot )
{
    auto subject_a = MakeMintV2NonceSubject(
        "1", "token-a", 100, kAddressA, kBurnHashA, 7, 1, 1, std::string( 64, 'c' ) );
    auto subject_b = MakeMintV2NonceSubject(
        "1", "token-b", 999, kAddressB, kBurnHashA, 7, 1, 99, std::string( 64, 'd' ) );

    auto slot_a = GetEmbeddedSlotId( subject_a );
    auto slot_b = GetEmbeddedSlotId( subject_b );
    ASSERT_TRUE( slot_a.has_value() );
    ASSERT_TRUE( slot_b.has_value() );
    EXPECT_EQ( slot_a.value(), slot_b.value() );
}

TEST( CanonicalTransactionSlotTest, MintBurnComponentsEachChangeSlot )
{
    auto base_slot = GetEmbeddedSlotId(
        MakeMintV2NonceSubject( "1", "token", 100, kAddressA, kBurnHashA, 7 ) );
    auto chain_slot = GetEmbeddedSlotId(
        MakeMintV2NonceSubject( "2", "token", 100, kAddressA, kBurnHashA, 7 ) );
    auto hash_slot = GetEmbeddedSlotId(
        MakeMintV2NonceSubject( "1", "token", 100, kAddressA, kBurnHashB, 7 ) );
    auto index_slot = GetEmbeddedSlotId(
        MakeMintV2NonceSubject( "1", "token", 100, kAddressA, kBurnHashA, 8 ) );

    ASSERT_TRUE( base_slot.has_value() );
    ASSERT_TRUE( chain_slot.has_value() );
    ASSERT_TRUE( hash_slot.has_value() );
    ASSERT_TRUE( index_slot.has_value() );
    EXPECT_NE( base_slot.value(), chain_slot.value() );
    EXPECT_NE( base_slot.value(), hash_slot.value() );
    EXPECT_NE( base_slot.value(), index_slot.value() );
}

TEST( CanonicalTransactionSlotTest, MintRejectsNoncanonicalOrIncompleteIdentity )
{
    for ( const auto &chain : { "", "01", "+1", "-1", " 1", "1 " } )
    {
        EXPECT_TRUE( GetEmbeddedSlotId(
                         MakeMintV2NonceSubject( chain, "token", 100, kAddressA, kBurnHashA, 7 ) )
                         .has_error() )
            << chain;
    }

    EXPECT_TRUE( GetEmbeddedSlotId(
                     MakeMintV2NonceSubject( "1", "token", 100, kAddressA, kBurnHashA, 7, 0 ) )
                     .has_error() );
    EXPECT_TRUE( GetEmbeddedSlotId(
                     MakeMintV2NonceSubject( "1", "token", 100, kAddressA, kBurnHashA, 7, 2 ) )
                     .has_error() );
    EXPECT_TRUE( GetEmbeddedSlotId(
                     MakeMintV2NonceSubject( "1", "token", 100, kAddressA, std::string( 64, 'A' ), 7 ) )
                     .has_error() );
    EXPECT_TRUE( GetEmbeddedSlotId(
                     MakeMintV2NonceSubject( "1", "token", 100, kAddressA, "0x" + kBurnHashA, 7 ) )
                     .has_error() );
    EXPECT_TRUE( GetEmbeddedSlotId(
                     MakeMintV2NonceSubject( "1", "token", 100, kAddressA, std::string( 64, 'g' ), 7 ) )
                     .has_error() );
}
