#include "ping_session.hpp"
#include <ipfs/ipfs_dht.hpp>
#include <bitswap.hpp>

#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/injector/kademlia_injector.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/protocol/identify/identify.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/di/extension/scopes/shared.hpp>

#include <iostream>

namespace
{
    // cmd line options
    struct Options
    {
        // optional remote peer to connect to
        std::optional<std::string> remote;
    };

    boost::optional<Options> parseCommandLine(int argc, char** argv) {
        namespace po = boost::program_options;
        try
        {
            Options o;
            std::string remote;

            po::options_description desc("ipfs service options");
            desc.add_options()("help,h", "print usage message")
                ("remote,r", po::value(&remote), "remote service multiaddress to connect to")
                ;

            po::variables_map vm;
            po::store(parse_command_line(argc, argv, desc), vm);
            po::notify(vm);

            if (vm.count("help") != 0 || argc == 1)
            {
                std::cerr << desc << "\n";
                return boost::none;
            }

            if (!remote.empty())
            {
                o.remote = remote;
            }

            return o;
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
        return boost::none;
    }

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

    class BlockAccessor
    {
    public:
        BlockAccessor(std::shared_ptr<sgns::ipfs::IpfsDHT> dht, std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap);
        void RequestBlock(const sgns::ipfs_bitswap::CID& cid, std::function<void(std::optional<std::string>)> cb);

    private:
        void OnProvidersFound(
            sgns::ipfs_bitswap::CID cid,
            std::function<void(std::optional<std::string>)> cb,
            libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> providersRes);

        void LogProviders(const std::vector<libp2p::peer::PeerInfo>& providers);

        std::shared_ptr<sgns::ipfs::IpfsDHT> m_dht;
        std::shared_ptr<sgns::ipfs_bitswap::Bitswap> m_bitswap;

        sgns::base::Logger m_logger = sgns::base::createLogger("BlockAccessor");
    };

    BlockAccessor::BlockAccessor(std::shared_ptr<sgns::ipfs::IpfsDHT> dht, std::shared_ptr<sgns::ipfs_bitswap::Bitswap> bitswap)
        : m_dht(dht)
        , m_bitswap(bitswap)
    {
    }

    void BlockAccessor::RequestBlock(const sgns::ipfs_bitswap::CID& cid, std::function<void(std::optional<std::string>)> cb)
    {
        m_dht->FindProviders(cid, std::bind(&BlockAccessor::OnProvidersFound, this, cid, cb, std::placeholders::_1));
    }

    void BlockAccessor::OnProvidersFound(
        sgns::ipfs_bitswap::CID cid,
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
            blockRequestor->RequestBlock([this, cid, cb, blockRequestor](std::optional<std::string> data) {
                if (data)
                {
                    this->m_logger->debug("BITSWAP_DATA_RECEIVED: {} bytes", data.value().size());
                    cb(data);
                }
                else
                {
                    this->m_logger->debug("CANNOT_GET_REQUESTED_DATA");
                    this->m_dht->FindProviders(
                        cid, std::bind(&BlockAccessor::OnProvidersFound, this, cid, cb, std::placeholders::_1));
                }
                });
        }
        else
        {
            // Empty providers list received
            this->m_dht->FindProviders(
                cid, std::bind(&BlockAccessor::OnProvidersFound, this, cid, cb, std::placeholders::_1));
        }
    };

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
}

