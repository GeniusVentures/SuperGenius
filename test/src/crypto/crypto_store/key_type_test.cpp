

#include <gtest/gtest.h>

#include <string_view>

#include "crypto/crypto_store/key_type.hpp"

using sgns::crypto::key_types::kAcco;
using sgns::crypto::key_types::kAudi;
using sgns::crypto::key_types::kProd;
using sgns::crypto::key_types::kGran;
using sgns::crypto::key_types::kImon;
using sgns::crypto::key_types::kLp2p;

using sgns::crypto::decodeKeyTypeId;
using sgns::crypto::KeyTypeId;

namespace {
  std::tuple<KeyTypeId, std::string_view, bool> good(KeyTypeId id,
                                                     std::string_view v) {
    return {id, v, true};
  }

  std::tuple<KeyTypeId, std::string_view, bool> bad(KeyTypeId id,
                                                    std::string_view v) {
    return {id, v, false};
  }

}  // namespace

struct KeyTypeTest : public ::testing::TestWithParam<
                         std::tuple<KeyTypeId, std::string_view, bool>> {};

TEST_P(KeyTypeTest, DecodeSuccess) {
  auto [key_type, repr, should_succeed] = GetParam();
  auto &&key_type_str = decodeKeyTypeId(key_type);

  if (should_succeed) {
    ASSERT_EQ(key_type_str, repr);
  } else {
    ASSERT_NE(key_type_str, repr);
  }
}

INSTANTIATE_TEST_SUITE_P(KeyTypeTestCases,
                        KeyTypeTest,
                        ::testing::Values(good(kProd, "prod"),
                                          good(kGran, "gran"),
                                          good(kAcco, "acco"),
                                          good(kImon, "imon"),
                                          good(kAudi, "audi"),
                                          good(kLp2p, "lp2p"),
                                          bad(kProd - 5, "prod"),
                                          bad(kProd + 1000, "prod")));
