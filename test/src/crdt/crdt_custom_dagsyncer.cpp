#include "crdt_custom_dagsyncer.hpp"

#include <vector>

namespace
{
    // InMemoryDatastore is shared by these simulated peers and is not thread
    // safe. Protect its DAG operations across all CustomDagSyncer instances.
    // select() callbacks may synchronously read another block through a syncer.
    std::recursive_mutex shared_datastore_mutex;
}

namespace sgns::crdt
{
    CustomDagSyncer::CustomDagSyncer( std::shared_ptr<IpfsDatastore> service ) : dagService_( std::move( service ) ) {}

    outcome::result<bool> CustomDagSyncer::HasBlock( const CID &cid ) const
    {
        std::lock_guard lock( state_mutex_ );
        return local_cids_.count( cid ) != 0;
    }

    outcome::result<void> CustomDagSyncer::addNode( std::shared_ptr<const IPLDNode> node )
    {
        std::lock_guard datastore_lock( shared_datastore_mutex );
        auto result = dagService_.addNode( node );
        if ( result.has_value() )
        {
            std::lock_guard state_lock( state_mutex_ );
            local_cids_.insert( node->getCID() );
        }
        return result;
    }

    outcome::result<std::shared_ptr<IPLDNode>> CustomDagSyncer::getNode( const CID &cid ) const
    {
        std::lock_guard lock( shared_datastore_mutex );
        return dagService_.getNode( cid );
    }

    outcome::result<void> CustomDagSyncer::removeNode( const CID &cid )
    {
        // Removing a local replica does not remove the remote peer's block.
        std::lock_guard lock( state_mutex_ );
        local_cids_.erase( cid );
        requested_cids_.erase( cid );
        resolved_cids_.erase( cid );
        return outcome::success();
    }

    outcome::result<size_t> CustomDagSyncer::select(
        gsl::span<const uint8_t>                                    root_cid,
        gsl::span<const uint8_t>                                    selector,
        std::function<bool( std::shared_ptr<const IPLDNode> node )> handler ) const
    {
        std::lock_guard lock( shared_datastore_mutex );
        return dagService_.select( root_cid, selector, handler );
    }

    outcome::result<std::shared_ptr<CustomDagSyncer::Leaf>> CustomDagSyncer::fetchGraph( const CID &cid ) const
    {
        std::lock_guard lock( shared_datastore_mutex );
        return dagService_.fetchGraph( cid );
    }

    outcome::result<std::shared_ptr<CustomDagSyncer::Leaf>> CustomDagSyncer::fetchGraphOnDepth( const CID &cid,
                                                                                                uint64_t   depth ) const
    {
        std::lock_guard lock( shared_datastore_mutex );
        return dagService_.fetchGraphOnDepth( cid, depth );
    }

    void CustomDagSyncer::InitCIDBlock( const CID &cid )
    {
        std::lock_guard lock( state_mutex_ );
        requested_cids_.insert( cid );
    }

    bool CustomDagSyncer::IsCIDInCache( const CID &cid ) const
    {
        std::lock_guard lock( state_mutex_ );
        return requested_cids_.count( cid ) != 0;
    }

    outcome::result<void> CustomDagSyncer::DeleteCIDBlock( const CID &cid )
    {
        std::lock_guard lock( state_mutex_ );
        requested_cids_.erase( cid );
        return outcome::success();
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> CustomDagSyncer::GetNodeWithoutRequest(
        const CID &cid ) const
    {
        if ( !HasBlock( cid ).value() )
        {
            return outcome::failure( std::errc::no_such_file_or_directory );
        }
        return getNode( cid );
    }

    std::pair<DAGSyncer::LinkInfoSet, DAGSyncer::LinkInfoSet> CustomDagSyncer::TraverseCIDsLinks(
        ipfs_lite::ipld::IPLDNode &node,
        std::string                link_name,
        DAGSyncer::LinkInfoSet     visited_links ) const
    {
        DAGSyncer::LinkInfoSet links_to_fetch;
        DAGSyncer::LinkInfoSet visited = std::move( visited_links );

        if ( isResolved( node.getCID() ).value() )
        {
            return { std::move( links_to_fetch ), std::move( visited ) };
        }

        std::vector<std::shared_ptr<IPLDNode>> pending{
            std::shared_ptr<IPLDNode>( &node, []( IPLDNode * ) {} ) };
        while ( !pending.empty() )
        {
            auto current = std::move( pending.back() );
            pending.pop_back();
            for ( const auto &link : current->getLinks() )
            {
                const CID         &child = link.get().getCID();
                const std::string &name  = link.get().getName();
                LinkInfoPair       pair{ child, name };

                if ( ( !link_name.empty() && name != link_name ) || !visited.insert( pair ).second ||
                     isResolved( child ).value() )
                {
                    continue;
                }

                auto get_child_result = GetNodeWithoutRequest( child );
                if ( get_child_result.has_failure() )
                {
                    links_to_fetch.insert( pair );
                }
                else
                {
                    pending.push_back( get_child_result.value() );
                }
            }
        }

        return { std::move( links_to_fetch ), std::move( visited ) };
    }

    outcome::result<void> CustomDagSyncer::markResolved( const CID &cid )
    {
        std::lock_guard lock( state_mutex_ );
        resolved_cids_.insert( cid );
        return outcome::success();
    }

    outcome::result<bool> CustomDagSyncer::isResolved( const CID &cid ) const
    {
        std::lock_guard lock( state_mutex_ );
        return resolved_cids_.count( cid ) != 0;
    }

    void CustomDagSyncer::Stop() {}

} // namespace sgns::crdt
