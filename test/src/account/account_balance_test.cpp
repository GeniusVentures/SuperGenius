#include <gtest/gtest.h>

#include "base/blob.hpp" // for sgns::base::Hash256
#include "account/GeniusAccount.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"

using namespace sgns;
using namespace sgns::base;

// Test constants
static const std::string     TOKEN_NAME = "TEST_TOKEN";
static const std::string     DATA_DIR   = ".";
static constexpr const char *PRIV_KEY   = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

TEST( GeniusAccount, InitialUTXOCount )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    // Insert four unique UTXOs
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    // Duplicate should be ignored
    EXPECT_FALSE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_EQ( account->utxos.size(), 4u );
}

TEST( GeniusAccount, TotalBalance )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    EXPECT_EQ( account->GetBalance<uint64_t>(), 140ull );
}

TEST( GeniusAccount, BalanceByToken )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    EXPECT_EQ( account->GetBalance( "TOKA" ), 70ull );
    EXPECT_EQ( account->GetBalance( "TOKB" ), 30ull );
    EXPECT_EQ( account->GetBalance( "TOKC" ), 40ull );
}

TEST( GeniusAccount, BalanceByTokenNonexistent )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    EXPECT_EQ( account->GetBalance( "UNKNOWN" ), 0ull );
}

TEST( GeniusAccount, StringTemplateBalance )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 50, "TOKB" ) ) );
    std::string s = account->GetBalance<std::string>();
    EXPECT_EQ( s, std::to_string( account->GetBalance<uint64_t>() ) );
}

TEST( GeniusAccount, RefreshNoUTXOsLeavesAll )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    size_t before = account->utxos.size();
    account->RefreshUTXOs( {} );
    EXPECT_EQ( account->utxos.size(), before );
}

TEST( GeniusAccount, RefreshPartialUTXOsRemovesOnlySpecified )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    InputUTXOInfo info;
    info.txid_hash_  = h;
    info.output_idx_ = 1; // remove idx 1
    account->RefreshUTXOs( { info } );
    EXPECT_EQ( account->GetBalance( "TOKB" ), 0ull );
    EXPECT_EQ( account->GetBalance( "TOKA" ), 70ull );
}

TEST( GeniusAccount, RefreshAllUTXOsRemovesAll )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
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
    Hash256     h;
    uint32_t    idx = 5;
    uint64_t    amt = 12345;
    std::string tok = "TOKX";
    GeniusUTXO  utxo( h, idx, amt, tok );
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
