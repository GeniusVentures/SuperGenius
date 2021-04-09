
#ifndef SUPERGENIUS_DAGSYNCER_HPP
#define SUPERGENIUS_DAGSYNCER_HPP

#include <boost/outcome.hpp>
#include <primitives/cid/cid.hpp>
#include <ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.hpp>

namespace sgns::crdt
{
  /**
   * @brief A DAGSyncer is an abstraction to an IPLD-based p2p storage layer.
   * A DAGSyncer is a DAGService with the ability to publish new ipld nodes
   * to the network, and retrieving others from it.
   */
  class DAGSyncer : public ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl
  {
  public:
    using IpfsDatastore = ipfs_lite::ipfs::IpfsDatastore;

    DAGSyncer(std::shared_ptr<IpfsDatastore> service) : MerkleDagServiceImpl(service) {};
    
    virtual ~DAGSyncer() = default;

    /**
    * Check if the block with {@param cid} is locally available (therefore, it
     * is considered processed).
    * @param cid Content identifier of the node
    * @return true if the block is locally available or outcome::failure on error 
    */
    virtual outcome::result<bool> HasBlock(const CID& cid) const = 0;
  };
}  // namespace sgns::crdt 

#endif  // SUPERGENIUS_DAGSYNCER_HPP
