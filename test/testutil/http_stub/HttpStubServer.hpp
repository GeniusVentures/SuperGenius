/**
 * Header file for the HttpStubServer — scriptable plain-HTTP stub for
 * hermetic price-client tests (TEST-03).
 */
#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sgns::testutil
{
    /**
     * A scriptable HTTP stub server bound to 127.0.0.1 with an OS-assigned
     * port. Each path is scripted with a status/content-type/body/delay/hang
     * response; the server speaks plain HTTP over its own io_context + thread
     * so client tests keep their own executor (D-09).
     */
    class HttpStubServer
    {
    public:
        /// @brief Scripted reply for one path.
        struct ScriptedResponse
        {
            unsigned                status      = 200;
            std::string             contentType = "application/json";
            std::string             body;
            std::chrono::milliseconds delay{ 0 };
            bool                    hang = false;
        };

        HttpStubServer()  = default;
        ~HttpStubServer();

        /// @brief Script a reply for a request path (e.g. "/ok").
        void OnPath( std::string path, ScriptedResponse r );

        /// @brief Bind 127.0.0.1:0, listen, and start the server thread.
        void Start();

        /// @brief The OS-assigned port (call after Start()).
        uint16_t Port() const;

        /// @brief Convenience URL builder: http://127.0.0.1:<port><path>
        std::string Url( std::string path ) const;

        /// @brief Stop accepting, close tracked sockets, join the thread.
        void Shutdown();

        /// @brief Last received User-Agent per path (for UA-propagation tests).
        std::string LastUserAgent( const std::string &path ) const;

    private:
        void DoAccept();
        void HandleSession( boost::asio::ip::tcp::socket socket );

        std::shared_ptr<boost::asio::io_context>                       ioc_;
        std::shared_ptr<boost::asio::ip::tcp::acceptor>                acceptor_;
        std::shared_ptr<boost::asio::executor_work_guard<
            boost::asio::io_context::executor_type>>                   workGuard_;
        std::thread                                                    thread_;
        mutable std::mutex                                             scriptsMutex_;
        std::map<std::string, ScriptedResponse>                        scripts_;
        mutable std::mutex                                             uaMutex_;
        std::map<std::string, std::string>                             lastUserAgent_;
        std::vector<std::shared_ptr<boost::asio::ip::tcp::socket>>     sessions_;
        std::mutex                                                     sessionsMutex_;
        bool                                                           started_ = false;
    };
} // namespace sgns::testutil
