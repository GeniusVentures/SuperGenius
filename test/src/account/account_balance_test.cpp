#include <gtest/gtest.h>

#include "base/blob.hpp" // for sgns::base::Hash256
#include "account/GeniusAccount.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"
#include "account/TokenID.hpp"

using namespace sgns;
using namespace sgns::base;

// Test constants
static const sgns::TokenID   TOKEN_NAME = sgns::TokenID::FromBytes( { 0x01, 0x02 } );
static const std::string     DATA_DIR   = ".";
static constexpr const char *PRIV_KEY   = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

TEST( GeniusAccount, InitialUTXOCount )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    // Insert four unique UTXOs
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, sgns::TokenID::FromBytes( { 0x04 } ) ) ) );
    // Duplicate should be ignored
    EXPECT_FALSE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_EQ( account->utxos.size(), 4u );
}

TEST( GeniusAccount, TotalBalance )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( account->GetBalance<uint64_t>(), 140ull );
}

TEST( GeniusAccount, BalanceByToken )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ), 70ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 30ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x03 } ) ), 40ull );
}

TEST( GeniusAccount, BalanceByTokenNonexistent )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0xFF } ) ), 0ull );
}

TEST( GeniusAccount, StringTemplateBalance )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 50, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    std::string s = account->GetBalance<std::string>();
    EXPECT_EQ( s, std::to_string( account->GetBalance<uint64_t>() ) );
}

TEST( GeniusAccount, RefreshNoUTXOsLeavesAll )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    size_t before = account->utxos.size();
    account->RefreshUTXOs( {} );
    EXPECT_EQ( account->utxos.size(), before );
}

TEST( GeniusAccount, RefreshPartialUTXOsRemovesOnlySpecified )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    InputUTXOInfo info;
    info.txid_hash_  = h;
    info.output_idx_ = 1; // remove idx 1
    account->RefreshUTXOs( { info } );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 0ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ), 70ull );
}

TEST( GeniusAccount, RefreshAllUTXOsRemovesAll )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    std::vector<InputUTXOInfo> infos;
    for ( const auto &utxo : account->utxos )
    {
        InputUTXOInfo i;
        i.txid_hash_  = utxo.GetTxID();
        i.output_idx_ = utxo.GetOutputIdx();
        infos.push_back( i );
    }
    account->RefreshUTXOs( infos );
    EXPECT_TRUE( account->utxos.empty() );
    EXPECT_EQ( account->GetBalance<uint64_t>(), 0ull );
}

TEST( GeniusUTXO, PropertyAccessors )
{
    Hash256    h;
    uint32_t   idx = 5;
    uint64_t   amt = 12345;
    auto       tok = TokenID::FromBytes( { 0x01, 0x02 } );
    GeniusUTXO utxo( h, idx, amt, tok );
    EXPECT_EQ( utxo.GetTxID(), h );
    EXPECT_EQ( utxo.GetOutputIdx(), idx );
    EXPECT_EQ( utxo.GetAmount(), amt );
    EXPECT_EQ( utxo.GetTokenID(), tok );
    EXPECT_FALSE( utxo.GetLock() );
}

TEST( InputUTXOInfo, FieldAssignment )
{
    InputUTXOInfo info;
    Hash256       h;
    info.txid_hash_  = h;
    info.output_idx_ = 2;
    EXPECT_EQ( info.txid_hash_, h );
    EXPECT_EQ( info.output_idx_, 2u );
}
