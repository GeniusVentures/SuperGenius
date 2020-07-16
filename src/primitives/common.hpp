

#ifndef SUPERGENIUS_SRC_PRIMITIVES_COMMON_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_COMMON_HPP

#include <cstdint>

#include <boost/operators.hpp>
#include "primitives/session_key.hpp"

namespace sgns::primitives {
  using BlocksRequestId = uint64_t;

  using BlockNumber = uint64_t;
  using BlockHash = base::Hash256;

  namespace detail {
    // base data structure for the types describing block information
    // (BlockInfo, Prevote, Precommit, PrimaryPropose)
    template <typename Tag>
    struct BlockInfoT : public boost::equality_comparable<BlockInfoT<Tag>> {
      BlockInfoT() = default;

      BlockInfoT(const BlockNumber &n, const BlockHash &h)
          : block_number(n), block_hash(h) {}

      BlockNumber block_number{};
      BlockHash block_hash{};

      bool operator==(const BlockInfoT<Tag> &o) const {
        return block_number == o.block_number && block_hash == o.block_hash;
      }
    };

    template <class Stream,
              typename Tag,
              typename = std::enable_if_t<Stream::is_encoder_stream>>
    Stream &operator<<(Stream &s, const BlockInfoT<Tag> &msg) {
      return s << msg.block_hash << msg.block_number;
    }

    template <class Stream,
              typename Tag,
              typename = std::enable_if_t<Stream::is_decoder_stream>>
    Stream &operator>>(Stream &s, BlockInfoT<Tag> &msg) {
      return s >> msg.block_hash >> msg.block_number;
    }
  }  // namespace detail

  using BlockInfo = detail::BlockInfoT<struct BlockInfoTag>;

}  // namespace sgns::primitives

#endif  // SUPERGENIUS_SRC_PRIMITIVES_COMMON_HPP
