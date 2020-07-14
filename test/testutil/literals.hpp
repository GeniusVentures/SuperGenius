

#ifndef SUPERGENIUS_TEST_TESTUTIL_LITERALS_HPP_
#define SUPERGENIUS_TEST_TESTUTIL_LITERALS_HPP_

#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/multi/multihash.hpp>
#include <libp2p/peer/peer_id.hpp>
#include "common/blob.hpp"
#include "common/buffer.hpp"
#include "common/hexutil.hpp"

/// creates a buffer filled with characters from the original string
/// mind that it does not perform unhexing, there is ""_unhex for it
inline sgns::base::Buffer operator"" _buf(const char *c, size_t s) {
  std::vector<uint8_t> chars(c, c + s);
  return sgns::base::Buffer(std::move(chars));
}

inline sgns::base::Hash256 operator"" _hash256(const char *c, size_t s) {
  sgns::base::Hash256 hash{};
  std::copy_n(c, std::min<unsigned long>(s, 32ul), hash.rbegin());
  return hash;
}

inline std::vector<uint8_t> operator"" _v(const char *c, size_t s) {
  std::vector<uint8_t> chars(c, c + s);
  return chars;
}

inline sgns::base::Buffer operator"" _hex2buf(const char *c, size_t s) {
  return sgns::base::Buffer::fromHex(std::string_view(c, s)).value();
}

inline std::vector<uint8_t> operator""_unhex(const char *c, size_t s) {
  return sgns::base::unhex(std::string_view(c, s)).value();
}

inline libp2p::multi::Multiaddress operator""_multiaddr(const char *c,
                                                        size_t s) {
  return libp2p::multi::Multiaddress::create(std::string_view(c, s)).value();
}

/// creates a multihash instance from a hex string
inline libp2p::multi::Multihash operator""_multihash(const char *c, size_t s) {
  return libp2p::multi::Multihash::createFromHex(std::string_view(c, s))
      .value();
}

inline libp2p::peer::PeerId operator""_peerid(const char *c, size_t s) {
  //  libp2p::crypto::PublicKey p;
  auto data = std::vector<uint8_t>(c, c + s);
  libp2p::crypto::ProtobufKey pb_key(std::move(data));

  using libp2p::peer::PeerId;

  return PeerId::fromPublicKey(pb_key).value();
}

#endif  // SUPERGENIUS_TEST_TESTUTIL_LITERALS_HPP_
