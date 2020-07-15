

#ifndef SUPERGENIUS_CORE_STORAGE_TRIE_BUFFER_STREAM
#define SUPERGENIUS_CORE_STORAGE_TRIE_BUFFER_STREAM

#include <gsl/span>

#include "base/buffer.hpp"

namespace sgns::storage::trie {

  /**
   * Needed for decoding, might be replaced to a more common stream in the
   * future, when one appears
   */
  class BufferStream {
    using index_type = gsl::span<const uint8_t>::index_type;

   public:
    explicit BufferStream(const base::Buffer &buf) : data_{buf.toVector()} {}

    bool hasMore(index_type num_bytes) const {
      return data_.size() >= num_bytes;
    }

    uint8_t next() {
      auto byte = data_.at(0);
      data_ = data_.last(data_.size() - 1);
      return byte;
    }

    gsl::span<const uint8_t> leftBytes() const {
      return data_;
    }

   private:
    gsl::span<const uint8_t> data_;
  };
}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_CORE_STORAGE_TRIE_BUFFER_STREAM
