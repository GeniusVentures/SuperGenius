/**
 * @file       mint_source_hash.hpp
 * @brief      Auxiliary function to generate unique mint source hashes for testing.
 * @date       2026-04-17
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef TESTUTIL_MINT_SOURCE_HASH_HPP
#define TESTUTIL_MINT_SOURCE_HASH_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <random>
#include <string>

namespace sgns::test
{
    /**
     * @brief       Computes a unique hash string to be used as a mint source identifier in tests.
     * @return      unique hash string
     */
    inline std::string NextMintSourceHash()
    {
        static std::atomic<uint64_t> counter{ 0 };
        static std::mutex            rng_mutex;
        static std::mt19937_64       rng(
            []
            {
                const auto now = static_cast<uint64_t>(
                    std::chrono::high_resolution_clock::now().time_since_epoch().count() );
                std::random_device rd;
                std::seed_seq      seed{ static_cast<uint32_t>( now ),
                                    static_cast<uint32_t>( now >> 32 ),
                                    rd(),
                                    rd(),
                                    rd(),
                                    rd() };
                return std::mt19937_64( seed );
            }() );

        const uint64_t sequence = counter.fetch_add( 1, std::memory_order_relaxed );
        uint64_t       random_hi;
        uint64_t       random_lo;

        {
            std::lock_guard<std::mutex> lock( rng_mutex );
            random_hi = rng();
            random_lo = rng();
        }

        char buffer[65] = {};
        std::snprintf( buffer,
                       sizeof( buffer ),
                       "%016llx%016llx%016llx%016llx",
                       static_cast<unsigned long long>( random_hi ),
                       static_cast<unsigned long long>( random_lo ),
                       static_cast<unsigned long long>( sequence ),
                       static_cast<unsigned long long>( sequence ^ random_hi ^ random_lo ) );
        return std::string( buffer );
    }
} // namespace sgns::test

#endif // TESTUTIL_MINT_SOURCE_HASH_HPP
