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
        std::lock_guard<std::mutex> lock( routing_mutex_ );
        routing_.insert( std::make_pair( cid, std::make_tuple( peer, address ) ) );
    }

    outcome::result<void> GraphsyncDAGSyncer::addNode( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )
    {
        return AddNodeToMerkleDAG( std::move( node ) );
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::getNode( const CID &cid ) const
    {
        auto node = GetNodeFromMerkleDAG( cid );

        if ( !node.has_error() )
        {
            return node;
        }

        OUTCOME_TRY( ( auto &&, route_info ), GetRoute( cid ) );

        auto [peerID, address] = route_info;

        OUTCOME_TRY( ( auto &&, subscription ), RequestNode( peerID, address, cid ) );

        auto start_time = std::chrono::steady_clock::now();
        while ( std::chrono::steady_clock::now() - start_time < std::chrono::seconds( 20 ) )
        {
            auto result = GetNodeFromMerkleDAG( cid ); // Call the internal GrabCIDBlock
            if ( result )
            {
                return result; // Return the block if successfully grabbed
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // Retry after a short delay
        }
        BlackListPeer( cid, peerID );
        logger_->error( "Timeout while waiting for node fetch: {}, adding peer {} to blacklist ",
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
        auto getNodeResult = GetNodeFromMerkleDAG( cid );
        return getNodeResult.has_value();
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

        if ( hb.value() )
        {
            logger_->error( "We already had this {}", cid.toString().value() );
            return;
        }

        auto node = ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes( buffer );
        if ( node.has_failure() )
        {
            logger_->error( "Cannot create node from received block data for CID {}", cid.toString().value() );
            return;
        }

        auto res = AddNodeToMerkleDAG( node.value() );
        if ( !res )
        {
            logger_->error( "Error adding node to dagservice {}", res.error().message() );
        }
        std::stringstream sslinks;
        for ( const auto &link : node.value()->getLinks() )
        {
            sslinks << "[";
            sslinks << link.get().getCID().toString().value();
            sslinks << link.get().getName();
            sslinks << "], ";
        }
        logger_->error( "Node added to dagService. CID: {}, links: [{}]",
                        node.value()->getCID().toString().value(),
                        sslinks.str() );

        // @todo performance optimization is required

        logger_->debug( "Request found {}", cid.toString().value() );

        auto maybe_router_info = GetRoute( cid );
        if ( maybe_router_info )
        {
            auto [peerID, address] = maybe_router_info.value();
            logger_->debug( "Seeing if peer {} has links to AddRoute", peerID.toBase58() );
            if ( !IsOnBlackList( peerID ) )
            {
                for ( auto link : node.value()->getLinks() )
                {
                    auto linkhb = HasBlock( link.get().getCID() );
                    if ( linkhb.has_value() && !linkhb.value() )
                    {
                        logger_->trace( "Adding route for peer {} and CID {}",
                                        peerID.toBase58(),
                                        link.get().getCID().toString().value() );
                        AddRoute( link.get().getCID(), peerID, address );
                    }
                }
            }
            else
            {
                //I don't think it should ever land here
                logger_->error( "Peer {} was blacklisted", peerID.toBase58() );
            }
        }
    }

    void GraphsyncDAGSyncer::AddCIDBlock( const CID &cid, const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &block )
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        received_blocks_[cid] = block; // Insert or update the block associated with the CID
    }

    outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::GrabCIDBlock( const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        auto                        it = received_blocks_.find( cid );
        if ( it != received_blocks_.end() )
        {
            return it->second;
        }
        return outcome::failure( boost::system::error_code{} ); // Return an appropriate failure result
    }

    outcome::result<void> GraphsyncDAGSyncer::DeleteCIDBlock( const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        auto                        it = received_blocks_.find( cid );
        if ( it != received_blocks_.end() )
        {
            received_blocks_.erase( it );
            return outcome::success();
        }
        return outcome::failure( boost::system::error_code{} ); // Return an appropriate failure result
    }

    void GraphsyncDAGSyncer::AddToBlackList( const PeerId &peer ) const
    {
        std::lock_guard<std::mutex> lock( blacklist_mutex_ );

        uint64_t now       = GetCurrentTimestamp();
        auto [it, inserted] = blacklist_.emplace( peer, std::make_pair( now, 1 ) );

        if ( !inserted )
        {
            if ( now - it->second.first > TIMEOUT_SECONDS )
            {
                it->second.second = 1;
            }
            else if ( it->second.second < MAX_FAILURES )
            {
                it->second.second++;
            }
            it->second.first = now;
        }
    }

    bool GraphsyncDAGSyncer::IsOnBlackList( const PeerId &peer ) const
    {
        std::lock_guard<std::mutex> lock( blacklist_mutex_ );
        bool                        ret = false;
        do
        {
            auto it = blacklist_.find( peer );
            if ( it == blacklist_.end() )
            {
                break;
            }
            uint64_t now = GetCurrentTimestamp();
            if ( now - it->second.first > TIMEOUT_SECONDS )
            {
                it->second.second = 0;
                break;
            }

            if ( it->second.second > MAX_FAILURES )
            {
                ret = true;
                break;
            }
            it->second.second = 0;
        } while ( 0 );

        return ret;
    }

    outcome::result<void> GraphsyncDAGSyncer::BlackListPeer( const CID &cid, const PeerId &peer ) const
    {
        AddToBlackList( peer );
        EraseRoute( cid );
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

    outcome::result<GraphsyncDAGSyncer::RouterInfo> GraphsyncDAGSyncer::GetRoute( const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( routing_mutex_ );
        auto                        it = routing_.find( cid );
        if ( it == routing_.end() )
        {
            return outcome::failure( Error::ROUTE_NOT_FOUND );
        }
        return it->second;
    }

    void GraphsyncDAGSyncer::EraseRoute( const CID &cid ) const
    {
        std::lock_guard<std::mutex> lock( routing_mutex_ );

        auto it = routing_.find( cid );
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
}
