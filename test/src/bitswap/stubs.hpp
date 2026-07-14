#ifndef SUPERGENIUS_BITSWAP_STUBS_HPP
#define SUPERGENIUS_BITSWAP_STUBS_HPP

#include <memory>
#include <stdexcept>

#include <libp2p/host/host.hpp>
#include <libp2p/network/router.hpp>
#include <libp2p/network/network.hpp>
#include <libp2p/network/dialer.hpp>
#include <libp2p/network/listener_manager.hpp>
#include <libp2p/network/connection_manager.hpp>
#include <libp2p/network/route_helper.hpp>
#include <libp2p/peer/peer_repository.hpp>
#include <libp2p/peer/address_repository.hpp>
#include <libp2p/peer/key_repository.hpp>
#include <libp2p/peer/protocol_repository.hpp>
#include <libp2p/event/bus.hpp>
#include <libp2p/protocol/relay/relay_addresses.hpp>
#include <libp2p/protocol/identify/observed_addresses.hpp>

#include <bitswap.hpp>

#include "testutil/wait_condition.hpp"

#include <gtest/gtest.h>

namespace sgns::ipfs_bitswap
{
    class StubRouter : public libp2p::network::Router
    {
    public:
        void setProtocolHandler(
            libp2p::StreamProtocols /*protocols*/,
            libp2p::StreamAndProtocolCb /*cb*/,
            libp2p::ProtocolPredicate /*predicate*/ = {} ) override
        {
        }

        std::vector<libp2p::peer::Protocol> getSupportedProtocols() const override
        {
            return {};
        }

        void removeProtocolHandlers( const libp2p::peer::Protocol & ) override
        {
        }

        void removeAll() override
        {
        }

        libp2p::outcome::result<void> handle(
            const libp2p::peer::Protocol &,
            std::shared_ptr<libp2p::connection::Stream> ) override
        {
            return libp2p::outcome::success();
        }
    };

    class StubDialer : public libp2p::network::Dialer
    {
    public:
        void dial(
            const libp2p::peer::PeerInfo &,
            DialResultFunc,
            std::chrono::milliseconds,
            const libp2p::network::RouteHelper::SourceAddresses &,
            bool = false,
            bool = false ) override
        {
        }

        void newStream(
            const libp2p::peer::PeerInfo &,
            libp2p::StreamProtocols,
            libp2p::StreamAndProtocolOrErrorCb cb,
            std::chrono::milliseconds,
            const libp2p::network::RouteHelper::SourceAddresses & ) override
        {
            cb( libp2p::StreamAndProtocolOrError(
                libp2p::StreamAndProtocol{} ) );
        }

        void newStream(
            const libp2p::peer::PeerId &,
            libp2p::StreamProtocols,
            libp2p::StreamAndProtocolOrErrorCb cb,
            const libp2p::network::RouteHelper::SourceAddresses & ) override
        {
            cb( libp2p::StreamAndProtocolOrError(
                libp2p::StreamAndProtocol{} ) );
        }
    };

    class StubListenerManager : public libp2p::network::ListenerManager
    {
    public:
        bool isStarted() const override
        {
            return true;
        }

        void start() override
        {
        }

        void stop() override
        {
        }

