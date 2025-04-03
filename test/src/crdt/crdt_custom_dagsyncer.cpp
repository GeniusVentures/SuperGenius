#include "crdt_custom_dagsyncer.hpp"

namespace sgns::crdt
{
    CustomDagSyncer::CustomDagSyncer(std::shared_ptr<IpfsDatastore> service)
        : dagService_(service)
    {
    }

    outcome::result<bool> CustomDagSyncer::HasBlock(const CID& cid) const
    {
        auto getNodeResult = dagService_.getNode(cid);
        return getNodeResult.has_value();
    }

    outcome::result<void> CustomDagSyncer::addNode(std::shared_ptr<const IPLDNode> node)
    {
        return dagService_.addNode(node);
    }

    outcome::result<std::shared_ptr<IPLDNode>> CustomDagSyncer::getNode(const CID& cid) const
    {
        return dagService_.getNode(cid);
    }

    outcome::result<void> CustomDagSyncer::removeNode(const CID& cid)
    {
        return dagService_.removeNode(cid);
    }

    outcome::result<size_t> CustomDagSyncer::select(
        gsl::span<const uint8_t> root_cid,
        gsl::span<const uint8_t> selector,
        std::function<bool(std::shared_ptr<const IPLDNode> node)> handler) const
    {
        return dagService_.select(root_cid, selector, handler);
    }

    outcome::result<std::shared_ptr<CustomDagSyncer::Leaf>> CustomDagSyncer::fetchGraph(const CID& cid) const
    {
        return dagService_.fetchGraph(cid);
    }

    outcome::result<std::shared_ptr<CustomDagSyncer::Leaf>> CustomDagSyncer::fetchGraphOnDepth(
        const CID& cid, uint64_t depth) const
    {
        return dagService_.fetchGraphOnDepth(cid, depth);
    }

} // namespace sgns::crdt