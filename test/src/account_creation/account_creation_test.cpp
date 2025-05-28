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

    sgns::GeniusAccount account("0", ".", "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    sgns::GeniusAccount account2("0", ".", "deedbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    std::string address_main = account.GetAddress();
    std::string address_main2 = account2.GetAddress();
    EXPECT_EQ(address_main, "c865650410bdc1328cf99dc011c14cb52dc0aeb43b5f49dbf64a478fe2f6eafd2056ed0155770ba0a2832c1adb65c75df043c62e772d167437e4532d1b4e788f") << " Address is not expected" << address_main;
    EXPECT_NE(address_main, address_main2) << "Addresses are equal even though they should not be";
}
