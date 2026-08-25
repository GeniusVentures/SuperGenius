#include <gtest/gtest.h>

#include <string>

#include "account/TokenID.hpp"
#include "processing/processing_validation_core.hpp"

namespace
{
    std::string Address( char digit )
    {
        return std::string( 128, digit );
    }

    SGProcessing::SubTask MakeSubTask()
    {
        SGProcessing::SubTask subtask;
        subtask.set_subtaskid( "subtask" );
        subtask.add_chunkstoprocess()->set_chunkid( "chunk" );
        return subtask;
    }

    SGProcessing::SubTaskResult MakeResult()
    {
        const auto token_id = sgns::TokenID::FromBytes( { 0x01 } );

        SGProcessing::SubTaskResult result;
        result.set_subtaskid( "subtask" );
        result.add_chunk_hashes( "hash" );
        result.set_node_address( Address( '1' ) );
        result.set_developer_address( Address( '2' ) );
        result.set_developer_cut( 700'000 );
        result.set_token_id( token_id.bytes().data(), token_id.size() );
        return result;
    }
} // namespace

TEST( ProcessingPayoutMetadataTest, AcceptsCompleteMetadata )
{
    sgns::processing::ProcessingValidationCore validator;
    EXPECT_TRUE( validator.ValidateIndividualResult( MakeSubTask(), MakeResult() ).has_value() );
}

TEST( ProcessingPayoutMetadataTest, RejectsInvalidMetadata )
{
    sgns::processing::ProcessingValidationCore validator;
    const auto                                  subtask = MakeSubTask();

    auto result = MakeResult();
    result.clear_developer_address();
    EXPECT_EQ( validator.ValidateIndividualResult( subtask, result ).error(),
               make_error_code( sgns::processing::ProcessingValidationCore::Error::INVALID_PAYOUT_METADATA ) );

    result = MakeResult();
    result.set_node_address( "node" );
    EXPECT_TRUE( validator.ValidateIndividualResult( subtask, result ).has_error() );

    result = MakeResult();
    result.set_token_id( std::string( 31, '\0' ) );
    EXPECT_TRUE( validator.ValidateIndividualResult( subtask, result ).has_error() );

    result = MakeResult();
    result.set_developer_cut( 1'000'001 );
    EXPECT_TRUE( validator.ValidateIndividualResult( subtask, result ).has_error() );
}
