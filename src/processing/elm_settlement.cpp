/**
 * @file       elm_settlement.cpp
 * @brief      ELM settlement arithmetic implementation (elmbridge 04-04).
 * @date       2026-09-14
 */

#include "processing/elm_settlement.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include <boost/multiprecision/cpp_int.hpp>

#include <nlohmann/json.hpp>

namespace sgns::processing
{
    namespace
    {
        /// The rate's integer path constants (mirrors ElmEscrowMinions'
        /// implementation — $0.0003/h × 10^6 minions/$ at rate 1.0).
        constexpr uint64_t kMinionsPerMillihourNumerator   = 3;
        constexpr uint64_t kMinionsPerMillihourDenominator = 10;

        bool WindowIsWellFormed( const ElmSubtaskWindow &w )
        {
            // A zero/negative window is not billable (§2 clamp; §3.3 exclusion).
            // A missing subtaskid cannot be keyed to a payout either.
            return !w.subtaskid.empty() && w.finish_time_usec > w.grab_time_usec;
        }
    } // namespace

    uint64_t ElmWindowMillihours( const ElmSubtaskWindow &w )
    {
        const int64_t deltaUsec = w.finish_time_usec - w.grab_time_usec;
        if ( deltaUsec <= 0 )
        {
            return 0;
        }
        // µs → ms → milli-hours, llround (never bare truncation — Pitfall 3).
        const double windowMs         = static_cast<double>( deltaUsec ) / 1000.0;
        const double windowMillihours = windowMs / 3600000.0 * 1000.0;
        return static_cast<uint64_t>( std::llround( windowMillihours ) );
    }

    ElmSplitResult ElmWindowsToShares( const std::vector<ElmSubtaskWindow> &windows,
                                       uint64_t                             escrow_max_minions )
    {
        using boost::multiprecision::uint128_t;

        ElmSplitResult result;

        // Well-formed subset + per-window milli-hours (§3.3: excluded entries
        // appear in neither Σ nor shares).
        struct Entry
        {
            const ElmSubtaskWindow *window = nullptr;
            uint64_t                millihours = 0;
        };
        std::vector<Entry> entries;
        entries.reserve( windows.size() );
        uint64_t billableMillihours = 0;
        for ( const auto &w : windows )
        {
            if ( !WindowIsWellFormed( w ) )
            {
                continue;
            }
            const uint64_t mh = ElmWindowMillihours( w );
            if ( mh == 0 )
            {
                continue;
            }
            entries.push_back( { &w, mh } );
            billableMillihours += mh;
        }

        if ( entries.empty() || billableMillihours == 0 )
        {
            // All-malformed / all-zero: nothing billable; the whole escrow is
            // refund. The CALLER refuses the payout (fail-closed) rather than
            // paying everything to nobody — represented by empty shares.
            result.refundMinions = escrow_max_minions;
            return result;
        }

        // §2: billable = Σ * 3 / 10 (uint64 floor), capped at escrow max.
        uint64_t billable = ( billableMillihours / kMinionsPerMillihourDenominator ) * kMinionsPerMillihourNumerator
            + ( ( billableMillihours % kMinionsPerMillihourDenominator ) * kMinionsPerMillihourNumerator )
                  / kMinionsPerMillihourDenominator;
        // NOTE: the composite above == floor(Σ * 3 / 10) exactly (the constant
        // numerator makes the two-step form equal the direct division).
        billable = std::min( billable, escrow_max_minions );

        result.billableMinions = billable;

        // §3.2 proportional floor shares in uint128.
        uint128_t distributed = 0;
        std::vector<uint64_t> shareValues( entries.size(), 0 );
        for ( size_t i = 0; i < entries.size(); ++i )
        {
            const uint128_t s = ( uint128_t( billable ) * uint128_t( entries[i].millihours ) )
                / uint128_t( billableMillihours );
            shareValues[i]  = static_cast<uint64_t>( s );
            distributed    += s;
        }

        // Remainder: one minion at a time to the LARGEST-window entries,
        // ties broken by lexicographic subtaskid (§3.2 deterministic).
        uint64_t remainder = billable - static_cast<uint64_t>( distributed );
        if ( remainder > 0 )
        {
            std::vector<size_t> order( entries.size() );
            for ( size_t i = 0; i < order.size(); ++i )
            {
                order[i] = i;
            }
            std::stable_sort( order.begin(), order.end(), [ &entries ]( size_t a, size_t b )
            {
                if ( entries[a].millihours != entries[b].millihours )
                {
                    return entries[a].millihours > entries[b].millihours; // largest first
                }
                return entries[a].window->subtaskid < entries[b].window->subtaskid; // lex tiebreak
            } );
            for ( size_t k = 0; k < order.size() && remainder > 0; ++k, --remainder )
            {
                ++shareValues[order[k]];
            }
        }

        result.shares.reserve( entries.size() );
        for ( size_t i = 0; i < entries.size(); ++i )
        {
            result.shares.push_back( { entries[i].window->subtaskid, shareValues[i] } );
        }

        // Refund: what the cap did not consume (§3.4).
        result.refundMinions = escrow_max_minions - billable;
        return result;
    }

    std::optional<ElmSubtaskWindow> ElmWindowFromEnvelopeJson( const std::vector<uint8_t> &envelopeBytes,
                                                               const std::string          &subtaskid )
    {
        try
        {
            const auto doc = nlohmann::json::parse( envelopeBytes.begin(), envelopeBytes.end() );
            if ( !doc.is_object() )
            {
                return std::nullopt;
            }
            // Numeric stamps only; string/missing keys are malformed (§3.3).
            if ( !doc.contains( "grab_time_usec" ) || !doc.contains( "finish_time_usec" ) )
            {
                return std::nullopt;
            }
            const auto &grab   = doc.at( "grab_time_usec" );
            const auto &finish = doc.at( "finish_time_usec" );
            if ( !grab.is_number() || !finish.is_number() )
            {
                return std::nullopt;
            }
            ElmSubtaskWindow window;
            window.subtaskid        = subtaskid;
            window.grab_time_usec   = grab.get<int64_t>();
            window.finish_time_usec = finish.get<int64_t>();
            return window;
        }
        catch ( const std::exception & )
        {
            // Malformed JSON never throws past this filter and never blocks
            // honest payouts (§3.3).
            return std::nullopt;
        }
    }
} // namespace sgns::processing
