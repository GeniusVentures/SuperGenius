#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <boost/asio.hpp>
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "ProofSystem/ElGamalKeyGenerator.hpp"
#include "account/GeniusAccount.hpp"
#include "account/TokenID.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"

namespace fs = boost::filesystem;

using namespace sgns;

static const TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

TEST( AccountCreationTest, PredefinedElgamalPublicKeyMatchesSeed )
{
    const KeyGenerator::cpp_int generator = KeyGenerator::ElGamal::GENERATOR;
    const KeyGenerator::cpp_int prime     = KeyGenerator::ElGamal::SAFE_PRIME;
    const KeyGenerator::cpp_int public_key = powm( generator, KeyGenerator::cpp_int( 0x1234 ), prime );
    std::array<uint8_t, 32> exported;
    export_bits( public_key, exported.begin(), 8, false );

    EXPECT_EQ( GeniusAccount::ELGAMAL_PUBKEY_PREDEFINED, exported );
}

TEST( AccountCreationTest, CreationWithEthereumKey )
{
    // Inject in-memory secure storage to avoid OS keychain prompts during tests
    GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
        {
            return std::make_shared<MemorySecureStorage>( identifier );
        } );
    const auto dir   = boost::dll::program_location().parent_path();
    const auto path1 = dir / "account1";
    const auto path2 = dir / "account2";
    try
    {
        sgns::test::removeAllWithRetry( path1.string() );
        sgns::test::removeAllWithRetry( path2.string() );
    }
    catch ( ... )
    {
    };

    auto account1 = GeniusAccount::NewFromPrivateKey(
        TOKEN_ID,
        "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
        path1 );
    auto account2 = GeniusAccount::NewFromPrivateKey(
        TOKEN_ID,
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
        sgns::test::removeAllWithRetry( path.string() );
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

TEST( AccountCreationTest, CreationWithRandomMnemonicInAnEmptyDirectory )
{
    try
    {
        sgns::test::removeAllWithRetry( "./account1" );
        sgns::test::removeAllWithRetry( "./account2" );
    }
    catch ( ... )
    {
    };

    auto        account1      = GeniusAccount::New( TOKEN_ID, fs::path( "./account1" ) );
    auto        account2      = GeniusAccount::New( TOKEN_ID, fs::path( "./account2" ) );
    std::string address_main1 = account1->GetAddress();
    std::string address_main2 = account2->GetAddress();

    ASSERT_NE( address_main1, address_main2 ) << "Addresses are equal even though they should not be";
}

TEST( AccountCreationTest, CreationWithRandomMnemonic )
{
    try
    {
        sgns::test::removeAllWithRetry( "./account1" );
        sgns::test::removeAllWithRetry( "./account2" );
    }
    catch ( ... )
    {
    };

    auto        account1      = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, fs::path( "./account1" ) );
    auto        account2      = GeniusAccount::NewFromRandomMnemonic( TOKEN_ID, fs::path( "./account2" ) );
    std::string address_main1 = account1.first->GetAddress();
    std::string address_main2 = account2.first->GetAddress();

    EXPECT_NE( address_main1, address_main2 ) << "Addresses are equal even though they should not be";
    ASSERT_NE( account1.second, account2.second );
}
