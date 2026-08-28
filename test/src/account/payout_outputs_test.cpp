#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "account/TransactionManager.hpp"

namespace sgns
{
    class PayoutOutputsTestAccess
    {
    public:
        static outcome::result<std::vector<OutputDestInfo>> Build( const SGProcessing::TaskResult &task_result,
                                                                   uint64_t escrow_amount,
                                                                   const TokenID &escrow_token_id,
                                                                   uint64_t burn_basis_points )
        {
            return TransactionManager::BuildPayoutOutputs(
                task_result, escrow_amount, escrow_token_id, burn_basis_points );
        }
    };
}

namespace
{
    const sgns::TokenID ESCROW_TOKEN = sgns::TokenID::FromBytes( { 0x00 } );

    std::string Address( char digit )
    {
        return std::string( 128, digit );
    }

    sgns::TokenID Token( uint8_t marker )
    {
        return sgns::TokenID::FromBytes( { marker } );
    }

    void AddResult( SGProcessing::TaskResult &task_result,
                    std::string subtask_id,
                    std::string peer_address,
                    std::string developer_address,
                    uint64_t developer_cut,
                    const sgns::TokenID &token_id )
    {
        auto *result = task_result.add_subtask_results();
        result->set_subtaskid( std::move( subtask_id ) );
        result->set_node_address( std::move( peer_address ) );
        result->set_developer_address( std::move( developer_address ) );
        result->set_developer_cut( developer_cut );
        result->set_token_id( token_id.bytes().data(), token_id.size() );
    }

    void ExpectOutput( const sgns::OutputDestInfo &output,
                       uint64_t amount,
                       const std::string &address,
                       const sgns::TokenID &token_id )
    {
        EXPECT_EQ( output.encrypted_amount, amount );
        EXPECT_EQ( output.dest_address, address );
        EXPECT_EQ( output.token_id, token_id );
    }
}

