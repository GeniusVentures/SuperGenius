

#ifndef SUPERGENIUS_TEST_TESTUTIL_PRIMITIVES_MP_UTILS_HPP
#define SUPERGENIUS_TEST_TESTUTIL_PRIMITIVES_MP_UTILS_HPP

#include "common/blob.hpp"

namespace testutil {
  /**
   * @brief creates hash from initializers list
   * @param bytes initializers
   * @return newly created hash
   */
  sgns::common::Hash256 createHash256(std::initializer_list<uint8_t> bytes);

}  // namespace testutil

#endif  //SUPERGENIUS_TEST_TESTUTIL_PRIMITIVES_MP_UTILS_HPP
