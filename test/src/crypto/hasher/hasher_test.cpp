#include <gtest/gtest.h>

#include "base/blob.hpp"
#include "crypto/hasher.hpp"
#include "testutil/literals.hpp"

using sgns::base::Buffer;

/**
 * Hash helper fixture
 */
class HasherFixture : public testing::Test {
 public:
  /**
   * useful function for convenience
   */
  template <int size>
  static Buffer blob2buffer(const sgns::base::Blob<size> &blob) noexcept {
    Buffer out;
    out.put({blob.data(), static_cast<long>(blob.size())});
    return out;
  }

  static Buffer string2buffer(const std::string_view &view) noexcept {
    Buffer out;
    out.put(view);
    return out;
  }

};

/**
 * @given pre-known source value
 * @when crypto::twox_64 is applied
 * @then expected result obtained
 */
TEST_F(HasherFixture, twox_64) {
  auto hash = sgns::crypto::twox_64(Buffer().put("foo"));

  // match is output obtained from substrate
  sgns::base::Blob<8> match;
  match[0] = '?';
  match[1] = '\xba';
  match[2] = '\xc4';
  match[3] = 'Y';
  match[4] = '\xa8';
  match[5] = '\0';
  match[6] = '\xbf';
  match[7] = '3';

  ASSERT_EQ(hash, match);
}

// /**
//  * @given some common source value
//  * @when crypto::twox_128 is applied
//  * @then expected result obtained
//  */
TEST_F(HasherFixture, twox_128) {
  auto hash = sgns::crypto::twox_128(Buffer{"414243444546"_unhex});
  std::vector<uint8_t> match = {
      184, 65, 176, 250, 243, 129, 181, 3, 77, 82, 63, 150, 129, 221, 191, 251};
  ASSERT_EQ(blob2buffer<16>(hash).toVector(), match);
}

// /**
//  * @given some common source value
//  * @when crypto::twox_256 is applied
//  * @then expected result obtained
//  */
TEST_F(HasherFixture, twox_256) {
  // some value
  auto v = Buffer{0x41, 0x42, 0x43, 0x44, 0x45, 0x46};
  auto hash = sgns::crypto::twox_256(v);
  std::vector<uint8_t> match = {184, 65,  176, 250, 243, 129, 181, 3,
                                77,  82,  63,  150, 129, 221, 191, 251,
                                33,  226, 149, 136, 6,   232, 81,  118,
                                200, 28,  69,  219, 120, 179, 208, 237};
  ASSERT_EQ(blob2buffer<32>(hash).toVector(), match);
}

/**
 * @given some common source value
 * @when crypto::sha2_256 is applied
 * @then expected result obtained
 */
TEST_F(HasherFixture, sha2_256) {
  auto hash = sgns::crypto::sha2_256(string2buffer(
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
  std::string_view match =
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
  ASSERT_EQ(hash.toHex(), match);
}

/**
 * @given some common source value
 * @when crypto::blake2b_256 is applied
 * @then expected result obtained
 */
TEST_F(HasherFixture, blake2_256) {
  Buffer buffer{"6920616d2064617461"_unhex};
  std::vector<uint8_t> match =
      "ba67336efd6a3df3a70eeb757860763036785c182ff4cf587541a0068d09f5b2"_unhex;

  auto hash = sgns::crypto::blake2b_256(buffer);
  ASSERT_EQ(blob2buffer<32>(hash).toVector(), match);
}

/**
 * @given some common source value
 * @when crypto::blake2b_128 is applied
 * @then expected result obtained
 */
TEST_F(HasherFixture, blake2_128) {
  Buffer buffer{"6920616d2064617461"_unhex};
  std::vector<uint8_t> match = "de944c5c12e55ee9a07cf5bf4b674995"_unhex;

  auto hash = sgns::crypto::blake2b_128(buffer);
  ASSERT_EQ(blob2buffer<16>(hash).toVector(), match);
}
