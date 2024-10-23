
#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_GOSSIPER_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_GOSSIPER_HPP

#include "outcome/outcome.hpp"
#include "verification/finality/structs.hpp"

namespace sgns::verification::finality {

  /**
   * @class Gossiper
   * @brief Gossip messages to the network via this class
   */
  struct Gossiper {
    virtual ~Gossiper() = default;

    /**
     * Broadcast finality's \param vote_message
     */
    virtual void vote(const VoteMessage &vote_message) = 0;

    /**
     * Broadcast finality's \param fin_message
     */
    virtual void finalize(const Fin &fin_message) = 0;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_GOSSIPER_HPP
