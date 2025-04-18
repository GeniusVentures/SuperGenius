

#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_COMMON_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_COMMON_HPP

#include "clock/clock.hpp"
#include "crypto/ed25519_types.hpp"
#include "primitives/common.hpp"

namespace sgns::verification::finality {

  using Id = crypto::ED25519PublicKey;

  // vote signature
  using Signature = crypto::ED25519Signature;
  using BlockHash = primitives::BlockHash;
  using BlockNumber = primitives::BlockNumber;
  using RoundNumber = uint64_t;
  using MembershipCounter = uint64_t;

  using Clock = clock::SteadyClock;
  using Duration = Clock::Duration;
  using TimePoint = Clock::TimePoint;

  enum class State { START, PROPOSED, PREVOTED, PRECOMMITTED };
}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_COMMON_HPP
