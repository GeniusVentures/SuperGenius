#ifndef SUPERGENIUS_SRC_NETWORK_EXTRINSIC_GOSSIPER_HPP
#define SUPERGENIUS_SRC_NETWORK_EXTRINSIC_GOSSIPER_HPP

#include "network/types/transaction_announce.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::network {
  /**
   * Sends messages, related to author api, over the Gossip protocol
   */
  struct ExtrinsicGossiper : public IComponent {
      ~ExtrinsicGossiper() override = default;

    /**
     * Send TxAnnounce message
     * @param announce to be sent
     */
    virtual void transactionAnnounce(
        const network::TransactionAnnounce &announce) = 0;
  };
}

#endif
