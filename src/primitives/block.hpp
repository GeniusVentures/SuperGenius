

#ifndef SUPERGENIUS_PRIMITIVES_BLOCK_HPP
#define SUPERGENIUS_PRIMITIVES_BLOCK_HPP

#include "primitives/block_header.hpp"
#include "primitives/extrinsic.hpp"

namespace sgns::primitives {
  using BlockBody = std::vector<Extrinsic>;

  /**
   * @brief Block class represents supergenius block primitive
   */
  struct Block {
    BlockHeader header;  ///< block header
    BlockBody body{};    ///< extrinsics collection

    inline bool operator==(const Block &rhs) const {
      return header == rhs.header && body == rhs.body;
    }

    inline bool operator!=(const Block &rhs) const {
      return !operator==(rhs);
    }
    //added by Jin to fix link error in test mode
    friend std::ostream &operator<<(std::ostream &out, const Block &b)
    {
      out  << b.header;
      out << b.body.size();
      for (auto it = b.body.begin(); it != b.body.end(); ++it) {
        out << *it;
      }
      return out ;
    }
    //end
  };

  /**
   * @brief outputs object of type Block to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const Block &b) {
    return s << b.header << b.body;
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
  Stream &operator>>(Stream &s, Block &b) {
    return s >> b.header >> b.body;
  }
}  // namespace sgns::primitives

#endif  // SUPERGENIUS_PRIMITIVES_BLOCK_HPP