TEST( PayoutOutputsTest, AppliesEachResultsDeveloperCutAndToken )
{
    const auto peer_1 = Address( '1' );
    const auto peer_2 = Address( '2' );
    const auto dev_1  = Address( '3' );
    const auto dev_2  = Address( '4' );
    const auto token_1 = Token( 0x11 );
    const auto token_2 = Token( 0x22 );

    SGProcessing::TaskResult task_result;
    AddResult( task_result, "subtask-b", peer_2, dev_1, 200'000, token_2 );
    AddResult( task_result, "subtask-a", peer_1, dev_2, 700'000, token_1 );

    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 20, ESCROW_TOKEN, 0 );
    ASSERT_TRUE( outputs.has_value() );
    ASSERT_EQ( outputs.value().size(), 5U );
    ExpectOutput( outputs.value()[0], 8, peer_2, token_2 );
    ExpectOutput( outputs.value()[1], 3, peer_1, token_1 );
    ExpectOutput( outputs.value()[2], 2, dev_1, token_2 );
    ExpectOutput( outputs.value()[3], 7, dev_2, token_1 );
    ExpectOutput( outputs.value()[4], 0, "0x0000000000000000000000000000000000000000", ESCROW_TOKEN );
}

TEST( PayoutOutputsTest, BurnsSplitRemainderAndAggregatesDeveloperCredits )
{
    const auto developer = Address( 'a' );
    const auto token     = Token( 0x33 );

    SGProcessing::TaskResult task_result;
    AddResult( task_result, "a", Address( '1' ), developer, 500'000, token );
    AddResult( task_result, "b", Address( '2' ), developer, 500'000, token );
    AddResult( task_result, "c", Address( '3' ), developer, 500'000, token );

    // 11 does not split evenly three ways: each result gets 3, the 2 leftover minions are burned.
    // Every result's cut is 0.5, floored to 1, so each peer takes the 2-minion floor residue.
    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 11, ESCROW_TOKEN, 0 );
    ASSERT_TRUE( outputs.has_value() );
    ASSERT_EQ( outputs.value().size(), 5U );
    ExpectOutput( outputs.value()[0], 2, Address( '1' ), token );
    ExpectOutput( outputs.value()[1], 2, Address( '2' ), token );
    ExpectOutput( outputs.value()[2], 2, Address( '3' ), token );
    ExpectOutput( outputs.value()[3], 3, developer, token );
    ExpectOutput( outputs.value()[4], 2, "0x0000000000000000000000000000000000000000", ESCROW_TOKEN );
}

TEST( PayoutOutputsTest, OmitsZeroPeerAndDeveloperCreditsAndKeepsBurn )
{
    SGProcessing::TaskResult task_result;
    AddResult( task_result, "a", Address( '1' ), Address( '3' ), 0, Token( 0x11 ) );
    AddResult( task_result, "b", Address( '2' ), Address( '4' ), 1'000'000, Token( 0x22 ) );

    auto outputs = sgns::PayoutOutputsTestAccess::Build( task_result, 1'000, ESCROW_TOKEN, 100 );
    ASSERT_TRUE( outputs.has_value() );
    ASSERT_EQ( outputs.value().size(), 3U );
    ExpectOutput( outputs.value()[0], 495, Address( '1' ), Token( 0x11 ) );
    ExpectOutput( outputs.value()[1], 495, Address( '4' ), Token( 0x22 ) );
    ExpectOutput( outputs.value()[2], 10, "0x0000000000000000000000000000000000000000", ESCROW_TOKEN );
}

TEST( PayoutOutputsTest, SkipsInvalidPayoutMetadataAndPaysTheRest )
{
    const auto valid_token = Token( 0x11 );
    const auto valid_peer  = Address( '1' );
    const auto valid_dev   = Address( '2' );

    // A bad entry must not block the payout for honest peers, and must not be rewarded either.
    const auto build = []( const SGProcessing::TaskResult &task_result )
    { return sgns::PayoutOutputsTestAccess::Build( task_result, 10, ESCROW_TOKEN, 0 ); };

    SGProcessing::TaskResult missing_developer;
    AddResult( missing_developer, "a", valid_peer, {}, 0, valid_token );
    AddResult( missing_developer, "b", Address( '3' ), valid_dev, 0, valid_token );
    auto outputs = build( missing_developer );
    ASSERT_TRUE( outputs.has_value() );
    ASSERT_EQ( outputs.value().size(), 2U );
    ExpectOutput( outputs.value()[0], 10, Address( '3' ), valid_token );
    ExpectOutput( outputs.value()[1], 0, "0x0000000000000000000000000000000000000000", ESCROW_TOKEN );

    SGProcessing::TaskResult invalid_peer;
    AddResult( invalid_peer, "a", "peer", valid_dev, 0, valid_token );
    AddResult( invalid_peer, "b", Address( '3' ), valid_dev, 0, valid_token );
    outputs = build( invalid_peer );
    ASSERT_TRUE( outputs.has_value() );
    ASSERT_EQ( outputs.value().size(), 2U );
    ExpectOutput( outputs.value()[0], 10, Address( '3' ), valid_token );
    ExpectOutput( outputs.value()[1], 0, "0x0000000000000000000000000000000000000000", ESCROW_TOKEN );

    SGProcessing::TaskResult invalid_token;
    AddResult( invalid_token, "a", valid_peer, valid_dev, 0, valid_token );
    invalid_token.mutable_subtask_results( 0 )->set_token_id( std::string( 31, '\0' ) );
    AddResult( invalid_token, "b", Address( '3' ), valid_dev, 0, valid_token );
    outputs = build( invalid_token );
    ASSERT_TRUE( outputs.has_value() );
    ASSERT_EQ( outputs.value().size(), 2U );
    ExpectOutput( outputs.value()[0], 10, Address( '3' ), valid_token );
    ExpectOutput( outputs.value()[1], 0, "0x0000000000000000000000000000000000000000", ESCROW_TOKEN );

    SGProcessing::TaskResult invalid_cut;
    AddResult( invalid_cut, "a", valid_peer, valid_dev, 1'000'001, valid_token );
    AddResult( invalid_cut, "b", Address( '3' ), valid_dev, 0, valid_token );
    outputs = build( invalid_cut );
    ASSERT_TRUE( outputs.has_value() );
    ASSERT_EQ( outputs.value().size(), 2U );
    ExpectOutput( outputs.value()[0], 10, Address( '3' ), valid_token );
    ExpectOutput( outputs.value()[1], 0, "0x0000000000000000000000000000000000000000", ESCROW_TOKEN );

    // A duplicated subtask result pays out once, for the first entry.
    SGProcessing::TaskResult duplicate;
    AddResult( duplicate, "a", valid_peer, valid_dev, 0, valid_token );
    AddResult( duplicate, "a", Address( '3' ), Address( '4' ), 0, valid_token );
    outputs = build( duplicate );
    ASSERT_TRUE( outputs.has_value() );
    ASSERT_EQ( outputs.value().size(), 2U );
    ExpectOutput( outputs.value()[0], 10, valid_peer, valid_token );
    ExpectOutput( outputs.value()[1], 0, "0x0000000000000000000000000000000000000000", ESCROW_TOKEN );

    // With nothing valid left there is nothing to pay, so building fails.
    SGProcessing::TaskResult only_invalid;
    AddResult( only_invalid, "a", "peer", valid_dev, 0, valid_token );
    EXPECT_TRUE( build( only_invalid ).has_error() );

    SGProcessing::TaskResult empty;
    EXPECT_TRUE( build( empty ).has_error() );
}
