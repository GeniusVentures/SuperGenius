#ifndef SUPERGENIUS_IPFS_DHT_HPP
#define SUPERGENIUS_IPFS_DHT_HPP

#include <base/logger.hpp>

#include <libp2p/protocol/kademlia/kademlia.hpp>
#include <libp2p/multi/content_identifier.hpp>

namespace sgns
{
class IpfsDHT
{
public:
    IpfsDHT(
        std::shared_ptr<libp2p::protocol::kademlia::Kademlia> kademlia,
        std::vector<std::string> bootstrapAddresses);

    void Start();

    void FindProviders(
        const libp2p::multi::ContentIdentifier& cid,
        std::function<void(libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> onProvidersFound)> onProvidersFound);

    void FindPeer(
        const libp2p::peer::PeerId& peerId,
        std::function<void(libp2p::outcome::result<libp2p::peer::PeerInfo>)> onPeerFound);
private:
    std::vector<libp2p::peer::PeerInfo> GetBootstrapNodes() const;

    std::shared_ptr<libp2p::protocol::kademlia::Kademlia> kademlia_;
    std::vector<std::string> bootstrapAddresses_;

    base::Logger logger_ = base::createLogger("IpfsDHT");
};

}

#endif // SUPERGENIUS_IPFS_DHT_HPP
