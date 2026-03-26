#include <gtest/gtest.h>

#include "testutil/wait_condition.hpp"

#include "account/GeniusNode.hpp"

class AccountSelection : public ::testing::Test
{
public:
    ~AccountSelection() override
    {
        cleanup();
    }

    std::function<void( void )> cleanup;
};

using namespace sgns;

TEST_F( AccountSelection, CantSelectAccountThatWasNotAdded )
{
    this->cleanup = [] { std::filesystem::remove_all( "./account1" ); };
    auto node     = GeniusNode::New( { "0xcafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./account1" } );
    ASSERT_FALSE( node == nullptr ) << "Could not create genius node";
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 60000 ),
                                  "fullNode not synched" );
    // ASSERT_TRUE( node->SelectAccount( "foobar" ).has_error() );
}

TEST_F( AccountSelection, CanSelectAccountThatWasAdded )
{
    this->cleanup = []
    {
        std::filesystem::remove_all( "./account1" );
        std::filesystem::remove_all( "./account2" );
    };
    auto node = GeniusNode::New(
        { "0xafadafedeacafe", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./account1" } );
    auto account1_address = node->GetAddress();
    ASSERT_FALSE( node == nullptr ) << "Could not create first genius account";
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 30000 ),
                                  "fullNode not synched" );
    node.reset();
    node = GeniusNode::New( { "0xdeadbeef", "0.65", "1.0", sgns::TokenID::FromBytes( { 0x00 } ), "./account2" } );
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 30000 ),
                                  "fullNode not synched" );
    ASSERT_TRUE( node->SelectAccount( account1_address ).has_value() );
}
