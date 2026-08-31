#include "crdt/graphsync_dagsyncer.hpp"
#include "outcome/outcome.hpp"

#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include <memory>
#include <utility>
#include <thread>
#include <deque>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace
{
    std::mutex              g_request_wait_mutex;
    std::condition_variable g_request_wait_cv;
}

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::crdt, GraphsyncDAGSyncer::Error, e )
{
    switch ( e )
    {
        case sgns::crdt::GraphsyncDAGSyncer::Error::CID_NOT_FOUND:
            return "The Requested CID was not found";
        case sgns::crdt::GraphsyncDAGSyncer::Error::ROUTE_NOT_FOUND:
            return "No route to find the CID";
        case sgns::crdt::GraphsyncDAGSyncer::Error::PEER_BLACKLISTED:
            return "The peer who has the CID is blacklisted";
        case sgns::crdt::GraphsyncDAGSyncer::Error::TIMED_OUT:
            return "The request has timed out";
        case sgns::crdt::GraphsyncDAGSyncer::Error::DAGSYNCER_NOT_STARTED:
            return "The Start method was never called, or StopSync was called";
        case sgns::crdt::GraphsyncDAGSyncer::Error::GRAPHSYNC_IS_NULL:
            return "The graphsync member is null";
        case sgns::crdt::GraphsyncDAGSyncer::Error::HOST_IS_NULL:
            return "The host member is null";
    }
    return "Unknown error";
}

namespace sgns::crdt
{
    GraphsyncDAGSyncer::GraphsyncDAGSyncer( std::shared_ptr<IpfsDatastore> block_datastore,
                                            std::shared_ptr<Graphsync>     graphsync,
                                            std::shared_ptr<libp2p::Host>  host ) :
        dagService_(
            std::make_shared<ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl>( std::move( block_datastore ) ) ),
        graphsync_( std::move( graphsync ) ),
        host_( std::move( host ) )
    {
        logger_->debug( "GraphSyncer created {} ", reinterpret_cast<size_t>( this ) );
    }

    GraphsyncDAGSyncer::PeerKey GraphsyncDAGSyncer::RegisterPeer( const PeerId             &peer,
                                                                  std::vector<Multiaddress> address )
    {
        std::lock_guard lock( registry_mutex_ );

        // Check if peer already exists in the registry
        if ( auto it = peer_index_.find( peer ); it != peer_index_.end() )
        {
            // Peer already registered, update addresses
            PeerKey key                = it->second;
            peer_registry_[key].second = address;
            return key;
        }

        // Register new peer
        PeerKey key = peer_registry_.size();
        peer_registry_.emplace_back( peer, address );
        peer_index_.emplace( peer, key );

        logger_->debug( "Registered new peer {} with key {}", peer.toBase58(), key );
        return key;
    }

    outcome::result<GraphsyncDAGSyncer::PeerEntry> GraphsyncDAGSyncer::GetPeerById( PeerKey id ) const
    {
        std::lock_guard lock( registry_mutex_ );

        if ( id >= peer_registry_.size() )
        {
            logger_->error( "No route for the requested PeerID  {}", id );
            return outcome::failure( Error::ROUTE_NOT_FOUND );
        }

        return peer_registry_[id];
    }

    outcome::result<void> GraphsyncDAGSyncer::Listen( const Multiaddress &listen_to )
    {
        logger_->debug( "Starting to listen {} ", reinterpret_cast<size_t>( this ) );

        if ( this->host_ == nullptr )
        {
            return outcome::failure( Error::HOST_IS_NULL );
        }

        BOOST_OUTCOME_TRY( host_->listen( listen_to ) );
        BOOST_OUTCOME_TRY( this->StartSync() );
        host_->start();

        return outcome::success();
    }

