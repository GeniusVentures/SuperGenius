

#ifndef SUPERGENIUS_JUSTIFICATION_HPP
#define SUPERGENIUS_JUSTIFICATION_HPP

#include "common/buffer.hpp"

namespace sgns::primitives {
  /**
   * Justification of the finalized block
   */
  struct Justification {
    common::Buffer data;
  };

  /**
   * @brief compares two Justification instances
   * @param lhs first instance
   * @param rhs second instance
   * @return true if equal false otherwise
   */
  inline bool operator==(const Justification &lhs, const Justification &rhs) {
    return lhs.data == rhs.data;
  }

  /**
   * @brief outputs object of type Justification to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const Justification &v) {
    return s << v.data;
  }

  /**
   * @brief decodes object of type Justification from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, Justification &v) {
    return s >> v.data;
  }
}  // namespace sgns::primitives

#endif  // SUPERGENIUS_JUSTIFICATION_HPP
