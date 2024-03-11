

#ifndef SUPERGENIUS_SRC_NETWORK_TYPES_OWNPEERINFO
#define SUPERGENIUS_SRC_NETWORK_TYPES_OWNPEERINFO

#include <libp2p/peer/peer_info.hpp>
#include "integration/IComponent.hpp"

namespace sgns::network {

  struct OwnPeerInfo : public libp2p::peer::PeerInfo, public IComponent {
    OwnPeerInfo(libp2p::peer::PeerId peer_id,
                std::vector<libp2p::multi::Multiaddress> addresses)
        : PeerInfo{/*.id =*/ std::move(peer_id),
                   /*.addresses =*/ std::move(addresses)} {}
    std::string GetName() override
    {
      return "OwnPeerInfo";
    }
  };

}  // namespace sgns::network

#endif  // SUPERGENIUS_SRC_NETWORK_TYPES_OWNPEERINFO
