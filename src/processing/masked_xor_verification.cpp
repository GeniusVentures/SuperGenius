/**
 * @file masked_xor_verification.cpp
 * @brief Requestor nonce masking and verification of sampled parallel computations.
 */
#include "processing/masked_xor_verification.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "crypto/sha/sha256.hpp"

namespace sgns::processing::masked_xor
{
    namespace
    {
        void AppendField( std::string &bytes, std::string_view field )
        {
            const auto length = static_cast<uint64_t>( field.size() );
            for ( int shift = 56; shift >= 0; shift -= 8 )
            {
                bytes.push_back( static_cast<char>( ( length >> shift ) & 0xffu ) );
            }
            bytes.append( field.data(), field.size() );
        }

        void AppendIndex( std::string &bytes, uint32_t index )
        {
            for ( int shift = 24; shift >= 0; shift -= 8 )
            {
                bytes.push_back( static_cast<char>( ( index >> shift ) & 0xffu ) );
            }
        }

        Hash256 HashBytes( std::string_view bytes )
        {
            const auto digest = crypto::sha256( bytes.data(), bytes.size() );
            Hash256    result{};
            if ( digest.size() == result.size() )
            {
                std::copy( digest.begin(), digest.end(), result.begin() );
            }
            return result;
        }

        void XorInto( Hash256 &left, const Hash256 &right )
        {
            for ( size_t i = 0; i < left.size(); ++i )
            {
                left[i] ^= right[i];
            }
        }

        bool IsZero( const Hash256 &value )
        {
            return std::all_of( value.begin(), value.end(), []( uint8_t byte ) { return byte == 0; } );
        }

        bool CheckAssignment( std::string_view job_id, const Assignment &assignment )
        {
            return assignment.job_id == job_id && !assignment.assignment_id.empty() && !assignment.node_id.empty();
        }
    } // namespace

    std::optional<Hash256> DeriveNonceMask( std::string_view job_id,
                                            std::string_view assignment_id,
                                            uint32_t         local_index,
                                            std::string_view nonce,
                                            std::string_view salt )
    {
        if ( job_id.empty() || assignment_id.empty() || nonce.size() < 32u || salt.size() < 16u )
        {
            return std::nullopt;
        }
        std::string preimage( "GNUS-NONCE-MASK-V1" );
        AppendField( preimage, job_id );
        AppendField( preimage, assignment_id );
        AppendIndex( preimage, local_index );
        AppendField( preimage, salt );
        AppendField( preimage, nonce );
        return HashBytes( preimage );
    }

    Hash256 HashComputation( std::string_view canonical_output )
    {
        std::string preimage( "GNUS-COMPUTE-BLOCK-V1" );
        AppendField( preimage, canonical_output );
        return HashBytes( preimage );
    }

    Contribution ComputeContribution( const Assignment &assignment, std::string_view canonical_output )
    {
        auto masked = HashComputation( canonical_output );
        XorInto( masked, assignment.nonce_mask );
        return { assignment.job_id, assignment.assignment_id, assignment.node_id, assignment.local_index, masked };
    }

    Hash256 ExpectedNonceXor( const std::vector<Overlap> &overlaps )
    {
        Hash256 expected{};
        for ( const auto &overlap : overlaps )
        {
            XorInto( expected, overlap.first.nonce_mask );
            XorInto( expected, overlap.second.nonce_mask );
        }
        return expected;
    }

    Hash256 OverallComputationHash( const std::vector<Hash256> &ordered_block_hashes )
    {
        std::string preimage( "GNUS-COMPUTE-JOB-V1" );
        AppendIndex( preimage, static_cast<uint32_t>( ordered_block_hashes.size() ) );
        for ( const auto &digest : ordered_block_hashes )
        {
            preimage.append( reinterpret_cast<const char *>( digest.data() ), digest.size() );
        }
        return HashBytes( preimage );
    }

    Verdict Verify( std::string_view                 job_id,
                    const std::vector<Overlap>      &overlaps,
                    const std::vector<Contribution> &results,
                    const Hash256                   &precommitted_nonce_xor )
    {
        if ( job_id.empty() || overlaps.empty() )
        {
            return { Status::EMPTY_PLAN, {}, 0 };
        }

        std::unordered_map<std::string, const Assignment *> expected;
        expected.reserve( overlaps.size() * 2u );
        for ( const auto &overlap : overlaps )
        {
            const auto &a = overlap.first;
            const auto &b = overlap.second;
            if ( !CheckAssignment( job_id, a ) || !CheckAssignment( job_id, b ) ||
                 a.assignment_id == b.assignment_id || a.node_id == b.node_id ||
                 a.local_index == b.local_index || a.nonce_mask == b.nonce_mask ||
                 !expected.emplace( a.assignment_id, &a ).second ||
                 !expected.emplace( b.assignment_id, &b ).second )
            {
                return { Status::INVALID_PLAN, {}, 0 };
            }
        }

        if ( ExpectedNonceXor( overlaps ) != precommitted_nonce_xor )
        {
            return { Status::NONCE_AGGREGATE_MISMATCH, {}, 0 };
        }

        std::unordered_map<std::string, const Contribution *> submitted;
        submitted.reserve( results.size() );
        for ( const auto &result : results )
        {
            if ( !submitted.emplace( result.assignment_id, &result ).second )
            {
                return { Status::DUPLICATE_RESULT, {}, 0 };
            }
            auto found = expected.find( result.assignment_id );
            if ( found == expected.end() )
            {
                return { Status::UNEXPECTED_RESULT, {}, 0 };
            }
            const auto &assignment = *found->second;
            if ( result.job_id != assignment.job_id || result.node_id != assignment.node_id ||
                 result.local_index != assignment.local_index )
            {
                return { Status::ASSIGNMENT_MISMATCH, {}, 0 };
            }
        }
        if ( submitted.size() != expected.size() )
        {
            return { Status::MISSING_RESULT, {}, 0 };
        }

        Hash256 aggregate = precommitted_nonce_xor;
        bool    bad_pair  = false;
        for ( const auto &overlap : overlaps )
        {
            const auto &a = submitted.at( overlap.first.assignment_id )->masked_hash;
            const auto &b = submitted.at( overlap.second.assignment_id )->masked_hash;
            XorInto( aggregate, a );
            XorInto( aggregate, b );

            auto pair_residual = a;
            XorInto( pair_residual, b );
            XorInto( pair_residual, overlap.first.nonce_mask );
            XorInto( pair_residual, overlap.second.nonce_mask );
            bad_pair = bad_pair || !IsZero( pair_residual );
        }

        if ( bad_pair )
        {
            return { IsZero( aggregate ) ? Status::COMPENSATING_MISMATCH : Status::RESULT_MISMATCH,
                     aggregate,
                     overlaps.size() };
        }
        return { IsZero( aggregate ) ? Status::VALID : Status::RESULT_MISMATCH, aggregate, overlaps.size() };
    }
} // namespace sgns::processing::masked_xor
