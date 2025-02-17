

#ifndef SUPERGENIUS_SRC_PRIMITIVES_BLOCK_DATA_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_BLOCK_DATA_HPP

#include <boost/optional.hpp>

#include "primitives/block.hpp"
#include "primitives/justification.hpp"

namespace sgns::primitives {

  /**
   * Data, describing the block. Used for example in BlockRequest, where we need
   * to get certain information about the block
   */
  struct BlockData {
    primitives::BlockHash hash;
    boost::optional<primitives::BlockHeader>   header;
    boost::optional<primitives::BlockBody>     body;
    boost::optional<base::Buffer>              receipt;
    boost::optional<base::Buffer>              message_queue;
    boost::optional<primitives::Justification> justification;

    /**
     * Convert a block data into the block
     * @return block, if at least header exists in this BlockData, nothing
     * otherwise
     */
    boost::optional<primitives::Block> toBlock() const {
      if (!header) {
        return boost::none;
      }
      return body ? primitives::Block{*header, *body}
                  : primitives::Block{*header};
    }
  };

  /**
   * @brief compares two BlockData instances
   * @param lhs first instance
   * @param rhs second instance
   * @return true if equal false otherwise
   */
  inline bool operator==(const BlockData &lhs, const BlockData &rhs) {
    return lhs.hash == rhs.hash && lhs.header == rhs.header
           && lhs.body == rhs.body && lhs.receipt == rhs.receipt
           && lhs.message_queue == rhs.message_queue
           && lhs.justification == rhs.justification;
  }

  /**
   * @brief outputs object of type BlockData to stream
   * @tparam Stream output stream type
   * @param s stream reference
   * @param v value to output
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_encoder_stream>>
  Stream &operator<<(Stream &s, const BlockData &v) {
    return s << v.hash << v.header << v.body << v.receipt << v.message_queue
             << v.justification;
  }

  /**
   * @brief decodes object of type BlockData from stream
   * @tparam Stream input stream type
   * @param s stream reference
   * @param v value to decode
   * @return reference to stream
   */
  template <class Stream,
            typename = std::enable_if_t<Stream::is_decoder_stream>>
  Stream &operator>>(Stream &s, BlockData &v) {
    return s >> v.hash >> v.header >> v.body >> v.receipt >> v.message_queue
           >> v.justification;
  }

}  // namespace sgns::primitives

#endif  // SUPERGENIUS_SRC_PRIMITIVES_BLOCK_DATA_HPP
