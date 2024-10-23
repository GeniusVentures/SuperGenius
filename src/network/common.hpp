#ifndef SUPERGENIUS_NETWORK_COMMON_HPP
#define SUPERGENIUS_NETWORK_COMMON_HPP

#include <libp2p/peer/protocol.hpp>

namespace sgns::network {
  const libp2p::peer::Protocol kSyncProtocol = "/supergenius-sync/1.0.0";
  const libp2p::peer::Protocol kGossipProtocol = "/supergenius-gossip/1.0.0";
}

#endif
