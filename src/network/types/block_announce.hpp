

#ifndef SUPERGENIUS_BLOCK_ANNOUNCE_HPP
#define SUPERGENIUS_BLOCK_ANNOUNCE_HPP

#include "primitives/block_header.hpp"

namespace sgns::network {
  /**
   * Announce a new complete block on the network
   */
  struct BlockAnnounce {
    primitives::BlockHeader header;
    //added to fix link error
    friend std::ostream &operator<<(std::ostream &out, const BlockAnnounce &test_struct)
    {
      return out << test_struct.header;
    }
    //end

  };

  /**
   * @brief compares two BlockAnnounce instances
   * @param lhs first instance
   * @param rhs second instance
   * @return true if equal false otherwise
   */
  inline bool operator==(const BlockAnnounce &lhs, const BlockAnnounce &rhs) {
    return lhs.header == rhs.header;
  }
  inline bool operator!=(const BlockAnnounce &lhs, const BlockAnnounce &rhs) {
    return !(lhs == rhs);
  }

  /**
   * @brief outputs object of type BlockAnnounce to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const BlockAnnounce &v) {
    return s << v.header;
  }

  /**
   * @brief decodes object of type Block from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to decode
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, BlockAnnounce &v) {
    return s >> v.header;
  }
}  // namespace sgns::network

#endif  // SUPERGENIUS_BLOCK_ANNOUNCE_HPP
