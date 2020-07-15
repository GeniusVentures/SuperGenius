

#ifndef SUPERGENIUS_STORAGE_TRIE_SUPERGENIUS_NODE
#define SUPERGENIUS_STORAGE_TRIE_SUPERGENIUS_NODE

#include <boost/optional.hpp>

#include "base/blob.hpp"
#include "base/buffer.hpp"
#include "storage/trie/node.hpp"

namespace sgns::storage::trie {

  struct KeyNibbles : public base::Buffer {
    KeyNibbles() = default;

    explicit KeyNibbles(base::Buffer b) : Buffer{std::move(b)} {}
    KeyNibbles(std::initializer_list<uint8_t> b)
        : Buffer{b} {}

    KeyNibbles &operator=(base::Buffer b) {
      Buffer::operator=(std::move(b));
      return *this;
    }

    KeyNibbles subspan(size_t offset = 0, size_t length = -1) const {
      return KeyNibbles{Buffer::subbuffer(offset, length)};
    }
  };

  /**
   * For specification see
   * https://github.com/w3f/supergenius-re-spec/blob/master/supergenius_re_spec.pdf
   * 5.3 The Trie structure
   */

  struct SuperGeniusNode : public Node {
    SuperGeniusNode() = default;
    SuperGeniusNode(KeyNibbles key_nibbles, boost::optional<base::Buffer> value)
        : key_nibbles{std::move(key_nibbles)}, value{std::move(value)} {}

    ~SuperGeniusNode() override = default;

    enum class Type {
      Special = 0b00,
      Leaf = 0b01,
      BranchEmptyValue = 0b10,
      BranchWithValue = 0b11
    };

    // dummy nodes are used to avoid unnecessary reads from the storage
    virtual bool isDummy() const = 0;

    // just to avoid static_casts every time you need a switch on a node type
    Type getTrieType() const {
      return static_cast<Type>(getType());
    }

    KeyNibbles key_nibbles;
    boost::optional<base::Buffer> value;
  };

  struct BranchNode : public SuperGeniusNode {
    static constexpr int kMaxChildren = 16;

    BranchNode() = default;
    explicit BranchNode(KeyNibbles key_nibbles,
                        boost::optional<base::Buffer> value = boost::none)
        : SuperGeniusNode{std::move(key_nibbles), std::move(value)} {}

    ~BranchNode() override = default;

    bool isDummy() const override {
      return false;
    }
    int getType() const override;

    uint16_t childrenBitmap() const;
    uint8_t childrenNum() const;

    // Has 1..16 children.
    // Stores their hashes to search for them in a storage and encode them more
    // easily.
    std::array<std::shared_ptr<SuperGeniusNode>, kMaxChildren> children;
  };

  struct LeafNode : public SuperGeniusNode {
    LeafNode() = default;
    LeafNode(KeyNibbles key_nibbles, boost::optional<base::Buffer> value)
        : SuperGeniusNode{std::move(key_nibbles), std::move(value)} {}

    ~LeafNode() override = default;

    bool isDummy() const override {
      return false;
    }
    int getType() const override;
  };

  /**
   * Used in branch nodes to indicate that there is a node, but this node is not
   * interesting at the moment and need not be retrieved from the storage.
   */
  struct DummyNode : public SuperGeniusNode {
    /**
     * Constructs a dummy node
     * @param key a storage key, which is a hash of an encoded node according to
     * SuperGenius specification
     */
    explicit DummyNode(base::Buffer key) : db_key{std::move(key)} {}

    bool isDummy() const override {
      return true;
    }

    int getType() const override {
      // Special only because a node has to have a type. Actually this is not
      // the real node and the type of the underlying node is inaccessible
      // before reading from the storage
      return static_cast<int>(Type::Special);
    }

    base::Buffer db_key;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_SUPERGENIUS_NODE
