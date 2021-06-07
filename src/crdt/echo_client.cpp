#include <chrono>
#include <iostream>
#include <boost/asio/io_context.hpp>
#include <boost/program_options.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>

int main(int argc, char *argv[]) 
{
  using libp2p::crypto::Key;
  using libp2p::crypto::KeyPair;
  using libp2p::crypto::PrivateKey;
  using libp2p::crypto::PublicKey;
  using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
  using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;
  namespace po = boost::program_options;

  int portNumber = 0;
  int runDurationSec = 0;
  int numberOfClients = 0;
  std::string multiAddress;
  std::string topicName;
  po::options_description desc("Input arguments:");
  try
  {
    desc.add_options()
      ("help,h", "print help")
      ("port", po::value<int>(&portNumber)->default_value(33123), "Port number")
      ("duration", po::value<int>(&runDurationSec)->default_value(30), "Run duration in seconds")
      ("address", po::value<std::string>(&multiAddress), "Server address")
      ("topic", po::value<std::string>(&topicName)->default_value("globaldb-example"), "Topic name")
      ("clients", po::value<int>(&numberOfClients)->default_value(1), "Number of clients")
      ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (multiAddress.empty())
    {
      std::cout << "Please, provide an address of the server" << "\n\n";
      std::cout << desc << "\n";
      return EXIT_FAILURE;
    }

    if (vm.count("help"))
    {
      std::cout << desc << "\n";
      return EXIT_FAILURE;
    }
  }
  catch (std::exception& e)
  {
    std::cerr << "Error parsing arguments: " << e.what() << "\n";
    std::cout << desc << "\n";
    return EXIT_FAILURE;
  }

  auto run_duration = std::chrono::seconds(runDurationSec);

  // Create pubsub gossip node
  auto pubsub = std::make_shared<GossipPubSub>();
  pubsub->Start(portNumber, { multiAddress });

  auto pubsTopic = pubsub->Subscribe(topicName, [&](boost::optional<const GossipPubSub::Message&> message)
    {
      if (message)
      {
        std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
        std::cout << message << std::endl;
        //pubsub->Publish(topicName, std::vector<uint8_t>(message.begin(), message.end()));
      }
    });

  pubsTopic.wait();

  // create a default Host via an injector
  auto injector = libp2p::injector::makeHostInjector<BOOST_DI_CFG>();

  // create io_context - in fact, thing, which allows us to execute async
// operations
  auto context = injector.create<std::shared_ptr<boost::asio::io_context>>();

  // run the IO context
  context->run_for(run_duration);

  // Wait for message transmitting
  std::this_thread::sleep_for(run_duration);
}
