#ifndef SUPERGENIUS_CONFIGURATION_STORAGE_HPP
#define SUPERGENIUS_CONFIGURATION_STORAGE_HPP

#include <set>

#include <libp2p/peer/peer_info.hpp>

#include "application/genesis_raw_config.hpp"
#include "network/types/peer_list.hpp"
#include "singleton/IComponent.hpp"
#include "primitives/common.hpp"
namespace sgns::application {

  /**
   * Stores configuration of a sgns application and provides convenience
   * methods for accessing config parameters
   */
  class ConfigurationStorage : public IComponent {
   public:
       ~ConfigurationStorage() override = default;

       [[nodiscard]] virtual const std::string &name() const = 0;

       [[nodiscard]] virtual const std::string &id() const = 0;

       [[nodiscard]] virtual const std::string &chainType() const = 0;

       /// Return ids of peer nodes of the current node
       [[nodiscard]] virtual network::PeerList getBootNodes() const = 0;

       [[nodiscard]] virtual const std::vector<std::pair<std::string, size_t>> &telemetryEndpoints() const = 0;

       [[nodiscard]] virtual const std::string &protocolId() const = 0;

       [[nodiscard]] virtual const std::map<std::string, std::string> &properties() const = 0;

       [[nodiscard]] virtual boost::optional<std::reference_wrapper<const std::string>> getProperty(
           const std::string &property ) const = 0;

       [[nodiscard]] virtual const std::set<primitives::BlockHash> &forkBlocks() const = 0;

       [[nodiscard]] virtual const std::set<primitives::BlockHash> &badBlocks() const = 0;

       [[nodiscard]] virtual boost::optional<std::string> verificationEngine() const = 0;

       /**
     * @return genesis block of the chain
     */
       [[nodiscard]] virtual GenesisRawConfig getGenesis() const = 0;
  };

}  // namespace sgns::application

#endif  // SUPERGENIUS_CONFIGURATION_STORAGE_HPP
