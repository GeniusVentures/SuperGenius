#include "ping_session.hpp"
#include <libp2p/crypto/random_generator/boost_generator.hpp>

PingSession::PingSession(std::shared_ptr<boost::asio::io_context> io, std::shared_ptr<libp2p::Host> host)
    : host_(host)
{
    // Ping protocol setup
    libp2p::protocol::PingConfig pingConfig{};
    auto rng = std::make_shared<libp2p::crypto::random::BoostRandomGenerator>();
    ping_ = std::make_shared<libp2p::protocol::Ping>(*host, host->getBus(), *io, rng, pingConfig);

}

void PingSession::Init()
{
    subsOnNewConnection_ = host_->getBus().getChannel<libp2p::event::network::OnNewConnectionChannel>().subscribe(
        [ctx = shared_from_this()](auto&& conn) mutable {
        return ctx->OnNewConnection(conn, ctx->ping_);
    });

    host_->setProtocolHandler(
        ping_->getProtocolId(),
        [ctx = shared_from_this()](libp2p::protocol::BaseProtocol::StreamResult rstream) {
        ctx->ping_->handle(std::move(rstream));
    });
}

void PingSession::OnSessionPing(libp2p::outcome::result<std::shared_ptr<libp2p::protocol::PingClientSession>> session)
{
    if (session)
    {
        pingSession_ = std::move(session.value());
    }
}

void PingSession::OnNewConnection(
    const std::weak_ptr<libp2p::connection::CapableConnection>& conn,
    std::shared_ptr<libp2p::protocol::Ping> ping) {
    if (conn.expired())
    {
        return;
    }
    auto sconn = conn.lock();
    ping->startPinging(sconn, std::bind(&PingSession::OnSessionPing, this, std::placeholders::_1));
}
