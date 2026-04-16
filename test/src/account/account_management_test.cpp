#include <WalletCore/HDWallet.h>
#include <gtest/gtest.h>

#include <chrono>

#include <boost/dll/runtime_symbol_info.hpp>

#include "account/GeniusAccount.hpp"
#include "account/GeniusNode.hpp"
#include "testutil/wait_condition.hpp"

static sgns::TokenID TOKEN_ID = sgns::TokenID::FromBytes( { 0x00 } );

class AccountManagement : public ::testing::Test
{
public:
    static inline boost::filesystem::path path = boost::dll::program_location().parent_path() / "account";

    AccountManagement()
    {
        try
        {
            boost::filesystem::remove_all( path );
        }
        catch ( ... ) //NOLINT(bugprone-empty-catch)
        {
        }
        node_ = GeniusNode::New( { "0xcafe", "0.65", "1.0", TOKEN_ID, path.generic_string() },
                                 false,
                                 false,
                                 40069,
                                 true );
        sgns::Blockchain::SetAuthorizedFullNodeAddress( node_->GetAddress() );
        assert( node_ != nullptr );
        test::assertWaitForCondition(
            [&] { return node_->GetTransactionManagerState() == TransactionManager::State::READY; },
            std::chrono::milliseconds( 40000 ),
            "node not synced" );
    }

    std::shared_ptr<GeniusNode> node_;
};

using namespace sgns;

TEST_F( AccountManagement, CantSelectAccountThatWasNotAdded )
{
    ASSERT_TRUE( node_->SelectAccount( "foobar" ).has_error() );
}

TEST_F( AccountManagement, CanSelectAccountThatWasAdded )
{
    auto         old_account_address = node_->GetAddress();
    TW::HDWallet wallet( 128, "" );
    auto         new_account_address = GeniusAccount::NewFromMnemonic( TOKEN_ID, wallet.getMnemonic(), path, true )
                                           ->GetAddress();
    ASSERT_TRUE( node_->SelectAccount( new_account_address ).has_value() );
    test::assertWaitForCondition( [&]
                                  { return node_->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 40000 ),
                                  "node not synced" );
    ASSERT_EQ( node_->GetAddress(), new_account_address );
    // Can go back to previous account
    ASSERT_TRUE( node_->SelectAccount( old_account_address ).has_value() );
    test::assertWaitForCondition( [&]
                                  { return node_->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 40000 ),
                                  "node not synced" );
}

TEST_F( AccountManagement, TransferAccount )
{
    ASSERT_TRUE( node_->MintTokens( 200, "", "", TOKEN_ID, "", GeniusNode::TIMEOUT_MINT ).has_value() );
    auto         balance = node_->GetBalance();
    TW::HDWallet wallet( 128, "" );
    auto         other_account_address = GeniusAccount::NewFromMnemonic( TOKEN_ID, wallet.getMnemonic(), path, true )
                                             ->GetAddress();
    ASSERT_TRUE( node_->TransferAccount( other_account_address ).has_value() );
    test::assertWaitForCondition( [&]
                                  { return node_->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 40000 ),
                                  "node not synced" );
    ASSERT_EQ( node_->GetBalance(), balance );
}

TEST_F( AccountManagement, CanDeleteAccount )
{
    auto         old_account_address = node_->GetAddress();
    TW::HDWallet wallet( 128, "" );
    auto         new_account_address = GeniusAccount::NewFromMnemonic( TOKEN_ID, wallet.getMnemonic(), path, true )
                                           ->GetAddress();
    ASSERT_TRUE( node_->SelectAccount( new_account_address ).has_value() );
    test::assertWaitForCondition( [&]
                                  { return node_->GetTransactionManagerState() == TransactionManager::State::READY; },
                                  std::chrono::milliseconds( 40000 ),
                                  "node not synced" );
    ASSERT_TRUE( node_->DeleteAccount( old_account_address ).has_value() );
    ASSERT_TRUE( node_->SelectAccount( old_account_address ).has_error() );
}
