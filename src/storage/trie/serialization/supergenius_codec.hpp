

#ifndef SUPERGENIUS_TRIE_SUPERGENIUS_CODEC_IMPL_HPP
#define SUPERGENIUS_TRIE_SUPERGENIUS_CODEC_IMPL_HPP

#include <memory>
#include <string>

#include "storage/trie/serialization/buffer_stream.hpp"
#include "storage/trie/codec.hpp"
#include "storage/trie/supergenius_trie/supergenius_node.hpp"

namespace sgns::storage::trie {

  class SuperGeniusCodec : public Codec {
   public:
    using Buffer = sgns::base::Buffer;

    enum class Error {
      SUCCESS = 0,
      TOO_MANY_NIBBLES,   ///< number of nibbles in key is >= 2**16
      UNKNOWN_NODE_TYPE,  ///< node type is unknown
      INPUT_TOO_SMALL,    ///< cannot decode a node, not enough bytes on input
      NO_NODE_VALUE       ///< leaf node without value
    };

    ~SuperGeniusCodec() override = default;

    outcome::result<Buffer> encodeNode(const Node &node) const override;

    outcome::result<std::shared_ptr<Node>> decodeNode(
        const base::Buffer &encoded_data) const override;

    base::Buffer merkleValue(const Buffer &buf) const override;

    base::Hash256 hash256(const Buffer &buf) const override;

    std::string GetName() override
    {
      return "SuperGeniusCodec";
    }

    /**
     * Def. 14 KeyEncode
     * Splits a key to an array of nibbles (a nibble is a half of a byte)
     */
    static KeyNibbles keyToNibbles(const Buffer &key);

    /**
     * Collects an array of nibbles to a key
     */
    static Buffer nibblesToKey(const KeyNibbles &nibbles);

    /**
     * Encodes a node header accroding to the specification
     * @see Algorithm 3: partial key length encoding
     */
    outcome::result<Buffer> encodeHeader(const SuperGeniusNode &node) const;

   private:
    outcome::result<Buffer> encodeBranch(const BranchNode &node) const;
    outcome::result<Buffer> encodeLeaf(const LeafNode &node) const;

    outcome::result<std::pair<SuperGeniusNode::Type, size_t>> decodeHeader(
        BufferStream &stream) const;

    outcome::result<KeyNibbles> decodePartialKey(size_t nibbles_num,
                                             BufferStream &stream) const;

    outcome::result<std::shared_ptr<Node>> decodeBranch(
        SuperGeniusNode::Type type,
        const KeyNibbles &partial_key,
        BufferStream &stream) const;
  };

}  // namespace sgns::storage::trie

OUTCOME_HPP_DECLARE_ERROR_2(sgns::storage::trie, SuperGeniusCodec::Error);

#endif  // SUPERGENIUS_TRIE_SUPERGENIUS_CODEC_IMPL_HPP
