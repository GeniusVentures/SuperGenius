#include <boost/filesystem/operations.hpp>
#include <gtest/gtest.h>

#include <cmath>

#include <boost/filesystem/path.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "account/GeniusNode.hpp"

namespace fs = boost::filesystem;

class AccountCreationTest : public ::testing::Test
{
public:
    ~AccountCreationTest() override
    {
        cleanup();
    }

    std::function<void( void )> cleanup;
};

TEST_F( AccountCreationTest, CreationWithEthereumKey )
{
    this->cleanup = []
    {
        fs::remove_all( "./account1" );
        fs::remove_all( "./account2" );
    };

    auto        account1      = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ),
                                                    "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                                    fs::path( "./account1" ) );
    auto        account2      = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ),
                                                    "deedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                                    fs::path( "./account2" ) );
    std::string address_main1 = account1->GetAddress();
    std::string address_main2 = account2->GetAddress();

    EXPECT_EQ(
        address_main1,
        "c865650410bdc1328cf99dc011c14cb52dc0aeb43b5f49dbf64a478fe2f6eafd2056ed0155770ba0a2832c1adb65c75df043c62e772d167437e4532d1b4e788f" )
        << " Address is not expected" << address_main1;
    EXPECT_NE( address_main1, address_main2 ) << "Addresses are equal even though they should not be";
}

TEST_F( AccountCreationTest, CreationWithMnemonic )
{
    this->cleanup = [] { fs::remove_all( "./mnemonic" ); };

    auto account = GeniusAccount::NewFromMnemonic(
        sgns::TokenID::FromBytes( { 0x00 } ),
        "picture tooth meat version snack comic tribe craft switch cricket vacuum squeeze",
        fs::path( "./mnemonic" ) );

    ASSERT_FALSE( account == nullptr ) << "Could not create account from mnemonic";
    ASSERT_EQ(
        account->GetAddress(),
        "27d36713d68c35403832cc321199dac8ab5d2e66bea4d72718b84f6acb1fa69fb716991b5a39f7b3707822ba9eef059624c3bfde74b025f03e591d32c6d7b3ab" );
}

TEST_F( AccountCreationTest, CreationWithRandomKey )
{
    this->cleanup = []
    {
        fs::remove_all( "./account1" );
        fs::remove_all( "./account2" );
    };

    auto        account1      = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), fs::path( "./account1" ) );
    auto        account2      = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), fs::path( "./account2" ) );
    std::string address_main1 = account1->GetAddress();
    std::string address_main2 = account2->GetAddress();

    EXPECT_NE( address_main1, address_main2 ) << "Addresses are equal even though they should not be";
}
