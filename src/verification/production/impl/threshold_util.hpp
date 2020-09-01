

#ifndef SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_THRESHOLD_UTIL_HPP
#define SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_THRESHOLD_UTIL_HPP

#include "verification/production/common.hpp"
#include "primitives/authority.hpp"

namespace sgns::verification {

  /// Calculates the primary selection threshold for a given authority, taking
  /// into account `c` (`1 - c` represents the probability of a slot being
  /// empty).
  Threshold calculateThreshold(
      const std::pair<uint64_t, uint64_t> &c_pair,
      const primitives::AuthorityList &authorities,
      primitives::AuthorityIndex authority_index);

}  // namespace sgns::verification

#endif  // SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_THRESHOLD_UTIL_HPP
