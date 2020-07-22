
#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_FINALITY_CONFIG_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_FINALITY_CONFIG_HPP

#include "verification/finality/voter_set.hpp"

namespace sgns::verification::finality {

  // Structure containing necessary information for running finality voting round
  struct FinalityConfig {
    std::shared_ptr<VoterSet> voters;  // current round's authorities
    RoundNumber round_number;
    Duration
        duration;  // time bound which is enough to gossip messages to everyone
    Id peer_id;    //  key of the peer, do not confuse with libp2p peerid
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_FINALITY_CONFIG_HPP

