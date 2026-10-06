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
#include <optional>
#include <set>
#include <utility>

#include <algorithm>

namespace sgns
{
    LocalPriceManager::LocalPriceManager( std::shared_ptr<IPriceSource> coinGeckoTier,
                                          std::shared_ptr<IPriceSource> gnusServiceTier,
                                          std::chrono::milliseconds      coalescingWindow,
                                          Clock                          now,
                                          PriceHistoryConfig             historyConfig )
        : ioc_( std::make_shared<boost::asio::io_context>() ),
          work_( std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
              ioc_->get_executor() ) ),
          strand_( ioc_->get_executor() ),
          coinGeckoTier_( std::move( coinGeckoTier ) ),
          gnusServiceTier_( std::move( gnusServiceTier ) ),
          coalescingWindow_( coalescingWindow ),
          now_( std::move( now ) ),
          historyConfig_( historyConfig )
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

    LocalPriceManager::PriceHistoryStats LocalPriceManager::QueryHistory( std::chrono::system_clock::time_point from,
                                                                          std::chrono::system_clock::time_point to )
    {
        // Same self-deadlock guard as GetQuotes: the future below can only
        // be satisfied by a strand handler.
        assert( !strand_.running_in_this_thread() );

        std::promise<PriceHistoryStats> promise;
        auto                            future = promise.get_future();
        boost::asio::post( strand_,
                           [this, from, to, p = std::move( promise )]() mutable {
                               PriceHistoryStats stats;
                               // Linear scan (pitfall 5): observation times may be
                               // non-monotonic across tiers — no binary search.
                               for ( const auto &observation : history_ )
                               {
                                   if ( observation.at < from || observation.at > to )
                                   {
                                       continue; // [from, to] inclusive
                                   }
                                   if ( stats.count == 0 )
                                   {
                                       stats.min = observation.price;
                                       stats.max = observation.price;
                                   }
                                   else
                                   {
                                       stats.min = std::min( stats.min, observation.price );
                                       stats.max = std::max( stats.max, observation.price );
                                   }
                                   ++stats.count;
                               }
                               p.set_value( stats );
                           } );
        return future.get();
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

        // The fallback chain walk (LPM-04, D-09), inline on the strand:
        // a FAILED tier leaves `remaining` untouched (wholesale escalation
        // of the full miss-set); a successful-but-partial tier shrinks it by
        // exactly the ids it returned (gap-chase); completed ids are never
        // re-added because they left `remaining`.
        std::set<std::string>            remaining( batch.ids.begin(), batch.ids.end() );
        std::optional<PriceFetchFailure> lastTierFailure;
        std::vector<PriceQuote>          fetched; // quotes returned by the walk — served with TIER source

        // Tier 1 — CoinGecko direct. Always has ids at entry (the batch
        // exists because a miss joined it).
        {
            auto r1 = coinGeckoTier_->FetchPrices( std::vector<std::string>( remaining.begin(), remaining.end() ),
                                                   currency );
            if ( r1 )
            {
                StoreInL1( r1.value() );
                fetched.insert( fetched.end(), r1.value().begin(), r1.value().end() );
                for ( const auto &quote : r1.value() )
                {
                    remaining.erase( quote.asset );
                }
                m_logger->debug( "tier 1 (CoinGecko) served {} id(s)", r1.value().size() );
            }
            else
            {
                m_logger->warn( "tier 1 (CoinGecko) failed: {}", r1.error().Message() );
                lastTierFailure = r1.error();
            }
        }

        // Tier 2 — token.gnus.ai: ONLY for ids tier 1 did not serve.
        if ( !remaining.empty() )
        {
            auto r2 = gnusServiceTier_->FetchPrices( std::vector<std::string>( remaining.begin(), remaining.end() ),
                                                     currency );
            if ( r2 )
            {
                StoreInL1( r2.value() );
                fetched.insert( fetched.end(), r2.value().begin(), r2.value().end() );
                for ( const auto &quote : r2.value() )
                {
                    remaining.erase( quote.asset );
                }
                m_logger->debug( "tier 2 (token.gnus.ai) served {} id(s)", r2.value().size() );
            }
            else
            {
                m_logger->warn( "tier 2 (token.gnus.ai) failed: {}", r2.error().Message() );
                lastTierFailure = r2.error();
            }
        }

        // History (D-06-05): record every genius-ai quote the walk actually
        // fetched from a network tier — AFTER the walk so `fetched` is
        // final, BEFORE per-waiter assembly. L1 hits never pass through
        // here (HandleRequestOnStrand serves them without dispatching).
        RecordObservations( fetched );

        // Per-waiter resolution per the serving-source rule, extended with
        // band-aware L1 lookups (D-10/D-11) for requested ids neither the
        // immediate set nor either tier covered — exactly the ids both
        // network tiers failed to refresh (last-known-good territory).
        const auto now = now_();
        int resolvedFresh = 0, resolvedStale = 0, resolvedFailed = 0;
        for ( auto &waiter : batch.waiters )
        {
            std::vector<PriceQuote> assembled = std::move( waiter.immediate );
            // The walk's returned quotes serve with the TIER's source as
            // received (no rewrite — fetch provenance preserved).
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
            for ( const auto &id : waiter.requestedIds )
            {
                bool covered = false;
                for ( const auto &quote : assembled )
                {
                    if ( quote.asset == id )
                    {
                        covered = true;
                        break;
                    }
                }
                if ( covered )
                {
                    continue; // fresh-immediate or tier-returned — nothing to look up
                }

                const auto currencyIt = cache_.find( currency );
                if ( currencyIt == cache_.end() )
                {
                    continue;
                }
                const auto idIt = currencyIt->second.find( id );
                if ( idIt == currencyIt->second.end() )
                {
                    continue;
                }
                const auto band = ClassifyFreshness( idIt->second.timestamp, now );
                if ( band == FreshnessBand::Fresh )
                {
                    // Rare but reachable: an overlapping earlier batch
                    // refreshed the entry after this request classified it
                    // a miss.
                    PriceQuote served = idIt->second;
                    served.source     = PriceSource::LocalCache;
                    assembled.push_back( std::move( served ) );
                }
                else if ( band == FreshnessBand::StaleButUsable )
                {
                    // LKG serve (D-10): only reachable when both tiers
                    // failed to refresh the id — a tier success would have
                    // stored a fresh entry (structural FRESH-02 compliance).
                    // stale=true, timestamp UNTOUCHED (D-11).
                    PriceQuote served = idIt->second;
                    served.source     = PriceSource::LocalCache;
                    served.stale      = true;
                    assembled.push_back( std::move( served ) );
                }
                // Unavailable band: never served (FRESH-02) — skip.
            }

            if ( assembled.empty() )
            {
                // Nothing servable for this waiter: surface the last tier
                // failure, or NoDataFound when the tiers succeeded but
                // returned nothing for the requested ids.
                if ( lastTierFailure.has_value() )
                {
                    waiter.done.set_value( outcome::failure( *lastTierFailure ) );
                }
                else
                {
                    waiter.done.set_value(
                        outcome::failure( PriceFetchFailure{ PriceFetchError::NoDataFound, 0 } ) );
                }
                ++resolvedFailed;
                continue;
            }

            // A non-empty immediate set with failed tiers still resolves
            // SUCCESS with the partial data (GetCoinprice's
            // continue-with-what-we-have).
            if ( lastTierFailure.has_value() )
            {
                ++resolvedStale; // partial success over a failed walk
            }
            else
            {
                ++resolvedFresh;
            }
            waiter.done.set_value( outcome::success( std::move( assembled ) ) );
        }
        // Chain trace (RESEARCH §8.7): the serving tier is otherwise
        // indistinguishable when envelope quotes carry source=CoinGecko.
        m_logger->info( "Dispatched batch of {} id(s), {} waiter(s): {}/{} unresolved after tiers, {} fresh / {} stale-or-partial / {} failed",
                        missIds.size(),
                        batch.waiters.size(),
                        remaining.size(),
                        missIds.size(),
                        resolvedFresh,
                        resolvedStale,
                        resolvedFailed );
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

    void LocalPriceManager::RecordObservations( const std::vector<PriceQuote> &quotes )
    {
        // D-06-05: only genius-ai quotes actually fetched from a network
        // tier are recorded (hard-coded asset — the only asset the Phase-7
        // validator prices). Observation time is the quote's fetch time
        // (D-14), never local-store time.
        static constexpr const char *kHistoryAsset = "genius-ai";
        for ( const auto &quote : quotes )
        {
            if ( quote.asset != kHistoryAsset )
            {
                continue;
            }
            // A2 dedupe: a tier repeating an identical (timestamp, price)
            // observation (e.g. the gnus envelope re-serving one fetchedAt)
            // is not new evidence — skip consecutive duplicates so count
            // reflects distinct observations.
            const bool sameTime = !history_.empty() && history_.back().at == quote.timestamp;
            const bool samePrice = !history_.empty() && history_.back().price == quote.price;
            if ( sameTime && samePrice )
            {
                continue;
            }
            history_.push_back( PriceObservation{ quote.timestamp, quote.price, quote.source } );
        }
        // T-06-01 bounds enforcement, oldest first: retention prune against
        // the injected clock (hermetic — never system_clock::now()), then
        // the count cap. Observation times may be non-monotonic across
        // tiers, so retention compares EVERY entry rather than only
        // scanning a sorted front (pitfall 5); the cap still evicts from
        // the front (insertion order == eviction order).
        const auto cutoff = now_() - historyConfig_.retention;
        history_.erase( std::remove_if( history_.begin(),
                                        history_.end(),
                                        [&]( const PriceObservation &observation ) {
                                            return observation.at < cutoff;
                                        } ),
                        history_.end() );
        while ( history_.size() > historyConfig_.maxEntries )
        {
            history_.pop_front();
        }
    }
} // namespace sgns