        libp2p::outcome::result<void> listen(
            const libp2p::multi::Multiaddress & ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> closeListener(
            const libp2p::multi::Multiaddress & ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> removeListener(
            const libp2p::multi::Multiaddress & ) override
        {
            return libp2p::outcome::success();
        }

        std::vector<libp2p::multi::Multiaddress> getListenAddresses()
            const override
        {
            return {};
        }

        std::vector<libp2p::multi::Multiaddress>
        getListenAddressesInterfaces() const override
        {
            return {};
        }

        libp2p::network::Router &getRouter() override
        {
            return router_;
        }

        void onConnection(
            libp2p::outcome::result<std::shared_ptr<libp2p::connection::CapableConnection>> ) override
        {
        }

        void onConnectionRelay(
            libp2p::peer::PeerId,
            libp2p::outcome::result<std::shared_ptr<libp2p::connection::CapableConnection>> ) override
        {
        }

        void removeRelayedConnections(
            libp2p::peer::PeerId ) override
        {
        }

    private:
        StubRouter router_;
    };

    class StubConnectionManager : public libp2p::network::ConnectionManager
    {
    public:
        std::vector<ConnectionSPtr> getConnections() const override
        {
            return {};
        }

        std::vector<ConnectionSPtr> getConnectionsToPeer(
            const libp2p::peer::PeerId & ) const override
        {
            return {};
        }

        ConnectionSPtr getBestConnectionForPeer(
            const libp2p::peer::PeerId & ) const override
        {
            return nullptr;
        }

        void addConnectionToPeer(
            const libp2p::peer::PeerId &,
            ConnectionSPtr ) override
        {
        }

        void closeConnectionsToPeer(
            const libp2p::peer::PeerId & ) override
        {
        }

        void onConnectionClosed(
            const libp2p::peer::PeerId &,
            const std::shared_ptr<libp2p::connection::CapableConnection> & ) override
        {
        }

        void removeRelayedConnections(
            const libp2p::peer::PeerId & ) override
        {
        }

        void collectGarbage() override
        {
        }

        void purgeIdleConnections() override
        {
        }

        void tagPeer(
            const libp2p::peer::PeerId &,
            const std::string &,
            int ) override
        {
        }

        void untagPeer(
            const libp2p::peer::PeerId &,
            const std::string & ) override
        {
        }

        void protectPeer(
            const libp2p::peer::PeerId &,
            const std::string & ) override
        {
        }

        bool unprotectPeer(
            const libp2p::peer::PeerId &,
            const std::string & ) override
        {
            return false;
        }

        void forceTrim() override
        {
        }

        Config &getConfig() override
        {
            return config_;
        }

        const Config &getConfig() const override
        {
            return config_;
        }

    private:
        Config config_{};
    };

    class StubNetwork : public libp2p::network::Network
    {
    public:
        void closeConnections( const libp2p::peer::PeerId & ) override
        {
        }

        libp2p::network::Dialer &getDialer() override
        {
            return dialer_;
        }

        libp2p::network::ListenerManager &getListener() override
        {
            return listener_;
        }

        libp2p::network::ConnectionManager &getConnectionManager() override
        {
            return connMgr_;
        }

    private:
        StubDialer           dialer_;
        StubListenerManager  listener_;
        StubConnectionManager connMgr_;
    };

    class StubAddressRepository : public libp2p::peer::AddressRepository
    {
    public:
        using Milliseconds = std::chrono::milliseconds;

        void bootstrap( const libp2p::multi::Multiaddress &,
                        std::function<BootstrapCallback> ) override
        {
        }

        libp2p::outcome::result<bool> addAddresses(
            const libp2p::peer::PeerId &,
            gsl::span<const libp2p::multi::Multiaddress>,
            Milliseconds ) override
        {
            return true;
        }

        libp2p::outcome::result<bool> upsertAddresses(
            const libp2p::peer::PeerId &,
            gsl::span<const libp2p::multi::Multiaddress>,
            Milliseconds ) override
        {
            return true;
        }

        libp2p::outcome::result<void> updateAddresses(
            const libp2p::peer::PeerId &,
            Milliseconds ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<std::vector<libp2p::multi::Multiaddress>>
        getAddresses(
            const libp2p::peer::PeerId & ) const override
        {
            return std::vector<libp2p::multi::Multiaddress>{};
        }

        void clear( const libp2p::peer::PeerId & ) override
        {
        }

        std::unordered_set<libp2p::peer::PeerId> getPeers() const override
        {
            return {};
        }

        void collectGarbage() override
        {
        }
    };

    class StubKeyRepository : public libp2p::peer::KeyRepository
    {
    public:
        libp2p::outcome::result<void> addPublicKey(
            const libp2p::peer::PeerId &,
            const libp2p::crypto::PublicKey & ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<PubVecPtr>
        getPublicKeys( const libp2p::peer::PeerId & ) override
        {
            return PubVecPtr{};
        }

        libp2p::outcome::result<KeyPairVecPtr> getKeyPairs() override
        {
            return KeyPairVecPtr{};
        }

        libp2p::outcome::result<void> addKeyPair(
            const KeyPair & ) override
        {
            return libp2p::outcome::success();
        }

        void clear( const libp2p::peer::PeerId & ) override
        {
        }

        std::unordered_set<libp2p::peer::PeerId> getPeers() const override
        {
            return {};
        }
    };

    class StubProtocolRepository
        : public libp2p::peer::ProtocolRepository
    {
    public:
        libp2p::outcome::result<void> addProtocols(
            const libp2p::peer::PeerId &,
            gsl::span<const libp2p::peer::Protocol> ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> removeProtocols(
            const libp2p::peer::PeerId &,
            gsl::span<const libp2p::peer::Protocol> ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<std::vector<libp2p::peer::Protocol>>
        getProtocols( const libp2p::peer::PeerId & ) const override
        {
            return std::vector<libp2p::peer::Protocol>{};
        }

        libp2p::outcome::result<std::vector<libp2p::peer::Protocol>>
        supportsProtocols( const libp2p::peer::PeerId &,
                           const std::set<libp2p::peer::Protocol> & ) const override
        {
            return std::vector<libp2p::peer::Protocol>{};
        }

        void clear( const libp2p::peer::PeerId & ) override
        {
        }

        std::unordered_set<libp2p::peer::PeerId> getPeers() const override
        {
            return {};
        }

        void collectGarbage() override
        {
        }
    };

    class StubPeerRepository : public libp2p::peer::PeerRepository
    {
    public:
        libp2p::peer::AddressRepository &getAddressRepository() override
        {
            return addrRepo_;
        }

        libp2p::peer::KeyRepository &getKeyRepository() override
        {
            return keyRepo_;
        }

        libp2p::peer::ProtocolRepository &getProtocolRepository() override
        {
            return protoRepo_;
        }

        std::unordered_set<libp2p::peer::PeerId> getPeers() const override
        {
            return {};
        }

        libp2p::peer::PeerInfo getPeerInfo(
            const libp2p::peer::PeerId &peer_id ) const override
        {
            return libp2p::peer::PeerInfo{ peer_id, {} };
        }

    private:
        StubAddressRepository  addrRepo_;
        StubKeyRepository      keyRepo_;
        StubProtocolRepository protoRepo_;
    };

    class StubHost : public libp2p::Host
    {
    public:
        StubHost()
            : bus_()
        {
        }

        std::string_view getLibp2pVersion() const override
        {
            return "stub/0.1.0";
        }

        std::string_view getLibp2pClientVersion() const override
        {
            return "stub-client/0.1.0";
        }

        libp2p::peer::PeerId getId() const override
        {
            return libp2p::peer::PeerId::fromHash(
                libp2p::multi::Multihash::create( libp2p::multi::sha256,
                                                  libp2p::common::ByteArray( 32, 0 ) )
                    .value() )
                .value();
        }

        libp2p::peer::PeerInfo getPeerInfo() const override
        {
            return { getId(), {} };
        }

        std::vector<libp2p::multi::Multiaddress>
        getAddresses() const override
        {
            return {};
        }

        std::vector<libp2p::multi::Multiaddress>
        getAddressesInterfaces() const override
        {
            return {};
        }

        std::vector<libp2p::multi::Multiaddress>
        getObservedAddresses() const override
        {
            return {};
        }

        std::vector<libp2p::multi::Multiaddress>
        getRelayAddresses() const override
        {
            return {};
        }

        std::vector<libp2p::multi::Multiaddress>
        getObservedAddressesReal( bool /*checkconfirmed*/ = true ) const override
        {
            return {};
        }

        libp2p::event::Handle setOnNewConnectionHandler(
            const libp2p::Host::NewConnectionHandler & ) const override
        {
            return libp2p::event::Handle();
        }

        libp2p::Host::Connectedness connectedness(
            const libp2p::peer::PeerInfo & ) const override
        {
            return libp2p::Host::Connectedness::CAN_NOT_CONNECT;
        }

        void setProtocolHandler(
            libp2p::StreamProtocols,
            libp2p::StreamAndProtocolCb,
            libp2p::ProtocolPredicate = {} ) override
        {
        }

        void connect( const libp2p::peer::PeerInfo &,
                      const libp2p::Host::ConnectionResultHandler &handler,
                      std::chrono::milliseconds,
                      bool = false,
                      bool = false ) override
        {
            handler( libp2p::outcome::result<
                     std::shared_ptr<libp2p::connection::CapableConnection>>(
                libp2p::outcome::success() ) );
        }

        void disconnect( const libp2p::peer::PeerId & ) override
        {
        }

        void newStream( const libp2p::peer::PeerInfo &,
                        libp2p::StreamProtocols,
                        libp2p::StreamAndProtocolOrErrorCb cb,
                        std::chrono::milliseconds = {} ) override
        {
            cb( libp2p::StreamAndProtocolOrError(
                libp2p::StreamAndProtocol{} ) );
        }

        void newStream( const libp2p::peer::PeerId &,
                        libp2p::StreamProtocols,
                        libp2p::StreamAndProtocolOrErrorCb cb ) override
        {
            cb( libp2p::StreamAndProtocolOrError(
                libp2p::StreamAndProtocol{} ) );
        }

        libp2p::outcome::result<void> listen(
            const libp2p::multi::Multiaddress & ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> closeListener(
            const libp2p::multi::Multiaddress & ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> removeListener(
            const libp2p::multi::Multiaddress & ) override
        {
            return libp2p::outcome::success();
        }

        void start() override
        {
        }

        void stop() override
        {
        }

        libp2p::network::Network &getNetwork() override
        {
            return stubNetwork_;
        }

        libp2p::peer::PeerRepository &getPeerRepository() override
        {
            return stubPeerRepo_;
        }

        libp2p::protocol::RelayAddresses &getRelayRepository() override
        {
            return relayAddrs_;
        }

        libp2p::protocol::ObservedAddresses &getObservedRepository() override
        {
            return obsAddrs_;
        }

        libp2p::network::Router &getRouter() override
        {
            return stubRouter_;
        }

        libp2p::event::Bus &getBus() override
        {
            return bus_;
        }

        libp2p::network::ConnectionManager::Config &
        getConnectionManagerConfig() override
        {
            return connMgrConfig_;
        }

        const libp2p::network::ConnectionManager::Config &
        getConnectionManagerConfig() const override
        {
            return connMgrConfig_;
        }

        StubRouter stubRouter_;

    private:
        StubNetwork                             stubNetwork_;
        StubPeerRepository                      stubPeerRepo_;
        libp2p::protocol::RelayAddresses        relayAddrs_;
        libp2p::protocol::ObservedAddresses     obsAddrs_;
        libp2p::event::Bus                      bus_;
        libp2p::network::ConnectionManager::Config connMgrConfig_{};
    };

    class BitswapTestBase : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ioContext_ = std::make_shared<boost::asio::io_context>();
            stubHost_  = std::make_unique<StubHost>();
            bitswap_ = std::make_shared<Bitswap>(
                *stubHost_, stubHost_->getBus(), ioContext_ );
            bitswap_->initialize();

            ioWork_ = std::make_unique<boost::asio::io_context::work>(
                *ioContext_ );
            ioThread_ = std::thread(
                [this]()
                {
                    ioContext_->run();
                } );
        }

        void TearDown() override
        {
            ioWork_.reset();
            ioContext_->stop();
            if ( ioThread_.joinable() )
            {
                ioThread_.join();
            }
            bitswap_.reset();
            stubHost_.reset();
            ioContext_.reset();
        }

        std::shared_ptr<boost::asio::io_context> ioContext_;
        std::unique_ptr<StubHost>                stubHost_;
        std::shared_ptr<Bitswap>                 bitswap_;

        /// @brief Drain all pending io_context work.
        /// Resets the work guard so io_context::run() returns naturally after
        /// the handler queue is empty, then joins the io thread.
        /// Must be called before removing resources that pending callbacks depend on.
        void drainIoContext()
        {
            ioWork_.reset();
            if ( ioThread_.joinable() )
            {
                ioThread_.join();
            }
        }

    private:
        std::unique_ptr<boost::asio::io_context::work> ioWork_;
        std::thread                                     ioThread_;
    };

}  // namespace sgns::ipfs_bitswap

#endif  // SUPERGENIUS_BITSWAP_STUBS_HPP
