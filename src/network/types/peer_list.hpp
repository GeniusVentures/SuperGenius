

#ifndef SUPERGENIUS_SRC_NETWORK_TYPES_PEER_LIST_HPP
#define SUPERGENIUS_SRC_NETWORK_TYPES_PEER_LIST_HPP

#include <libp2p/peer/peer_info.hpp>

namespace sgns::network {

  struct PeerList {
    std::vector<libp2p::peer::PeerInfo> peers;

    bool operator==(const PeerList &rhs) const {
      return peers == rhs.peers;
    }
    bool operator!=(const PeerList &rhs) const {
      return ! operator==(rhs);
    }
  };

}  // namespace sgns::network

#endif  // SUPERGENIUS_SRC_NETWORK_TYPES_PEER_LIST_HPP
