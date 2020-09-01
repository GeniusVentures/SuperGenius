

#include "verification/production/impl/threshold_util.hpp"

#include <gtest/gtest.h>

using namespace sgns::verification;  // NOLINT
using namespace sgns;             // NOLINT

/**
 * Same test and outputs as in go:
 * @given arguments for calculateThreshold (c, authorities list weights,
 * authority index)
 * @when calculateThreshold with provided arguments
 * @then expected known threshold is returned
 */
TEST(ThresholdTest, OutputAsInGossamer) {
  std::pair<uint64_t, uint64_t> c;
  c.first = 5;
  c.second = 17;
  primitives::AuthorityIndex authority_index{3};
  primitives::AuthorityList authorities;
  authorities.push_back(primitives::Authority{{},/*.weight =*/ 3});
  authorities.push_back(primitives::Authority{{},/*.weight =*/ 1});
  authorities.push_back(primitives::Authority{{},/*.weight =*/ 4});
  authorities.push_back(primitives::Authority{{},/*.weight =*/ 6});
  authorities.push_back(primitives::Authority{{},/*.weight =*/ 10});

  Threshold expected{"28377230912881121443596276039380107264"};
  auto threshold = calculateThreshold(c, authorities, authority_index);
  ASSERT_EQ(threshold, expected);
}
