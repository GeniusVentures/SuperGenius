

#ifndef SUPERGENIUS_SRC_PRIMITIVES_PARACHAIN_HOST_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_PARACHAIN_HOST_HPP

#include <cstdint>
#include <vector>

#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include "base/blob.hpp"
#include "base/buffer.hpp"

namespace sgns::primitives::parachain {
  /**
   * @brief ValidatorId primitive is an ed25519 or sr25519 public key
   */
  using ValidatorId = base::Blob<32>;

  /**
   * @brief ParachainId primitive is an uint32_t
   */
  using ParaId = uint32_t;

  /**
   * @brief Relay primitive is empty in supergenius for now
   */
  struct Relay {};
  // TODO(yuraz): PRE-152 update Relay content when it updates in supergenius

  /**
   * @brief Parachain primitive
   */
  using Parachain = ParaId;

  /**
   * @brief Chain primitive
   */
  using Chain = boost::variant<Relay, Parachain>;

  /**
   * @brief DutyRoster primitive
   */
  using DutyRoster = std::vector<Chain>;
  // TODO(yuraz): PRE-152 update Relay << and >> operators
  //  when Relay updates in supergenius
  /**
   * @brief outputs Relay instance to stream
   * @tparam Stream stream type
   * @param s reference to stream
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const Relay &v) {
    return s;
  }

  /**
   * @brief decodes Relay instance from stream
   * @tparam Stream input stream type
   * @param s reference to stream
   * @param v value to decode
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, Relay &v) {
    return s;
  }
}  // namespace sgns::primitives::parachain
#endif  // SUPERGENIUS_SRC_PRIMITIVES_PARACHAIN_HOST_HPP
