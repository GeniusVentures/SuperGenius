/**
 * @file processing_clocks_elm.cpp
 * @brief Deterministic three-clock derivation for ELM (elm_processing) jobs.
 * @copyright 2026
 */

#include "processing_clocks_elm.hpp"

#include <cassert>
#include <cmath>

namespace sgns::processing
{
    uint64_t ElmEscrowMinions( double maximum_processing_hours )
    {
        // Pure integer path (Pitfall 3): llround to milli-hours first so
        // 1.3h -> 1300 (never 1299 from binary-float truncation), then
        // milli_hours * $0.0003 * 10^6 minions/$ == milli_hours * 300 == * 3 / 10
        // at kUsdPerGnusRate == 1.0. The final integer /10 floors, which is
        // requestor-favorable (D-02).
        const int64_t milliHours = static_cast<int64_t>( std::llround( maximum_processing_hours * 1000.0 ) );
        return static_cast<uint64_t>( ( milliHours * 3 ) / 10 );
    }

    ElmClocks DeriveElmClocks( double maximum_processing_hours )
    {
        // Input contract (D-05): hours in (0, 24] guaranteed upstream by the
        // schema bounds plus the ProcessingManager C++ gate. Debug-assert only;
        // never clamp -- a clamped clock would silently disagree with the
        // schema-rejected parse the requestor actually saw.
        assert( maximum_processing_hours > 0.0 && maximum_processing_hours <= kMaxProcessingHoursCap );

        ElmClocks clocks;
        // D-08: deadline == hours exactly; the grace period belongs to the
        // lockTimeout only.
        clocks.deadline = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double, std::ratio<3600>>( maximum_processing_hours ) );
        clocks.lockTimeout  = clocks.deadline + kLockGraceSeconds;
        clocks.escrowMinions = ElmEscrowMinions( maximum_processing_hours );
        return clocks;
    }
} // namespace sgns::processing
