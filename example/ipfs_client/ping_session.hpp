#ifndef SUPERGENIUS_PING_SESSION_HPP
#define SUPERGENIUS_PING_SESSION_HPP

#include <libp2p/host/host.hpp>
#include <libp2p/protocol/ping/ping.hpp>

#include <boost/asio/io_context.hpp>
#include <memory>

class PingSession: public std::enable_shared_from_this<PingSession>
{
public:
    PingSession(std::shared_ptr<boost::asio::io_context> io, std::shared_ptr<libp2p::Host> host);
   
    void Init();
    void OnSessionPing(libp2p::outcome::result<std::shared_ptr<libp2p::protocol::PingClientSession>> session);
    void OnNewConnection(
        const std::weak_ptr<libp2p::connection::CapableConnection>& conn,
        std::shared_ptr<libp2p::protocol::Ping> ping);
private:    
    std::shared_ptr<libp2p::Host> host_;
    std::shared_ptr<libp2p::protocol::PingClientSession> pingSession_;
    std::shared_ptr<libp2p::protocol::Ping> ping_;
    libp2p::event::Handle subsOnNewConnection_;
};    

#endif // SUPERGENIUS_PING_SESSION_HPP
