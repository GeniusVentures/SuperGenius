#include "graphsync_acceptance_common.hpp"

#include <boost/di/extension/scopes/shared.hpp>

#include <libp2p/injector/host_injector.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>

#include <ipfs_lite/ipfs/graphsync/impl/graphsync_impl.hpp>
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include "outcome/outcome.hpp"
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>

void runEventLoop(const std::shared_ptr<boost::asio::io_context>& io,
    size_t max_milliseconds) {
    boost::asio::signal_set signals(*io, SIGINT, SIGTERM);

    // io->run() can exit if we're not waiting for anything
    signals.async_wait(
        [&io](const boost::system::error_code&, int) { io->stop(); });

    if (max_milliseconds > 0) {
        io->run_for(std::chrono::milliseconds(max_milliseconds));
    }
    else {
        io->run();
    }
}

std::pair<std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Graphsync>, std::shared_ptr<libp2p::Host>>
createNodeObjects(std::shared_ptr<boost::asio::io_context> io)
{
    // [boost::di::override] allows for creating multiple hosts for testing
    // purposes
    auto injector = 
        libp2p::injector::makeHostInjector<boost::di::extension::shared_config>(
            boost::di::bind<boost::asio::io_context>.to(io)[boost::di::override]);


    std::pair<std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Graphsync>, std::shared_ptr<libp2p::Host>>
        objects;
    objects.second = injector.template create<std::shared_ptr<libp2p::Host>>();
    auto scheduler = std::make_shared<libp2p::protocol::AsioScheduler>(
        io, libp2p::protocol::SchedulerConfig{});
    auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( objects.second, scheduler );
    auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
    objects.first =
        std::make_shared<sgns::ipfs_lite::ipfs::graphsync::GraphsyncImpl>(objects.second, std::move(scheduler), graphsyncnetwork, generator );
    return objects;
}

std::pair<std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Graphsync>, std::shared_ptr<libp2p::Host>>
createNodeObjects(std::shared_ptr<boost::asio::io_context> io, libp2p::crypto::KeyPair keyPair)
{
    // [boost::di::override] allows for creating multiple hosts for testing
    // purposes
    auto injector = 
        libp2p::injector::makeHostInjector<boost::di::extension::shared_config>(
            boost::di::bind<boost::asio::io_context>.to(io)[boost::di::override],
            boost::di::bind<libp2p::crypto::KeyPair>.to(std::move(keyPair))[boost::di::override]);


    std::pair<std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Graphsync>, std::shared_ptr<libp2p::Host>>
        objects;
    objects.second = injector.template create<std::shared_ptr<libp2p::Host>>();
    auto scheduler = std::make_shared<libp2p::protocol::AsioScheduler>(
        io, libp2p::protocol::SchedulerConfig{});
    auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( objects.second, scheduler );
    auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
    objects.first = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::GraphsyncImpl>( objects.second,
                                                                                       std::move( scheduler ),
                                                                                       graphsyncnetwork,
                                                                                       generator );
    return objects;
}

bool TestDataService::onDataBlock(sgns::CID cid, sgns::common::Buffer data) {
    bool expected = false;
    auto it = expected_.find(cid);
    if (it != expected_.end() && it->second == data) {
        expected = (received_.count(cid) == 0);
    }
    received_[cid] = std::move(data);
    return expected;
}

void TestDataService::insertNode(TestDataService::Storage& dst,
    const std::string& data_str) {
    using NodeImpl = sgns::ipfs_lite::ipld::IPLDNodeImpl;
    auto node = NodeImpl::createFromString(data_str);
    dst[node->getCID()] = node->getRawBytes();
}

outcome::result<size_t> TestDataService::select(
    const sgns::CID& cid,
    gsl::span<const uint8_t> selector,
    std::function<bool(const sgns::CID& cid, const sgns::common::Buffer& data)> handler)
    const {
    auto it = data_.find(cid);
    if (it != data_.end()) {
        handler(it->first, it->second);
        return 1;
    }
    return 0;
}
