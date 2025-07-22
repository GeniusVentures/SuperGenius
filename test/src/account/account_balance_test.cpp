#include <gtest/gtest.h>

#include "base/blob.hpp" // for sgns::base::Hash256
#include "account/GeniusAccount.hpp"
#include "account/GeniusUTXO.hpp"
#include "account/UTXOTxParameters.hpp"
<<<<<<< HEAD
=======
#include "account/TokenID.hpp"
>>>>>>> origin/dev_logfixes

using namespace sgns;
using namespace sgns::base;

// Test constants
<<<<<<< HEAD
static const std::string     TOKEN_NAME = "TEST_TOKEN";
=======
static const sgns::TokenID   TOKEN_NAME = sgns::TokenID::FromBytes( { 0x01, 0x02 } );
>>>>>>> origin/dev_logfixes
static const std::string     DATA_DIR   = ".";
static constexpr const char *PRIV_KEY   = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

TEST( GeniusAccount, InitialUTXOCount )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    // Insert four unique UTXOs
<<<<<<< HEAD
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    // Duplicate should be ignored
    EXPECT_FALSE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
=======
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, sgns::TokenID::FromBytes( { 0x04 } ) ) ) );
    // Duplicate should be ignored
    EXPECT_FALSE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
>>>>>>> origin/dev_logfixes
    EXPECT_EQ( account->utxos.size(), 4u );
}

TEST( GeniusAccount, TotalBalance )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
<<<<<<< HEAD
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
=======
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
>>>>>>> origin/dev_logfixes
    EXPECT_EQ( account->GetBalance<uint64_t>(), 140ull );
}

TEST( GeniusAccount, BalanceByToken )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
<<<<<<< HEAD
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    EXPECT_EQ( account->GetBalance( "TOKA" ), 70ull );
    EXPECT_EQ( account->GetBalance( "TOKB" ), 30ull );
    EXPECT_EQ( account->GetBalance( "TOKC" ), 40ull );
=======
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ), 70ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 30ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x03 } ) ), 40ull );
>>>>>>> origin/dev_logfixes
}

TEST( GeniusAccount, BalanceByTokenNonexistent )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
<<<<<<< HEAD
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    EXPECT_EQ( account->GetBalance( "UNKNOWN" ), 0ull );
}

TEST( GeniusAccount, BalanceByTokenSet )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    std::vector<std::string> tokens = { "TOKA", "TOKC" };
    EXPECT_EQ( account->GetBalance( tokens ), 110ull );
}

TEST( GeniusAccount, BalanceByTokenSetEmpty )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, "TOKC" ) ) );
    std::vector<std::string> empty;
    EXPECT_EQ( account->GetBalance( empty ), 0ull );
=======
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 3, 40, sgns::TokenID::FromBytes( { 0x03 } ) ) ) );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0xFF } ) ), 0ull );
>>>>>>> origin/dev_logfixes
}

TEST( GeniusAccount, StringTemplateBalance )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
<<<<<<< HEAD
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 50, "TOKB" ) ) );
=======
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 50, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
>>>>>>> origin/dev_logfixes
    std::string s = account->GetBalance<std::string>();
    EXPECT_EQ( s, std::to_string( account->GetBalance<uint64_t>() ) );
}

TEST( GeniusAccount, RefreshNoUTXOsLeavesAll )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
<<<<<<< HEAD
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
=======
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
>>>>>>> origin/dev_logfixes
    size_t before = account->utxos.size();
    account->RefreshUTXOs( {} );
    EXPECT_EQ( account->utxos.size(), before );
}

TEST( GeniusAccount, RefreshPartialUTXOsRemovesOnlySpecified )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
<<<<<<< HEAD
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, "TOKA" ) ) );
=======
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 2, 20, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
>>>>>>> origin/dev_logfixes
    InputUTXOInfo info;
    info.txid_hash_  = h;
    info.output_idx_ = 1; // remove idx 1
    account->RefreshUTXOs( { info } );
<<<<<<< HEAD
    EXPECT_EQ( account->GetBalance( "TOKB" ), 0ull );
    EXPECT_EQ( account->GetBalance( "TOKA" ), 70ull );
=======
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x02 } ) ), 0ull );
    EXPECT_EQ( account->GetBalance( sgns::TokenID::FromBytes( { 0x01 } ) ), 70ull );
>>>>>>> origin/dev_logfixes
}

TEST( GeniusAccount, RefreshAllUTXOsRemovesAll )
{
    auto    account = std::make_unique<GeniusAccount>( TOKEN_NAME, DATA_DIR, PRIV_KEY );
    Hash256 h;
<<<<<<< HEAD
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, "TOKA" ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, "TOKB" ) ) );
=======
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 0, 50, sgns::TokenID::FromBytes( { 0x01 } ) ) ) );
    EXPECT_TRUE( account->PutUTXO( GeniusUTXO( h, 1, 30, sgns::TokenID::FromBytes( { 0x02 } ) ) ) );
>>>>>>> origin/dev_logfixes
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
<<<<<<< HEAD
    Hash256     h;
    uint32_t    idx = 5;
    uint64_t    amt = 12345;
    std::string tok = "TOKX";
    GeniusUTXO  utxo( h, idx, amt, tok );
=======
    Hash256    h;
    uint32_t   idx = 5;
    uint64_t   amt = 12345;
    auto       tok = TokenID::FromBytes( { 0x01, 0x02 } );
    GeniusUTXO utxo( h, idx, amt, tok );
>>>>>>> origin/dev_logfixes
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
