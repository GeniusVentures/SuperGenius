#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>

#include "account/GeniusAccount.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/remove_all.hpp"

using namespace sgns;
namespace fs = boost::filesystem;

TEST( SecureStorage, JsonMigration )
{
    // Inject in-memory secure storage to avoid OS keychain prompts during tests
    GeniusAccount::SetSecureStorageFactory(
        []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
        {
            return std::make_shared<MemorySecureStorage>( identifier );
        } );
    auto binary_parent = boost::dll::program_location().parent_path();
    const fs::path json_storage_path = binary_parent / "j";
    const fs::path test_path = fs::unique_path( fs::temp_directory_path() / "json_migration-%%%%-%%%%" );

    fs::copy( json_storage_path, test_path, fs::copy_options::recursive);

    auto account_attempt = GeniusAccount::New( sgns::TokenID::FromBytes( { 0x00 } ), test_path );
    
    ASSERT_FALSE(account_attempt == nullptr) << "Could not create GeniusAccount from JSON secure storage alone";
    ASSERT_EQ(account_attempt->GetAddress(), "c865650410bdc1328cf99dc011c14cb52dc0aeb43b5f49dbf64a478fe2f6eafd2056ed0155770ba0a2832c1adb65c75df043c62e772d167437e4532d1b4e788f");
    account_attempt.reset();
    sgns::test::removeAllWithRetry( test_path.string() );
}
