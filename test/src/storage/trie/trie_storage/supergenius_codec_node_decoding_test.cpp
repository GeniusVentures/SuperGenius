

#include <memory>

#include <gtest/gtest.h>
#include <boost/optional/optional_io.hpp>
#include "storage/trie/supergenius_trie/supergenius_node.hpp"
#include "storage/trie/serialization/buffer_stream.hpp"
#include "storage/trie/serialization/supergenius_codec.hpp"
#include "testutil/literals.hpp"
#include "testutil/outcome.hpp"

using namespace sgns;
using namespace base;
using namespace storage;
using namespace trie;
using namespace testing;

struct NodeDecodingTest
    : public ::testing::TestWithParam<std::shared_ptr<SuperGeniusNode>> {
  std::unique_ptr<SuperGeniusCodec> codec = std::make_unique<SuperGeniusCodec>();
   
    friend std::ostream &operator<<(std::ostream &out, const NodeDecodingTest &test_struct)
  {
    return out; 
  };
  //    friend std::ostream &operator<<(std::ostream &out, const NodeEncodingTest &test_struct)
  // {
  //   return out; 
  // }

};
 std::ostream &operator<<(std::ostream &out, const boost::optional<class sgns::base::Buffer> &test_struct)
  {
    return out; 
  };

TEST_P(NodeDecodingTest, GetHeader) {
  auto node = GetParam();

  EXPECT_OUTCOME_TRUE(encoded, codec->encodeNode(*node));
  EXPECT_OUTCOME_TRUE(decoded, codec->decodeNode(encoded));
  auto decoded_node = std::dynamic_pointer_cast<SuperGeniusNode>(decoded);
  EXPECT_EQ(decoded_node->key_nibbles, node->key_nibbles);
  EXPECT_EQ(decoded_node->value, node->value);
}

template <typename T>
std::shared_ptr<SuperGeniusNode> make(const base::Buffer &key_nibbles,
                                   const base::Buffer &value) {
  auto node = std::make_shared<T>();
  node->key_nibbles = key_nibbles;
  node->value = value;
  return node;
}

std::shared_ptr<SuperGeniusNode> branch_with_2_children = []() {
  auto node =
      std::make_shared<BranchNode>(KeyNibbles{"010203"_hex2buf}, "0a"_hex2buf);
  auto child1 =
      std::make_shared<LeafNode>(KeyNibbles{"01"_hex2buf}, "0b"_hex2buf);
  auto child2 =
      std::make_shared<LeafNode>(KeyNibbles{"02"_hex2buf}, "0c"_hex2buf);
  node->children[0] = child1;
  node->children[1] = child2;
  return node;
}();

using T = SuperGeniusNode::Type;

static const std::vector<std::shared_ptr<SuperGeniusNode>> CASES = {
    make<LeafNode>("010203"_hex2buf, "abcdef"_hex2buf),
    make<LeafNode>("0a0b0c"_hex2buf, "abcdef"_hex2buf),
    make<BranchNode>("010203"_hex2buf, "abcdef"_hex2buf),
    branch_with_2_children};

INSTANTIATE_TEST_SUITE_P(SuperGeniusCodec, NodeDecodingTest, ValuesIn(CASES));
