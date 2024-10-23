

#ifndef SUPERGENIUS_VERIFICATION_VERIFICATION_PRODUCTION_GOSSIPER_HPP
#define SUPERGENIUS_VERIFICATION_VERIFICATION_PRODUCTION_GOSSIPER_HPP

#include "network/types/block_announce.hpp"

namespace sgns::verification {
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
}  // namespace sgns::verification

#endif  // SUPERGENIUS_VERIFICATION_VERIFICATION_PRODUCTION_GOSSIPER_HPP
