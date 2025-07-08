
#ifndef SUPERGENIUS_DAGSYNCER_HPP
#define SUPERGENIUS_DAGSYNCER_HPP

#include <primitives/cid/cid.hpp>
#include <ipfs_lite/ipfs/merkledag/merkledag_service.hpp>
#include "outcome/outcome.hpp"
#include <set>

namespace sgns::crdt
{
    /**
     * @brief A DAGSyncer is an abstraction to an IPLD-based p2p storage layer.
     * A DAGSyncer is a DAGService with the ability to publish new ipld nodes
     * to the network, and retrieving others from it.
     */
    class DAGSyncer : public ipfs_lite::ipfs::merkledag::MerkleDagService
    {
    public:
        using LinkInfoPair = std::pair<CID, std::string>;
        using LinkInfoSet  = std::set<LinkInfoPair>;
        /**
        * Check if the block with {@param cid} is locally available (therefore, it
        * is considered processed).
        * @param cid Content identifier of the node
        * @return true if the block is locally available or outcome::failure on error 
        */
        virtual outcome::result<bool> HasBlock( const CID &cid ) const = 0;

        virtual outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GetNodeWithoutRequest(
            const CID &cid ) const = 0;
        virtual std::pair<LinkInfoSet, LinkInfoSet> TraverseCIDsLinks(
            const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &node,
            std::string                                       link_name,
            LinkInfoSet                                       visited_links,
            bool                                              skip_if_visited_root,
            int                                               max_depth ) const = 0;

        virtual void                  InitCIDBlock( const CID &cid )       = 0;
        virtual bool                  IsCIDInCache( const CID &cid ) const = 0;
        virtual outcome::result<void> DeleteCIDBlock( const CID &cid )     = 0;
        virtual void                  Stop()                               = 0;
    };
} // namespace sgns::crdt

#endif // SUPERGENIUS_DAGSYNCER_HPP
