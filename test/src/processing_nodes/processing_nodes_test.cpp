#include <gtest/gtest.h>

#include <math.h>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <atomic>

#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "local_secure_storage/impl/json/JSONSecureStorage.hpp"
#include "account/GeniusNode.hpp"
#include "FileManager.hpp"
class ProcessingNodesTest : public ::testing::Test
{
public:
    virtual void SetUp() override
    {

    }
};

TEST_F(ProcessingNodesTest, ProcessNodes)
{
    sgns::GeniusNode node_main( DEV_CONFIG, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
    sgns::GeniusNode node_proc1( DEV_CONFIG, "livebeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
    sgns::GeniusNode node_proc2( DEV_CONFIG, "zzombeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" );
}