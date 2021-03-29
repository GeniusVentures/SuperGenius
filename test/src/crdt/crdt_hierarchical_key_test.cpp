#include <gtest/gtest.h>
#include <testutil/outcome.hpp>
#include <crdt/hierarchical_key.hpp>

namespace sgns::crdt
{
  TEST(CrdtHierarchicalKeyTest, TestHierarchicalKeyNamespaces)
  {
    std::string strNamespace = "/namespace/";
    HierarchicalKey hKey(strNamespace);
    EXPECT_STRCASENE(strNamespace.c_str(), hKey.GetKey().c_str());

    std::string strNamespace2 = "namespace";
    HierarchicalKey hKey2(strNamespace2);
    EXPECT_STRCASENE(strNamespace2.c_str(), hKey2.GetKey().c_str());

    EXPECT_TRUE(hKey == hKey2);
    EXPECT_STRCASEEQ(hKey.GetKey().c_str(), hKey2.GetKey().c_str());

    strNamespace = hKey.GetKey();
    std::string childString = "qwerty";
    HierarchicalKey childKey = hKey.ChildString(childString);
    EXPECT_TRUE(hKey != childKey);
    EXPECT_STRCASEEQ((strNamespace + "/" + childString).c_str(), childKey.GetKey().c_str());
  }
}