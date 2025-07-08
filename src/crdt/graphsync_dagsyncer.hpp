#ifndef SUPERGENIUS_GRAPHSYNC_DAGSYNCER_HPP
#define SUPERGENIUS_GRAPHSYNC_DAGSYNCER_HPP

#include "crdt/dagsyncer.hpp"
#include "base/logger.hpp"

#include <ipfs_lite/ipfs/graphsync/impl/merkledag_bridge_impl.hpp>
#include <ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.hpp>

#include <libp2p/host/host.hpp>

#include <memory>
#include <chrono>
#include <unordered_map>

namespace sgns::crdt
{
    /**
   * @brief A DAGSyncer is an abstraction to an IPLD-based p2p storage layer.
   * A DAGSyncer is a DAGService with the ability to publish new ipld nodes
   * to the network, and retrieving others from it.
   */
    class GraphsyncDAGSyncer : public DAGSyncer, public std::enable_shared_from_this<GraphsyncDAGSyncer>
    {
    public:
        using IpfsDatastore       = ipfs_lite::ipfs::IpfsDatastore;
        using Graphsync           = ipfs_lite::ipfs::graphsync::Graphsync;
        using ResponseMetadata    = ipfs_lite::ipfs::graphsync::ResponseMetadata;
        using Extension           = ipfs_lite::ipfs::graphsync::Extension;
        using MerkleDagBridgeImpl = ipfs_lite::ipfs::graphsync::MerkleDagBridgeImpl;
        using ResponseStatusCode  = ipfs_lite::ipfs::graphsync::ResponseStatusCode;
        using Multiaddress        = libp2p::multi::Multiaddress;
        using Multihash           = libp2p::multi::Multihash;
        using PeerId              = libp2p::peer::PeerId;
        using Subscription        = libp2p::protocol::Subscription;
        using Logger              = base::Logger;
        using BlockCallback       = Graphsync::BlockCallback;

        // New peer registry types
        using PeerKey      = size_t; // Unique identifier for a peer in our registry
        using PeerEntry    = std::pair<PeerId, std::vector<Multiaddress>>;
        using RouteMapType = std::map<CID, PeerKey>; // Maps CIDs to peer registry keys

        enum class Error
        {
            CID_NOT_FOUND = 0,      ///< The requested CID wasn't found
            ROUTE_NOT_FOUND,        ///< Route for the CID wasn't found
            PEER_BLACKLISTED,       ///< The peer that has the CID is blacklisted
            TIMED_OUT,              ///< The request has timed out
            DAGSYNCHER_NOT_STARTED, ///< Start wasn't called, or StopSync was called
            GRAPHSYNC_IS_NULL,      ///< Graphsync member is nullptr
            HOST_IS_NULL,           ///< Graphsync member is nullptr
        };

        struct BlacklistEntry
        {
            uint64_t timestamp;        // When the peer was last updated
            uint64_t failures;         // Number of consecutive failures
            bool     ever_connected;   // Flag indicating if we've ever successfully connected
            uint64_t backoff_attempts; // Count of backoff attempts (for exponential calculation)

            BlacklistEntry( uint64_t time, uint64_t count, bool connected = false ) :
                timestamp( time ), failures( count ), ever_connected( connected ), backoff_attempts( 0 )
            {
            }
        };

        GraphsyncDAGSyncer( std::shared_ptr<IpfsDatastore> service,
                            std::shared_ptr<Graphsync>     graphsync,
                            std::shared_ptr<libp2p::Host>  host );

        ~GraphsyncDAGSyncer()
        {
            logger_->debug( "~GraphsyncDAGSyncer CALLED" );
            is_stopped_ = true;
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            graphsync_->stop();
        }

        outcome::result<void> Listen( const Multiaddress &listen_to );

        /** Starts instance and subscribes to blocks */
        outcome::result<void> StartSync();

        void AddRoute( const CID &cid, const PeerId &peer, std::vector<Multiaddress> &address );

        // DAGService interface implementation
        outcome::result<void> addNode( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node ) override;

        outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> getNode( const CID &cid ) const override;

        outcome::result<void> removeNode( const CID &cid ) override;

        outcome::result<size_t> select(
            gsl::span<const uint8_t>                                                     root_cid,
            gsl::span<const uint8_t>                                                     selector,
            std::function<bool( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )> handler ) const override;

        outcome::result<std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf>> fetchGraph( const CID &cid ) const override;

        outcome::result<std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf>> fetchGraphOnDepth(
            const CID &cid,
            uint64_t   depth ) const override;

        outcome::result<bool>                                       HasBlock( const CID &cid ) const override;
        outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GetNodeWithoutRequest(
            const CID &cid ) const override;
        std::pair<DAGSyncer::LinkInfoSet, DAGSyncer::LinkInfoSet> TraverseCIDsLinks(
            const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &node,
            std::string                                       link_name            = "",
            DAGSyncer::LinkInfoSet                            visited_links        = {},
            bool                                              skip_if_visited_root = false,
            int                                               max_depth            = 100 ) const override;
        /* Returns peer ID */
        outcome::result<PeerId> GetId() const;

