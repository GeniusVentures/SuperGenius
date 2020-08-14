
#include "network/impl/remote_sync_protocol_client.hpp"

#include "base/visitor.hpp"
#include "network/common.hpp"
#include "network/helpers/scale_message_read_writer.hpp"
#include "network/rpc.hpp"

namespace sgns::network {
  RemoteSyncProtocolClient::RemoteSyncProtocolClient(
      libp2p::Host &host, libp2p::peer::PeerInfo peer_info)
      : host_{host},
        peer_info_{std::move(peer_info)},
        log_(base::createLogger("RemoteSyncProtocolClient")) {}

  void RemoteSyncProtocolClient::requestBlocks(
      const network::BlocksRequest &request,
      std::function<void(outcome::result<network::BlocksResponse>)> cb) {
    visit_in_place(
        request.from,
        [this](primitives::BlockNumber from) {
          log_->debug("Requesting blocks: from {}", from);
        },
        [this, &request](const primitives::BlockHash &from) {
          if (! request.to) {
            log_->debug("Requesting blocks: from {}", from.toHex());
          } else {
            log_->debug("Requesting blocks: from {}, to {}",
                        from.toHex(),
                        request.to->toHex());
          }
        });
    network::RPC<network::ScaleMessageReadWriter>::
        write<network::BlocksRequest, network::BlocksResponse>(
            host_, peer_info_, network::kSyncProtocol, request, std::move(cb));
  }
}  // namespace sgns::network
