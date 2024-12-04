#ifndef SUPERGENIUS_GRAPHSYNC_DAGSYNCER_HPP
#define SUPERGENIUS_GRAPHSYNC_DAGSYNCER_HPP

#include "crdt/dagsyncer.hpp"
#include "base/logger.hpp"
#include "base/buffer.hpp"

#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include <ipfs_lite/ipfs/graphsync/extension.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/merkledag_bridge_impl.hpp>
#include <ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.hpp>

#include <libp2p/host/host.hpp>

#include <memory>

namespace sgns::crdt
{
  /**
   * @brief A DAGSyncer is an abstraction to an IPLD-based p2p storage layer.
   * A DAGSyncer is a DAGService with the ability to publish new ipld nodes
   * to the network, and retrieving others from it.
   */
  class GraphsyncDAGSyncer : public DAGSyncer, 
    public std::enable_shared_from_this<GraphsyncDAGSyncer>
  {
  public:
    using IpfsDatastore = ipfs_lite::ipfs::IpfsDatastore;
    using Graphsync = ipfs_lite::ipfs::graphsync::Graphsync;
    using ResponseMetadata = ipfs_lite::ipfs::graphsync::ResponseMetadata;
    using Extension = ipfs_lite::ipfs::graphsync::Extension;
    using MerkleDagBridgeImpl = ipfs_lite::ipfs::graphsync::MerkleDagBridgeImpl;
    using ResponseStatusCode = ipfs_lite::ipfs::graphsync::ResponseStatusCode;
    using Multiaddress = libp2p::multi::Multiaddress;
    using PeerId = libp2p::peer::PeerId;
    using Subscription = libp2p::protocol::Subscription;
    using Logger = base::Logger;
    using BlockCallback = Graphsync::BlockCallback;

    GraphsyncDAGSyncer( std::shared_ptr<IpfsDatastore> service,
                        std::shared_ptr<Graphsync>     graphsync,
                        std::shared_ptr<libp2p::Host>  host );

    outcome::result<void> Listen(const Multiaddress& listen_to);

    void AddRoute(const CID& cid, const PeerId& peer, std::vector<Multiaddress>& address);

    // DAGService interface implementation
    outcome::result<void> addNode(std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node) override;

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> getNode(const CID &cid) const override;

    outcome::result<void> removeNode(const CID &cid) override;

    outcome::result<size_t> select(
        gsl::span<const uint8_t> root_cid,
        gsl::span<const uint8_t> selector,
        std::function<bool(std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node)> handler) const override;

    outcome::result<std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf>> fetchGraph(const CID &cid) const override;

    outcome::result<std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf>> fetchGraphOnDepth(
        const CID &cid, uint64_t depth) const override;

    outcome::result<bool> HasBlock(const CID& cid) const override;

    /* Returns peer ID */
    outcome::result<PeerId> GetId() const;

    outcome::result<libp2p::peer::PeerInfo> GetPeerInfo() const;

  protected:
    outcome::result<std::future<std::shared_ptr<ipfs_lite::ipld::IPLDNode>>> RequestNode(
        const PeerId& peer, boost::optional<std::vector<Multiaddress>> address, const CID& root_cid) const;

    void RequestProgressCallback(ResponseStatusCode code, const std::vector<Extension>& extensions) const;
    void BlockReceivedCallback( const CID &cid, sgns::common::Buffer buffer );

    bool started_ = false;

    /** Starts instance and subscribes to blocks */
    outcome::result<void> StartSync();

    /** Stops instance */
    void StopSync();

    ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl dagService_;
    std::shared_ptr<Graphsync> graphsync_;

    std::shared_ptr<libp2p::Host> host_;

    Logger logger_ = base::createLogger("GraphsyncDAGSyncer");

    // keeping subscriptions alive, otherwise they cancel themselves
    // class Subscription have non-copyable constructor and operator, so it can not be used in std::vector
    // std::vector<Subscription> requests_;
    mutable std::map<CID, std::tuple<std::shared_ptr<Subscription>, 
        std::shared_ptr<std::promise<std::shared_ptr<ipfs_lite::ipld::IPLDNode>>>>> requests_;
    std::map<CID, std::tuple<PeerId, std::vector<Multiaddress>>> routing_;

  };
}
#endif
