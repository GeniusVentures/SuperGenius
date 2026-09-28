/**
 * @file       two_party_barrier.hpp
 * @brief      Rendezvous point for tests that must release two racing
 *             threads at the same instant.
 */
#ifndef SGNS_TESTUTIL_TWO_PARTY_BARRIER_HPP
#define SGNS_TESTUTIL_TWO_PARTY_BARRIER_HPP

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace sgns::testutil
{
    class TwoPartyBarrier
    {
    public:
        void ArriveAndWait()
        {
            std::unique_lock<std::mutex> lock( mutex_ );
            if ( ++arrived_ == 2 )
            {
                released_ = true;
                condition_.notify_all();
                return;
            }
            condition_.wait( lock, [&] { return released_; } );
        }

    private:
        std::mutex              mutex_;
        std::condition_variable condition_;
        size_t                  arrived_  = 0;
        bool                    released_ = false;
    };
} // namespace sgns::testutil

#endif // SGNS_TESTUTIL_TWO_PARTY_BARRIER_HPP