        outcome::result<libp2p::peer::PeerInfo> GetPeerInfo() const;

        void AddToBlackList( const PeerId &peer ) const;
        bool IsOnBlackList( const PeerId &peer ) const;

        void                  InitCIDBlock( const CID &cid ) override;
        bool                  IsCIDInCache( const CID &cid ) const override;
        outcome::result<void> DeleteCIDBlock( const CID &cid ) override;

        void Stop() override;

    protected:
        static constexpr uint64_t TIMEOUT_SECONDS = 1200;
        static constexpr uint64_t MAX_FAILURES    = 3;

        outcome::result<std::shared_ptr<ipfs_lite::ipfs::graphsync::Subscription>> RequestNode(
            const PeerId                              &peer,
            boost::optional<std::vector<Multiaddress>> address,
            const CID                                 &root_cid ) const;

        void RequestProgressCallback( ResponseStatusCode code, const std::vector<Extension> &extensions ) const;
        void BlockReceivedCallback( const CID &cid, sgns::common::Buffer buffer );

        bool             started_ = false;
        std::vector<CID> unexpected_blocks;

        /** Stops instance */
        void StopSync();

        ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl dagService_;
        std::shared_ptr<Graphsync>                       graphsync_;

        std::shared_ptr<libp2p::Host> host_;

        Logger logger_ = base::createLogger( "GraphsyncDAGSyncer" );

        // keeping subscriptions alive, otherwise they cancel themselves
        // class Subscription have non-copyable constructor and operator, so it can not be used in std::vector
        // std::vector<Subscription> requests_;
        mutable std::map<CID,
                         std::tuple<std::shared_ptr<Subscription>,
                                    std::shared_ptr<std::promise<std::shared_ptr<ipfs_lite::ipld::IPLDNode>>>>>
            requests_;

        // New peer registry - stores unique peers and their addresses
        mutable std::vector<PeerEntry>    peer_registry_;
        mutable std::map<PeerId, PeerKey> peer_index_; // Maps PeerIds to registry indices
        mutable std::mutex                registry_mutex_;

        // Routing table that references peers in the registry
        mutable RouteMapType routing_;
        mutable std::mutex   routing_mutex_;

        mutable std::map<Multihash, BlacklistEntry>                       blacklist_;
        mutable std::mutex                                                blacklist_mutex_;
        mutable std::mutex                                                mutex_;
        mutable std::map<CID, std::shared_ptr<ipfs_lite::ipld::IPLDNode>> received_blocks_;

        // Helper methods for the peer registry
        PeerKey                    RegisterPeer( const PeerId &peer, const std::vector<Multiaddress> &address ) const;
        outcome::result<PeerEntry> GetPeerById( PeerKey id ) const;

        bool AddCIDBlock( const CID &cid, const std::shared_ptr<ipfs_lite::ipld::IPLDNode> &block );
        outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GrabCIDBlock( const CID &cid ) const;

        outcome::result<void> BlackListPeer( const PeerId &peer ) const;

        outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GetNodeFromMerkleDAG( const CID &cid ) const;

        outcome::result<void>   AddNodeToMerkleDAG( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node );
        outcome::result<void>   RemoveNodeFromMerkleDAG( const CID &cid );
        outcome::result<size_t> SelectFromMerkleDAG(
            gsl::span<const uint8_t>                                                     root_cid,
            gsl::span<const uint8_t>                                                     selector,
            std::function<bool( std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node )> handler ) const;

        outcome::result<PeerEntry> GetRoute( const CID &cid ) const;
        void                       EraseRoutesFromPeerID( const PeerId &peer ) const;
        void                       EraseRoute( const CID &cid ) const;

        uint64_t GetCurrentTimestamp() const;

        /// Using exponential backoff for both cases but with different base values
        uint64_t getBackoffTimeout( uint64_t attempts, bool ever_connected ) const;

        /// record successful connections
        void RecordSuccessfulConnection( const PeerId &peer ) const;

    private:
        struct LRUCIDCache
        {
            // Maximum number of blocks to store in the cache
            static constexpr size_t MAX_CACHE_SIZE = 250;

            // Main storage: CID -> (Node, list iterator)
            std::map<CID, std::pair<std::shared_ptr<ipfs_lite::ipld::IPLDNode>, std::list<CID>::iterator>> cache_map_;

            // LRU list: most recently used at front, least recently used at back
            std::list<CID> lru_list_;

            void init( const CID &cid );
            bool add( const CID &cid, std::shared_ptr<ipfs_lite::ipld::IPLDNode> node );
            std::shared_ptr<ipfs_lite::ipld::IPLDNode> get( const CID &cid );
            bool                                       remove( const CID &cid );
            bool                                       contains( const CID &cid ) const;

            bool hasContent( const CID &cid ) const;

            size_t size() const
            {
                return cache_map_.size();
            }
        };

        mutable LRUCIDCache lru_cid_cache_;
        mutable std::mutex  cache_mutex_;
        std::atomic<bool>   is_stopped_{ false };
    };

}

/**
 * @brief       Macro for declaring error handling in the IBasicProof class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, GraphsyncDAGSyncer::Error );

#endif
