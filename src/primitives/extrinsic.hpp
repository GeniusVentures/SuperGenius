

#ifndef SUPERGENIUS_PRIMITIVES_EXTRINSIC_HPP
#define SUPERGENIUS_PRIMITIVES_EXTRINSIC_HPP

#include "base/buffer.hpp"

namespace sgns::primitives {

  /**
   * Index of an extrinsic in a block
   */
  using ExtrinsicIndex = uint32_t;

  /**
   * @brief Extrinsic class represents extrinsic
   */
  struct Extrinsic {
    base::Buffer data;  ///< extrinsic content as byte array

    bool operator==( const Extrinsic &rhs ) const
    {
        return data == rhs.data;
    }
    friend std::ostream &operator<<(std::ostream &out, const Extrinsic &v)
    {
      return out <<  v.data;
    }

  };

  /**
   * @brief outputs object of type Extrinisic to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const Extrinsic &v) {
    return s << v.data;
  }

  /**
   * @brief decodes object of type Extrinisic from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, Extrinsic &v) {
    return s >> v.data;
  }
}  // namespace sgns::primitives

#endif  // SUPERGENIUS_PRIMITIVES_EXTRINSIC_HPP
