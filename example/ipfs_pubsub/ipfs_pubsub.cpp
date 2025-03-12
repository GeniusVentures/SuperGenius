#include "ipfs_pubsub/gossip_pubsub.hpp"
#include <iostream>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include <libp2p/host/basic_host.hpp>

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: gossip_pubsub_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

int main(int argc, char* argv[])
{
    // prepare log system
    auto logging_system = std::make_shared<soralog::LoggingSystem>(
        std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config));
    logging_system->configure();



    libp2p::log::setLoggingSystem(logging_system);
    libp2p::log::setLevelOfGroup("gossip_pubsub_test", soralog::Level::ERROR_);
    libp2p::protocol::gossip::Config config;
    config.echo_forward_mode = true;
    std::vector<std::string> receivedMessages;

    auto pubs = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST/pubs_dapp").GetKeyPair().value());
    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
        sgns::crdt::KeyPairFileStorage("CRDT.Datastore.TEST2/pubs_processor").GetKeyPair().value());
    pubs2->Start(40002, { pubs->GetLocalAddress(), "",});
    pubs->Start(40001, { pubs2->GetLocalAddress(), "",});
    std::string topic = "SuperGenius";
    auto pubsTopic1 = pubs->Subscribe(topic, [&](boost::optional<const GossipPubSub::Message&> msg)
        {
            if (msg)
            {
                std::string message(reinterpret_cast<const char*>(msg->data.data()), msg->data.size());
                std::cout << "Pubs 1 Got message: " << message << std::endl;
                std::cout << "Choose pubsub to send message (1 for pubs, 2 for pubs2, 3 to quit): ";
                receivedMessages.push_back(std::move(message));
            }
        });
    auto pubsTopic2 = pubs2->Subscribe(topic, [&](boost::optional<const GossipPubSub::Message&> msg)
        {
            if (msg)
            {
                std::string message(reinterpret_cast<const char*>(msg->data.data()), msg->data.size());
                std::cout << "Pubs 2 Got message: " << message << std::endl;
                std::cout << "Choose pubsub to send message (1 for pubs, 2 for pubs2, 3 to quit): ";
                receivedMessages.push_back(std::move(message));
            }
        });
    //std::string message("Hello Pubsub");
    //pubs.Publish(topic, std::vector<uint8_t>(message.begin(), message.end()));
    auto publishMessage = [&](GossipPubSub& pubsub) {
        std::string message;
        std::cout << "Enter message to send: ";
        std::getline(std::cin, message);
        std::vector<uint8_t> messageData(message.begin(), message.end());
        pubsub.Publish(topic, messageData);
        };
    std::cout << "Choose pubsub to send message (1 for pubs, 2 for pubs2, 3 to quit): ";
    while (true) {
        //std::cout << "Choose pubsub to send message (1 for pubs, 2 for pubs2, 3 to quit): ";
        int choice;
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (choice == 1) {
            publishMessage(*pubs);
        }
        else if (choice == 2) {
            publishMessage(*pubs2);
        }
        else if (choice == 3) {
            break;
        }
        else {
            std::cout << "Invalid choice. Please enter 1, 2, or 3." << std::endl;
        }
    }
}