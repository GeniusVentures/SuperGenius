#include "crdt/graphsync_dagsyncer.hpp"

#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include <memory>
#include <utility>

namespace sgns::crdt
{
    GraphsyncDAGSyncer::GraphsyncDAGSyncer( std::shared_ptr<IpfsDatastore> service,
                                            std::shared_ptr<Graphsync>     graphsync,
                                            std::shared_ptr<libp2p::Host>  host ) :
        dagService_( std::move( service ) ), graphsync_( std::move( graphsync ) ), host_( std::move( host ) )
    {
    }

outcome::result<void> GraphsyncDAGSyncer::Listen(const Multiaddress& listen_to)
{
    if (this->host_ == nullptr)
    {
        return outcome::failure(boost::system::error_code{});
    }

    auto listen_res = host_->listen(listen_to);
    if (listen_res.has_failure())
    {
        /*logger->trace("Cannot listen to multiaddress {}, {}",
            listen_to.getStringAddress(),
            listen_res.error().message());*/
        return listen_res.error();
    }
    auto startResult = this->StartSync();
    if (startResult.has_failure())
    {
        return startResult.error();
    }

    return outcome::success();
}

outcome::result<std::future<std::shared_ptr<ipfs_lite::ipld::IPLDNode>>> GraphsyncDAGSyncer::RequestNode(
    const PeerId& peer, boost::optional<std::vector<Multiaddress>> address, const CID& root_cid) const
{
    if(!started_)
    {
        return outcome::failure(boost::system::error_code{});
    }

    if (graphsync_ == nullptr)
    {
        return outcome::failure(boost::system::error_code{});
    }
    auto result = std::make_shared<std::promise<std::shared_ptr<ipfs_lite::ipld::IPLDNode>>>();
    std::vector<Extension> extensions;
    ResponseMetadata response_metadata{};
    Extension response_metadata_extension = ipfs_lite::ipfs::graphsync::encodeResponseMetadata( response_metadata );
    extensions.push_back(response_metadata_extension);

    std::vector<CID> cids;
    Extension        do_not_send_cids_extension = ipfs_lite::ipfs::graphsync::encodeDontSendCids( cids );
    extensions.push_back(do_not_send_cids_extension);
    auto subscription = graphsync_->makeRequest(
        peer,
        std::move( address ),
        root_cid,
        {},
        extensions,
        std::bind( &GraphsyncDAGSyncer::RequestProgressCallback, this, std::placeholders::_1, std::placeholders::_2 ) );

    // keeping subscriptions alive, otherwise they cancel themselves
    logger_->debug("Requesting Node {} ", root_cid.toString().value());
    requests_.insert( std::make_pair(
        root_cid, std::make_tuple( std::make_shared<Subscription>( std::move( subscription ) ), result ) ) );

    return result->get_future();
}

void GraphsyncDAGSyncer::AddRoute(const CID& cid, const PeerId& peer, std::vector<Multiaddress>& address)
{
    routing_.insert(std::make_pair(cid, std::make_tuple(peer, address)));
}

outcome::result<void> GraphsyncDAGSyncer::addNode(std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node)
{
    return dagService_.addNode(std::move(node));
}

outcome::result<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> GraphsyncDAGSyncer::getNode(const CID& cid) const
{
    auto node = dagService_.getNode(cid);
    if (node.has_error())
    {
        auto it = routing_.find(cid);
        if (it != routing_.end())
        {
            auto res = RequestNode(std::get<0>(it->second), std::get<1>(it->second), it->first);
            if (res.has_failure())
            {
                return res.as_failure();
            }
            //res.value().wait();
            if (res.value().wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
                node = res.value().get();
            } else {
                logger_->error("Timeout while waiting for node fetch: {}", cid.toString().value());
                return outcome::failure(boost::system::errc::timed_out);
            }
            //node = res.value().get();
        }
    }
    return node;
}

outcome::result<void> GraphsyncDAGSyncer::removeNode(const CID& cid)
{
    return dagService_.removeNode(cid);
}

outcome::result<size_t> GraphsyncDAGSyncer::select(
    gsl::span<const uint8_t> root_cid,
    gsl::span<const uint8_t> selector,
    std::function<bool(std::shared_ptr<const ipfs_lite::ipld::IPLDNode> node)> handler) const
{
    return dagService_.select(root_cid, selector, handler);
}

outcome::result<std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf>> GraphsyncDAGSyncer::fetchGraph(const CID& cid) const
{
    return ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl::fetchGraphOnDepth(
        std::bind(&MerkleDagService::getNode, this, std::placeholders::_1), cid, {});
}

outcome::result<std::shared_ptr<ipfs_lite::ipfs::merkledag::Leaf>> GraphsyncDAGSyncer::fetchGraphOnDepth(
    const CID& cid, uint64_t depth) const
{
    return ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl::fetchGraphOnDepth(
        std::bind(&MerkleDagService::getNode, this, std::placeholders::_1), cid, depth);
}

outcome::result<bool> GraphsyncDAGSyncer::HasBlock(const CID& cid) const
{
    auto getNodeResult = dagService_.getNode(cid);
    return getNodeResult.has_value();
}

outcome::result<bool> GraphsyncDAGSyncer::StartSync()
{
    if (!started_)
    {
        if (graphsync_ == nullptr)
        {
            return outcome::failure(boost::system::error_code{});
        }

        auto dagService = std::make_shared<MerkleDagBridgeImpl>(shared_from_this());
        if (dagService == nullptr)
        {
            return outcome::failure(boost::system::error_code{});
        }

        BlockCallback blockCallback = std::bind(
            &GraphsyncDAGSyncer::BlockReceivedCallback, this, std::placeholders::_1, std::placeholders::_2);
        graphsync_->start(dagService, blockCallback);

        if (host_ == nullptr)
        {
            return outcome::failure(boost::system::error_code{});
        }
        host_->start();

        started_ = true;
    }
    return started_;
}

void GraphsyncDAGSyncer::StopSync()
{
    if (graphsync_ != nullptr)
    {
        graphsync_->stop();
    }
    if (host_ != nullptr)
    {
        host_->stop();
    }
    started_ = false;
}

outcome::result<GraphsyncDAGSyncer::PeerId> GraphsyncDAGSyncer::GetId() const
{
    if (host_ != nullptr)
    {
        return host_->getId();
    }
    return outcome::failure(boost::system::error_code{});
}

outcome::result<libp2p::peer::PeerInfo> GraphsyncDAGSyncer::GetPeerInfo() const
{
    if (host_ != nullptr)
    {
        return host_->getPeerInfo();
    }
    return outcome::failure(boost::system::error_code{});
}

namespace
{
    std::string formatExtensions(const std::vector<GraphsyncDAGSyncer::Extension>& extensions)
    {
        std::string s;
        for (const auto& item : extensions) {
            s += fmt::format(
                "({}: 0x{}) ", item.name, common::Buffer(item.data).toHex());
        }
        return s;
    };
}

void GraphsyncDAGSyncer::RequestProgressCallback(
    ResponseStatusCode code, const std::vector<Extension>& extensions) const
{
    logger_->trace("request progress: code={}, extensions={}", statusCodeToString(code), formatExtensions(extensions));
}

void GraphsyncDAGSyncer::BlockReceivedCallback( const CID &cid, sgns::common::Buffer buffer )
{
    logger_->trace("Block received: cid={}, extensions={}", cid.toString().value(), buffer.toHex());
    auto hb = HasBlock(cid);

    if (hb.has_failure())
    {
        logger_->debug("HasBlock failed: {}, cid: {}" , hb.error().message().c_str(), cid.toString().value());
    }
    else
    {
        logger_->debug("HasBlock: {}, cid: {}", hb.value(), cid.toString().value());
    }

    if (hb.has_value() && !hb.value())
    {
        auto node = ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes(buffer);
        if (!node.has_failure())
        {
            auto res = dagService_.addNode(node.value());
            if (!res)
            {
                logger_->error("Error adding node to dagservice {}", res.error().message());
            }
            std::stringstream sslinks;
            for (const auto& link : node.value()->getLinks())
            {
                sslinks << "[";
                sslinks << link.get().getCID().toString().value();
                sslinks << link.get().getName();
                sslinks << "], ";
            }
            logger_->error("Node added to dagService. CID: {}, links: [{}]",
                node.value()->getCID().toString().value(),
                sslinks.str());

            // @todo performance optimization is required
            auto itSubscription = requests_.find(cid);
            if (itSubscription != requests_.end())
            {
                logger_->debug("Request found {}", cid.toString().value());

                auto it = routing_.find(cid);
                if (it != routing_.end())
                {
                    for (auto link : node.value()->getLinks())
                    {
                        auto linkhb = HasBlock(link.get().getCID());
                        if (linkhb.has_value() && !linkhb.value())
                        {
                            AddRoute(link.get().getCID(), std::get<0>(it->second), std::get<1>(it->second));
                        }
                    }
                }

                // @todo check if multiple requests of the same CID works as expected.
                std::get<1>(itSubscription->second)->set_value(node.value());
            }
            else
            {
                logger_->debug("Unexpected block received {}", cid.toString().value());
            }
        }
        else
        {
            logger_->error("Cannot create node from received block data for CID {}", cid.toString().value());
        }
    }
    else {
        logger_->error("We already had this {}", cid.toString().value());
    }
}
}
