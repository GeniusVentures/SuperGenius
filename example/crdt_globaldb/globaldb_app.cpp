#include <iterator>
#include <iostream>
#include <algorithm>
#include <boost/program_options.hpp>
#include <boost/algorithm/string/erase.hpp>
#include <boost/asio/io_context.hpp>
#include <storage/rocksdb/rocksdb.hpp>
#include <boost/filesystem.hpp>
#include <boost/random.hpp>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

using Buffer = sgns::base::Buffer;
using HierarchicalKey = sgns::crdt::HierarchicalKey;
using SubscriptionData = libp2p::protocol::gossip::Gossip::SubscriptionData;

namespace po = boost::program_options;

/** Display key and value added to CRDT datastore
*/
void PutHook(const std::string& k, const Buffer& v, const sgns::base::Logger& logger);

/** Display key removed from CRDT datastore
*/
void DeleteHook(const std::string& k, const sgns::base::Logger& logger);

int main(int argc, char** argv)
{
  std::string strDatabasePath;
  int pubsubListeningPort = 0;
  bool daemonMode = false;
  bool echoProtocol = false;
  std::string multiAddress;
  po::options_description desc("Input arguments:");
  try
  {
    desc.add_options()
      ("help,h", "print help")
      ("daemon,d", "Running in daemon mode")
      ("echo,e", "Using echo protocol in daemon mode")
      ("databasePath,db", po::value<std::string>(&strDatabasePath)->default_value("CRDT.Datastore"),
        "Path to CRDT datastore")
      ("port, p", po::value<int>(&pubsubListeningPort)->default_value(33123), "Port number")
      ("pubsub-address", po::value<std::string>(&multiAddress), "Pubsub server address")
      ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("daemon"))
    {
      daemonMode = true;
    }

    if (vm.count("echo"))
    {
      echoProtocol = true;
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

  const std::string logger_config(R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: true
    groups:
      - name: globaldb_app
        sink: console
        level: info
        children:
          - name: libp2p
          - name: Gossip
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

  auto io = std::make_shared<boost::asio::io_context>();
  auto logger = sgns::base::createLogger("globaldb");

  std::string broadcastChannel = "globaldb-example";

  std::vector<std::string> pubsubBootstrapPeers;
  if (!multiAddress.empty())
  {
      pubsubBootstrapPeers.push_back(multiAddress);
  }

  auto pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
      sgns::crdt::KeyPairFileStorage(strDatabasePath + "/pubsub").GetKeyPair().value());
  pubsub->Start(pubsubListeningPort, pubsubBootstrapPeers);

  auto globalDB = std::make_shared<sgns::crdt::GlobalDB>(
      io, strDatabasePath,
      std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>(pubsub, broadcastChannel));


  auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
  crdtOptions->logger = logger;
  // Bind PutHook function pointer for notification purposes
  crdtOptions->putHookFunc = std::bind(&PutHook, std::placeholders::_1, std::placeholders::_2, logger);
  // Bind DeleteHook function pointer for notification purposes
  crdtOptions->deleteHookFunc = std::bind(&DeleteHook, std::placeholders::_1, logger);

  globalDB->Init(crdtOptions);

  std::ostringstream streamDisplayDetails;
  // @todo fix commented output
  //streamDisplayDetails << "\n\n\nPeer ID: " << peerID.toBase58() << std::endl;
  //streamDisplayDetails << "Listen address: " << pubsub->GetLocalAddress() << std::endl;
  //streamDisplayDetails << "DAG syncer address: " << listen_to.getStringAddress() << std::endl;
  streamDisplayDetails << "Broadcast channel: " << broadcastChannel << std::endl;
  //streamDisplayDetails << "Data folder: " << strDatabasePathAbsolute << std::endl;
  streamDisplayDetails << std::endl;
  streamDisplayDetails << "Ready!" << std::endl;
  streamDisplayDetails << std::endl;
  streamDisplayDetails << "Commands: " << std::endl;
  streamDisplayDetails << std::endl;
  streamDisplayDetails << "> list               -> list items in the store" << std::endl;
  streamDisplayDetails << "> get <key>          -> get value for a key" << std::endl;
  streamDisplayDetails << "> put <key> <value>  -> store value on a key" << std::endl;
  streamDisplayDetails << "> exit               -> quit" << std::endl;
  streamDisplayDetails << std::endl;
  std::cout << streamDisplayDetails.str();

  if (daemonMode)
  {
    std::cout << "Running in daemon mode" << (echoProtocol ? " with echo protocol\n" : "\n") << std::endl;
    boost::asio::signal_set signals(*io, SIGINT, SIGTERM);
    signals.async_wait(
      [&io](const boost::system::error_code&, int) { io->stop(); });

    // run event loop
    io->run();
  }
  else
  {
    std::cout << "> ";
    std::string command;
    while (std::getline(std::cin, command))
    {
      if (command.empty())
      {
        std::cout << "> ";
      }
      else if (command == "exit" || command == "quit")
      {
        break;
      }
      else if (command == "list")
      {
        auto queryResult = globalDB->QueryKeyValues("");
        if (queryResult.has_failure())
        {
          std::cout << "Unable list keys from CRDT datastore" << std::endl;
        }
        else
        {
          for (const auto& element : queryResult.value())
          {
            // key name: /crdt/s/k/<key>/v
            auto strKey = std::string(element.first.toString());
            std::cout << "[" << strKey << "] -> " << element.second.toString() << std::endl;
          }
        }
      }
      else if (command.rfind("get") == 0)
      {
        std::string key = command.substr(3);
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
        if (key.empty())
        {
          std::cout << "get <key>" << std::endl;
        }
        else
        {
          auto getKeyResult = globalDB->Get(HierarchicalKey(key));
          if (getKeyResult.has_failure())
          {
            std::cout << "Unable to find key in CRDT datastore: " << key << std::endl;
          }
          else
          {
            std::cout << "[" << key << "] -> " << getKeyResult.value().toString() << std::endl;
          }
        }
      }
      else if (command.rfind("put") == 0)
      {
        size_t pos = 0;
        std::vector<std::string> commandList;
        std::string commandToParse = command;
        while ((pos = commandToParse.find(" ")) != std::string::npos)
        {
          commandList.push_back(commandToParse.substr(0, pos));
          commandToParse.erase(0, pos + 1);
        }
        if (!commandToParse.empty())
        {
          commandList.push_back(commandToParse);
        }

        if (commandList.size() < 3)
        {
          std::cout << "put <key> <value>" << std::endl;
        }
        else
        {
          auto key = commandList[1];
          std::string value = commandList[2];
          if (commandList.size() > 3)
          {
            for (size_t i = 3; i < commandList.size(); ++i)
            {
              value += " " + commandList[i];
            }
          }
          Buffer valueBuffer;
          valueBuffer.put(value);
          auto setKeyResult = globalDB->Put(HierarchicalKey(key), valueBuffer);
          if (setKeyResult.has_failure())
          {
            std::cout << "Unable to put key-value to CRDT datastore: " << key << " " << value << std::endl;
          }
        }
      }
      std::cout << "> ";
    }
  }

  return EXIT_SUCCESS;
}


void PutHook(const std::string& k, const Buffer& v, const sgns::base::Logger& logger)
{
  if (logger != nullptr)
  {
    std::string key = k;
    if (!key.empty() && key[0] == '/')
    {
      key.erase(0, 1);
    }
    logger->info("CRDT datastore: Added [" + key + "] -> " + std::string(v.toString()));
  }
}

void DeleteHook(const std::string& k, const sgns::base::Logger& logger)
{
  if (logger != nullptr)
  {
    std::string key = k;
    if (!key.empty() && key[0] == '/')
    {
      key.erase(0, 1);
    }
    logger->info("CRDT datastore: Removed [" + key + "]");
  }
}

