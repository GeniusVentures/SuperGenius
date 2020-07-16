

#ifndef SUPERGENIUS_SYNC_CLIENTS_SET_HPP
#define SUPERGENIUS_SYNC_CLIENTS_SET_HPP

#include <memory>
#include <unordered_set>

#include "network/sync_protocol_client.hpp"

namespace sgns::network {
  /**
   * Keeps all known Sync clients
   */
  struct SyncClientsSet {
    std::vector<std::shared_ptr<SyncProtocolClient>> clients;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_SYNC_CLIENTS_SET_HPP
