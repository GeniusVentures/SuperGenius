

#ifndef SUPERGENIUS_SRC_NETWORK_GOSSIPER_HPP
#define SUPERGENIUS_SRC_NETWORK_GOSSIPER_HPP

#include "verification/production/production_gossiper.hpp"
#include "verification/finality/gossiper.hpp"
#include "network/extrinsic_gossiper.hpp"

#include <libp2p/connection/stream.hpp>
#include <libp2p/peer/peer_info.hpp>

namespace sgns::network {
  /**
   * Joins all available gossipers
   */
  struct Gossiper : public ExtrinsicGossiper,
                    public verification::ProductionGossiper,
                    public verification::finality::Gossiper {
    virtual void reserveStream(
        const libp2p::peer::PeerInfo &info,
        std::shared_ptr<libp2p::connection::Stream> stream) = 0;

    // Add new stream to gossip
    virtual void addStream(
        std::shared_ptr<libp2p::connection::Stream> stream) = 0;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_SRC_NETWORK_GOSSIPER_HPP
