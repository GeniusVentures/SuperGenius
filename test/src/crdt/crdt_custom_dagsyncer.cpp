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
        return outcome::failure( boost::system::error_code{} );
    }

    std::pair<DAGSyncer::LinkInfoSet, DAGSyncer::LinkInfoSet> CustomDagSyncer::TraverseCIDsLinks(
        const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &node,
        std::string                                       link_name,
        DAGSyncer::LinkInfoSet                            visited_links,
        bool                                              skip_if_visited_root,
        int                                               max_depth ) const
    {
        DAGSyncer::LinkInfoSet links_to_fetch;
        DAGSyncer::LinkInfoSet visited = std::move( visited_links );

        const CID &root_cid = node->getCID();

        if ( skip_if_visited_root )
        {
            bool already_seen_root = std::any_of( visited.begin(),
                                                  visited.end(),
                                                  [&]( const LinkInfoPair &p ) { return p.first == root_cid; } );

            if ( already_seen_root )
            {
                return { std::move( links_to_fetch ), std::move( visited ) };
            }
        }

        for ( const auto &link : node->getLinks() )
        {
            const CID         &child = link.get().getCID();
            const std::string &name  = link.get().getName();
            LinkInfoPair       pair{ child, name };

            if ( !link_name.empty() && name != link_name )
            {
                continue;
            }

            if ( !visited.insert( pair ).second )
            {
                continue;
            }

            auto get_child_result = GetNodeWithoutRequest( child );
            if ( get_child_result.has_failure() )
            {
                links_to_fetch.insert( pair );
                continue;
            }
            if ( max_depth == 0 )
            {
                continue;
            }

            auto [child_links, child_visited] = TraverseCIDsLinks( get_child_result.value(),
                                                                   link_name,
                                                                   visited,
                                                                   skip_if_visited_root,
                                                                   max_depth - 1 );

            links_to_fetch.merge( child_links );
            visited.merge( child_visited );
        }

        return { std::move( links_to_fetch ), std::move( visited ) };
    }

    void CustomDagSyncer::Stop() {}
} // namespace sgns::crdt
