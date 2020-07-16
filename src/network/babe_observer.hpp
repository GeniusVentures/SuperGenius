

#ifndef SUPERGENIUS_CONSENSUS_PRODUCTION_OBSERVER_HPP
#define SUPERGENIUS_CONSENSUS_PRODUCTION_OBSERVER_HPP

#include "network/types/block_announce.hpp"

namespace sgns::network {
  /**
   * Reacts to messages, related to Production
   */
  struct ProductionObserver {
    virtual ~ProductionObserver() = default;

    /**
     * Triggered when a BlockAnnounce message arrives
     * @param announce - arrived message
     */
    virtual void onBlockAnnounce(const BlockAnnounce &announce) = 0;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_CONSENSUS_PRODUCTION_OBSERVER_HPP
