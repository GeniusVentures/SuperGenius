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
            // 03-02 extends: cancel window timers, resolve parked waiters
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

        // Single-tier inline walk for this plan (03-02 batches behind the
        // coalescing window; 03-03 adds tier 2 + LKG assembly). Blocking the
        // manager's dedicated thread is the accepted D-03/D-04 design: the
        // thread is private, nothing else runs there.
        auto tierResult = coinGeckoTier_->FetchPrices( misses, currency );

        if ( tierResult )
        {
            StoreInL1( tierResult.value() );
        }
        else
        {
            m_logger->warn( "tier 1 (CoinGecko) failed: {}", tierResult.error().Message() );
        }

        // Per-waiter assembly per the serving-source rule: immediate
        // LocalCache copies + the tier's returned quotes for this waiter's
        // requested ids SERVED WITH THE TIER'S SOURCE AS RECEIVED (no
        // rewrite — fetch provenance preserved).
        std::vector<PriceQuote> assembled = std::move( immediate );
        for ( const auto &quote : tierResult ? tierResult.value() : std::vector<PriceQuote>{} )
        {
            for ( const auto &id : ids )
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
            // Nothing servable: surface the tier failure, or NoDataFound when
            // the tier succeeded but returned nothing for the requested ids.
            if ( tierResult )
            {
                done.set_value( outcome::failure( PriceFetchFailure{ PriceFetchError::NoDataFound, 0 } ) );
            }
            else
            {
                done.set_value( outcome::failure( tierResult.error() ) );
            }
            return;
        }

        // A non-empty immediate set with a failed tier still resolves SUCCESS
        // with the partial data (GetCoinprice's continue-with-what-we-have).
        m_logger->debug( "GetQuotes dispatched to tier 1 for {} miss(es), assembled {} quote(s)",
                         misses.size(),
                         assembled.size() );
        done.set_value( outcome::success( std::move( assembled ) ) );
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