    outcome::result<ipfs_lite::ipfs::graphsync::Subscription> GraphsyncDAGSyncer::RequestNode(
        const PeerId                              &peer,
        boost::optional<std::vector<Multiaddress>> address,
        const CID                                 &root_cid ) const
    {
        if ( !started_ )
        {
            logger_->error( "DagSyncer not started" );
            return outcome::failure( Error::DAGSYNCER_NOT_STARTED );
        }

        if ( graphsync_ == nullptr )
        {
            logger_->error( "graphsyncer is null" );
            return outcome::failure( Error::GRAPHSYNC_IS_NULL );
        }

        Extension              response_metadata_extension = ipfs_lite::ipfs::graphsync::encodeResponseMetadata( {} );
        std::vector<Extension> extensions{ response_metadata_extension };

        Extension do_not_send_cids_extension = ipfs_lite::ipfs::graphsync::encodeDontSendCids( {} );
        extensions.push_back( do_not_send_cids_extension );

        auto subscription = graphsync_->makeRequest(
            peer,
            std::move( address ),
            root_cid,
            {},
            extensions,
            [weakptr = weak_from_this(), root_cid]( const ResponseStatusCode      code,
                                                    const std::vector<Extension> &extensions )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->SetRequestStatus( root_cid, code );
                    self->RequestProgressCallback( code, extensions );
                }
            } );

        logger_->debug( "Requesting Node {} on this {}",
                        root_cid.toString().value(),
                        reinterpret_cast<size_t>( this ) );

        return subscription;
    }

    void GraphsyncDAGSyncer::AddRoute( const CID &cid, const PeerId &peer, std::vector<Multiaddress> address )
    {
        // Register the peer (or get existing key if already registered)
        PeerKey peerKey = RegisterPeer( peer, std::move( address ) );

        // Add the CID route to the routing table
        std::lock_guard lock( routing_mutex_ );
        auto           &routes = routing_[cid];
        if ( std::find( routes.begin(), routes.end(), peerKey ) == routes.end() )
        {
            routes.push_back( peerKey );
        }

        logger_->debug( "Added route for CID {} to peer {} (key {}, route_count={})",
                        cid.toString().value(),
                        peer.toBase58(),
                        peerKey,
                        routes.size() );
    }

    outcome::result<void> GraphsyncDAGSyncer::addNode( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )
    {
        std::lock_guard lock( dagMutex_ );
        auto            cid = node->getCID();
        auto            ret = dagService_->addNode( std::move( node ) );

        if ( !ret.has_error() )
        {
            logger_->debug( "{}: Added node {} on dagService ", __func__, cid.toString().value() );
            EraseRoute( cid );
        }
        else
        {
            logger_->error( "{}: ERROR Adding node {} on dagService ", __func__, cid.toString().value() );
        }
        return ret;
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::getNode( const CID &cid ) const
    {
        auto node = GrabCIDBlock( cid );

        if ( !node.has_error() )
        {
            logger_->debug( "Return node for CID {} instance={}",
                            cid.toString().value(),
                            reinterpret_cast<size_t>( this ) );
            return node;
        }

        node = GetNodeFromMerkleDAG( cid );

        if ( !node.has_error() )
        {
            logger_->debug( "Return node for CID {} instance={}",
                            cid.toString().value(),
                            reinterpret_cast<size_t>( this ) );
            return node;
        }

        BOOST_OUTCOME_TRY( auto route_keys, GetRouteKeys( cid ) );

        for ( const auto peer_key : route_keys )
        {
            BOOST_OUTCOME_TRY( auto peerEntry, GetPeerById( peer_key ) );
            auto &peerID  = peerEntry.first;
            auto &address = peerEntry.second;

            if ( IsOnBlackList( peerID ) )
            {
                logger_->debug( "Skipping blacklisted peer {} for CID {}", peerID.toBase58(), cid.toString().value() );
                continue;
            }

            if ( HasRecentCIDFailure( peerID, cid ) )
            {
                logger_->debug( "Skipping peer {} for CID {} due to recent CID-specific failure",
                                peerID.toBase58(),
                                cid.toString().value() );
                continue;
            }

            ClearRequestStatus( cid );
            BOOST_OUTCOME_TRY( auto subscription, RequestNode( peerID, address, cid ) );
            const auto    request_start_time      = std::chrono::steady_clock::now();
            auto          next_in_progress_log_at = request_start_time + std::chrono::seconds( 30 );
            std::uint64_t in_progress_checks      = 0;

            while ( true )
            {
                bool try_next_peer = false;

                if ( is_stopped_ )
                {
                    logger_->error( "We exited while trying to sync {} as it must have been still in progress.",
                                    cid.toString().value() );
                    return outcome::failure( Error::DAGSYNCER_NOT_STARTED );
                }
                // Check request state
                auto state_result = graphsync_->getRequestState( cid );
                if ( !state_result )
                {
                    // Request not found - This could indicate a failure, but it's also possible it just got cleaned up,
                    // so check cache or storage to see if we have the block.
                    if ( auto result = GrabCIDBlock( cid ) )
                    {
                        logger_->debug( "Return node for CID {} instance={}",
                                        cid.toString().value(),
                                        reinterpret_cast<size_t>( this ) );
                        MoveRoutePeerToFront( cid, peer_key );
                        ClearRequestStatus( cid );
                        return result;
                    }
                    if ( auto result = GetNodeWithoutRequest( cid ) )
                    {
                        logger_->debug( "Return node for CID {} instance={}",
                                        cid.toString().value(),
                                        reinterpret_cast<size_t>( this ) );
                        MoveRoutePeerToFront( cid, peer_key );
                        ClearRequestStatus( cid );
                        return result;
                    }

                    logger_->error( "Request state not found for CID {} from peer {}",
                                    cid.toString().value(),
                                    peerID.toBase58() );
                    (void) BlackListPeer( peerID );
                    ClearRequestStatus( cid );
                    try_next_peer = true;
                }

                if ( try_next_peer )
                {
                    break;
                }

                switch ( state_result.value() )
                {
                    case Graphsync::RequestState::COMPLETED:
                    {
                        // Request completed but we don't have the block?
                        // Try one more cache grab
                        if ( auto result = GrabCIDBlock( cid ) )
                        {
                            logger_->debug( "Return node for CID {} instance={}",
                                            cid.toString().value(),
                                            reinterpret_cast<size_t>( this ) );
                            RecordSuccessfulConnection( peerID );
                            ClearCIDFailure( peerID, cid );
                            MoveRoutePeerToFront( cid, peer_key );
                            ClearRequestStatus( cid );
                            return result;
                        }
                        if ( auto result = GetNodeWithoutRequest( cid ) )
                        {
                            logger_->debug( "Return node for CID {} instance={}",
                                            cid.toString().value(),
                                            reinterpret_cast<size_t>( this ) );
                            RecordSuccessfulConnection( peerID );
                            ClearCIDFailure( peerID, cid );
                            MoveRoutePeerToFront( cid, peer_key );
                            ClearRequestStatus( cid );
                            return result;
                        }
                        // If still not found, this is strange but we'll fail over
                        logger_->error( "Request marked COMPLETED but block not in cache or storage: {} from peer {}",
                                        cid.toString().value(),
                                        peerID.toBase58() );
                        RecordCIDFailure( peerID, cid );
                        ClearRequestStatus( cid );
                        try_next_peer = true;
                        break;
                    }
                    case Graphsync::RequestState::FAILED:
                    {
                        auto status = GetRequestStatus( cid );
                        if ( status && IsConnectionFailureStatus( *status ) )
                        {
                            logger_->warn( "Request failed for CID {} from peer {} with connection error {}. "
                                           "Blacklisting peer and trying fallback.",
                                           cid.toString().value(),
                                           peerID.toBase58(),
                                           statusCodeToString( *status ) );
                            (void) BlackListPeer( peerID );
                        }
                        else
                        {
                            logger_->debug( "Request failed for CID {} from peer {} - recording CID-specific failure",
                                            cid.toString().value(),
                                            peerID.toBase58() );
                            RecordCIDFailure( peerID, cid );
                        }
                        ClearRequestStatus( cid );
                        try_next_peer = true;
                        break;
                    }
                    case Graphsync::RequestState::IN_PROGRESS:
                    {
                        // Still in progress, keep waiting
                        ++in_progress_checks;
                        const auto now = std::chrono::steady_clock::now();
                        if ( now >= next_in_progress_log_at )
                        {
                            const auto elapsed_seconds =
                                std::chrono::duration_cast<std::chrono::seconds>( now - request_start_time ).count();
                            logger_->debug( "Request for CID {} from peer {} still IN_PROGRESS after {}s "
                                            "(checks={}, stopped={}, instance={})",
                                            cid.toString().value(),
                                            peerID.toBase58(),
                                            elapsed_seconds,
                                            in_progress_checks,
                                            is_stopped_.load(),
                                            reinterpret_cast<size_t>( this ) );
                            next_in_progress_log_at = now + std::chrono::seconds( 30 );
                        }
                        logger_->trace( "Request for CID {} from peer {} - In Progress",
                                        cid.toString().value(),
                                        peerID.toBase58() );

                        std::unique_lock<std::mutex> wait_lock( g_request_wait_mutex );
                        g_request_wait_cv.wait_for( wait_lock,
                                                    std::chrono::milliseconds( 100 ),
                                                    [this]() { return this->is_stopped_.load(); } );
                        break;
                    }
                }

                if ( try_next_peer )
                {
                    break;
                }
            }
        }

        logger_->error( "No usable route candidates left for CID {}", cid.toString().value() );
        return outcome::failure( Error::ROUTE_NOT_FOUND );
    }

    outcome::result<void> GraphsyncDAGSyncer::removeNode( const CID &cid )
    {
        std::lock_guard lock( dagMutex_ );
        return dagService_->removeNode( cid );
    }

    outcome::result<size_t> GraphsyncDAGSyncer::select(
        gsl::span<const uint8_t>                                                     root_cid,
        gsl::span<const uint8_t>                                                     selector,
        std::function<bool( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )> handler ) const
    {
        std::lock_guard lock( dagMutex_ );
        return dagService_->select( root_cid, selector, handler );
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf>> GraphsyncDAGSyncer::fetchGraph(
        const CID &cid ) const
    {
        return ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl::fetchGraphOnDepth(
            [weakptr = weak_from_this()](
                const CID &cid ) -> outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>>
            {
                if ( auto self = weakptr.lock() )
                {
                    return self->getNode( cid );
                }
                return outcome::failure( boost::system::error_code{} );
            },
            cid,
            {} );
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf>> GraphsyncDAGSyncer::fetchGraphOnDepth(
        const CID &cid,
        uint64_t   depth ) const
    {
        return ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl::fetchGraphOnDepth(
            [weakptr = weak_from_this()](
                const CID &cid ) -> outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>>
            {
                if ( auto self = weakptr.lock() )
                {
                    return self->getNode( cid );
                }
                return outcome::failure( boost::system::error_code{} );
            },
            cid,
            depth );
    }

    outcome::result<bool> GraphsyncDAGSyncer::HasBlock( const CID &cid ) const
    {
        auto getNodeResult       = GetNodeFromMerkleDAG( cid );
        auto getCachedNodeResult = GrabCIDBlock( cid );

        return getNodeResult.has_value() || getCachedNodeResult.has_value();
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::GetNodeWithoutRequest(
        const CID &cid ) const
    {
        auto getNodeResult = GetNodeFromMerkleDAG( cid );

        if ( getNodeResult.has_value() )
        {
            return getNodeResult;
        }

        return GrabCIDBlock( cid );
    }

    outcome::result<void> GraphsyncDAGSyncer::StartSync()
    {
        if ( started_ )
        {
            return outcome::success();
        }

        if ( graphsync_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        BlockCallback blockCallback = [weakptr = weak_from_this()]( const CID &cid, common::Buffer buffer )
        {
            if ( auto self = weakptr.lock() )
            {
                self->BlockReceivedCallback( cid, buffer );
            }
        };

        graphsync_->start( shared_from_this(), blockCallback );

        if ( host_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        started_ = true;

        return outcome::success();
    }

    void GraphsyncDAGSyncer::StopSync()
    {
        if ( graphsync_ != nullptr )
        {
            graphsync_->stop();
        }
        started_ = false;
    }

    outcome::result<GraphsyncDAGSyncer::PeerId> GraphsyncDAGSyncer::GetId() const
    {
        if ( host_ != nullptr )
        {
            return host_->getId();
        }
        return outcome::failure( boost::system::error_code{} );
    }

    outcome::result<libp2p::peer::PeerInfo> GraphsyncDAGSyncer::GetPeerInfo() const
    {
        if ( host_ != nullptr )
        {
            return host_->getPeerInfo();
        }
        return outcome::failure( boost::system::error_code{} );
    }

    void GraphsyncDAGSyncer::RequestProgressCallback( ResponseStatusCode            code,
                                                      const std::vector<Extension> &extensions ) const
    {
        std::string s;
        for ( const auto &[name, data] : extensions )
        {
            s += fmt::format( "({}: 0x{}) ", name, common::Buffer( data ).toHex() );
        }

        logger_->debug( "request progress: code={}, extensions={}", statusCodeToString( code ), s );
    }

    void GraphsyncDAGSyncer::BlockReceivedCallback( const CID &cid, common::Buffer buffer )
    {
        logger_->trace( "Block received: cid={}", cid.toString().value() );
        auto hb = HasBlock( cid );

        if ( hb.has_failure() )
        {
            logger_->debug( "HasBlock failed: {}, cid: {}", hb.error().message().c_str(), cid.toString().value() );
            return;
        }
        logger_->debug( "HasBlock: {}, cid: {}", hb.value(), cid.toString().value() );

        if ( hb.value() )
        {
            logger_->debug( "We already have this node {}", cid.toString().value() );
            return;
        }

        auto node = ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes( buffer );
        if ( node.has_failure() )
        {
            logger_->error( "Cannot create node from received block data for CID {}", cid.toString().value() );
            return;
        }

        if ( AddCIDBlock( cid, node.value() ) )
        {
            logger_->error( " Block received without CRDT asking for it explicitly " );
        }

        std::stringstream sslinks;
        for ( const auto &link : node.value()->getLinks() )
        {
            sslinks << "[";
            sslinks << link.get().getCID().toString().value();
            sslinks << link.get().getName();
            sslinks << "], ";
        }
        logger_->debug( "Node added to dagService. CID: {}, links: [{}]",
                        node.value()->getCID().toString().value(),
                        sslinks.str() );

        if ( auto maybe_route_info = GetRoute( cid ) )
        {
            auto &[peerID, address] = maybe_route_info.value();
            logger_->debug( "Seeing if peer {} has links to AddRoute", peerID.toBase58() );
            if ( IsOnBlackList( peerID ) )
            {
                //I don't think it should ever land here
                logger_->debug( "Peer {} was blacklisted", peerID.toBase58() );
            }
            else
            {
                RecordSuccessfulConnection( peerID );

                // Clear any CID failure record for this peer and CID since they successfully provided it
                ClearCIDFailure( peerID, cid );

                auto [links_to_fetch, _1] = TraverseCIDsLinks( *node.value() );
                for ( const auto &[cid, _2] : links_to_fetch )
                {
                    logger_->trace( "Adding route for peer {} and CID {}", peerID.toBase58(), cid.toString().value() );
                    AddRoute( cid, peerID, address );
                }
            }
        }
    }

    std::pair<DAGSyncer::LinkInfoSet, DAGSyncer::LinkInfoSet> GraphsyncDAGSyncer::TraverseCIDsLinks(
        ipfs_lite::ipld::IPLDNode &node,
        std::string                link_name,
        LinkInfoSet                visited ) const
    {
        LinkInfoSet links_to_fetch;

        // Use iterative approach with a work queue to avoid stack overflow
        // Each work item contains the node to process
        struct WorkItem
        {
            std::shared_ptr<ipfs_lite::ipld::IPLDNode> node;
        };

        std::deque<WorkItem> work_queue;

        // Start with the root node
        const CID &root_cid = node.getCID();

        auto tree_resolved_res = isResolved( root_cid );
        if ( tree_resolved_res.has_failure() )
        {
            logger_->error( "{}: isResolved failed: {}, cid: {}",
                            __func__,
                            tree_resolved_res.error().message().c_str(),
                            root_cid.toString().value() );
            return { std::move( links_to_fetch ), std::move( visited ) };
        }

        if ( tree_resolved_res.value() )
        {
            logger_->debug( "TraverseCIDsLinks: Skipping traversal of root {} (already resolved)",
                            root_cid.toString().value() );
            return { std::move( links_to_fetch ), std::move( visited ) };
        }

        logger_->info( "TraverseCIDsLinks: Checking links on {{ cid=\"{}\", name=\"{}\" }}",
                       root_cid.toString().value(),
                       link_name );

        // Add the root node to the work queue
        // Create a shared_ptr from the reference - note this assumes the node is already managed
        work_queue.push_back(
            WorkItem{ std::shared_ptr<ipfs_lite::ipld::IPLDNode>( &node, []( ipfs_lite::ipld::IPLDNode * ) {} ) } );

        // Process the queue iteratively
        while ( !work_queue.empty() )
        {
            WorkItem current_item = std::move( work_queue.front() );
            work_queue.pop_front();

            auto      &current_node = *current_item.node;
            const CID &current_cid  = current_node.getCID();

            logger_->trace( "TraverseCIDsLinks: Processing node {}", current_cid.toString().value() );

            // Process all links in the current node
            for ( const auto &link : current_node.getLinks() )
            {
                const CID         &child = link.get().getCID();
                const std::string &name  = link.get().getName();
                LinkInfoPair       pair{ child, name };

                logger_->trace( "TraverseCIDsLinks: Link: name '{}' != '{}'", name, link_name );
                if ( !link_name.empty() && name != link_name )
                {
                    logger_->debug( "TraverseCIDsLinks: Skipping link: name '{}' != '{}'", name, link_name );
                    continue;
                }

                if ( !visited.insert( pair ).second )
                {
                    logger_->info( "TraverseCIDsLinks: Already visited {{ link=\"{}\", name=\"{}\" }}",
                                   child.toString().value(),
                                   name );
                    continue;
                }

                auto child_resolved_res = isResolved( child );
                if ( child_resolved_res.has_failure() )
                {
                    logger_->error( "{}: isResolved failed: {}, cid: {}",
                                    __func__,
                                    child_resolved_res.error().message().c_str(),
                                    child.toString().value() );
                }
                else if ( child_resolved_res.value() )
                {
                    logger_->debug( "TraverseCIDsLinks: Skipping traversal of resolved child {}",
                                    child.toString().value() );
                    continue;
                }

                logger_->info( "TraverseCIDsLinks: Found link {{ cid=\"{}\", name=\"{}\", size={} }}",
                               child.toString().value(),
                               name,
                               link.get().getSize() );

                auto get_child_result = GetNodeWithoutRequest( child );
                if ( get_child_result.has_failure() )
                {
                    logger_->debug( "TraverseCIDsLinks: Missing block {}, adding as link to be fetched",
                                    child.toString().value() );
                    links_to_fetch.insert( pair );
                    continue;
                }

                // Add the child node to the front of the work queue for depth-first processing
                // This ensures children are resolved before their parents, maintaining the original
                // recursive behavior where we don't mark a CID as complete until all its children are processed
                work_queue.push_front( WorkItem{ get_child_result.value() } );

                logger_->trace( "TraverseCIDsLinks: Added child {} to work queue (queue size: {})",
                                child.toString().value(),
                                work_queue.size() );
            }
        }

        logger_->info( "TraverseCIDsLinks: Completed traversal. Links to fetch: {}, Visited: {}",
                       links_to_fetch.size(),
                       visited.size() );

        return { std::move( links_to_fetch ), std::move( visited ) };
    }

    outcome::result<void> GraphsyncDAGSyncer::markResolved( const CID &cid )
    {
        std::lock_guard<std::mutex> lock( dagMutex_ );
        return dagService_->markResolved( cid );
    }

    outcome::result<bool> GraphsyncDAGSyncer::isResolved( const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( dagMutex_ );
        return dagService_->isResolved( cid );
    }

    void GraphsyncDAGSyncer::InitCIDBlock( const CID &cid )
    {
        lru_cid_cache_.init( cid );
        logger_->debug( "Block initialized without content to LRU cache: CID {}, cache size: {}",
                        cid.toString().value(),
                        lru_cid_cache_.size() );
    }

    bool GraphsyncDAGSyncer::AddCIDBlock( const CID &cid, const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &block )
    {
        bool was_created = lru_cid_cache_.add( cid, block );

        if ( was_created )
        {
            logger_->debug( "New block added to LRU cache: CID {}, cache size: {}",
                            cid.toString().value(),
                            lru_cid_cache_.size() );
        }
        else
        {
            logger_->debug( "Existing block updated in LRU cache: CID {}", cid.toString().value() );
        }

        return was_created;
    }

    bool GraphsyncDAGSyncer::IsCIDInCache( const CID &cid ) const
    {
        return lru_cid_cache_.contains( cid );
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::GrabCIDBlock( const CID &cid ) const
    {
        if ( lru_cid_cache_.hasContent( cid ) )
        {
            if ( auto node = lru_cid_cache_.get( cid ) )
            {
                logger_->trace( "Block retrieved from LRU cache: CID {}", cid.toString().value() );
                return node;
            }
        }
        return outcome::failure( Error::CID_NOT_FOUND );
    }

    outcome::result<void> GraphsyncDAGSyncer::DeleteCIDBlock( const CID &cid )
    {
        if ( lru_cid_cache_.remove( cid ) )
        {
            logger_->debug( "Block removed from LRU cache: CID {}", cid.toString().value() );
        }
        return outcome::success();
    }

    void GraphsyncDAGSyncer::AddToBlackList( const PeerId &peer ) const
    {
        std::lock_guard lock( blacklist_mutex_ );

        uint64_t now = GetCurrentTimestamp();

        if ( auto [it, inserted] = blacklist_.emplace( peer.toMultihash(), BlacklistEntry( now, 1 ) ); !inserted )
        {
            BlacklistEntry &entry   = it->second;
            uint64_t        timeout = getBackoffTimeout( entry.failures, entry.ever_connected );

            if ( now - entry.timestamp > timeout )
            {
                logger_->debug( "Peer {} blacklist timeout expired", peer.toBase58() );
            }

            entry.failures++;
            logger_->debug( "Peer {} failures incremented to {}", peer.toBase58(), entry.failures );

            entry.timestamp = now;
        }
    }

    uint64_t GraphsyncDAGSyncer::getBackoffTimeout( uint64_t failures, bool ever_connected )
    {
        if ( ever_connected )
        {
            // For previously connected peers:
            // - Start with 5 seconds
            // - Cap at 30 seconds
            uint64_t base_seconds = 5;
            uint64_t max_seconds  = 30;

            // Calculate exponential backoff
            uint64_t timeout = base_seconds * ( 1ULL << failures );
            return std::min( timeout, max_seconds );
        }
        // For never-connected peers:
        // - Start with 10 seconds
        // - Cap at 1800 seconds (30 minutes)
        uint64_t base_seconds = 10;
        uint64_t max_seconds  = 1800;

        // Calculate exponential backoff
        uint64_t timeout = base_seconds * ( 1ULL << failures );
        return std::min( timeout, max_seconds );
    }

    bool GraphsyncDAGSyncer::IsOnBlackList( const PeerId &peer ) const
    {
        bool            ret = false;
        std::lock_guard lock( blacklist_mutex_ );
        do
        {
            auto it = blacklist_.find( peer.toMultihash() );
            if ( it == blacklist_.end() )
            {
                logger_->trace( "Peer {} in NOT blacklisted", peer.toBase58() );
                break;
            }

            uint64_t        now   = GetCurrentTimestamp();
            BlacklistEntry &entry = it->second;

            // If no failures yet, not blacklisted
            if ( entry.failures == 0 )
            {
                break;
            }

            // Calculate timeout based on connection history and failure count
            uint64_t timeout = getBackoffTimeout( entry.failures - 1, entry.ever_connected );

            if ( now - entry.timestamp > timeout )
            {
                // Timeout expired, so peer is NOT currently blacklisted
                // We don't reset failures, but we do allow this peer to be tried again
                entry.backoff_attempts++; // Track the number of times we've retried this peer
                entry.timestamp = now;
                logger_->trace( "Peer {} blacklist timeout expired, allowing retry (attempt {}, failures {})",
                                peer.toBase58(),
                                entry.backoff_attempts,
                                entry.failures );
                // ret remains false - peer is NOT on blacklist
                break;
            }

            // Still within blacklist timeout and has failures
            ret = true; // This peer IS on the blacklist
            logger_->trace( "Peer {} BLACKLISTED (failures: {}, timeout: {}s)",
                            peer.toBase58(),
                            entry.failures,
                            timeout );
        } while ( false );

        return ret;
    }

    // Record successful connections
    void GraphsyncDAGSyncer::RecordSuccessfulConnection( const PeerId &peer ) const
    {
        std::lock_guard lock( blacklist_mutex_ );
        if ( auto it = blacklist_.find( peer.toMultihash() ); it != blacklist_.end() )
        {
            BlacklistEntry &entry  = it->second;
            entry.ever_connected   = true;
            entry.failures         = 0;
            entry.backoff_attempts = 0; // Reset backoff on successful connection
            logger_->debug( "Recorded successful connection for peer {}", peer.toBase58() );
        }
    }

    outcome::result<void> GraphsyncDAGSyncer::BlackListPeer( const PeerId &peer ) const
    {
        AddToBlackList( peer );
        if ( IsOnBlackList( peer ) )
        {
            EraseRoutesFromPeerID( peer );
        }
        return outcome::success();
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::GetNodeFromMerkleDAG(
        const CID &cid ) const
    {
        std::lock_guard lock( dagMutex_ );
        return dagService_->getNode( cid );
    }

    outcome::result<GraphsyncDAGSyncer::PeerEntry> GraphsyncDAGSyncer::GetRoute( const CID &cid ) const
    {
        BOOST_OUTCOME_TRY( auto routeKeys, GetRouteKeys( cid ) );
        for ( const auto key : routeKeys )
        {
            auto peerEntry = GetPeerById( key );
            if ( peerEntry.has_value() )
            {
                return peerEntry.value();
            }
        }
        logger_->error( "No route for the requested CID  {}", cid.toString().value() );
        return outcome::failure( Error::ROUTE_NOT_FOUND );
    }

    outcome::result<std::vector<GraphsyncDAGSyncer::PeerKey>> GraphsyncDAGSyncer::GetRouteKeys( const CID &cid ) const
    {
        std::lock_guard lock( routing_mutex_ );
        auto            it = routing_.find( cid );
        if ( it == routing_.end() || it->second.empty() )
        {
            logger_->error( "No route for the requested CID  {}", cid.toString().value() );
            return outcome::failure( Error::ROUTE_NOT_FOUND );
        }

        return it->second;
    }

    void GraphsyncDAGSyncer::MoveRoutePeerToFront( const CID &cid, PeerKey peerKey ) const
    {
        std::lock_guard lock( routing_mutex_ );
        auto            it = routing_.find( cid );
        if ( it == routing_.end() )
        {
            return;
        }

        auto &routes = it->second;
        auto  pos    = std::find( routes.begin(), routes.end(), peerKey );
        if ( pos == routes.end() || pos == routes.begin() )
        {
            return;
        }

        routes.erase( pos );
        routes.insert( routes.begin(), peerKey );
    }

    void GraphsyncDAGSyncer::EraseRoutesFromPeerID( const PeerId &peer ) const
    {
        // First find the peer key in the peer index
        PeerKey peerKeyToRemove;

        {
            std::lock_guard registry_lock( registry_mutex_ );
            auto            it = peer_index_.find( peer );
            if ( it == peer_index_.end() )
            {
                // Peer not found in registry, nothing to erase
                return;
            }
            peerKeyToRemove = it->second;
        }

        // Remove all routes that point to this peer
        std::lock_guard routing_lock( routing_mutex_ );
        for ( auto it = routing_.begin(); it != routing_.end(); )
        {
            auto &route_keys = it->second;
            route_keys.erase( std::remove( route_keys.begin(), route_keys.end(), peerKeyToRemove ), route_keys.end() );
            if ( route_keys.empty() )
            {
                logger_->debug( "Erasing route for CID {} to blacklisted peer", it->first.toString().value() );
                it = routing_.erase( it );
            }
            else
            {
                ++it;
            }
        }
    }

    void GraphsyncDAGSyncer::EraseRoute( const CID &cid )
    {
        std::lock_guard lock( routing_mutex_ );
        if ( auto it = routing_.find( cid ); it != routing_.end() )
        {
            routing_.erase( it );
        }
    }

    uint64_t GraphsyncDAGSyncer::GetCurrentTimestamp()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    void GraphsyncDAGSyncer::RecordCIDFailure( const PeerId &peer, const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( cid_failures_mutex_ );
        auto                        key = std::make_pair( peer.toMultihash(), cid );
        cid_failures_[key]              = GetCurrentTimestamp();
        logger_->debug( "Recorded CID failure: peer {} cannot provide CID {}",
                        peer.toBase58(),
                        cid.toString().value() );
    }

    bool GraphsyncDAGSyncer::HasRecentCIDFailure( const PeerId &peer, const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( cid_failures_mutex_ );
        auto                        key = std::make_pair( peer.toMultihash(), cid );
        auto                        it  = cid_failures_.find( key );

        if ( it == cid_failures_.end() )
        {
            return false; // No failure recorded
        }

        // Consider failure "recent" for 3 minutes (180 seconds)
        // This prevents immediate re-requests but allows retry after some time
        uint64_t       now             = GetCurrentTimestamp();
        uint64_t       failure_age     = now - it->second;
        const uint64_t FAILURE_TIMEOUT = 180; // 3 minutes

        if ( failure_age > FAILURE_TIMEOUT )
        {
            // Failure is old, remove it and allow retry
            cid_failures_.erase( it );
            return false;
        }

        return true; // Recent failure exists
    }

    void GraphsyncDAGSyncer::ClearCIDFailure( const PeerId &peer, const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( cid_failures_mutex_ );
        auto                        key = std::make_pair( peer.toMultihash(), cid );
        auto                        it  = cid_failures_.find( key );
        if ( it != cid_failures_.end() )
        {
            cid_failures_.erase( it );
            logger_->debug( "Cleared CID failure record: peer {} can now be tried again for CID {}",
                            peer.toBase58(),
                            cid.toString().value() );
        }
    }

    bool GraphsyncDAGSyncer::IsConnectionFailureStatus( ResponseStatusCode code )
    {
        // RS_TIMEOUT is deliberately excluded: a timeout means the peer was slow
        // to answer (e.g. busy serving other requests), not that it is
        // unconnectable. Blacklisting on timeout excludes the peer for an
        // escalating backoff (up to 30 min) — fatal when it is the only route
        // for a CID. Slow peers are handled by the CID-specific failure count.
        return code == ipfs_lite::ipfs::graphsync::RS_NO_PEERS ||
               code == ipfs_lite::ipfs::graphsync::RS_CANNOT_CONNECT ||
               code == ipfs_lite::ipfs::graphsync::RS_CONNECTION_ERROR;
    }

    void GraphsyncDAGSyncer::SetRequestStatus( const CID &cid, ResponseStatusCode code ) const
    {
        std::lock_guard lock( request_status_mutex_ );
        request_status_[cid] = code;
    }

    boost::optional<GraphsyncDAGSyncer::ResponseStatusCode> GraphsyncDAGSyncer::GetRequestStatus( const CID &cid ) const
    {
        std::lock_guard lock( request_status_mutex_ );
        if ( auto it = request_status_.find( cid ); it != request_status_.end() )
        {
            return it->second;
        }
        return boost::none;
    }

    void GraphsyncDAGSyncer::ClearRequestStatus( const CID &cid ) const
    {
        std::lock_guard lock( request_status_mutex_ );
        request_status_.erase( cid );
    }

    void GraphsyncDAGSyncer::LRUCIDCache::init( const CID &cid )
    {
        std::lock_guard lock( mutex_ );
        // Check if the item already exists
        if ( auto it = cache_map_.find( cid ); it != cache_map_.end() )
        {
            // Already exists, just update its position in LRU list
            lru_list_.erase( it->second.second );
            lru_list_.push_front( cid );
            it->second.second = lru_list_.begin();
            return;
        }

        // If cache is full, remove the least recently used item
        if ( cache_map_.size() >= MAX_CACHE_SIZE )
        {
            // Get the least recently used CID
            const CID &lru_cid = lru_list_.back();
            // Remove it from the cache
            cache_map_.erase( lru_cid );
            // Remove it from the LRU list
            lru_list_.pop_back();
        }

        // Add the new item to the front of the LRU list
        lru_list_.push_front( cid );
        // Add to the cache map with nullptr and reference to its position in the LRU list
        cache_map_[cid] = std::make_pair( nullptr, lru_list_.begin() );
    }

    bool GraphsyncDAGSyncer::LRUCIDCache::add( const CID &cid, std::shared_ptr<ipfs_lite::ipld::IPLDNode> node )
    {
        std::lock_guard lock( mutex_ );
        // Check if the item already exists
        if ( auto it = cache_map_.find( cid ); it != cache_map_.end() )
        {
            // Existing entry - update it
            // Move the CID to the front of the LRU list
            lru_list_.erase( it->second.second );
            lru_list_.push_front( cid );
            // Update the cache item with new node and new iterator
            it->second = std::make_pair( node, lru_list_.begin() );
            return false; // Entry was updated, not created
        }

        // If cache is full, remove the least recently used item
        if ( cache_map_.size() >= MAX_CACHE_SIZE )
        {
            // Get the least recently used CID
            const CID &lru_cid = lru_list_.back();
            // Remove it from the cache
            cache_map_.erase( lru_cid );
            // Remove it from the LRU list
            lru_list_.pop_back();
        }

        // Add the new item to the front of the LRU list
        lru_list_.push_front( cid );
        // Add to the cache map with a reference to its position in the LRU list
        cache_map_[cid] = std::make_pair( std::move( node ), lru_list_.begin() );
        return true; // New entry was created
    }

    std::shared_ptr<ipfs_lite::ipld::IPLDNode> GraphsyncDAGSyncer::LRUCIDCache::get( const CID &cid )
    {
        std::lock_guard lock( mutex_ );
        auto            it = cache_map_.find( cid );
        if ( it == cache_map_.end() )
        {
            return nullptr;
        }

        // Move this item to the front of the LRU list
        lru_list_.erase( it->second.second );
        lru_list_.push_front( cid );

        // Update the iterator in the cache map
        it->second.second = lru_list_.begin();

        // Return the node
        return it->second.first;
    }

    bool GraphsyncDAGSyncer::LRUCIDCache::remove( const CID &cid )
    {
        std::lock_guard lock( mutex_ );
        auto            it = cache_map_.find( cid );
        if ( it == cache_map_.end() )
        {
            return false;
        }

        // Remove from LRU list
        lru_list_.erase( it->second.second );

        // Remove from cache map
        cache_map_.erase( it );

        return true;
    }

    bool GraphsyncDAGSyncer::LRUCIDCache::contains( const CID &cid ) const
    {
        std::lock_guard lock( mutex_ );
        return cache_map_.find( cid ) != cache_map_.end();
    }

    // Check if CID exists and has content
    bool GraphsyncDAGSyncer::LRUCIDCache::hasContent( const CID &cid ) const
    {
        std::lock_guard lock( mutex_ );
        auto            it = cache_map_.find( cid );
        return it != cache_map_.end() && it->second.first != nullptr;
    }

    void GraphsyncDAGSyncer::Stop()
    {
        logger_->debug( "Stopping Dagsyncer" );
        is_stopped_ = true;
        g_request_wait_cv.notify_all();
    }

}
