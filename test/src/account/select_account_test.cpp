#include <gtest/gtest.h>
#include <boost/dll/runtime_symbol_info.hpp>

#include "testutil/wait_condition.hpp"

#include "account/GeniusNode.hpp"

class AccountSelection : public ::testing::Test
{
public:
    static inline boost::filesystem::path path1 = boost::dll::program_location().parent_path() / "account1";
    static inline boost::filesystem::path path2 = boost::dll::program_location().parent_path() / "account2";

    AccountSelection()
    {
        try
        {
            boost::filesystem::remove_all( path1 );
            boost::filesystem::remove_all( path2 );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }
    }
};

using namespace sgns;

TEST_F( AccountSelection, CantSelectAccountThatWasNotAdded )
{
    auto node = GeniusNode::New( { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), path1.generic_string() } );
    ASSERT_FALSE( node == nullptr ) << "Could not create genius node";
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 60000 ),
                                  "fullNode not synched" );
    ASSERT_TRUE( node->SelectAccount( "foobar" ).has_error() );
}

TEST_F( AccountSelection, CanSelectAccountThatWasAdded )
{
    auto node = GeniusNode::New(
        { "0xafadafedeacafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), path1.generic_string() } );
    auto account1_address = node->GetAddress();
    ASSERT_FALSE( node == nullptr ) << "Could not create first genius account";
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 30000 ),
                                  "fullNode not synched" );
    node.reset();
    node = GeniusNode::New( { "0xdeadbeef", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), path2.generic_string() } );
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 30000 ),
                                  "fullNode not synched" );
    ASSERT_TRUE( node->SelectAccount( account1_address ).has_value() );
}
