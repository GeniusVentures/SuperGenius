

#ifndef SUPERGENIUS_CONSENSUS_CONSENSUS_PRODUCTION_GOSSIPER_HPP
#define SUPERGENIUS_CONSENSUS_CONSENSUS_PRODUCTION_GOSSIPER_HPP

#include <functional>

#include <outcome/outcome.hpp>
#include "network/types/block_announce.hpp"

namespace sgns::consensus {
  /**
   * Sends messages, related to Production, over the Gossip protocol
   */
  struct ProductionGossiper {
    virtual ~ProductionGossiper() = default;

    /**
     * Send BlockAnnounce message
     * @param announce to be sent
     */
    virtual void blockAnnounce(const network::BlockAnnounce &announce) = 0;
  };
}  // namespace sgns::consensus

#endif  // SUPERGENIUS_CONSENSUS_CONSENSUS_PRODUCTION_GOSSIPER_HPP
