

#include "network/impl/dummy_sync_protocol_client.hpp"

#include "base/visitor.hpp"

namespace sgns::network {
  DummySyncProtocolClient::DummySyncProtocolClient()
      : log_(base::createLogger("DummySyncProtocolClient")) {}

  void DummySyncProtocolClient::requestBlocks(
      const BlocksRequest &request,
      std::function<void(outcome::result<BlocksResponse>)> cb) {
    visit_in_place(
        request.from,
        [this](primitives::BlockNumber from) {
          log_->debug("Skipped self-requesting blocks: from {}", from);
        },
        [this, &request](const primitives::BlockHash &from) {
          if (not request.to) {
            log_->debug("Skipped self-requesting blocks: from {}",
                        from.toHex());
          } else {
            log_->debug("Skipped self-requesting blocks: from {}, to {}",
                        from.toHex(),
                        request.to->toHex());
          }
        });
  }

}  // namespace sgns::network
