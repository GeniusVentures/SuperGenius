/**
 * Source file for LocalPriceManager — executor lifecycle (ioc -> work guard
 * -> strand -> thread), blocking GetQuotes promise/future bridge, band-aware
 * L1 cache with copy-on-serve, single-tier inline dispatch, and the
 * drain-then-join destructor (Phase 3 plan 03-01: LPM-01, D-01..D-06,
 * D-13/D-15).
 */
#include "LocalPriceManager.hpp"

#include "PriceFreshness.hpp"

#include <cassert>
#include <future>
#include <utility>

namespace sgns
{
    LocalPriceManager::LocalPriceManager( std::shared_ptr<IPriceSource> coinGeckoTier,
                                          std::shared_ptr<IPriceSource> gnusServiceTier,
                                          std::chrono::milliseconds      coalescingWindow,
                                          Clock                          now )
        : ioc_( std::make_shared<boost::asio::io_context>() ),
          work_( std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
              ioc_->get_executor() ) ),
          strand_( ioc_->get_executor() ),
          coinGeckoTier_( std::move( coinGeckoTier ) ),
          gnusServiceTier_( std::move( gnusServiceTier ) ),
          coalescingWindow_( coalescingWindow ),
          now_( std::move( now ) )
    {
        // The work guard is held for the manager's whole lifetime (no natural
        // idle point — timers and posted handlers come and go); without it
        // run() returns the instant the queue drains.
        thread_ = std::thread( [ioc = ioc_]() { ioc->run(); } );
    }

    LocalPriceManager::~LocalPriceManager()
    {
        // Shutdown ordering (drain-then-join): the shutdown post runs on the
        // strand while the guard still holds run() alive; resetting the guard
        // unblocks run() after the queue drains; join precedes member
        // destruction so handlers referencing `this` are always safe.
        // ioc_->stop() is NEVER called — it would abandon queued handlers
        // (the HttpStubServer uses stop() only because it has no waiters).
        boost::asio::post( strand_, [this]() {
            m_logger->info( "LocalPriceManager shutting down" );
            // Resolve parked waiters BEFORE reset/join: this is what keeps
            // blocked GetQuotes callers from hanging at shutdown
            // (RESEARCH §1 landmine 1). Cancel each open window's timer and
            // fail its waiters — the walk can no longer run for them.
            for ( auto &[currency, window] : windows_ )
            {
                if ( window.timer )
                {
                    boost::system::error_code ec;
                    window.timer->cancel( ec );
                }
                for ( auto &waiter : window.waiters )
                {
                    waiter.done.set_value(
                        outcome::failure( PriceFetchFailure{ PriceFetchError::NetworkError, 0 } ) );
                }
            }
            windows_.clear();
        } );
        work_->reset();
        thread_.join();
    }

    PriceResult<std::vector<PriceQuote>> LocalPriceManager::GetQuotes( const std::vector<std::string> &ids,
                                                                       const std::string              &currency )
    {
        // Cheap self-deadlock guard: GetQuotes parks on a future that only a
        // strand handler can satisfy — calling it from the manager's own
        // runner thread would deadlock (debug-asserted, Doxygen-documented).
        assert( !strand_.running_in_this_thread() );

        if ( ids.empty() )
        {
            // Facade parity (PriceHttpClient.cpp:49-52): empty input fails
            // synchronously without posting.
            return outcome::failure( PriceFetchFailure{ PriceFetchError::EmptyInput, 0 } );
        }

        std::promise<PriceResult<std::vector<PriceQuote>>> promise;
        auto future = promise.get_future();
        boost::asio::post( strand_,
                           [this, ids, currency, p = std::move( promise )]() mutable {
                               HandleRequestOnStrand( ids, currency, std::move( p ) );
                           } );
        return future.get(); // D-01: blocking bridge — caller parks until the chain resolves
    }

    void LocalPriceManager::HandleRequestOnStrand( const std::vector<std::string>                     &ids,
                                                   const std::string                                  &currency,
                                                   std::promise<PriceResult<std::vector<PriceQuote>>> done )
    {
        // D-06 fresh/miss split — modeled on the GeniusNode::GetCoinprice
        // miss-collection loop with ClassifyFreshness as the only freshness
        // authority (bands come from PriceFreshness.hpp constants).
        std::vector<PriceQuote>   immediate;
        std::vector<std::string>  misses;
        const auto                currentTime = now_();
        for ( const auto &id : ids )
        {
            const auto currencyIt = cache_.find( currency );
            if ( currencyIt != cache_.end() )
            {
                const auto idIt = currencyIt->second.find( id );
                if ( idIt != currencyIt->second.end()
                     && ClassifyFreshness( idIt->second.timestamp, currentTime ) == FreshnessBand::Fresh )
                {
                    // Copy-on-serve (P-9): the stored entry keeps its
                    // fetch-time source and timestamp for diagnostics; only
                    // the served copy is rewritten to LocalCache.
                    PriceQuote served = idIt->second;
                    served.source     = PriceSource::LocalCache;
                    immediate.push_back( std::move( served ) );
                    continue;
                }
            }
            misses.push_back( id );
        }

        if ( misses.empty() )
        {
            // LPM-01: every requested id fresh in L1 — zero network.
            m_logger->debug( "GetQuotes served {} id(s) entirely from L1 (currency {})", immediate.size(), currency );
            done.set_value( outcome::success( std::move( immediate ) ) );
            return;
        }

        // D-05: misses join the currency's pending window; the first miss
        // arms the ~50ms coalescing timer. Ids already fetched in the
        // current walk are NOT re-added because dispatch removes the window
        // from windows_ BEFORE walking (a new miss creates a fresh window —
        // D-07).
        PendingWindow &window = windows_[currency];
        if ( window.timer == nullptr )
        {
            window.timer = std::make_unique<boost::asio::steady_timer>( *ioc_ );
            window.timer->expires_after( coalescingWindow_ );
            // The timer is constructed from *ioc_, NOT from the strand — a
            // bare lambda would therefore NOT be strand-bound;
            // bind_executor( strand_, ... ) puts the callback on the strand
            // explicitly. The load-bearing safety invariant is that exactly
            // ONE thread runs ioc_: single runner thread + strand together
            // serialize all window state. A second runner thread on ioc_ —
            // not strand membership — is what would break the
            // timer-to-dispatch serialization.
            window.timer->async_wait( boost::asio::bind_executor(
                strand_, [this, currency]( const boost::system::error_code & ) { DispatchBatchOnStrand( currency ); } ) );
        }
        window.ids.insert( misses.begin(), misses.end() ); // set union (D-05)
        window.waiters.push_back( PendingWaiter{ ids, std::move( immediate ), std::move( done ) } );
        // The caller (including the FIRST caller — D-05's accepted cost)
        // blocks on its future until the window fires and the walk lands.
    }

    void LocalPriceManager::DispatchBatchOnStrand( const std::string &currency )
    {
        auto windowIt = windows_.find( currency );
        if ( windowIt == windows_.end() )
        {
            return; // already dispatched (or shutdown cleared it)
        }
        // The window closes the moment dispatch begins: requests arriving
        // during the walk land in a NEW window (D-05/D-07).
        PendingWindow batch = std::move( windowIt->second );
        windows_.erase( windowIt );

        const std::vector<std::string> missIds( batch.ids.begin(), batch.ids.end() );
        if ( missIds.empty() )
        {
            return;
        }

        // Inline single-tier walk for this plan (03-03 adds tier 2 + LKG
        // assembly). Blocking the manager's dedicated thread is the accepted
        // D-03/D-04 design: the thread is private, nothing else runs there.
        auto tierResult = coinGeckoTier_->FetchPrices( missIds, currency );

        if ( tierResult )
        {
            StoreInL1( tierResult.value() );
        }
        else
        {
            m_logger->warn( "tier 1 (CoinGecko) failed: {}", tierResult.error().Message() );
        }

        // Per-waiter resolution per the serving-source rule: immediate
        // LocalCache copies + the tier's returned quotes for the waiter's
        // requested ids SERVED WITH THE TIER'S SOURCE AS RECEIVED (no
        // rewrite — fetch provenance preserved).
        const std::vector<PriceQuote> &fetched = tierResult ? tierResult.value() : std::vector<PriceQuote>{};
        for ( auto &waiter : batch.waiters )
        {
            std::vector<PriceQuote> assembled = std::move( waiter.immediate );
            for ( const auto &quote : fetched )
            {
                for ( const auto &id : waiter.requestedIds )
                {
                    if ( quote.asset == id )
                    {
                        assembled.push_back( quote );
                        break;
                    }
                }
            }

            if ( assembled.empty() )
            {
                // Nothing servable for this waiter: surface the tier failure,
                // or NoDataFound when the tier succeeded but returned nothing
                // for the requested ids.
                if ( tierResult )
                {
                    waiter.done.set_value(
                        outcome::failure( PriceFetchFailure{ PriceFetchError::NoDataFound, 0 } ) );
                }
                else
                {
                    waiter.done.set_value( outcome::failure( tierResult.error() ) );
                }
                continue;
            }

            // A non-empty immediate set with a failed tier still resolves
            // SUCCESS with the partial data (GetCoinprice's
            // continue-with-what-we-have).
            waiter.done.set_value( outcome::success( std::move( assembled ) ) );
        }
        m_logger->debug( "Dispatched batch of {} id(s) to tier 1 for {} waiter(s) (currency {})",
                         missIds.size(),
                         batch.waiters.size(),
                         currency );
    }

    void LocalPriceManager::StoreInL1( const std::vector<PriceQuote> &quotes )
    {
        // Keeps fetch-time source and timestamp verbatim. Only called on tier
        // SUCCESS — tier failures never write L1 entries (no negative
        // caching, PITFALLS #14).
        for ( const auto &quote : quotes )
        {
            cache_[quote.currency][quote.asset] = quote;
        }
    }
} // namespace sgns
