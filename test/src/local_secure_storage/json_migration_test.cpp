#include <gtest/gtest.h>
#include <boost/filesystem.hpp>

#include "account/GeniusAccount.hpp"

using namespace sgns;
namespace fs = boost::filesystem;

TEST( SecureStorage, JsonMigration )
{
    const fs::path json_storage_path( "json_storage" );
    const fs::path test_path( "json_storage_test" );

    fs::remove_all(test_path);
    fs::copy( json_storage_path, test_path, fs::copy_options::recursive);

    auto account_attempt = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), test_path );
    
    ASSERT_FALSE(account_attempt == nullptr) << "Could not create GeniusAccount from JSON secure storage alone";
    ASSERT_EQ(account_attempt->GetAddress(), "c865650410bdc1328cf99dc011c14cb52dc0aeb43b5f49dbf64a478fe2f6eafd2056ed0155770ba0a2832c1adb65c75df043c62e772d167437e4532d1b4e788f");
}
