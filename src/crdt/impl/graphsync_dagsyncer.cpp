#include "crdt/graphsync_dagsyncer.hpp"
#include "outcome/outcome.hpp"

#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include <memory>
#include <utility>
#include <thread>

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
        case sgns::crdt::GraphsyncDAGSyncer::Error::DAGSYNCHER_NOT_STARTED:
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
    GraphsyncDAGSyncer::GraphsyncDAGSyncer( std::shared_ptr<IpfsDatastore> service,
                                            std::shared_ptr<Graphsync>     graphsync,
                                            std::shared_ptr<libp2p::Host>  host ) :
        dagService_( std::move( service ) ), graphsync_( std::move( graphsync ) ), host_( std::move( host ) )
    {
        logger_->debug( "GraphSyncher created{} ", reinterpret_cast<size_t>( this ) );
    }

    GraphsyncDAGSyncer::PeerKey GraphsyncDAGSyncer::RegisterPeer( const PeerId                    &peer,
                                                                  const std::vector<Multiaddress> &address ) const
    {
        std::lock_guard<std::mutex> lock( registry_mutex_ );

        // Check if peer already exists in the registry
        auto it = peer_index_.find( peer );
        if ( it != peer_index_.end() )
        {
            // Peer already registered, update addresses if needed
            PeerKey key                = it->second;
            peer_registry_[key].second = address; // Update the addresses
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
        std::lock_guard<std::mutex> lock( registry_mutex_ );

        if ( id >= peer_registry_.size() )
        {
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

        auto listen_res = host_->listen( listen_to );
        if ( listen_res.has_failure() )
        {
            return listen_res.error();
        }

        auto startResult = this->StartSync();
        if ( startResult.has_failure() )
        {
            return startResult.error();
        }

        return outcome::success();
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipfs::graphsync::Subscription>> GraphsyncDAGSyncer::RequestNode(
        const PeerId                              &peer,
        boost::optional<std::vector<Multiaddress>> address,
        const CID                                 &root_cid ) const
    {
        if ( !started_ )
        {
            return outcome::failure( Error::DAGSYNCHER_NOT_STARTED );
        }

        if ( graphsync_ == nullptr )
        {
            return outcome::failure( Error::GRAPHSYNC_IS_NULL );
        }
        std::vector<Extension> extensions;
        ResponseMetadata       response_metadata{};
        Extension response_metadata_extension = ipfs_lite::ipfs::graphsync::encodeResponseMetadata( response_metadata );
        extensions.push_back( response_metadata_extension );

        std::vector<CID> cids;
        Extension        do_not_send_cids_extension = ipfs_lite::ipfs::graphsync::encodeDontSendCids( cids );
        extensions.push_back( do_not_send_cids_extension );
        auto subscription = graphsync_->makeRequest(
            peer,
            std::move( address ),
            root_cid,
            {},
            extensions,
            [weakptr = weak_from_this()]( ResponseStatusCode code, const std::vector<Extension> &extensions )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->RequestProgressCallback( code, extensions );
                }
            } );

        // keeping subscriptions alive, otherwise they cancel themselves
        logger_->debug( "Requesting Node {} on this {}",
                        root_cid.toString().value(),
                        reinterpret_cast<size_t>( this ) );
        return std::make_shared<Subscription>( std::move( subscription ) );
    }

    void GraphsyncDAGSyncer::AddRoute( const CID &cid, const PeerId &peer, std::vector<Multiaddress> &address )
    {
        // Register the peer (or get existing key if already registered)
        PeerKey peerKey = RegisterPeer( peer, address );

        // Add the CID route to the routing table
        std::lock_guard<std::mutex> lock( routing_mutex_ );
        routing_[cid] = peerKey;

        logger_->trace( "Added route for CID {} to peer {} (key {})",
                        cid.toString().value(),
                        peer.toBase58(),
                        peerKey );
    }

    outcome::result<void> GraphsyncDAGSyncer::addNode( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )
    {
        return AddNodeToMerkleDAG( std::move( node ) );
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::getNode( const CID &cid ) const
    {
        auto node = GrabCIDBlock( cid );

        if ( !node.has_error() )
        {
            logger_->debug( "Return node for CID {} and {} instance={}",
                            cid.toString().value(),
                            node.value()->getCID().toString().value(),
                            reinterpret_cast<size_t>( this ) );
            return node;
        }

        node = GetNodeFromMerkleDAG( cid );

        if ( !node.has_error() )
        {
            logger_->debug( "Return node for CID {} and {} instance={}",
                            cid.toString().value(),
                            node.value()->getCID().toString().value(),
                            reinterpret_cast<size_t>( this ) );
            return node;
        }
        // Get the peer info from our routing system
        OUTCOME_TRY( auto peerEntry, GetRoute( cid ) );

        auto &peerID  = peerEntry.first;
        auto &address = peerEntry.second;

        OUTCOME_TRY( ( auto &&, subscription ), RequestNode( peerID, address, cid ) );

        auto start_time = std::chrono::steady_clock::now();
        while ( std::chrono::steady_clock::now() - start_time < std::chrono::seconds( 20 ) )
        {
            auto result = GrabCIDBlock( cid ); // Call the internal GrabCIDBlock
            if ( result )
            {
                logger_->debug( "Return node for CID {} and {} instance={}",
                                cid.toString().value(),
                                result.value()->getCID().toString().value(),
                                reinterpret_cast<size_t>( this ) );
                return result; // Return the block if successfully grabbed
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // Retry after a short delay
        }
        BlackListPeer( peerID );
        logger_->debug( "Timeout while waiting for node fetch: {}, penalizing peer {} ",
                        cid.toString().value(),
                        peerID.toBase58() );
        return outcome::failure( boost::system::errc::timed_out );
    }

    outcome::result<void> GraphsyncDAGSyncer::removeNode( const CID &cid )
    {
        return RemoveNodeFromMerkleDAG( cid );
    }

    outcome::result<size_t> GraphsyncDAGSyncer::select(
        gsl::span<const uint8_t>                                                     root_cid,
        gsl::span<const uint8_t>                                                     selector,
        std::function<bool( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )> handler ) const
    {
        return SelectFromMerkleDAG( std::move( root_cid ), std::move( selector ), std::move( handler ) );
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

        auto dagService = std::make_shared<MerkleDagBridgeImpl>( shared_from_this() );

        BlockCallback blockCallback = [weakptr = weak_from_this()]( const CID &cid, sgns::common::Buffer buffer )
        {
            if ( auto self = weakptr.lock() )
            {
                self->BlockReceivedCallback( cid, buffer );
            }
        };

        graphsync_->start( dagService, blockCallback );

        if ( host_ == nullptr )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        host_->start();

        started_ = true;

        return outcome::success();
    }

    void GraphsyncDAGSyncer::StopSync()
    {
        if ( graphsync_ != nullptr )
        {
            graphsync_->stop();
        }
        if ( host_ != nullptr )
        {
            host_->stop();
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

    namespace
    {
        std::string formatExtensions( const std::vector<GraphsyncDAGSyncer::Extension> &extensions )
        {
            std::string s;
            for ( const auto &item : extensions )
            {
                s += fmt::format( "({}: 0x{}) ", item.name, common::Buffer( item.data ).toHex() );
            }
            return s;
        }
    }

    void GraphsyncDAGSyncer::RequestProgressCallback( ResponseStatusCode            code,
                                                      const std::vector<Extension> &extensions ) const
    {
        logger_->trace( "request progress: code={}, extensions={}",
                        statusCodeToString( code ),
                        formatExtensions( extensions ) );
    }

    void GraphsyncDAGSyncer::BlockReceivedCallback( const CID &cid, sgns::common::Buffer buffer )
    {
        logger_->trace( "Block received: cid={}, extensions={}", cid.toString().value(), buffer.toHex() );
        auto hb = HasBlock( cid );

        if ( hb.has_failure() )
        {
            logger_->debug( "HasBlock failed: {}, cid: {}", hb.error().message().c_str(), cid.toString().value() );
            return;
        }
        logger_->debug( "HasBlock: {}, cid: {}", hb.value(), cid.toString().value() );

        auto node = ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes( buffer );
        if ( !hb.value() )
        {
            if ( node.has_failure() )
            {
                logger_->error( "Cannot create node from received block data for CID {}", cid.toString().value() );
                return;
            }

            auto ret = AddCIDBlock( cid, node.value() );
            if ( ret )
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
        }
        else
        {
            logger_->debug( "We already had this node {}", cid.toString().value() );
            return;
        }

        // @todo performance optimization is required

        logger_->debug( "Request found {}", cid.toString().value() );

        auto maybe_route_info = GetRoute( cid );
        if ( maybe_route_info )
        {
            auto &[peerID, address] = maybe_route_info.value();
            logger_->debug( "Seeing if peer {} has links to AddRoute", peerID.toBase58() );
            if ( !IsOnBlackList( peerID ) )
            {
                // Record successful data reception
                RecordSuccessfulConnection( peerID );
                for ( auto link : node.value()->getLinks() )
                {
                    auto linkhb = HasBlock( link.get().getCID() );
                    if ( linkhb.has_value() && !linkhb.value() )
                    {
                        logger_->trace( "Adding route for peer {} and CID {}",
                                        peerID.toBase58(),
                                        link.get().getCID().toString().value() );

                        // Use a non-const copy of the address for AddRoute
                        std::vector<Multiaddress> addr_copy = address;
                        AddRoute( link.get().getCID(), peerID, addr_copy );
                    }
                }
            }
            else
            {
                //I don't think it should ever land here
                logger_->debug( "Peer {} was blacklisted", peerID.toBase58() );
            }
        }
        EraseRoute( cid );
    }

    void GraphsyncDAGSyncer::InitCIDBlock( const CID &cid )
    {
        std::lock_guard<std::mutex> lock( cache_mutex_ );
        lru_cid_cache_.init( cid );
        logger_->debug( "Block initialized without content to LRU cache: CID {}, cache size: {}",
                        cid.toString().value(),
                        lru_cid_cache_.size() );
    }

    bool GraphsyncDAGSyncer::AddCIDBlock( const CID &cid, const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &block )
    {
        std::lock_guard<std::mutex> lock( cache_mutex_ );
        bool                        was_created = lru_cid_cache_.add( cid, block );

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
        std::lock_guard<std::mutex> lock( cache_mutex_ );
        return lru_cid_cache_.contains( cid );
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::GrabCIDBlock( const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( cache_mutex_ );
        if ( lru_cid_cache_.hasContent( cid ) )
        {
            auto node = lru_cid_cache_.get( cid );
            if ( node )
            {
                logger_->trace( "Block retrieved from LRU cache: CID {}", cid.toString().value() );
                return node;
            }
        }
        return outcome::failure( Error::CID_NOT_FOUND );
    }

    outcome::result<void> GraphsyncDAGSyncer::DeleteCIDBlock( const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( cache_mutex_ );
        if ( lru_cid_cache_.remove( cid ) )
        {
            logger_->debug( "Block removed from LRU cache: CID {}", cid.toString().value() );
            return outcome::success();
        }
        return outcome::failure( Error::CID_NOT_FOUND );
    }

    void GraphsyncDAGSyncer::AddToBlackList( const PeerId &peer ) const
    {
        std::lock_guard<std::mutex> lock( blacklist_mutex_ );

        uint64_t now        = GetCurrentTimestamp();
        auto [it, inserted] = blacklist_.emplace( peer.toMultihash(), BlacklistEntry( now, 1 ) );

        if ( !inserted )
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

    uint64_t GraphsyncDAGSyncer::getBackoffTimeout( uint64_t failures, bool ever_connected ) const
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
        else
        {
            // For never-connected peers:
            // - Start with 10 seconds
            // - Cap at 1800 seconds (30 minutes)
            uint64_t base_seconds = 10;
            uint64_t max_seconds  = 1800;

            // Calculate exponential backoff
            uint64_t timeout = base_seconds * ( 1ULL << failures );
            return std::min( timeout, max_seconds );
        }
    }

    bool GraphsyncDAGSyncer::IsOnBlackList( const PeerId &peer ) const
    {
        std::lock_guard<std::mutex> lock( blacklist_mutex_ );
        bool                        ret = false;
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
        std::lock_guard<std::mutex> lock( blacklist_mutex_ );
        auto                        it = blacklist_.find( peer.toMultihash() );
        if ( it != blacklist_.end() )
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
        std::lock_guard<std::mutex> lock( mutex_ );
        return dagService_.getNode( cid );
    }

    outcome::result<void> GraphsyncDAGSyncer::AddNodeToMerkleDAG(
        std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return dagService_.addNode( std::move( node ) );
    }

    outcome::result<void> GraphsyncDAGSyncer::RemoveNodeFromMerkleDAG( const CID &cid )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return dagService_.removeNode( cid );
    }

    outcome::result<size_t> GraphsyncDAGSyncer::SelectFromMerkleDAG(
        gsl::span<const uint8_t>                                                     root_cid,
        gsl::span<const uint8_t>                                                     selector,
        std::function<bool( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )> handler ) const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return dagService_.select( root_cid, selector, handler );
    }

    outcome::result<GraphsyncDAGSyncer::PeerEntry> GraphsyncDAGSyncer::GetRoute( const CID &cid ) const
    {
        PeerKey peerKey;
        // First find the peer key in the routing table
        {
            std::lock_guard<std::mutex> lock( routing_mutex_ );
            auto                        it = routing_.find( cid );
            if ( it == routing_.end() )
            {
                return outcome::failure( Error::ROUTE_NOT_FOUND );
            }

            peerKey = it->second;
        }
        // Now get the actual peer entry from the registry
        return GetPeerById( peerKey );
    }

    void GraphsyncDAGSyncer::EraseRoutesFromPeerID( const PeerId &peer ) const
    {
        // First find the peer key in the peer index
        PeerKey peerKeyToRemove;
        bool    peerFound = false;

        {
            std::lock_guard<std::mutex> registry_lock( registry_mutex_ );
            auto                        it = peer_index_.find( peer );
            if ( it == peer_index_.end() )
            {
                // Peer not found in registry, nothing to erase
                return;
            }
            peerKeyToRemove = it->second;
            peerFound       = true;
        }

        if ( peerFound )
        {
            // Remove all routes that point to this peer
            std::lock_guard<std::mutex> routing_lock( routing_mutex_ );
            for ( auto it = routing_.begin(); it != routing_.end(); )
            {
                if ( it->second == peerKeyToRemove )
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
    }

    void GraphsyncDAGSyncer::EraseRoute( const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( routing_mutex_ );
        auto                        it = routing_.find( cid );
        if ( it != routing_.end() )
        {
            routing_.erase( it );
        }
    }

    uint64_t GraphsyncDAGSyncer::GetCurrentTimestamp() const
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    void GraphsyncDAGSyncer::LRUCIDCache::init( const CID &cid )
    {
        // Check if the item already exists
        auto it = cache_map_.find( cid );
        if ( it != cache_map_.end() )
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
        // Check if the item already exists
        auto it = cache_map_.find( cid );
        if ( it != cache_map_.end() )
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
        auto it = cache_map_.find( cid );
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
        auto it = cache_map_.find( cid );
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
        return cache_map_.find( cid ) != cache_map_.end();
    }

    // Check if CID exists and has content
    bool GraphsyncDAGSyncer::LRUCIDCache::hasContent( const CID &cid ) const
    {
        auto it = cache_map_.find( cid );
        return it != cache_map_.end() && it->second.first != nullptr;
    }
}
