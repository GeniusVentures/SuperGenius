/**
 * @file masked_xor_verification_functional_test.cpp
 * @brief Parallel N+1 job simulation and failure scenarios for sampled computation.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <future>
#include <string>
#include <vector>

#include "processing/masked_xor_verification.hpp"

namespace
{
    using namespace sgns::processing::masked_xor;

    Assignment PreparedAssignment( size_t index, bool copy )
    {
        const std::string assignment = std::string( copy ? "opaque-check-" : "opaque-work-" ) + std::to_string( index );
        const std::string node = copy ? "node-hidden-check" : "node-" + std::to_string( index );
        const auto local_index = static_cast<uint32_t>( copy ? index + 50u : index );
        const std::string nonce( 32, static_cast<char>( index + ( copy ? 65 : 97 ) ) );
        const std::string salt( 16, static_cast<char>( index + ( copy ? 33 : 51 ) ) );
        auto mask = DeriveNonceMask( "parallel-job", assignment, local_index, nonce, salt );
        return { "parallel-job", assignment, node, local_index, mask.value() };
    }

    struct Run
    {
        std::vector<Overlap> overlaps;
        std::vector<Contribution> submissions;
    };

    Run Simulate( bool corrupt_worker )
    {
        Run run;
        std::vector<std::future<Contribution>> futures;
        constexpr size_t block_count = 12;
        for ( size_t i = 0; i < block_count; ++i )
        {
            auto first = PreparedAssignment( i, false );
            auto second = PreparedAssignment( i, true );
            run.overlaps.push_back( { first, second } );
            const auto output = "computed-block-" + std::to_string( i );
            futures.emplace_back( std::async( std::launch::async, [first, output, corrupt_worker, i]()
            {
                return ComputeContribution( first, corrupt_worker && i == 5 ? "wrong-output" : output );
            } ) );
            futures.emplace_back( std::async( std::launch::async, [second, output]()
            {
                return ComputeContribution( second, output );
            } ) );
        }
        // Different completion/arrival order must not affect XOR aggregation.
        for ( auto it = futures.rbegin(); it != futures.rend(); ++it )
        {
            run.submissions.push_back( it->get() );
        }
        return run;
    }
} // namespace

TEST( MaskedXorVerificationFunctionalTest, TwelveWorkersAndHiddenOverlapNodePassInParallel )
{
    auto run = Simulate( false );
    EXPECT_EQ( 12u, run.overlaps.size() );
    EXPECT_EQ( 24u, run.submissions.size() );
    for ( const auto &pair : run.overlaps )
    {
        EXPECT_NE( pair.first.assignment_id, pair.second.assignment_id );
        EXPECT_NE( pair.first.local_index, pair.second.local_index );
        EXPECT_NE( pair.first.nonce_mask, pair.second.nonce_mask );
    }
    auto verdict = Verify( "parallel-job", run.overlaps, run.submissions, ExpectedNonceXor( run.overlaps ) );
    EXPECT_EQ( Status::VALID, verdict.status );
    EXPECT_EQ( 12u, verdict.checked_pairs );
}

TEST( MaskedXorVerificationFunctionalTest, IncorrectWorkerResultIsRejected )
{
    auto run = Simulate( true );
    EXPECT_EQ( Status::RESULT_MISMATCH,
               Verify( "parallel-job", run.overlaps, run.submissions, ExpectedNonceXor( run.overlaps ) ).status );
}

TEST( MaskedXorVerificationFunctionalTest, LostResultIsRejected )
{
    auto run = Simulate( false );
    run.submissions.erase( run.submissions.begin() );
    EXPECT_EQ( Status::MISSING_RESULT,
               Verify( "parallel-job", run.overlaps, run.submissions, ExpectedNonceXor( run.overlaps ) ).status );
}

TEST( MaskedXorVerificationFunctionalTest, CrossJobReplayIsRejected )
{
    auto run = Simulate( false );
    run.submissions.back().job_id = "another-job";
    EXPECT_EQ( Status::ASSIGNMENT_MISMATCH,
               Verify( "parallel-job", run.overlaps, run.submissions, ExpectedNonceXor( run.overlaps ) ).status );
}
