
#include <boost/asio.hpp>

#include <libp2p/host/host.hpp>

#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include "outcome/outcome.hpp"

/// runs event loop for max_milliseconds or until SIGINT or SIGTERM
void runEventLoop(const std::shared_ptr<boost::asio::io_context>& io,
    size_t max_milliseconds);

// Creates per-node objects using libp2p hos injector
std::pair<std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Graphsync>, std::shared_ptr<libp2p::Host>>
createNodeObjects(std::shared_ptr<boost::asio::io_context> io);

std::pair<std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Graphsync>, std::shared_ptr<libp2p::Host>>
createNodeObjects(std::shared_ptr<boost::asio::io_context> io, libp2p::crypto::KeyPair keyPair);

inline std::ostream& operator << (std::ostream& os, const sgns::CID& cid) {
    os << cid.toString().value();
    return os;
}

// MerkleDAG bridge interface for test purposes
class TestDataService : public sgns::ipfs_lite::ipfs::graphsync::MerkleDagBridge {
public:
    using Storage = std::map<sgns::CID, sgns::common::Buffer>;

    TestDataService& addData(const std::string& s) {
        insertNode(data_, s);
        return *this;
    }

    TestDataService& addExpected(const std::string& s) {
        insertNode(expected_, s);
        return *this;
    }

    const Storage& getData() const {
        return data_;
    }

    const Storage& getExpected() const {
        return expected_;
    }

    const Storage& getReceived() const {
        return received_;
    }

    // places into data_, returns true if expected
    bool onDataBlock(sgns::CID cid, sgns::common::Buffer data);

private:
    static void insertNode(Storage& dst, const std::string& data_str);

    outcome::result<size_t> select(
        const sgns::CID& cid,
        gsl::span<const uint8_t> selector,
        std::function<bool(const sgns::CID& cid, const sgns::common::Buffer& data)> handler)
        const override;

    Storage data_;
    Storage expected_;
    Storage received_;
};
