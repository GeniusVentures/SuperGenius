

#include <memory>

#include <gtest/gtest.h>
#include "storage/trie/supergenius_trie/supergenius_node.hpp"
#include "storage/trie/serialization/supergenius_codec.hpp"
#include "testutil/literals.hpp"
#include "testutil/outcome.hpp"

using namespace sgns;
using namespace base;
using namespace storage;
using namespace trie;
using namespace testing;

struct Case
{
    std::shared_ptr<SuperGeniusNode> node;
    Buffer                           encoded_header;

    friend std::ostream& operator<<(std::ostream& out,
                                    const Case&   test_struct)
    {
        return out;
    };
};

struct NodeEncodingTest : public ::testing::TestWithParam<Case>
{
    std::unique_ptr<SuperGeniusCodec> codec =
        std::make_unique<SuperGeniusCodec>();

    friend std::ostream& operator<<(std::ostream&           out,
                                    const NodeEncodingTest& test_struct)
    {
        return out;
    };
};

TEST_P(NodeEncodingTest, GetHeader)
{
    auto testCase = GetParam();

    EXPECT_OUTCOME_TRUE_2(actual, codec->encodeHeader(*(testCase.node)));
    EXPECT_EQ(actual.toHex(), testCase.encoded_header.toHex());
}

/**
 * @brief       Makes a Supergenius derived node
 * @param[in]   key_nibbles: nibbles to assign to node
 * @param[in]   value: Value to assign to node
 * @return      Supergenius derived class
 */
template <typename T>
std::shared_ptr<SuperGeniusNode> make(const base::Buffer& key_nibbles,
                                      const base::Buffer& value)
{
    auto node         = std::make_shared<T>();
    node->key_nibbles = key_nibbles;
    node->value       = value;
    return node;
}
//TODO - Improve this cases and check if this is even relevant
static const std::vector<Case> CASES = {
    {make<LeafNode>("010203"_hex2buf, "abcdef"_hex2buf), "43"_hex2buf},
    {make<LeafNode>(base::Buffer(64, 0xffu), base::Buffer(64, 0xfeuF)),
     "7f01"_hex2buf}};

INSTANTIATE_TEST_CASE_P(SuperGeniusCodec, NodeEncodingTest, ValuesIn(CASES));