int main(int argc, char* argv[])
{
    //auto options = parseCommandLine(argc, argv);
    //if (!options)
    //{
    //    return EXIT_FAILURE;
    //}

    const std::string logger_config(R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: false
    groups:
      - name: main
        sink: console
        level: error
        children:
          - name: libp2p
          - name: kademlia
    # ----------------
    )");

    // prepare log system
    auto logging_system = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config));
    logging_system->configure();

    libp2p::log::setLoggingSystem(logging_system);

    auto loggerIdentifyMsgProcessor = libp2p::log::createLogger("IdentifyMsgProcessor");
    loggerIdentifyMsgProcessor->setLevel(soralog::Level::ERROR_);

    auto loggerBitswap = sgns::ipfs_bitswap::createLogger("Bitswap");
    loggerBitswap->set_level(spdlog::level::debug);

    auto loggerBlockAccessor = sgns::base::createLogger("BlockAccessor");
    loggerBlockAccessor->set_level(spdlog::level::debug);

    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    libp2p::protocol::kademlia::Config kademlia_config;
    kademlia_config.randomWalk.enabled = true;
    kademlia_config.randomWalk.interval = std::chrono::seconds(300);
    kademlia_config.requestConcurency = 20;

    auto injector = libp2p::injector::makeHostInjector(
        // libp2p::injector::useKeyPair(kp), // Use predefined keypair
        libp2p::injector::makeKademliaInjector(
            libp2p::injector::useKademliaConfig(kademlia_config)));

    try
    {
        auto ma = libp2p::multi::Multiaddress::create("/ip4/127.0.0.1/tcp/40000").value();  // NOLINT

        auto io = injector.create<std::shared_ptr<boost::asio::io_context>>();
        auto host = injector.create<std::shared_ptr<libp2p::Host>>();

        auto kademlia =
            injector
            .create<std::shared_ptr<libp2p::protocol::kademlia::Kademlia>>();

        std::vector<std::string> bootstrapAddresses = {
            "/ip4/202.61.244.123/tcp/4001/p2p/QmUikGKv3RysyCYZxNoGWjf8Y1qPjhrp8NeV4fYyNyqcDQ",

            // bootstrap peers list is obtained using ipfs config show command
            "/dnsaddr/bootstrap.libp2p.io/p2p/QmcZf59bWwK5XFi76CZX8cbJ4BhTzzA3gU1ZjYZcYW3dwt",
            "/ip4/104.131.131.82/tcp/4001/p2p/QmaCpDMGvV2BGHeYERUEnRQAwe3N8SzbUtfsmvsqQLuvuJ",
            // @note /quic address is not supported by libp2p implementation
            //"/ip4/104.131.131.82/udp/4001/quic/p2p/QmaCpDMGvV2BGHeYERUEnRQAwe3N8SzbUtfsmvsqQLuvuJ",
            "/dnsaddr/bootstrap.libp2p.io/p2p/QmNnooDu7bfjPFoTZYxMNLWUQJyrVwtbZg5gBMjTezGAJN",
            "/dnsaddr/bootstrap.libp2p.io/p2p/QmQCU2EcMqAqQPR2i9bChDtGNJchTbq5TbXJJ16u19uLTa",
            "/dnsaddr/bootstrap.libp2p.io/p2p/QmbLHAnMoJPWSCR5Zhtx6BHJX9KiKNN6tpvbUcqanj75Nb",
        };

        // Hello world
        //auto cid = libp2p::multi::ContentIdentifierCodec::fromString("QmWATWQ7fVPP2EFGu71UkfnqhYXDYH566qy47CnJDgvs8u").value();
        auto cid = libp2p::multi::ContentIdentifierCodec::fromString("QmYCvbfNbCwFR45HiNP45rwJgvatpiW38D961L5qAhUM5Y").value();

        // Peer id specified manually only for a single bitswap block request call
        // The peer id can be found in findProviders response.
        //auto peer_id = libp2p::peer::PeerId::fromBase58("QmRXP6S7qwSH4vjSrZeJUGT68ww8rQVhoFWU5Kp7UkVkPN").value();
        auto peer_id = libp2p::peer::PeerId::fromBase58("QmSrq3jnqGAja4z96Jq9SMQFJ8TzbRAgrMLi1sTR6Ane6W").value();

        // Peers addresses:
        // The adressed are specified manually only for a single bitswap block request call
        auto peer_address = 
            libp2p::multi::Multiaddress::create(
                //"/ip4/10.0.65.121/tcp/4001/p2p/QmRXP6S7qwSH4vjSrZeJUGT68ww8rQVhoFWU5Kp7UkVkPN"
                //"/ip4/54.89.142.24/tcp/4001/p2p/QmRXP6S7qwSH4vjSrZeJUGT68ww8rQVhoFWU5Kp7UkVkPN"
                "/ip4/54.89.112.218/tcp/4001/p2p/QmSrq3jnqGAja4z96Jq9SMQFJ8TzbRAgrMLi1sTR6Ane6W"
            ).value();


        auto identityManager = injector.create<std::shared_ptr<libp2p::peer::IdentityManager>>();
        auto keyMarshaller = injector.create<std::shared_ptr<libp2p::crypto::marshaller::KeyMarshaller>>();

        auto identifyMessageProcessor = std::make_shared<libp2p::protocol::IdentifyMessageProcessor>(
            *host, host->getNetwork().getConnectionManager(), *identityManager, keyMarshaller);
        auto identify = std::make_shared<libp2p::protocol::Identify>(*host, identifyMessageProcessor, host->getBus());

        auto pingSession = std::make_shared<PingSession>(io, host);
        pingSession->Init();

        auto dht = std::make_shared<sgns::ipfs::IpfsDHT>(kademlia, bootstrapAddresses);

        // Bitswap setup
        auto bitswap = std::make_shared<sgns::ipfs_bitswap::Bitswap>(*host, host->getBus(), io);

        auto blockAccessor = std::make_shared<BlockAccessor>(dht, bitswap);

        std::shared_ptr<BlockRequestor> blockRequestor;

        ////////////////////////////////////////////////////////////////////////////////
        io->post([&] {
            auto listen = host->listen(ma);
            if (!listen) {
                std::cerr << "Cannot listen address " << ma.getStringAddress().data()
                    << ". Error: " << listen.error().message() << std::endl;
                std::exit(EXIT_FAILURE);
            }

            identify->start();
            bitswap->start();
            host->start();

            /*
            bitswap->RequestBlock({ peer_id, {peer_address} }, cid,
                [](libp2p::outcome::result<std::string> data)
                {
                    if (data)
                    {
                        std::cout << "Bitswap data received: " << data.value() << std::endl;
                    }
                    else
                    {
                        std::cout << "CANNOT FIND REQUESTED DATA: " << data.error().message() << std::endl;
                    }
                });
            /*/
            dht->Start();

            blockAccessor->RequestBlock(cid, [](std::optional<std::string> data){
                if (data)
                {
                    std::cout << "Bitswap data received :" << data.value() << std::endl;
                }
                else
                {
                    std::cout << "Cannot get requested data" << std::endl;
                }
                });

            //*/

        }); // io->post()

        boost::asio::signal_set signals(*io, SIGINT, SIGTERM);
        signals.async_wait(
            [&io](const boost::system::error_code&, int) { io->stop(); });
        io->run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

