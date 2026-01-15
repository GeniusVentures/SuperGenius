#include <gtest/gtest.h>

#include "base/blob.hpp" // for sgns::base::Hash256
#include "account/GeniusAccount.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"
#include "account/TokenID.hpp"
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"

using namespace sgns;
using namespace sgns::base;

// Test constants
static const sgns::TokenID   TOKEN_NAME = sgns::TokenID::FromBytes( { 0x01, 0x02 } );
static const auto            STORAGE    = std::make_shared<JSONSecureStorage>( "." );
static constexpr const char *PRIV_KEY   = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
static const Hash256         DUMMY_HASH{};

class GeniusAccountTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        account = GeniusAccount::New( TOKEN_NAME, STORAGE, PRIV_KEY );
    }

    std::shared_ptr<GeniusAccount> account;
};

TEST_F( GeniusAccountTest, InitialUTXOCount )
{
    // Insert four unique UTXOs
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x04 } ) ) ) );
    // Duplicate should be ignored
    EXPECT_FALSE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_EQ( account->GetUTXOs().size(), 4u );
}

TEST_F( GeniusAccountTest, TotalBalance )
{
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( account->GetBalance(), 140ull );
}

TEST_F( GeniusAccountTest, BalanceByToken )
{
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ), 70ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 30ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x03 } ) ), 40ull );
}

TEST_F( GeniusAccountTest, BalanceByTokenNonexistent )
{
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0xFF } ) ), 0ull );
}

TEST_F( GeniusAccountTest, StringTemplateBalance )
{
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 50, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    std::string s = std::to_string( account->GetBalance() );
    EXPECT_EQ( s, std::to_string( account->GetBalance() ) );
}

TEST_F( GeniusAccountTest, RefreshNoUTXOsLeavesAll )
{
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    size_t before = account->GetUTXOs().size();
    account->ConsumeUTXOs( {} );
    EXPECT_EQ( account->GetUTXOs().size(), before );
}

TEST_F( GeniusAccountTest, RefreshPartialUTXOsRemovesOnlySpecified )
{
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    InputUTXOInfo info;
    info.txid_hash_  = DUMMY_HASH;
    info.output_idx_ = 1; // remove idx 1
    account->ConsumeUTXOs( { info } );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 0ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ), 70ull );
}

TEST_F( GeniusAccountTest, RefreshAllUTXOsRemovesAll )
{
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( DUMMY_HASH, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    std::vector<InputUTXOInfo> infos;
    for ( const auto &utxo : account->GetUTXOs() )
    {
        InputUTXOInfo i;
        i.txid_hash_  = utxo.GetTxID();
        i.output_idx_ = utxo.GetOutputIdx();
        infos.push_back( i );
    }
    account->ConsumeUTXOs( infos );
    EXPECT_TRUE( account->GetUTXOs().empty() );
    EXPECT_EQ( account->GetBalance(), 0ull );
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
    EXPECT_FALSE( utxo.GetLock() );
}

TEST( InputUTXOInfo, FieldAssignment )
{
    InputUTXOInfo info;
    info.txid_hash_  = DUMMY_HASH;
    info.output_idx_ = 2;
    EXPECT_EQ( info.txid_hash_, DUMMY_HASH );
    EXPECT_EQ( info.output_idx_, 2u );
}
