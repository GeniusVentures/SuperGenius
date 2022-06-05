#ifndef SUPERGENIUS_IPFS_BLOCK_ACCESSOR_HPP
#define SUPERGENIUS_IPFS_BLOCK_ACCESSOR_HPP

#include <ipfs/ipfs_dht.hpp>
#include <bitswap.hpp>

namespace sgns::ipfs
{
class BlockAccessor
{
public:
    BlockAccessor(std::shared_ptr<sgns::ipfs::IpfsDHT> dht, std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap);

    void SetBlockRequestTimeout(std::chrono::system_clock::duration blockRequestTimeout);
    void RequestBlock(const sgns::ipfs_bitswap::CID& cid, std::function<void(std::optional<std::string>)> cb);

private:
    void OnProvidersFound(
        sgns::ipfs_bitswap::CID cid,
        std::chrono::time_point<std::chrono::system_clock> requestTime,
        std::function<void(std::optional<std::string>)> cb,
        libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> providersRes);

    void SubmitRequest(
        sgns::ipfs_bitswap::CID cid,
        std::chrono::time_point<std::chrono::system_clock> requestTime,
        std::function<void(std::optional<std::string>)> cb);

    void LogProviders(const std::vector<libp2p::peer::PeerInfo>& providers);

    std::shared_ptr<sgns::ipfs::IpfsDHT> m_dht;
    std::shared_ptr<sgns::ipfs_bitswap::Bitswap> m_bitswap;

    std::chrono::system_clock::duration m_blockRequestTimeout;

    sgns::base::Logger m_logger = sgns::base::createLogger("IpfsBlockAccessor");
};
}

#endif // SUPERGENIUS_IPFS_BLOCK_ACCESSOR_HPP
