

#ifndef SUPERGENIUS_CORE_NETWORK_GOSSIPER_HPP
#define SUPERGENIUS_CORE_NETWORK_GOSSIPER_HPP

#include "verification/production/CONSENSUS_PRODUCTION_gossiper.hpp"
#include "verification/grandpa/gossiper.hpp"
#include "network/extrinsic_gossiper.hpp"

#include <libp2p/connection/stream.hpp>
#include <libp2p/peer/peer_info.hpp>

namespace sgns::network {
  /**
   * Joins all available gossipers
   */
  struct Gossiper : public ExtrinsicGossiper,
                    public verification::BabeGossiper,
                    public verification::grandpa::Gossiper {
    virtual void reserveStream(
        const libp2p::peer::PeerInfo &info,
        std::shared_ptr<libp2p::connection::Stream> stream) = 0;

    // Add new stream to gossip
    virtual void addStream(
        std::shared_ptr<libp2p::connection::Stream> stream) = 0;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_CORE_NETWORK_GOSSIPER_HPP
