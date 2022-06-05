/**
* Header file for IPFS DHT implementation
* @author creativeid00
*/
#ifndef SUPERGENIUS_IPFS_DHT_HPP
#define SUPERGENIUS_IPFS_DHT_HPP

#include <base/logger.hpp>

#include <libp2p/protocol/kademlia/kademlia.hpp>
#include <libp2p/multi/content_identifier.hpp>

namespace sgns::ipfs
{
/** IPFS DHT implementation
* IPFS DHT implementation based on libp2p kademlia
*/
class IpfsDHT
{
public:
    /** Constructs IPFS DHT object
    * @param kademlia - libp2p kademlia instance
    * @param bootstrapAddresses - a list of multi-adressed of IPFS bootstrap nodes
    */
    IpfsDHT(
        std::shared_ptr<libp2p::protocol::kademlia::Kademlia> kademlia,
        std::vector<std::string> bootstrapAddresses);

    /** Starts bootstraping
    */
    void Start();

    /** Asynchronous providers search
    * @param cid - block cid that providers need to be found for
    * @param onProvidersFound - asynchronous callback which is called once block providers found
    * @note Each call of the method can return different set of block providers, including an empty set.
    * Once an empty set received it doesn't mean that providers do not exist and the next cal can return a
    * non-empty set of providers.
    */
    void FindProviders(
        const libp2p::multi::ContentIdentifier& cid,
        std::function<void(libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> onProvidersFound)> onProvidersFound);

    /** Gets peers info
    * @param peerId - peer identifier
    * @param onPeerFound - callback which is called once peer info found
    */
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
