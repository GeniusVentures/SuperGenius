

#include <memory>

#include <gtest/gtest.h>
#include "crypto/blake2/blake2b.h"
#include "storage/trie/serialization/supergenius_codec.hpp"

using namespace sgns;
using namespace base;
using namespace storage;
using namespace trie;
using namespace testing;

struct Hash256Test
    : public ::testing::TestWithParam<std::pair<Buffer, Buffer>> {};

TEST_P(Hash256Test, Valid) {
  auto [in, out] = GetParam();
  auto codec = std::make_unique<SuperGeniusCodec>();
  auto actualOut = codec->merkleValue(in);
  EXPECT_EQ(actualOut.toHex(), out.toHex());
}

Buffer getBlake2b(const Buffer &in) {
  Buffer out(32, 0);
  sgns_blake2b(out.data(), 32, nullptr, 0, in.data(), in.size());
  return out;
}

const std::vector<std::pair<Buffer, Buffer>> cases = {
    // pair{in, out}
    {{0}, {0}},  // if length < 32, do not hash, pad to 32 and return
    {{1, 3, 3, 7},
     {
         1, 3, 3, 7}},
    // buffer of size 32, consists of ones
    {Buffer(32, 1), getBlake2b(Buffer(32, 1))}};

INSTANTIATE_TEST_SUITE_P(SuperGeniusCodec, Hash256Test, ValuesIn(cases));
