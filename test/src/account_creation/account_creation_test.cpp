#include <gtest/gtest.h>

#include <cmath>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <boost/asio.hpp>
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "account/GeniusAccount.hpp"
#include "account/TokenID.hpp"

namespace fs = boost::filesystem;

static const sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

TEST( AccountCreationTest, CreationWithEthereumKey )
{
    const auto dir   = boost::dll::program_location().parent_path();
    const auto path1 = dir / "account1";
    const auto path2 = dir / "account2";
    try
    {
        fs::remove_all( path1 );
        fs::remove_all( path2 );
    }
    catch ( ... )
    {
    };

    auto        account1      = GeniusAccount::New( TOKEN_ID,
                                                    "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                                    path1 );
    auto        account2      = GeniusAccount::New( TOKEN_ID,
                                                    "deedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                                    path2 );
    std::string address_main1 = account1->GetAddress();
    std::string address_main2 = account2->GetAddress();

    EXPECT_EQ(
        address_main1,
        "c865650410bdc1328cf99dc011c14cb52dc0aeb43b5f49dbf64a478fe2f6eafd2056ed0155770ba0a2832c1adb65c75df043c62e772d167437e4532d1b4e788f" )
        << " Address is not expected" << address_main1;
    EXPECT_NE( address_main1, address_main2 ) << "Addresses are equal even though they should not be";

    // Load account directly from storage after creation
    account1 = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), path1 );
    ASSERT_EQ( account1->GetAddress(), address_main1 );
}

TEST( AccountCreationTest, CreationWithMnemonic )
{
    const auto path = boost::dll::program_location().parent_path() / "mnemonic";
    try
    {
        fs::remove_all( path );
    }
    catch ( ... )
    {
    };

    auto account = GeniusAccount::NewFromMnemonic(
        TOKEN_ID,
        "picture tooth meat version snack comic tribe craft switch cricket vacuum squeeze",
        fs::path( path ) );

    ASSERT_FALSE( account == nullptr ) << "Could not create account from mnemonic";

    auto address = account->GetAddress();
    ASSERT_EQ(
        address,
        "27d36713d68c35403832cc321199dac8ab5d2e66bea4d72718b84f6acb1fa69fb716991b5a39f7b3707822ba9eef059624c3bfde74b025f03e591d32c6d7b3ab" );

    // Load account directly from storage after creation
    account = GeniusAccount::New( TOKEN_ID, path );
    ASSERT_EQ( account->GetAddress(), address );
}

TEST( AccountCreationTest, CreationWithRandomKey )
{
    try
    {
        fs::remove_all( "./account1" );
        fs::remove_all( "./account2" );
    }
    catch ( ... )
    {
    };

    auto        account1      = GeniusAccount::New( TOKEN_ID, fs::path( "./account1" ) );
    auto        account2      = GeniusAccount::New( TOKEN_ID, fs::path( "./account2" ) );
    std::string address_main1 = account1->GetAddress();
    std::string address_main2 = account2->GetAddress();

    EXPECT_NE( address_main1, address_main2 ) << "Addresses are equal even though they should not be";
}
