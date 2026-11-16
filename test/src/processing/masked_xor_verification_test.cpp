/**
 * @file masked_xor_verification_test.cpp
 * @brief Unit tests, including adversarial cases, for parallel nonce-masked XOR.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "processing/masked_xor_verification.hpp"

namespace
{
    using namespace sgns::processing::masked_xor;

    Assignment MakeAssignment( const std::string &job_id,
                               const std::string &assignment_id,
                               const std::string &node_id,
                               uint32_t           local_index,
                               char               seed )
    {
        const std::string nonce( 32, seed );
        const std::string salt( 16, static_cast<char>( seed + 1 ) );
        const auto mask = DeriveNonceMask( job_id, assignment_id, local_index, nonce, salt );
        return { job_id, assignment_id, node_id, local_index, mask.value() };
    }

    Overlap MakePair( const std::string &job_id = "job-one" )
    {
        return { MakeAssignment( job_id, "opaque-a", "worker-a", 2, 'a' ),
                 MakeAssignment( job_id, "opaque-b", "worker-b", 7, 'b' ) };
    }

    std::vector<Contribution> CorrectResults( const Overlap &pair )
    {
        return { ComputeContribution( pair.first, "canonical-block" ),
                 ComputeContribution( pair.second, "canonical-block" ) };
    }

    Verdict Check( const std::vector<Overlap> &pairs, const std::vector<Contribution> &results )
    {
        return Verify( pairs.front().first.job_id, pairs, results, ExpectedNonceXor( pairs ) );
    }
} // namespace

TEST( MaskedXorVerificationTest, MatchingOutputsWithDifferentIndicesAndNoncesPass )
{
    const auto pair = MakePair();
    EXPECT_NE( pair.first.local_index, pair.second.local_index );
    EXPECT_NE( pair.first.nonce_mask, pair.second.nonce_mask );
    const auto result = Check( { pair }, CorrectResults( pair ) );
    EXPECT_EQ( Status::VALID, result.status );
    EXPECT_EQ( 1u, result.checked_pairs );
    EXPECT_EQ( Hash256{}, result.residual );
}

TEST( MaskedXorVerificationTest, NonceHashIsBoundToJobAssignmentIndexSaltAndNonce )
{
    const std::string nonce( 32, 'n' );
    const std::string salt( 16, 's' );
    const auto base = DeriveNonceMask( "job", "assignment", 3, nonce, salt );
    ASSERT_TRUE( base.has_value() );
    EXPECT_EQ( base, DeriveNonceMask( "job", "assignment", 3, nonce, salt ) );
    EXPECT_NE( base, DeriveNonceMask( "different-job", "assignment", 3, nonce, salt ) );
    EXPECT_NE( base, DeriveNonceMask( "job", "other-assignment", 3, nonce, salt ) );
    EXPECT_NE( base, DeriveNonceMask( "job", "assignment", 4, nonce, salt ) );
    EXPECT_NE( base, DeriveNonceMask( "job", "assignment", 3, std::string( 32, 'x' ), salt ) );
    EXPECT_NE( base, DeriveNonceMask( "job", "assignment", 3, nonce, std::string( 16, 'x' ) ) );
}

TEST( MaskedXorVerificationTest, WeakOrMissingNonceParametersAreRejected )
{
    EXPECT_FALSE( DeriveNonceMask( "", "id", 1, std::string( 32, 'n' ), std::string( 16, 's' ) ) );
    EXPECT_FALSE( DeriveNonceMask( "job", "", 1, std::string( 32, 'n' ), std::string( 16, 's' ) ) );
    EXPECT_FALSE( DeriveNonceMask( "job", "id", 1, std::string( 31, 'n' ), std::string( 16, 's' ) ) );
    EXPECT_FALSE( DeriveNonceMask( "job", "id", 1, std::string( 32, 'n' ), std::string( 15, 's' ) ) );
}

TEST( MaskedXorVerificationTest, ChangedWorkerOutputIsRejected )
{
    const auto pair = MakePair();
    auto results = CorrectResults( pair );
    results.front() = ComputeContribution( pair.first, "tampered-block" );
    EXPECT_EQ( Status::RESULT_MISMATCH, Check( { pair }, results ).status );
}

TEST( MaskedXorVerificationTest, ChangedVerifierOutputIsRejected )
{
    const auto pair = MakePair();
    auto results = CorrectResults( pair );
    results.back() = ComputeContribution( pair.second, "tampered-block" );
    EXPECT_EQ( Status::RESULT_MISMATCH, Check( { pair }, results ).status );
}

TEST( MaskedXorVerificationTest, WrongPrehashedNonceIsRejected )
{
    auto pair = MakePair();
    auto results = CorrectResults( pair );
    auto fake_assignment = pair.second;
    fake_assignment.nonce_mask[0] ^= 0xff;
    results.back() = ComputeContribution( fake_assignment, "canonical-block" );
    EXPECT_EQ( Status::RESULT_MISMATCH, Check( { pair }, results ).status );
}

TEST( MaskedXorVerificationTest, WrongNonceAggregateIsRejected )
{
    const auto pair = MakePair();
    auto claimed = ExpectedNonceXor( { pair } );
    claimed[0] ^= 1;
    EXPECT_EQ( Status::NONCE_AGGREGATE_MISMATCH,
               Verify( "job-one", { pair }, CorrectResults( pair ), claimed ).status );
}

TEST( MaskedXorVerificationTest, MissingResultIsRejected )
{
    const auto pair = MakePair();
    auto results = CorrectResults( pair );
    results.pop_back();
    EXPECT_EQ( Status::MISSING_RESULT, Check( { pair }, results ).status );
}

TEST( MaskedXorVerificationTest, DuplicateResultIsRejected )
{
    const auto pair = MakePair();
    auto results = CorrectResults( pair );
    results.push_back( results.front() );
    EXPECT_EQ( Status::DUPLICATE_RESULT, Check( { pair }, results ).status );
}

TEST( MaskedXorVerificationTest, UnexpectedResultIsRejected )
{
    const auto pair = MakePair();
    auto results = CorrectResults( pair );
    auto unknown = results.front();
    unknown.assignment_id = "unknown-id";
    results.push_back( unknown );
    EXPECT_EQ( Status::UNEXPECTED_RESULT, Check( { pair }, results ).status );
}

TEST( MaskedXorVerificationTest, ReplayedJobResultIsRejected )
{
    const auto pair = MakePair();
    auto results = CorrectResults( pair );
    results.front().job_id = "old-job";
    EXPECT_EQ( Status::ASSIGNMENT_MISMATCH, Check( { pair }, results ).status );
}

TEST( MaskedXorVerificationTest, WrongNodeOrLocalIndexIsRejected )
{
    const auto pair = MakePair();
    auto results = CorrectResults( pair );
    results.front().node_id = "different-node";
    EXPECT_EQ( Status::ASSIGNMENT_MISMATCH, Check( { pair }, results ).status );
    results = CorrectResults( pair );
    results.front().local_index = 99;
    EXPECT_EQ( Status::ASSIGNMENT_MISMATCH, Check( { pair }, results ).status );
}

TEST( MaskedXorVerificationTest, InvalidOverlappingAssignmentsAreRejected )
{
    EXPECT_EQ( Status::EMPTY_PLAN, Verify( "job-one", {}, {}, {} ).status );
    auto pair = MakePair();
    pair.second.assignment_id = pair.first.assignment_id;
    EXPECT_EQ( Status::INVALID_PLAN, Check( { pair }, {} ).status );
    pair = MakePair();
    pair.second.node_id = pair.first.node_id;
    EXPECT_EQ( Status::INVALID_PLAN, Check( { pair }, {} ).status );
    pair = MakePair();
    pair.second.local_index = pair.first.local_index;
    EXPECT_EQ( Status::INVALID_PLAN, Check( { pair }, {} ).status );
    pair = MakePair();
    pair.second.nonce_mask = pair.first.nonce_mask;
    EXPECT_EQ( Status::INVALID_PLAN, Check( { pair }, {} ).status );
}

TEST( MaskedXorVerificationTest, ReusingAnAssignmentInMultiplePairsIsRejected )
{
    const auto pair = MakePair();
    EXPECT_EQ( Status::INVALID_PLAN, Check( { pair, pair }, {} ).status );
}

TEST( MaskedXorVerificationTest, TwoCompensatingCorruptionsDoNotPassPairChecks )
{
    const auto pair1 = MakePair();
    const Overlap pair2{ MakeAssignment( "job-one", "opaque-c", "worker-c", 3, 'c' ),
                         MakeAssignment( "job-one", "opaque-d", "worker-d", 8, 'd' ) };
    std::vector<Overlap> pairs{ pair1, pair2 };
    auto results = CorrectResults( pair1 );
    auto more = CorrectResults( pair2 );
    results.insert( results.end(), more.begin(), more.end() );
    // Two identical XOR deltas cancel in the aggregate, but neither sampled pair agrees.
    results[0].masked_hash[0] ^= 0x5a;
    results[2].masked_hash[0] ^= 0x5a;
    const auto verdict = Check( pairs, results );
    EXPECT_EQ( Status::COMPENSATING_MISMATCH, verdict.status );
    EXPECT_EQ( Hash256{}, verdict.residual );
}

TEST( MaskedXorVerificationTest, SameIncorrectOutputOnBothNodesIsNotDetectable )
{
    // An honest limitation of sampled replication; no correctness claim for collusion.
    const auto pair = MakePair();
    const std::vector<Contribution> coordinated{
        ComputeContribution( pair.first, "same-incorrect-output" ),
        ComputeContribution( pair.second, "same-incorrect-output" ) };
    EXPECT_EQ( Status::VALID, Check( { pair }, coordinated ).status );
}

TEST( MaskedXorVerificationTest, OverallComputationDigestIsOrderedAndSeparate )
{
    const auto first = HashComputation( "block-A" );
    const auto second = HashComputation( "block-B" );
    EXPECT_EQ( OverallComputationHash( { first, second } ), OverallComputationHash( { first, second } ) );
    EXPECT_NE( OverallComputationHash( { first, second } ), OverallComputationHash( { second, first } ) );
    EXPECT_NE( OverallComputationHash( { first, second } ), OverallComputationHash( { first } ) );
}
