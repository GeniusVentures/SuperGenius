#include "ipfs_block_accessor.hpp"

////////////////////////////////////////////////////////////////////////////////
namespace
{
class BlockRequestor
{
public:
    BlockRequestor(
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap,
        const sgns::ipfs_bitswap::CID& cid,
        const std::vector<libp2p::peer::PeerInfo>& providers);

    void RequestBlock(std::function<void(std::optional<std::string>)> cb);
private:
    void RequestBlock(
        std::vector<libp2p::peer::PeerInfo>::iterator addressBeginIt,
        std::vector<libp2p::peer::PeerInfo>::iterator addressEndIt,
        std::function<void(std::optional<std::string>)> cd);

    std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap_;
    sgns::ipfs_bitswap::CID cid_;
    std::vector<libp2p::peer::PeerInfo> providers_;
};

BlockRequestor::BlockRequestor(
    std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap,
    const sgns::ipfs_bitswap::CID& cid,
    const std::vector<libp2p::peer::PeerInfo>& providers)
    : bitswap_(std::move(bitswap))
    , cid_(cid)
    , providers_(providers)
{
}

void BlockRequestor::RequestBlock(std::function<void(std::optional<std::string>)> cb)
{
    RequestBlock(providers_.begin(), providers_.end(), std::move(cb));
}

void BlockRequestor::RequestBlock(
    std::vector<libp2p::peer::PeerInfo>::iterator addressBeginIt,
    std::vector<libp2p::peer::PeerInfo>::iterator addressEndIt,
    std::function<void(std::optional<std::string>)> cb)
{
    if (addressBeginIt != addressEndIt)
    {
        bitswap_->RequestBlock(*addressBeginIt, cid_,
            [this, addressBeginIt, addressEndIt, cb(std::move(cb))](libp2p::outcome::result<std::string> data)
        {
            if (data)
            {
                cb(data.value());
            }
            else
            {
                RequestBlock(addressBeginIt + 1, addressEndIt, std::move(cb));
            }
        });
    }
    else
    {
        cb(std::nullopt);
    }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////
namespace sgns::ipfs
{
BlockAccessor::BlockAccessor(std::shared_ptr<sgns::ipfs::IpfsDHT> dht, std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap)
    : m_dht(dht)
    , m_bitswap(bitswap)
    , m_blockRequestTimeout(std::chrono::seconds(10))
{
}

void BlockAccessor::SetBlockRequestTimeout(std::chrono::system_clock::duration blockRequestTimeout)
{
    m_blockRequestTimeout = blockRequestTimeout;
}

void BlockAccessor::RequestBlock(const sgns::ipfs_bitswap::CID& cid, std::function<void(std::optional<std::string>)> cb)
{
    auto t = std::chrono::system_clock::now();
    m_dht->FindProviders(
        cid, 
        std::bind(&BlockAccessor::OnProvidersFound, this, cid, t, std::move(cb), std::placeholders::_1));
}

void BlockAccessor::OnProvidersFound(
    sgns::ipfs_bitswap::CID cid,
    std::chrono::time_point<std::chrono::system_clock> requestTime,
    std::function<void(std::optional<std::string>)> cb,
    libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> providersRes)
{
    if (!providersRes)
    {
        m_logger->error("CANNOT_FIND_PROVIDERS: {}", providersRes.error().message());
    }

    auto& providers = providersRes.value();
    if (m_logger->should_log(spdlog::level::debug))
    {
        LogProviders(providers);
    }

    if (!providers.empty())
    {
        auto blockRequestor = std::make_shared<BlockRequestor>(m_bitswap, cid, providers);
        blockRequestor->RequestBlock(
            [this, cid, requestTime, cb(std::move(cb)), blockRequestor](std::optional<std::string> data) {
            if (data)
            {
                this->m_logger->debug("BITSWAP_DATA_RECEIVED: {} bytes", data.value().size());
                cb(data);
            }
            else
            {
                this->m_logger->debug("CANNOT_GET_REQUESTED_DATA");
                this->SubmitRequest(cid, requestTime, std::move(cb));
            }
            });
    }
    else
    {
        // Empty providers list received
        this->SubmitRequest(cid, requestTime, std::move(cb));
    }
}

void BlockAccessor::SubmitRequest(
    sgns::ipfs_bitswap::CID cid,
    std::chrono::time_point<std::chrono::system_clock> requestTime,
    std::function<void(std::optional<std::string>)> cb)
{
    auto t = std::chrono::system_clock::now();
    if (t - requestTime > m_blockRequestTimeout)
    {
        m_logger->debug("BLOCK_REQUEST_TIMEOUT");
        cb(std::nullopt);
    }
    else
    {
        m_dht->FindProviders(
            cid,
            std::bind(&BlockAccessor::OnProvidersFound, this, cid, requestTime, std::move(cb), std::placeholders::_1));
    }
}

void BlockAccessor::LogProviders(const std::vector<libp2p::peer::PeerInfo>& providers)
{
    std::stringstream ss;
    for (const auto& provider : providers)
    {
        ss << provider.id.toBase58();
        ss << ", addresses: [";
        std::string sep = "";
        for (auto& address : provider.addresses)
        {
            ss << sep << address.getStringAddress();
            sep = ",";
        }
        ss << "]";
        ss << std::endl;
    }

    m_logger->debug("PROVIDERS_LIST: [{}]", ss.str());
}

} // namespace sgns::ipfs
