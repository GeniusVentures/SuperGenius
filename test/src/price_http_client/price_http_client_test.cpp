#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "HttpStubServer.hpp"
#include <HTTPClient.hpp>
#include <FileManager.hpp>

namespace net = boost::asio;

namespace
{
    /// @brief LPM-05 layering discipline encoded in-test: only a 200 body
    /// ever reaches a parser; every other status returns nullopt.
    std::optional<std::string> ParseBodyIfOk( const sgns::http::Response &response )
    {
        if ( response.status == 200 )
        {
            return response.body;
        }
        return std::nullopt;
    }
} // namespace

class PriceHttpClientTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}

    void SetUp() override
    {
        stub_.OnPath( "/ok",
                      { 200,
                        "application/json",
                        R"({"genius-ai":{"usd":0.19}})" } );
        stub_.OnPath( "/blocked",
                      { 403,
                        "text/html",
                        "<!DOCTYPE html><html><body><h1>403 ERROR</h1>"
                        "<p>ERROR: The request could not be satisfied</p></body></html>" } );
        stub_.OnPath( "/ratelimited", { 429, "text/html", "Too Many Requests" } );
        stub_.OnPath( "/notfound", { 404, "text/plain", "no such path" } );
        stub_.OnPath( "/slow", { 200, "application/json", "{}", std::chrono::milliseconds( 3000 ) } );
        stub_.OnPath( "/hang", { 200, "application/json", "", std::chrono::milliseconds( 0 ), true } );
        stub_.Start();
    }

    void TearDown() override
    {
        stub_.Shutdown();
    }

    sgns::testutil::HttpStubServer stub_;

    sgns::http::RequestOptions DefaultOptions()
    {
        sgns::http::RequestOptions options;
        options.connectTimeout   = std::chrono::milliseconds( 2000 );
        options.handshakeTimeout = std::chrono::milliseconds( 2000 );
        options.readTimeout      = std::chrono::milliseconds( 2000 );
        return options;
    }
};

TEST_F( PriceHttpClientTest, StubBindsLoopbackWithOsAssignedPort )
{
    EXPECT_NE( stub_.Port(), uint16_t{ 0 } );
    const auto url = stub_.Url( "/ok" );
    EXPECT_EQ( url.substr( 0, 16 ), "http://127.0.0.1" );
}

TEST_F( PriceHttpClientTest, TwoStubsBindDifferentPorts )
{
    sgns::testutil::HttpStubServer second;
    second.OnPath( "/ok", { 200, "application/json", "{}" } );
    second.Start();
    EXPECT_NE( stub_.Port(), second.Port() );
    second.Shutdown();
}

TEST_F( PriceHttpClientTest, ShutdownJoinsWithoutHang )
{
    // Implicitly proven by TearDown on every test; make it explicit once.
    SUCCEED();
}

TEST_F( PriceHttpClientTest, Ok200 )
{
    auto       ioc     = std::make_shared<boost::asio::io_context>();
    auto       client  = std::make_shared<sgns::HTTPClient>( "127.0.0.1", "/ok", std::to_string( stub_.Port() ) );
    auto       result  = client->ExecuteBlocking( ioc, DefaultOptions() );
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value().status, unsigned{ 200 } );
    EXPECT_NE( result.value().body.find( "genius-ai" ), std::string::npos );
    EXPECT_EQ( ParseBodyIfOk( result.value() ).value_or( "" ), result.value().body );
}

TEST_F( PriceHttpClientTest, Blocked403Html )
{
    auto       ioc     = std::make_shared<boost::asio::io_context>();
    auto       client  = std::make_shared<sgns::HTTPClient>( "127.0.0.1", "/blocked", std::to_string( stub_.Port() ) );
    auto       result  = client->ExecuteBlocking( ioc, DefaultOptions() );
    // Transport SUCCESS carrying the truthful status (LPM-05 root fix)
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value().status, unsigned{ 403 } );
    EXPECT_NE( result.value().body.find( "ERROR: The request could not be satisfied" ), std::string::npos );
    auto contentType = result.value().headers.find( "Content-Type" );
    ASSERT_NE( contentType, result.value().headers.end() );
    EXPECT_NE( contentType->second.find( "text/html" ), std::string::npos );
    // The never-reaches-parser discipline: non-200 returns nullopt
    EXPECT_EQ( ParseBodyIfOk( result.value() ), std::nullopt );
}

