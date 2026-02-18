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

    auto        account       = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ),
                                       "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                       fs::path( "./account1" ) );
    auto        account2      = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ),
                                        "deedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                        fs::path( "./account2" ) );
    std::string address_main  = account->GetAddress();
    std::string address_main2 = account2->GetAddress();
    
    EXPECT_EQ(
        address_main,
        "c865650410bdc1328cf99dc011c14cb52dc0aeb43b5f49dbf64a478fe2f6eafd2056ed0155770ba0a2832c1adb65c75df043c62e772d167437e4532d1b4e788f" )
        << " Address is not expected" << address_main;
    EXPECT_NE( address_main, address_main2 ) << "Addresses are equal even though they should not be";
}

TEST_F( AccountCreationTest, CreationWithCredentials )
{
    this->cleanup = []
    {
        fs::remove_all( "./account1" );
        fs::remove_all( "./account2" );
    };

    auto        account       = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ),
                                                    { "account1@gnus.ai", "1234" },
                                       fs::path( "./account1" ) );
    auto        account2      = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ),
                                                    { "account2@gnus.ai", "4321" },
                                        fs::path( "./account2" ) );
    std::string address_main  = account->GetAddress();
    std::string address_main2 = account2->GetAddress();

    EXPECT_NE( address_main, address_main2 ) << "Addresses are equal even though they should not be";
}

TEST_F( AccountCreationTest, CreationWithRandomKeys )
{
    this->cleanup = []
    {
        fs::remove_all( "./account1" );
        fs::remove_all( "./account2" );
    };

    auto        account       = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), fs::path( "./account1" ) );
    auto        account2      = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), fs::path( "./account2" ) );
    std::string address_main  = account->GetAddress();
    std::string address_main2 = account2->GetAddress();

    EXPECT_NE( address_main, address_main2 ) << "Addresses are equal even though they should not be";
}
