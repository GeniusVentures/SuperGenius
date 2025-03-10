#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>

#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <functional>
#include <thread>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>
class NodeDeleteTest : public ::testing::Test
{
protected:
    static inline sgns::GeniusNode *node_proc = nullptr;

    static inline DevConfig_st DEV_CONFIG = { "0xcafe", "0.65", 1.0, 0, "./nodeDelete5/" };

    static inline std::string PRIVATE_KEY = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";

    static void SetUpTestSuite() {}

    static void TearDownTestSuite()
    {
        if ( node_proc != nullptr )
        {
            delete node_proc;
            node_proc = nullptr;
        }
    }
};

TEST_F( NodeDeleteTest, RecreateNode )
{
    std::string binary_path = boost::dll::program_location().parent_path().string();
    std::strncpy( DEV_CONFIG.BaseWritePath, ( binary_path + "/node10/" ).c_str(), sizeof( DEV_CONFIG.BaseWritePath ) );

    // Ensure null termination in case the string is too long
    DEV_CONFIG.BaseWritePath[sizeof( DEV_CONFIG.BaseWritePath ) - 1] = '\0';

    node_proc = new sgns::GeniusNode( DEV_CONFIG, PRIVATE_KEY.c_str(), false, false, 3000 );
    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );
    ASSERT_NE( nullptr, node_proc );
    delete node_proc;
    node_proc = nullptr;

    std::this_thread::sleep_for( std::chrono::milliseconds( 5000 ) );

    node_proc = new sgns::GeniusNode( DEV_CONFIG, PRIVATE_KEY.c_str(), false, false, 3000 );
    ASSERT_NE( nullptr, node_proc );
    std::this_thread::sleep_for( std::chrono::milliseconds( 2000 ) );
}

