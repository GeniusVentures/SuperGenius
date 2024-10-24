

#include "base/hexutil.hpp"

#include <gtest/gtest.h>
#include "testutil/literals.hpp"
#include "testutil/outcome.hpp"

using namespace sgns::base;
using namespace std::string_literals;

/**
 * @given Array of bytes
 * @when hex it
 * @then hex matches expected encoding
 */
TEST(Common, Hexutil_Hex) {
  auto bin = "00010204081020FF"_unhex;
  auto hexed = hex_upper(bin);
  ASSERT_EQ(hexed, "00010204081020FF"s);
}

/**
 * @given Hexencoded string of even length
 * @when unhex
 * @then no exception, result matches expected value
 */
TEST(Common, Hexutil_UnhexEven) {
  auto s = "00010204081020ff"s;

  std::vector<uint8_t> actual;
  ASSERT_NO_THROW(actual = unhex(s).value())
      << "unhex result does not contain expected std::vector<uint8_t>";

  auto expected = "00010204081020ff"_unhex;

  ASSERT_EQ(actual, expected);
}

/**
 * @given Hexencoded string of odd length
 * @when unhex
 * @then unhex result contains error
 */
TEST(Common, Hexutil_UnhexOdd) {
  ASSERT_NO_THROW({ unhex("0").error(); })
      << "unhex did not return an error as expected";
}

/**
 * @given Hexencoded string with non-hex letter
 * @when unhex
 * @then unhex result contains error
 */
TEST(Common, Hexutil_UnhexInvalid) {
  ASSERT_NO_THROW({ unhex("keks").error(); })
      << "unhex did not return an error as expected";
}

struct UnhexNumber32Test
    : public ::testing::TestWithParam<std::pair<std::string, size_t>> {};

namespace {
  std::pair<std::string, size_t> makePair(std::string s, size_t v) {
    return std::make_pair(std::move(s), v);
  }
}  // namespace