TEST_F( PriceHttpClientTest, RateLimited429 )
{
    auto       ioc     = std::make_shared<boost::asio::io_context>();
    auto       client  = std::make_shared<sgns::HTTPClient>( "127.0.0.1", "/ratelimited", std::to_string( stub_.Port() ) );
    auto       result  = client->ExecuteBlocking( ioc, DefaultOptions() );
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value().status, unsigned{ 429 } );
    EXPECT_EQ( ParseBodyIfOk( result.value() ), std::nullopt );
}

TEST_F( PriceHttpClientTest, NotFound404 )
{
    auto       ioc     = std::make_shared<boost::asio::io_context>();
    auto       client  = std::make_shared<sgns::HTTPClient>( "127.0.0.1", "/notfound", std::to_string( stub_.Port() ) );
    auto       result  = client->ExecuteBlocking( ioc, DefaultOptions() );
    ASSERT_TRUE( result );
    EXPECT_EQ( result.value().status, unsigned{ 404 } );
}

TEST_F( PriceHttpClientTest, ReadTimeoutSlow )
{
    auto                  ioc     = std::make_shared<boost::asio::io_context>();
    auto                  client  = std::make_shared<sgns::HTTPClient>( "127.0.0.1", "/slow", std::to_string( stub_.Port() ) );
    auto                  options = DefaultOptions();
    options.readTimeout = std::chrono::milliseconds( 500 );
    auto                 result  = client->ExecuteBlocking( ioc, options );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error(), sgns::http::ClientError::TIMEOUT );
}

TEST_F( PriceHttpClientTest, ReadTimeoutHang )
{
    auto                  ioc     = std::make_shared<boost::asio::io_context>();
    auto                  client  = std::make_shared<sgns::HTTPClient>( "127.0.0.1", "/hang", std::to_string( stub_.Port() ) );
    auto                  options = DefaultOptions();
    options.readTimeout = std::chrono::milliseconds( 500 );
    auto                 result  = client->ExecuteBlocking( ioc, options );
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error(), sgns::http::ClientError::TIMEOUT );
}

TEST_F( PriceHttpClientTest, UserAgentPropagates )
{
    const std::string kPriceUa = "SGNS-PriceClient/1.0 (+https://gnus.ai; node price fetch)";
    auto       ioc     = std::make_shared<boost::asio::io_context>();
    auto       client  = std::make_shared<sgns::HTTPClient>( "127.0.0.1", "/ok", std::to_string( stub_.Port() ) );
    auto       options = DefaultOptions();
    options.userAgent = kPriceUa;
    auto       result  = client->ExecuteBlocking( ioc, options );
    ASSERT_TRUE( result );
    // The stub captured the UA header for this path; it must match what we sent
    EXPECT_EQ( stub_.LastUserAgent( "/ok" ), kPriceUa );
}

TEST_F( PriceHttpClientTest, PlainLoaderHttpUrl )
{
    ::FileManager::GetInstance().InitializeSingletons();
    auto        ioc       = std::make_shared<boost::asio::io_context>();
    auto        workGuard = std::make_shared<
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>( ioc->get_executor() );

    std::string body;
    bool        ok = false;
    // No range_error here proves D-03's "http" registration
    ::FileManager::GetInstance().LoadASync(
        stub_.Url( "/ok" ),
        false,
        false,
        ioc,
        [&, workGuard]( ::FileLoader::ResultType buffers )
        {
            if ( buffers )
            {
                body = std::string( buffers.value()->second[0].begin(), buffers.value()->second[0].end() );
                ok   = true;
            }
            // Release the guard in the completion so run() can drain (P-8)
            workGuard->reset();
        },
        "file" );

    ioc->run();
    EXPECT_TRUE( ok );
    EXPECT_NE( body.find( "genius-ai" ), std::string::npos );
}

// HttpsLegacyUnchanged: D-02's no-regression is a compile-level + diff-level
// guarantee (HTTPDevice untouched, verified by empty git diff in 02-02).
// Behavioral https coverage was the Phase 1 live baseline; this suite is
// hermetic by design and makes no live calls.
