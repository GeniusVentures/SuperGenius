

#ifndef SUPERGENIUS_SYNC_PROTOCOL_OBSERVER_HPP
#define SUPERGENIUS_SYNC_PROTOCOL_OBSERVER_HPP

#include <outcome/outcome.hpp>
#include "network/types/blocks_request.hpp"
#include "network/types/blocks_response.hpp"
#include "integration/IComponent.hpp"

namespace sgns::network {
  /**
   * Reactive part of Sync protocol
   */
  struct SyncProtocolObserver : public IComponent {
    virtual ~SyncProtocolObserver() = default;

    /**
     * Process a blocks request
     * @param request to be processed
     * @return blocks request or error
     */
    virtual outcome::result<BlocksResponse> onBlocksRequest(
        const BlocksRequest &request) const = 0;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_SYNC_PROTOCOL_OBSERVER_HPP
