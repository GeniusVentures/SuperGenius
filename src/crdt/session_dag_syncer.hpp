
#ifndef SUPERGENIUS_SESSIONDAGSYNCER_HPP
#define SUPERGENIUS_SESSIONDAGSYNCER_HPP

#include <crdt/dagsyncer.hpp>

namespace sgns::crdt
{
  /**
   * @brief A SessionDAGSyncer is an abstraction to an IPLD-based p2p storage layer.
   * A SessionDAGSyncer is a DAGService with the ability to publish new ipld nodes
   * to the network, and retrieving others from it.
   */
  class SessionDAGSyncer : public DAGSyncer
  {
  public:
    virtual ~SessionDAGSyncer() = default;

    /**
       * Check if the block with {@param cid} is locally available (therefore, it
       * is considered processed).
       * @param cid Content identifier of the node
       * @return true if the block is locally available
       */
    outcome::result<bool> HasBlock(CID cid) const = 0;
  };
}  // namespace sgns::crdt

#endif  // SUPERGENIUS_SESSIONDAGSYNCER_HPP
