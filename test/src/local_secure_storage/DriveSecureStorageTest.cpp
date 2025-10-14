#include <boost/asio/io_context.hpp>
#include <gtest/gtest.h>

#include "local_secure_storage/impl/drive/DriveSecureStorage.hpp"

using namespace sgns;

TEST( DriveSecureStorage, Works )
{
    auto ioc = std::make_shared<boost::asio::io_context>();

    auto storage = DriveSecureStorage( "", ioc );

    std::cout << storage.GenerateAuthUrl() << std::endl;

    std::string auth_code;
    std::cin >> auth_code;

    auto auth_result = storage.Authenticate(auth_code);
    ASSERT_FALSE(auth_result.has_error());

    constexpr auto CONTENT = "123456";
    constexpr auto KEY     = "drive_secure_storage_key";

    auto save_result = storage.Save( KEY, CONTENT, "" );
    ASSERT_FALSE( save_result.has_error() );

    auto load_result = storage.Load( KEY, "" );
    ASSERT_FALSE( load_result.has_error() );

    ASSERT_EQ( load_result.value(), CONTENT );
}
