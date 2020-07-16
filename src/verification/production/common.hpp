

#ifndef SUPERGENIUS_COMMON_HPP
#define SUPERGENIUS_COMMON_HPP

#include <cstdint>

#include "clock/clock.hpp"
#include "crypto/sr25519_types.hpp"

namespace sgns::verification {
  using ProductionClock = clock::SystemClock;

  /// Production uses system clock's time points
  using ProductionTimePoint = ProductionClock::TimePoint;

  // Production uses system clock's duration
  using ProductionDuration = ProductionClock::Duration;

  /// slot number of the production
  using ProductionSlotNumber = uint64_t;

  /// number of the epoch in the production
  using EpochIndex = uint64_t;

  // number of slots in a single epoch
  using EpochLength = EpochIndex;

  /// threshold, which must not be exceeded for the party to be a slot leader
  using Threshold = crypto::VRFThreshold;

  /// random value, which serves as a seed for VRF slot leadership selection
  using Randomness = base::Blob<crypto::constants::sr25519::vrf::OUTPUT_SIZE>;
}  // namespace sgns::verification

#endif  // SUPERGENIUS_COMMON_HPP
