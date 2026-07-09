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
#include <libp2p/peer/peer_repository.hpp>
#include <libp2p/peer/address_repository.hpp>
#include <libp2p/peer/key_repository.hpp>
#include <libp2p/peer/protocol_repository.hpp>
#include <libp2p/event/bus.hpp>
#include <libp2p/protocol/relay/relay_addresses.hpp>
#include <libp2p/protocol/identify/observed_addresses.hpp>

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
        void dial( const libp2p::multi::Multiaddress &,
                   libp2p::peer::ProtocolPredicate,
                   libp2p::network::Dialer::DialResultFunc ) override
        {
        }

        void dial( libp2p::multi::Multiaddress &&,
                   libp2p::peer::ProtocolPredicate,
                   libp2p::network::Dialer::DialResultFunc ) override
        {
        }

        void dial( const libp2p::peer::PeerInfo &,
                   libp2p::peer::ProtocolPredicate,
                   libp2p::network::Dialer::DialResultFunc,
                   std::chrono::milliseconds ) override
        {
        }

        void dial( libp2p::peer::PeerInfo &&,
                   libp2p::peer::ProtocolPredicate,
                   libp2p::network::Dialer::DialResultFunc,
                   std::chrono::milliseconds ) override
        {
        }

        void newStream( const libp2p::peer::PeerInfo &,
                        libp2p::StreamProtocols,
                        libp2p::StreamAndProtocolOrErrorCb cb,
                        std::chrono::milliseconds ) override
        {
            cb( libp2p::outcome::result<std::shared_ptr<libp2p::connection::Stream>>(
                libp2p::outcome::success() ) );
        }

        void newStream( const libp2p::peer::PeerId &,
                        libp2p::StreamProtocols,
                        libp2p::StreamAndProtocolOrErrorCb cb ) override
        {
            cb( libp2p::outcome::result<std::shared_ptr<libp2p::connection::Stream>>(
                libp2p::outcome::success() ) );
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

        libp2p::event::Handle setProtocolHandler(
            libp2p::StreamProtocols,
            libp2p::StreamAndProtocolCb,
            libp2p::ProtocolPredicate = {} ) override
        {
            return libp2p::event::Handle();
        }

        void removeProtocolHandlers(
            const libp2p::peer::Protocol & ) override
        {
        }

        void removeAll() override
        {
        }
    };

    class StubConnectionManager : public libp2p::network::ConnectionManager
    {
    public:
        std::vector<libp2p::peer::PeerId>
        getConnectedness() const override
        {
            return {};
        }

        void addConnectionToPeer(
            const libp2p::peer::PeerId &,
            std::shared_ptr<libp2p::connection::CapableConnection> ) override
        {
        }

        void closeConnectionsToPeer(
            const libp2p::peer::PeerId & ) override
        {
        }

        void collectGarbage() override
        {
        }

        const std::string &getBestConnection(
            const libp2p::peer::PeerId & ) const override
        {
            static const std::string empty;
            return empty;
        }

        libp2p::network::ConnectionManager::Config &getConfig() override
        {
            return config_;
        }

        const libp2p::network::ConnectionManager::Config &getConfig()
            const override
        {
            return config_;
        }

    private:
        libp2p::network::ConnectionManager::Config config_{};
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
        libp2p::outcome::result<void> addAddresses(
            const libp2p::peer::PeerId &,
            gsl::span<const libp2p::multi::Multiaddress>,
            libp2p::peer::ttl::ttl_type ) override
        {
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> upsertAddresses(
            const libp2p::peer::PeerId &,
            gsl::span<const libp2p::multi::Multiaddress>,
            libp2p::peer::ttl::ttl_type ) override
        {
            return libp2p::outcome::success();
        }

        void updateAddresses(
            const libp2p::peer::PeerId &,
            libp2p::peer::ttl::ttl_type ) override
        {
        }

        void removeAddresses(
            const libp2p::peer::PeerId &,
            gsl::span<const libp2p::multi::Multiaddress> ) override
        {
        }

        std::vector<libp2p::multi::Multiaddress> getAddresses(
            const libp2p::peer::PeerId & ) const override
        {
            return {};
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
        void addPublicKey( const libp2p::peer::PeerId &,
                           const libp2p::crypto::PublicKey & ) override
        {
        }

        libp2p::outcome::result<PubVecPtr>
        getPublicKeys( const libp2p::peer::PeerId & ) const override
        {
            return PubVecPtr{};
        }

        void clear( const libp2p::peer::PeerId & ) override
        {
        }

        std::unordered_set<libp2p::peer::PeerId> getPeers() const override
        {
            return {};
        }

        libp2p::outcome::result<KeyPairVecPtr> getAllKeyPairs() const
        {
            return KeyPairVecPtr{};
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

        std::vector<libp2p::peer::Protocol>
        getProtocols( const libp2p::peer::PeerId & ) const override
        {
            return {};
        }

        std::set<libp2p::peer::Protocol>
        supportsProtocols( const libp2p::peer::PeerId &,
                           gsl::span<const libp2p::peer::Protocol> ) const override
        {
            return {};
        }

        void removeProtocols( const libp2p::peer::PeerId &,
                              gsl::span<const libp2p::peer::Protocol> ) override
        {
        }

        void collectGarbage() override
        {
        }

        const std::set<libp2p::peer::PeerId> &getPeers() const override
        {
            static const std::set<libp2p::peer::PeerId> empty;
            return empty;
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
                    .value() );
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
            cb( libp2p::outcome::result<
                std::shared_ptr<libp2p::connection::Stream>>(
                libp2p::outcome::success() ) );
        }

        void newStream( const libp2p::peer::PeerId &,
                        libp2p::StreamProtocols,
                        libp2p::StreamAndProtocolOrErrorCb cb ) override
        {
            cb( libp2p::outcome::result<
                std::shared_ptr<libp2p::connection::Stream>>(
                libp2p::outcome::success() ) );
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

    private:
        std::unique_ptr<boost::asio::io_context::work> ioWork_;
        std::thread                                     ioThread_;
    };

}  // namespace sgns::ipfs_bitswap

#endif  // SUPERGENIUS_BITSWAP_STUBS_HPP
