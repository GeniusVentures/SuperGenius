

#ifndef SUPERGENIUS_GOSSIPER_BROADCAST_HPP
#define SUPERGENIUS_GOSSIPER_BROADCAST_HPP

#include <gsl/span>
#include <unordered_map>

#include <libp2p/connection/stream.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/peer/peer_info.hpp>
#include "network/gossiper.hpp"
#include "network/types/gossip_message.hpp"
#include "network/types/peer_list.hpp"
#include "base/logger.hpp"
namespace sgns::network {
  /**
   * Sends gossip messages using broadcast strategy
   */
  class GossiperBroadcast
      : public Gossiper,
        public std::enable_shared_from_this<GossiperBroadcast> {
    using Precommit = verification::finality::Precommit;
    using Prevote = verification::finality::Prevote;
    using PrimaryPropose = verification::finality::PrimaryPropose;

   public:
    GossiperBroadcast(libp2p::Host &host);

    ~GossiperBroadcast() override = default;

    void reserveStream(
        const libp2p::peer::PeerInfo &peer_info,
        std::shared_ptr<libp2p::connection::Stream> stream) override;

    void transactionAnnounce(const TransactionAnnounce &announce) override;

    void blockAnnounce(const BlockAnnounce &announce) override;

    void vote(const verification::finality::VoteMessage &msg) override;

    void finalize(const verification::finality::Fin &fin) override;

    void addStream(std::shared_ptr<libp2p::connection::Stream> stream) override;

    std::string GetName() override
    {
      return "GossiperBroadcast";
    }

   private:
    void broadcast(GossipMessage &&msg);

    libp2p::Host &host_;
    std::unordered_map<libp2p::peer::PeerInfo,
                       std::shared_ptr<libp2p::connection::Stream>>
        streams_;
    std::vector<std::shared_ptr<libp2p::connection::Stream>> syncing_streams_;
    base::Logger logger_;
  };
}  // namespace sgns::network

#endif  // SUPERGENIUS_GOSSIPER_BROADCAST_HPP
