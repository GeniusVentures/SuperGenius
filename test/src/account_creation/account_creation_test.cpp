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
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
#include <boost/dll.hpp>
#include <boost/algorithm/string/replace.hpp>

class AccountCreationTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {

    }

    static void TearDownTestSuite()
    {
    }
};

// Static member initialization

TEST_F(AccountCreationTest, AccountCreationAddress )
{

    sgns::GeniusAccount account(0, ".", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    std::string address_main = account.GetAddress<std::string>();
    EXPECT_EQ(address_main, "0xcffb285925b6e961cd9f3edf6568042f8282eee682f173b6330c8dc024a701a7") << "Address is not expected" << address_main;
}
