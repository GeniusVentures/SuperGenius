

#ifndef SUPERGENIUS_TRIE_CODEC_HPP
#define SUPERGENIUS_TRIE_CODEC_HPP

#include "base/blob.hpp"  // for Hash256
#include "base/buffer.hpp"
#include "storage/trie/node.hpp"
#include "integration/IComponent.hpp"

namespace sgns::storage::trie {

  /**
   * @brief Internal codec for nodes in the Trie. Eth and substrate have
   * different codecs, but rest of the code should be same.
   */
  class Codec : public IComponent{
   public:
    virtual ~Codec() = default;

    /**
     * @brief Encode node to byte representation
     * @param node node in the trie
     * @return encoded representation of a {@param node}
     */
    virtual outcome::result<base::Buffer> encodeNode(
        const Node &node) const = 0;

    /**
     * @brief Decode node from bytes
     * @param encoded_data a buffer containing encoded representation of a node
     * @return a node in the trie
     */
    virtual outcome::result<std::shared_ptr<Node>> decodeNode(
        const base::Buffer &encoded_data) const = 0;

    /**
     * @brief Get the merkle value of a node
     * @param buf byte representation of the node
     * @return hash of \param buf or \param buf if it is shorter than the hash
     */
    virtual base::Buffer merkleValue(const base::Buffer &buf) const = 0;

    /**
     * @brief Get the hash of a node
     * @param buf byte representation of the node
     * @return hash of \param buf
     */
    virtual base::Hash256 hash256(const base::Buffer &buf) const = 0;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_TRIE_CODEC_HPP
