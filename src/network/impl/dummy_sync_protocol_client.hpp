

#ifndef SUPERGENIUS_SRCDUMMY_SYNC_PROTOCOL_CLIENT
#define SUPERGENIUS_SRCDUMMY_SYNC_PROTOCOL_CLIENT

#include "network/sync_protocol_client.hpp"

#include "base/logger.hpp"

namespace sgns::network {

  class DummySyncProtocolClient
      : public network::SyncProtocolClient,
        public std::enable_shared_from_this<DummySyncProtocolClient> {
    using BlocksResponse = network::BlocksResponse;
    using BlocksRequest = network::BlocksRequest;

   public:
    DummySyncProtocolClient();

    void requestBlocks(
        const BlocksRequest &request,
        std::function<void(outcome::result<BlocksResponse>)> cb) override;

   private:
    base::Logger log_;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_SRCDUMMY_SYNC_PROTOCOL_CLIENT
