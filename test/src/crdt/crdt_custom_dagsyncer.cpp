#include "crdt_custom_dagsyncer.hpp"

namespace sgns::crdt
{
    CustomDagSyncer::CustomDagSyncer( std::shared_ptr<IpfsDatastore> service ) : dagService_( service ) {}

    outcome::result<bool> CustomDagSyncer::HasBlock( const CID &cid ) const
    {
        if ( IsCIDInCache( cid ) )
        {
            auto getNodeResult = dagService_.getNode( cid );
            return getNodeResult.has_value();
        }
        return false;
    }

    outcome::result<void> CustomDagSyncer::addNode( std::shared_ptr<const IPLDNode> node )
    {
        return dagService_.addNode( node );
    }

    outcome::result<std::shared_ptr<IPLDNode>> CustomDagSyncer::getNode( const CID &cid ) const
    {
        return dagService_.getNode( cid );
    }

    outcome::result<void> CustomDagSyncer::removeNode( const CID &cid )
    {
        return dagService_.removeNode( cid );
    }

    outcome::result<size_t> CustomDagSyncer::select(
        gsl::span<const uint8_t>                                    root_cid,
        gsl::span<const uint8_t>                                    selector,
        std::function<bool( std::shared_ptr<const IPLDNode> node )> handler ) const
    {
        return dagService_.select( root_cid, selector, handler );
    }

    outcome::result<std::shared_ptr<CustomDagSyncer::Leaf>> CustomDagSyncer::fetchGraph( const CID &cid ) const
    {
        return dagService_.fetchGraph( cid );
    }

    outcome::result<std::shared_ptr<CustomDagSyncer::Leaf>> CustomDagSyncer::fetchGraphOnDepth( const CID &cid,
                                                                                                uint64_t   depth ) const
    {
        return dagService_.fetchGraphOnDepth( cid, depth );
    }

    void CustomDagSyncer::InitCIDBlock( const CID &cid )
    {
        cids_cache.emplace( cid );
    }

    bool CustomDagSyncer::IsCIDInCache( const CID &cid ) const
    {
        return cids_cache.find( cid ) != cids_cache.end();
    }

    outcome::result<void> CustomDagSyncer::DeleteCIDBlock( const CID &cid )
    {
        return outcome::success();
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> CustomDagSyncer::GetNodeWithoutRequest(
        const CID &cid ) const
    {
        return getNode( cid );
    }

    std::pair<std::set<CID>, std::set<CID>> CustomDagSyncer::TraverseCIDsLinks(
        const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &node,
        std::set<CID>                                     visited_cids ) const
    {
        std::set<CID> visited = std::move( visited_cids );
        std::set<CID> links_to_fetch;

        for ( const auto &link : node->getLinks() )
        {
            auto child = link.get().getCID();

            if ( !visited.insert( child ).second )
            {
                continue; // already visited
            }

            auto get_child_result = GetNodeWithoutRequest( child );

            if ( get_child_result.has_failure() )
            {
                links_to_fetch.insert( child );
                continue;
            }

            auto cid_pair = TraverseCIDsLinks( get_child_result.value(), visited );
            links_to_fetch.merge( cid_pair.first );
            visited.merge( cid_pair.second );
        }

        return std::make_pair( std::move( links_to_fetch ), std::move( visited ) );
    }

    void CustomDagSyncer::Stop() {}
} // namespace sgns::crdt
