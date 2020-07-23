#ifndef SUPERGENIUS_CONFIGURATION_STORAGE_HPP
#define SUPERGENIUS_CONFIGURATION_STORAGE_HPP

#include <libp2p/peer/peer_info.hpp>
#include "application/genesis_raw_config.hpp"
#include "crypto/ed25519_types.hpp"
#include "crypto/sr25519_types.hpp"
#include "network/types/peer_list.hpp"
#include "primitives/block.hpp"

namespace sgns::application {

  /**
   * Stores configuration of a sgns application and provides convenience
   * methods for accessing config parameters
   */
  class ConfigurationStorage {
   public:
    virtual ~ConfigurationStorage() = default;

    /**
     * @return genesis block of the chain
     */
    virtual GenesisRawConfig getGenesis() const = 0;

    /**
     * Return ids of peer nodes of the current node
     */
    virtual network::PeerList getBootNodes() const = 0;
  };

}  // namespace sgns::application

#endif  // SUPERGENIUS_CONFIGURATION_STORAGE_HPP
