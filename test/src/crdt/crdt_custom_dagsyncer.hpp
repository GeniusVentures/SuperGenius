#ifndef SUPERGENIUS_CRDT_CUSTOM_DAGSYNCER_HPP
#define SUPERGENIUS_CRDT_CUSTOM_DAGSYNCER_HPP

#include "crdt/crdt_datastore.hpp"
#include "crdt/dagsyncer.hpp"
#include <ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.hpp>
#include <ipfs_lite/ipfs/impl/in_memory_datastore.hpp>
#include "ipfs_lite/ipld/ipld_node.hpp"
#include <memory>

namespace sgns::crdt
{
    using ipfs_lite::ipld::IPLDNode;

    /**
     * @brief Test implementation of DAGSyncer for unit testing
     */
    class CustomDagSyncer : public DAGSyncer
    {
    public:
        using IpfsDatastore        = ipfs_lite::ipfs::IpfsDatastore;
        using MerkleDagServiceImpl = ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl;
        using Leaf                 = ipfs_lite::ipfs::merkledag::Leaf;

        /**
         * @brief Constructor
         * @param service shared pointer to IPFS datastore
         */
        explicit CustomDagSyncer( std::shared_ptr<IpfsDatastore> service );

        /**
         * @brief Check if block exists
         * @param cid content identifier to check
         * @return true if block exists, false otherwise
         */
        outcome::result<bool> HasBlock( const CID &cid ) const override;

        /**
         * @brief Add node to DAG
         * @param node node to add
         * @return success or failure
         */
        outcome::result<void> addNode( std::shared_ptr<const IPLDNode> node ) override;

        /**
         * @brief Get node from DAG
         * @param cid content identifier of node to get
         * @return node or failure
         */
        outcome::result<std::shared_ptr<IPLDNode>> getNode( const CID &cid ) const override;

        /**
         * @brief Remove node from DAG
         * @param cid content identifier of node to remove
         * @return success or failure
         */
        outcome::result<void> removeNode( const CID &cid ) override;

        /**
         * @brief Select nodes from DAG
         * @param root_cid root content identifier
         * @param selector selector bytes
         * @param handler handler function
         * @return number of nodes selected or failure
         */
        outcome::result<size_t> select(
            gsl::span<const uint8_t>                                    root_cid,
            gsl::span<const uint8_t>                                    selector,
            std::function<bool( std::shared_ptr<const IPLDNode> node )> handler ) const override;

        /**
         * @brief Fetch graph from DAG
         * @param cid content identifier of root node
         * @return leaf node or failure
         */
        outcome::result<std::shared_ptr<Leaf>> fetchGraph( const CID &cid ) const override;

        /**
         * @brief Fetch graph with depth limit from DAG
         * @param cid content identifier of root node
         * @param depth maximum depth to fetch
         * @return leaf node or failure
         */
        outcome::result<std::shared_ptr<Leaf>> fetchGraphOnDepth( const CID &cid, uint64_t depth ) const override;

        void Stop() override;

        void                  InitCIDBlock( const CID &cid ) override;
        bool                  IsCIDInCache( const CID &cid ) const override;
        outcome::result<void> DeleteCIDBlock( const CID &cid ) override;

        outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GetNodeWithoutRequest(
            const CID &cid ) const override;
        std::pair<DAGSyncer::LinkInfoSet, DAGSyncer::LinkInfoSet> TraverseCIDsLinks(
            const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &node,
            std::string                                       link_name     = "",
            DAGSyncer::LinkInfoSet                            visited_links = {},
            bool                                              skip_if_visited_root  = true) const override;
        /** DAG service implementation */
        MerkleDagServiceImpl dagService_;

        std::set<CID> cids_cache;
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_CUSTOM_DAGSYNCER_HPP
