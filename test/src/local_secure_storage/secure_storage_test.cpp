#include <gtest/gtest.h>

#include "local_secure_storage/ISecureStorage.hpp"

using namespace sgns;

struct SecureStorageTest : public ::testing::TestWithParam<std::shared_ptr<ISecureStorage>>
{
};

TEST_P( SecureStorageTest, Works )
{
    auto &storage = *GetParam();

    ASSERT_TRUE( storage.Save( "foo", "bar" ).has_value() );

    auto result_load = storage.Load( "foo" );

    ASSERT_TRUE( result_load.has_value() );

    ASSERT_EQ( result_load.value(), "bar" );

    auto result_remove = storage.DeleteKey( "foo" );

    ASSERT_TRUE( result_remove.has_value() );

    ASSERT_TRUE( result_remove.value() );

    result_remove = storage.DeleteKey( "foo" );

    ASSERT_TRUE( result_remove.has_value() );

    ASSERT_FALSE( result_remove.value() );
}

TEST_P( SecureStorageTest, StoringSameKeyTwiceUpdates )
{
    auto &storage = *GetParam();

    ASSERT_TRUE( storage.Save( "foo", "bar" ).has_value() );

    ASSERT_TRUE( storage.Save( "foo", "biz" ).has_value() );

    auto result_load = storage.Load( "foo" );

    ASSERT_TRUE( result_load.has_value() );

    ASSERT_EQ( result_load.value(), "biz" );
}

TEST_P( SecureStorageTest, CantGetKeyThatWasntSaved )
{
    auto &storage = *GetParam();

    ASSERT_TRUE( storage.DeleteKey( "foo" ).has_value() );

    ASSERT_FALSE( storage.Load( "foo" ).has_value() );
}

TEST_P( SecureStorageTest, WorksWithBinaryData )
{
    auto &storage = *GetParam();

    ISecureStorage::SecureBufferType binary( "B\0I\0N\0A\0R\0Y" );

    ASSERT_TRUE( storage.Save( "foo", binary ).has_value() );

    auto result_load = storage.Load( "foo" );

    EXPECT_TRUE( result_load.has_value() );

    ASSERT_EQ( result_load.value().size(), binary.size() );
    ASSERT_EQ( result_load.value(), binary );
}

static const std::string TEST_ID = "SGNSTest";

#ifdef __ANDROID__

#include "local_secure_storage/impl/Android.hpp"

INSTANTIATE_TEST_SUITE_P( SecureStorage,
                          SecureStorageTest,
                          testing::Values( std::make_shared<AndroidSecureStorage>( TEST_ID ) ) );

#elif defined( __linux__ )

#include "local_secure_storage/impl/Linux.hpp"

INSTANTIATE_TEST_SUITE_P( SecureStorage,
                          SecureStorageTest,
                          testing::Values( std::make_shared<LinuxSecureStorage>( TEST_ID ) ) );

#elif defined( _WIN32 )

#include "local_secure_storage/impl/Windows.hpp"

INSTANTIATE_TEST_SUITE_P( SecureStorage,
                          SecureStorageTest,
                          testing::Values( std::make_shared<WindowsSecureStorage>( TEST_ID ) ) );

#elif defined( __APPLE__ )

// Use in-memory storage for tests — avoids keychain password prompts.
// AppleSecureStorage is tested separately in integration tests.
#include "local_secure_storage/impl/MemorySecureStorage.hpp"

INSTANTIATE_TEST_SUITE_P( SecureStorage,
                          SecureStorageTest,
                          testing::Values( std::make_shared<MemorySecureStorage>( TEST_ID ) ) );

#endif
