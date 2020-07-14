

#ifndef SUPERGENIUS_BABE_OBSERVER_HPP
#define SUPERGENIUS_BABE_OBSERVER_HPP

#include "network/types/block_announce.hpp"

namespace sgns::network {
  /**
   * Reacts to messages, related to BABE
   */
  struct BabeObserver {
    virtual ~BabeObserver() = default;

    /**
     * Triggered when a BlockAnnounce message arrives
     * @param announce - arrived message
     */
    virtual void onBlockAnnounce(const BlockAnnounce &announce) = 0;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_BABE_OBSERVER_HPP
