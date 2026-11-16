/**
 * @file masked_xor_verification.hpp
 * @brief Experimental requestor-masked, parallel sampled-computation verifier.
 * @warning This prototype is not a proof of arbitrary computation or a consensus authorization.
 */
#ifndef SUPERGENIUS_MASKED_XOR_VERIFICATION_HPP
#define SUPERGENIUS_MASKED_XOR_VERIFICATION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sgns::processing::masked_xor
{
    using Hash256 = std::array<uint8_t, 32>;

    /** A node sees its own assignment only; the requestor retains the overlap map. */
    struct Assignment
    {
        std::string job_id;
        std::string assignment_id;
        std::string node_id;
        uint32_t    local_index = 0;
        Hash256     nonce_mask{};
    };

    /** Private requestor mapping of two independently assigned copies of one physical block. */
    struct Overlap
    {
        Assignment first;
        Assignment second;
    };

    /** Submitted by a node; signature verification belongs to the caller. */
    struct Contribution
    {
        std::string job_id;
        std::string assignment_id;
        std::string node_id;
        uint32_t    local_index = 0;
        Hash256     masked_hash{};
    };

    enum class Status
    {
        VALID,
        EMPTY_PLAN,
        INVALID_PLAN,
        NONCE_AGGREGATE_MISMATCH,
        DUPLICATE_RESULT,
        UNEXPECTED_RESULT,
        MISSING_RESULT,
        ASSIGNMENT_MISMATCH,
        RESULT_MISMATCH,
        COMPENSATING_MISMATCH
    };

    struct Verdict
    {
        Status  status = Status::INVALID_PLAN;
        Hash256 residual{};
        size_t  checked_pairs = 0;
    };

    /**
     * Requestor-side derivation of the prehashed, salted nonce mask.
     * nonce and salt must be independently generated with a cryptographically secure RNG.
     * Both may remain private; the worker receives only the derived mask.
     * Requires a 32-byte nonce and a 16-byte salt (at minimum).
     */
    std::optional<Hash256> DeriveNonceMask( std::string_view job_id,
                                            std::string_view assignment_id,
                                            uint32_t         local_index,
                                            std::string_view nonce,
                                            std::string_view salt );

    /** Hash canonical output bytes with domain separation. Identical work must have identical bytes. */
    Hash256 HashComputation( std::string_view canonical_output );

    /** Node-side operation: SHA256(canonical output) XOR prehashed requestor nonce. */
    Contribution ComputeContribution( const Assignment &assignment, std::string_view canonical_output );

    /** Requestor-side XOR of nonce masks for the selected overlapping assignments. */
    Hash256 ExpectedNonceXor( const std::vector<Overlap> &overlaps );

    /** Separate ordered digest of *all* canonical output hashes; does not prove their correctness. */
    Hash256 OverallComputationHash( const std::vector<Hash256> &ordered_block_hashes );

    /**
     * Checks a precommitted nonce aggregate, exact assignment membership and sampled equality.
     * Pair checks reject compensating mismatches that a lone global XOR would miss.
     * No compute-stage barrier is needed: nodes can execute and publish independently.
     * The caller must authenticate assignment owners and precommit the plan before results arrive.
     */
    Verdict Verify( std::string_view                 job_id,
                    const std::vector<Overlap>      &overlaps,
                    const std::vector<Contribution> &results,
                    const Hash256                   &precommitted_nonce_xor );
}

#endif // SUPERGENIUS_MASKED_XOR_VERIFICATION_HPP
