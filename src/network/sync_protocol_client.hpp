

#ifndef SUPERGENIUS_SYNC_PROTOCOL_CLIENT_HPP
#define SUPERGENIUS_SYNC_PROTOCOL_CLIENT_HPP

#include <functional>

#include "outcome/outcome.hpp"
#include "network/types/blocks_request.hpp"
#include "network/types/blocks_response.hpp"

namespace sgns::network {
  /**
   * Client part of Sync protocol
   */
  struct SyncProtocolClient {
    virtual ~SyncProtocolClient() = default;

    /**
     * Make a blocks request
     * @param request to be made
     * @param cb to be called, when a response or error arrives
     */
    virtual void requestBlocks(
        const BlocksRequest &request,
        std::function<void(outcome::result<BlocksResponse>)> cb) = 0;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_SYNC_PROTOCOL_CLIENT_HPP
