
#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_COMPLETED_ROUND_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_COMPLETED_ROUND_HPP

#include "verification/finality/round_state.hpp"

namespace sgns::verification::finality {

  // Structure containing state and number of completed round. Used to start new
  // round
  struct CompletedRound {
    RoundNumber round_number{};
    RoundState state;

    bool operator==(const CompletedRound &rhs) const {
      return round_number == rhs.round_number && state == rhs.state;
    }
    bool operator!=(const CompletedRound &rhs) const {
      return !operator==(rhs);
    }
  };

  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const CompletedRound &round) {
    return s << round.round_number << round.state;
  }

  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, CompletedRound &round) {
    return s >> round.round_number >> round.state;
  }

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_COMPLETED_ROUND_HPP
