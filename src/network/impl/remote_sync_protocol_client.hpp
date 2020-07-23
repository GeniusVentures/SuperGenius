

#ifndef SUPERGENIUS_SRC_REMOTE_SYNC_PROTOCOL_CLIENT
#define SUPERGENIUS_SRC_REMOTE_SYNC_PROTOCOL_CLIENT

#include "network/sync_protocol_client.hpp"

#include <libp2p/host/host.hpp>
#include <libp2p/peer/peer_info.hpp>

#include "base/logger.hpp"

namespace sgns::network {

  class RemoteSyncProtocolClient
      : public network::SyncProtocolClient,
        public std::enable_shared_from_this<RemoteSyncProtocolClient> {
   public:
    RemoteSyncProtocolClient(libp2p::Host &host,
                             libp2p::peer::PeerInfo peer_info);

    void requestBlocks(
        const network::BlocksRequest &request,
        std::function<void(outcome::result<network::BlocksResponse>)> cb)
        override;

   private:
    libp2p::Host &host_;
    const libp2p::peer::PeerInfo peer_info_;
    base::Logger log_;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_SRC_REMOTE_SYNC_PROTOCOL_CLIENT
