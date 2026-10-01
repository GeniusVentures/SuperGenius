/**
 * Header file for LocalPriceManager — the device-side local price manager
 * (Phase 3, D-15): the counterpart of the Worker's server-side
 * PriceCoordinator durable object. Same single-flight idea, two scopes.
 *
 * A self-contained strand-serialized state machine (D-02/D-03): one
 * io_context + one work guard + one strand + ONE dedicated runner thread.
 * All state (the L1 cache, later the per-currency pending windows) is
 * strand-confined — there is not a single mutex in this class.
 */
#pragma once

#include "IPriceSource.hpp"
#include "PriceFetchError.hpp"
#include "PriceQuote.hpp"

#include "boost/asio.hpp"

#include "base/logger.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sgns
{
    class LocalPriceManager
    {
    public:
        /// @brief Injectable clock for hermetic band tests — same type as
        /// RateLimitHoldOff::Clock (PriceRetryPolicy.hpp).
        using Clock = std::function<std::chrono::system_clock::time_point()>;

        /// @brief Strand over the manager's io_context executor (P-3 typedef
        /// precedent: ws_client_impl.hpp:22). All manager state is confined
        /// to handlers running on this strand.
        using Strand = boost::asio::strand<boost::asio::io_context::executor_type>;

        /// @brief Construct the manager and start its runner thread.
        /// @param coinGeckoTier Tier 1 — CoinGecko direct (IPriceSource seam, D-13)
        /// @param gnusServiceTier Tier 2 — token.gnus.ai (held from day one;
        /// the chain walk reaches it in 03-03 — the ctor signature is final
        /// for the phase)
        /// @param coalescingWindow Batch-collection window before dispatch
        /// (D-05, default 50ms; tests inject 0ms / 1000ms regimes)
        /// @param now Injectable clock for hermetic freshness-band tests
        LocalPriceManager( std::shared_ptr<IPriceSource> coinGeckoTier,
                           std::shared_ptr<IPriceSource> gnusServiceTier,
                           std::chrono::milliseconds      coalescingWindow = std::chrono::milliseconds( 50 ),
                           Clock                          now              = [] { return std::chrono::system_clock::now(); } );

        /// @brief Drain-then-join teardown: posts a shutdown handler onto the
        /// strand (03-02 extends it to cancel window timers and resolve
        /// parked waiters), releases the work guard, then joins the runner
        /// thread. ioc_->stop() is NEVER called — it would abandon queued
        /// handlers.
        ~LocalPriceManager();

        LocalPriceManager( const LocalPriceManager & )            = delete;
        LocalPriceManager &operator=( const LocalPriceManager & ) = delete;

        /// @brief Get quotes for the given asset ids, serving fresh L1
        /// entries with zero network (LPM-01) and fetching only misses.
        /// @param ids Asset ids, e.g. {"genius-ai"}
        /// @param currency Target currency, default "usd"
        /// @return One PriceQuote per servable requested id, or a
        /// status-bearing PriceFetchFailure
        /// @note BLOCKING (D-01): posts a handler onto the strand and parks
        /// the calling thread on a future until the chain resolves. Worst-case
        /// latency is bounded by the tier retry schedule when both tiers time
        /// out.
        /// @note MUST NOT be called from the manager's own runner thread —
        /// the future-wait would self-deadlock (the GeniusNode::
        /// HostConnectedness caveat). Phase 3 callers (test threads) and
        /// Phase 4 node threads are all off-thread — safe by construction.
        PriceResult<std::vector<PriceQuote>> GetQuotes( const std::vector<std::string> &ids,
                                                        const std::string              &currency = "usd" );

    private:
        /// @brief Strand-side request handler: D-06 fresh/miss split over the
        /// L1 cache, then single-tier inline dispatch for the miss set
        /// (03-02 replaces the immediate dispatch with window coalescing).
        void HandleRequestOnStrand( const std::vector<std::string>                     &ids,
                                    const std::string                                  &currency,
                                    std::promise<PriceResult<std::vector<PriceQuote>>> done );

        /// @brief Store tier-success quotes in the L1 cache, keyed
        /// currency -> id, keeping fetch-time source and timestamp verbatim.
        /// Only ever called on tier SUCCESS — no negative caching.
        void StoreInL1( const std::vector<PriceQuote> &quotes );

        // Declaration order is load-bearing (reverse destruction: timers and
        // state maps die before the io_context; the thread is joined in the
        // dtor body BEFORE any member dies): ioc_ FIRST, thread_ LAST
        // (P-1: the HttpStubServer member-layout precedent).
        std::shared_ptr<boost::asio::io_context> ioc_;
        std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
        Strand                                   strand_;
        std::shared_ptr<IPriceSource>            coinGeckoTier_;
        std::shared_ptr<IPriceSource>            gnusServiceTier_;
        std::chrono::milliseconds                coalescingWindow_;
        Clock                                    now_;

        /// @brief L1 cache keyed currency -> id (D-08 granularity): quotes for
        /// the same id in different currencies are distinct values.
        std::map<std::string, std::map<std::string, PriceQuote>> cache_;

        base::Logger m_logger = sgns::base::createLogger( "LocalPriceManager" );
        std::thread  thread_;
    };
} // namespace sgns
