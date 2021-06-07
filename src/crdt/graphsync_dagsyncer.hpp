#ifndef SUPERGENIUS_GRAPHSYNC_DAGSYNCER_HPP
#define SUPERGENIUS_GRAPHSYNC_DAGSYNCER_HPP

#include "crdt/dagsyncer.hpp"
#include <base/logger.hpp>
#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include <ipfs_lite/ipfs/graphsync/extension.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/merkledag_bridge_impl.hpp>
#include <libp2p/host/host.hpp>
#include <base/buffer.hpp>
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
    using RequestProgressCallback = Graphsync::RequestProgressCallback;
    using BlockCallback = Graphsync::BlockCallback;

    GraphsyncDAGSyncer(const std::shared_ptr<IpfsDatastore>& service, 
      const std::shared_ptr<Graphsync>& graphsync, 
      const std::shared_ptr<libp2p::Host>& host);

    outcome::result<void> Listen(const Multiaddress& listen_to);

    outcome::result<void> RequestNode(const PeerId& peer,
      boost::optional<Multiaddress> address,
      const CID& root_cid);

    virtual outcome::result<bool> HasBlock(const CID& cid) const override;

    /* Returns peer ID */
    outcome::result<PeerId> GetId() const;

  protected:
    RequestProgressCallback RequestNodeCallback();

    static void BlockReceivedCallback(CID cid, sgns::common::Buffer buffer, GraphsyncDAGSyncer* dagSyncer);

    bool started_ = false;

    /** Starts instance and subscribes to blocks */
    outcome::result<bool> StartSync();

    /** Stops instance */
    void StopSync();

    std::shared_ptr<Graphsync> graphsync_ = nullptr;

    std::shared_ptr<libp2p::Host> host_ = nullptr;

    Logger logger_;

    // keeping subscriptions alive, otherwise they cancel themselves
    // class Subscription have non-copyable constructor and operator, so it can not be used in std::vector
    // std::vector<Subscription> requests_;
    std::vector<std::shared_ptr<Subscription>> requests_;

  };
}
#endif
