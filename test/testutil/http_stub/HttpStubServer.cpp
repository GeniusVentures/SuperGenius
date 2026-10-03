/**
 * Source file for the HttpStubServer — Beast accept/session loop over
 * 127.0.0.1 with an OS-assigned port (TEST-03, R5/R6).
 */
#include "HttpStubServer.hpp"

#include <boost/asio/steady_timer.hpp>

#include <utility>

namespace sgns::testutil
{
    namespace beast = boost::beast;
    namespace http  = beast::http;
    namespace net   = boost::asio;
    using tcp       = net::ip::tcp;

    HttpStubServer::~HttpStubServer()
    {
        if ( started_ )
        {
            Shutdown();
        }
    }

    void HttpStubServer::OnPath( std::string path, ScriptedResponse r )
    {
        std::lock_guard<std::mutex> lock( scriptsMutex_ );
        scripts_[std::move( path )] = std::move( r );
    }

    void HttpStubServer::Start()
    {
        ioc_      = std::make_shared<boost::asio::io_context>();
        acceptor_ = std::make_shared<tcp::acceptor>( *ioc_ );
        workGuard_ = std::make_shared<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            ioc_->get_executor() );

        acceptor_->open( tcp::v4() );
        acceptor_->bind( tcp::endpoint{ net::ip::make_address( "127.0.0.1" ), 0 } );
        acceptor_->listen( net::socket_base::max_listen_connections );
        started_ = true;
        DoAccept();
        // Capture the io_context and work guard BY VALUE, never `this`: the
        // thread outlives neither guarantee — Shutdown() nulls ioc_/workGuard_
        // and the test may destroy the HttpStubServer immediately after join.
        // A `this` capture dangles once members are reset (Linux SIGSEGV in
        // the thread-state destructor after run() returns; Windows/Ubuntu
        // 22.04 masked it — EL8 + Boost 1.85 reliably faults).
        auto ioc        = ioc_;
        auto workGuard  = workGuard_;
        thread_         = std::thread( [ioc, workGuard]() { ioc->run(); } );
    }

    uint16_t HttpStubServer::Port() const
    {
        return acceptor_->local_endpoint().port();
    }

    std::string HttpStubServer::Url( std::string path ) const
    {
        return "http://127.0.0.1:" + std::to_string( Port() ) + path;
    }

    std::string HttpStubServer::LastUserAgent( const std::string &path ) const
    {
        std::lock_guard<std::mutex> lock( uaMutex_ );
        auto                        it = lastUserAgent_.find( path );
        return it != lastUserAgent_.end() ? it->second : std::string();
    }

    void HttpStubServer::Shutdown()
    {
        if ( !started_ )
        {
            return;
        }
        started_ = false;
        // Post-then-stop ordering avoids the join-deadlock (R6): the close
        // runs on the io thread, then stop() unblocks run(), then join.
        // Capture members via shared_ptrs by value: after join the server may
        // be destroyed, and `this` would dangle inside the posted handler if
        // the io thread is the last referrer.
        auto acceptor    = acceptor_;
        auto sessions    = std::make_shared<std::vector<std::shared_ptr<tcp::socket>>>();
        auto sessionsMtx = std::make_shared<std::mutex>();
        net::post( *ioc_,
                   [acceptor, sessions, sessionsMtx]()
                   {
                       boost::system::error_code ig;
                       acceptor->close( ig );
                   } );
        ioc_->stop();
        if ( thread_.joinable() )
        {
            thread_.join();
        }
        // Sockets are closed after join: the io thread is done touching them.
        {
            std::lock_guard<std::mutex> lock( sessionsMutex_ );
            for ( auto &s : sessions_ )
            {
                boost::system::error_code ig;
                s->close( ig );
            }
            sessions_.clear();
        }
        workGuard_->reset();
    }

    void HttpStubServer::DoAccept()
    {
        auto socket = std::make_shared<tcp::socket>( *ioc_ );
        {
            std::lock_guard<std::mutex> lock( sessionsMutex_ );
            sessions_.push_back( socket );
        }
        acceptor_->async_accept(
            *socket,
            [this, socket]( const boost::system::error_code &ec )
            {
                if ( !ec )
                {
                    HandleSession( std::move( *socket ) );
                    DoAccept();
                }
                // On error (acceptor closed at shutdown): stop accepting
            } );
    }

    void HttpStubServer::HandleSession( boost::asio::ip::tcp::socket socket )
    {
        auto shared_socket = std::make_shared<tcp::socket>( std::move( socket ) );
        {
            std::lock_guard<std::mutex> lock( sessionsMutex_ );
            sessions_.push_back( shared_socket );
        }
        auto buffer  = std::make_shared<beast::flat_buffer>();
        auto request = std::make_shared<http::request<http::string_body>>();

        http::async_read(
            *shared_socket,
            *buffer,
            *request,
            [this, shared_socket, buffer, request]( const boost::system::error_code &ec, std::size_t )
            {
                if ( ec )
                {
                    return;
                }
                const std::string target( request->target() );
                // Script keys are path-only: strip any query string so
                // /api/v3/simple/price?ids=... matches the "/api/v3/simple/price" script
                const auto         queryPos = target.find( '?' );
                const std::string pathOnly( queryPos == std::string::npos ? target : target.substr( 0, queryPos ) );
                {
                    std::lock_guard<std::mutex> lock( uaMutex_ );
                    lastUserAgent_[pathOnly] = std::string( request->at( http::field::user_agent ) );
                }

                ScriptedResponse script;
                {
                    std::lock_guard<std::mutex> lock( scriptsMutex_ );
                    auto                        it = scripts_.find( pathOnly );
                    if ( it != scripts_.end() )
                    {
                        script = it->second;
                    }
                    else
                    {
                        script.status      = 404;
                        script.contentType = "text/plain";
                        script.body        = "not scripted";
                    }
                }

                if ( script.hang )
                {
                    // Park: never write; socket is closed at teardown (R12)
                    return;
                }

                auto respond = [this, shared_socket, script]()
                {
                    http::response<http::string_body> response{ http::status::ok, 11 };
                    response.result( static_cast<http::status>( script.status ) );
                    response.set( http::field::server, "SGNS-HttpStubServer" );
                    response.set( http::field::content_type, script.contentType );
                    response.body() = script.body;
                    response.keep_alive( false );
                    response.prepare_payload();
                    http::async_write(
                        *shared_socket,
                        response,
                        [shared_socket]( const boost::system::error_code &, std::size_t )
                        {
                            boost::system::error_code ig;
                            shared_socket->shutdown( tcp::socket::shutdown_both, ig );
                        } );
                };

                if ( script.delay > std::chrono::milliseconds::zero() )
                {
                    auto timer = std::make_shared<boost::asio::steady_timer>( *ioc_ );
                    timer->expires_after( script.delay );
                    timer->async_wait( [timer, respond]( const boost::system::error_code & )
                                       {
                                           (void)timer;
                                           respond();
                                       } );
                }
                else
                {
                    respond();
                }
            } );
    }
} // namespace sgns::testutil
