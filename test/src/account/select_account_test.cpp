#include <chrono>
#include <gtest/gtest.h>
#include <boost/dll/runtime_symbol_info.hpp>

#include "testutil/wait_condition.hpp"

#include "account/GeniusNode.hpp"

static sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

class AccountSelection : public ::testing::Test
{
public:
    static inline boost::filesystem::path path = boost::dll::program_location().parent_path() / "account";

    AccountSelection()
    {
        try
        {
            boost::filesystem::remove_all( path );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }
    }
};

using namespace sgns;

TEST_F( AccountSelection, CantSelectAccountThatWasNotAdded )
{
    auto node = GeniusNode::New( { "0xcafe", "0.65", "1.0", TOKEN_ID, path.generic_string() },
                                 false,
                                 false,
                                 40069,
                                 true );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
    ASSERT_FALSE( node == nullptr ) << "Could not create genius node";
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 40000 ),
                                  "fullNode not synched" );
    ASSERT_TRUE( node->SelectAccount( "foobar" ).has_error() );
}

TEST_F( AccountSelection, CanSelectAccountThatWasAdded )
{
    auto node = GeniusNode::New( { "0xcafe", "0.65", "1.0", TOKEN_ID, path.generic_string() },
                                 false,
                                 false,
                                 40069,
                                 true );
    sgns::Blockchain::SetAuthorizedFullNodeAddress( node->GetAddress() );
    ASSERT_FALSE( node == nullptr ) << "Could not create genius node";
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 40000 ),
                                  "fullNode not synched" );

    auto new_account_address = GeniusAccount::New( TOKEN_ID, path, true )->GetAddress();
    auto available_accounts  = GeniusAccount::GetAvailableAccounts( path );
    ASSERT_FALSE( std::find( available_accounts.begin(), available_accounts.end(), new_account_address ) ==
                  available_accounts.end() );
    ASSERT_TRUE( node->SelectAccount( new_account_address ).has_value() );
    test::assertWaitForCondition( [&]()
                                  { return node->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 40000 ),
                                  "fullNode not synched" );
    ASSERT_EQ( node->GetAddress(), new_account_address );
}
