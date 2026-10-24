/**
 * @file       TxPruner.cpp
 * @brief      PRUNE-04 real prune-cycle service implementation.
 * @details    The cycle orchestration: tick gate -> Evaluate (reused
 *             verbatim) -> block-grouped K budget -> fresh spent-ness
 *             re-verify -> atomic per-tx erase -> status + ONE INFO summary.
 *             All window and eligibility arithmetic lives in
 *             PruneEligibility::Evaluate; this file composes verdicts into
 *             bounded, crash-consistent mutation.
 * @date       2026-10-21
 */

#include "transaction/TxPruner.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "account/GeniusTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "transaction/TransactionManager.hpp"

namespace sgns
{
    namespace
    {
        /// Reverse of TransactionManager::GetTransactionPath(tx_hash): the
        /// hash is the path's terminal segment (the same invariant
        /// PruneEligibility::LookupPrunedEntry checks on marker records —
        /// WR-04). One home for this direction: here.
        std::string TxHashFromPath( const std::string &tx_path )
        {
            const auto slash = tx_path.rfind( '/' );
            return slash == std::string::npos ? tx_path : tx_path.substr( slash + 1 );
        }

        /// Milliseconds elapsed since @p start (steady clock — never wall).
        uint64_t ElapsedMs( std::chrono::steady_clock::time_point start )
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - start )
                    .count() );
        }

        /// Human watermark clause for the RECL-03 INFO line: the advance is
        /// stated explicitly, never implied.
        std::string DescribeWatermark( const std::optional<uint64_t> &before, const std::optional<uint64_t> &after )
        {
            if ( !after.has_value() )
            {
                return "none";
            }
            if ( !before.has_value() )
            {
                return "set to " + std::to_string( after.value() );
            }
            if ( after.value() > before.value() )
            {
                return "advanced " + std::to_string( before.value() ) + " -> " + std::to_string( after.value() );
            }
            return "unchanged at " + std::to_string( after.value() );
        }
    } // anonymous namespace

    std::function<bool( int step )> TxPruner::cycle_interrupt_hook_for_test_ = nullptr;

    outcome::result<std::shared_ptr<TxPruner>> TxPruner::New( NodeType node_type, const PruningConfig &config )
    {
        if ( node_type != NodeType::Light )
        {
            // GATE-01 role gate (the PruneEligibility::New mirror): full and
            // archive nodes never erase history. The wiring re-checks the
            // role before constructing — defense in depth (T-18-0401).
            return outcome::failure( std::errc::operation_not_permitted );
        }
        return std::shared_ptr<TxPruner>( new TxPruner( node_type, config ) );
    }

    outcome::result<PruneCycleStats> TxPruner::RunCycle( crdt::GlobalDB     &globaldb,
                                                         TransactionManager &transaction_manager,
                                                         UTXOManager        &utxo_manager,
                                                         uint64_t            generation )
    {
        const auto      cycle_start = std::chrono::steady_clock::now();
        PruneCycleStats stats;
        stats.generation = generation;

        // (1) Tick gate (D-C2): READY + empty missing set + settle window.
        // A skipped tick is not a cycle: zeroed stats, status untouched.
        if ( !TickGateSettled( transaction_manager, cycle_start ) )
        {
            logger_->debug( "Prune cycle skipped: tick gate not settled (not READY, missing history, or the "
                            "{}s settle window has not elapsed)",
                            config_m.settle_seconds );
            return stats;
        }

        // (2) Evaluate VERBATIM (per-cycle construction from the PASSED
        // deps, never cached, never under any lock): every window and
        // eligibility decision comes from the evaluator — this service only
        // consumes verdicts.
        auto evaluator = PruneEligibility::New( globaldb, transaction_manager, utxo_manager, node_type_m, config_m );
        if ( evaluator.has_error() )
        {
            // Permanent (IsPermanentStop): stop rather than retry a role the
            // factory keeps rejecting every cycle (the 17-06 split).
            logger_->error( "Prune evaluator construction failed: {} — prune cycle stopped",
                            evaluator.error().message() );
            return outcome::failure( evaluator.error() );
        }

        auto evaluation = evaluator.value()->Evaluate();
        if ( evaluation.has_error() )
        {
            if ( evaluation.error() == std::errc::invalid_argument )
            {
                // Permanent: the fail-closed no-window contract is a broken
                // policy state no retry can fix — stop, never spin on it.
                logger_->error( "Prune evaluation failed: {} — prune cycle stopped", evaluation.error().message() );
                return outcome::failure( evaluation.error() );
            }
            // Transient (e.g. a GlobalDB restart surfacing as
            // operation_canceled): retry next cycle.
            logger_->warn( "Prune evaluation failed: {} — retrying next cycle", evaluation.error().message() );
            return outcome::failure( evaluation.error() );
        }

        const PruneEvaluationResult &result = evaluation.value();
        stats.examined                      = static_cast<uint64_t>( result.candidates.size() );
        stats.eligible                      = static_cast<uint64_t>( result.EligibleCount() );

        if ( InterruptRequested( kInterruptAfterEvaluation ) )
        {
            stats.duration_ms = ElapsedMs( cycle_start );
            return stats; // status deliberately untouched
        }

        // (3) Block-grouped work list (D-04 / PRUNE-03): eligible candidates
        // grouped by their delta-block CID; only groups whose block report
        // is ERASABLE are this cycle's work — a gate-held block keeps its
        // whole group (erasing a tx value whose block survives a
        // non-eligible sibling would strand the sibling's history). Group
        // order is the evaluation's first-appearance order (deterministic).
        std::set<std::string>           erasable_cids;
        std::map<std::string, uint64_t> block_share_by_cid;
        for ( const auto &report : result.block_reports )
        {
            if ( report.erasable )
            {
                erasable_cids.insert( report.cid );
                // Per-tx block share of the threshold estimate (the
                // evaluator-carried sizes): integer division — a fully
                // erased group's shares cover the block's carried-key
                // bytes, a deferred group contributes nothing.
                block_share_by_cid[report.cid] = report.sharing_tx_count > 0
                                                     ? report.block_bytes / report.sharing_tx_count
                                                     : 0;
            }
        }

        std::vector<std::string>                                         group_order;
        std::map<std::string, std::vector<const PruneCandidateResult *>> groups;
        for ( const auto &candidate : result.candidates )
        {
            if ( !candidate.eligible || !candidate.cid.has_value() )
            {
                continue;
            }
            const std::string &cid = candidate.cid.value();
            if ( erasable_cids.find( cid ) == erasable_cids.end() )
            {
                continue;
            }
            const auto inserted = groups.try_emplace( cid );
            if ( inserted.second )
            {
                group_order.push_back( cid );
            }
            inserted.first->second.push_back( &candidate );
        }

        // (4) Group-or-defer + (5) per-tx re-verify -> atomic erase.
        uint64_t remaining_k = config_m.max_erases_per_cycle;
        bool     interrupted = false;
        for ( const auto &cid : group_order )
        {
            const auto &group = groups.at( cid );
            if ( static_cast<uint64_t>( group.size() ) > remaining_k )
            {
                // DEFERRED whole (Pitfall 1, T-18-0403): a partial take would
                // strand same-block siblings behind an exhausted K; the
                // entire group retries next cycle.
                ++stats.held_blocks;
                continue;
            }
            for ( const auto *candidate : group )
            {
                if ( !OutputsRemainGenuinelyConsumed( globaldb, utxo_manager, *candidate ) )
                {
                    ++stats.skipped;
                    continue;
                }
                if ( InterruptRequested( kInterruptBeforeErase ) )
                {
                    interrupted = true;
                    break;
                }
                auto erase = transaction_manager.EraseTransactionEntryPruned( TxHashFromPath( candidate->tx_path ) );
                if ( erase.has_error() )
                {
                    // Per-tx failure: record and CONTINUE — log-and-continue
                    // is sanctioned only for this per-tx scan contract; the
                    // tx stays enumerated and a later cycle retries it.
                    ++stats.errors;
                    logger_->warn( "Prune erase failed for tx {}: {} — continuing the cycle",
                                   candidate->tx_path,
                                   erase.error().message() );
                    continue;
                }
                ++stats.erased;
                --remaining_k;
                // D-01 watermark accumulation: the evaluation-carried
                // ESTIMATE only (never a measured number). Skipped, held,
                // and errored candidates contribute nothing — their bytes
                // are still on disk.
                cumulative_erased_bytes_ += candidate->value_bytes + block_share_by_cid.at( cid );
            }
            if ( interrupted )
            {
                break;
            }
        }

        if ( interrupted )
        {
            // Seam-aborted: NOT a completed cycle — the status (including
            // the watermark) stays untouched (T-18-0406: no overclaiming).
            stats.duration_ms = ElapsedMs( cycle_start );
            logger_->warn( "Prune cycle interrupted before an erase — status left untouched" );
            return stats;
        }

        if ( InterruptRequested( kInterruptBeforeStatus ) )
        {
            stats.duration_ms = ElapsedMs( cycle_start );
            return stats;
        }

        RecordCompletedCycle( globaldb, stats, cycle_start );
        MaybeCompactAfterCycle( globaldb, stats );
        return stats;
    }

    void TxPruner::RecordCompletedCycle( crdt::GlobalDB                       &globaldb,
                                         PruneCycleStats                      &stats,
                                         std::chrono::steady_clock::time_point cycle_start )
    {
        // Watermark: maxHeight of a cycle that ran to completion without
        // hard error — K exhaustion, skips, and per-tx errors never block
        // advancement; interrupted and failed cycles never reach here. A
        // failed head read keeps the previous watermark (monotonic).
        auto head_list = globaldb.GetCRDTHeadList();
        if ( head_list.has_error() )
        {
            logger_->warn( "Prune cycle could not read the head list for the watermark: {} — the previous "
                           "watermark is kept",
                           head_list.error().message() );
        }

        stats.duration_ms = ElapsedMs( cycle_start );
        std::optional<uint64_t> watermark_before;
        std::optional<uint64_t> watermark_after;
        {
            const std::scoped_lock lock( status_mutex_ );
            watermark_before    = status_m.watermark;
            status_m.last_cycle = stats;
            if ( !head_list.has_error() )
            {
                const uint64_t max_height = head_list.value().second;
                if ( !status_m.watermark.has_value() || max_height > status_m.watermark.value() )
                {
                    status_m.watermark = max_height;
                }
            }
            watermark_after = status_m.watermark;
        }

        if ( InterruptRequested( kInterruptAfterStatus ) )
        {
            return; // status IS updated; only the INFO line is skipped
        }

        // (6) ONE per-cycle INFO summary (RECL-03): counts only. Reclaimed
        // bytes are measured at COMPACTION time and reported by the
        // compaction's own INFO line (D-02 — deletes alone reclaim nothing,
        // so an erase-time byte number would be a fabrication).
        logger_->info( "Prune cycle [generation {}]: examined={}, eligible={}, erased={}, held_blocks={}, "
                       "skipped={}, errors={}, duration_ms={}, watermark={}",
                       stats.generation,
                       stats.examined,
                       stats.eligible,
                       stats.erased,
                       stats.held_blocks,
                       stats.skipped,
                       stats.errors,
                       stats.duration_ms,
                       DescribeWatermark( watermark_before, watermark_after ) );
    }

    void TxPruner::MaybeCompactAfterCycle( crdt::GlobalDB &globaldb, PruneCycleStats &stats )
    {
        // D-01 gate conjuncts, in order; each false returns with the stats
        // untouched (a no-op cycle, a shutting-down node, or a sub-threshold
        // watermark never pays for a compaction).
        if ( stats.erased < 1 )
        {
            return;
        }
        if ( compaction_canceled_.load() )
        {
            return;
        }
        // MB is the single config representation; the byte comparison
        // derives HERE (the one threshold-derivation site).
        const uint64_t threshold_bytes = config_m.compact_threshold_mb << 20;
        if ( cumulative_erased_bytes_ < threshold_bytes )
        {
            return;
        }

        // Baseline-at-last-compaction, lazily initialized BEFORE any
        // compaction of this span has run — the eventual delta measures the
        // full accumulated span since the last compaction, never a per-cycle
        // under-count. A failed read stores nothing and skips compaction
        // this cycle (the still-crossed watermark retries next cycle).
        if ( !compaction_baseline_bytes_.has_value() )
        {
            auto baseline = globaldb.GetApproximateDiskUsageBytes();
            if ( baseline.has_error() )
            {
                logger_->warn( "Prune compaction skipped: the baseline size could not be read: {} — retrying next "
                               "cycle",
                               baseline.error().message() );
                return;
            }
            compaction_baseline_bytes_ = baseline.value();
        }
        const uint64_t before_bytes = compaction_baseline_bytes_.value();

        const auto compaction_start  = std::chrono::steady_clock::now();
        const auto compacted         = globaldb.CompactStorage( &compaction_canceled_ );
        stats.compaction_duration_ms = ElapsedMs( compaction_start );
        if ( compacted.has_error() )
        {
            if ( compacted.error() == std::errc::operation_canceled )
            {
                // The clean cancel-skip: nothing was started.
                logger_->info( "Prune compaction skipped: canceled (generation {})", stats.generation );
            }
            else
            {
                // Real failure: a completed cycle NEVER fails because of
                // compaction — the watermark is kept, so the still-crossed
                // threshold is the natural retry trigger next cycle.
                logger_->warn( "Prune compaction failed: {} — watermark kept, retrying next cycle (generation {})",
                               compacted.error().message(),
                               stats.generation );
            }
            return;
        }

        auto after = globaldb.GetApproximateDiskUsageBytes();
        if ( after.has_error() )
        {
            // The compaction ran but went unmeasured: keep the baseline so
            // the NEXT compaction measures the full span, stamp nothing
            // (never fabricate a delta), and still reset the watermark —
            // this span's erases were processed. Stated plainly.
            logger_->warn( "Prune compaction completed but the size could not be re-read: {} — bytes_reclaimed not "
                           "stamped this cycle (generation {})",
                           after.error().message(),
                           stats.generation );
            stats.compacted          = true;
            cumulative_erased_bytes_ = 0;
            return;
        }

        // Success: stamp the MEASURED delta under the status mutex (D-02),
        // reset the in-memory watermark + baseline (D-01), and report the
        // measured numbers once.
        const uint64_t reclaimed = before_bytes > after.value() ? before_bytes - after.value() : 0;
        stats.compacted          = true;
        {
            const std::scoped_lock lock( status_mutex_ );
            status_m.bytes_reclaimed = reclaimed;
            // WR-01 (review): re-copy so last_cycle reports compacted/
            // compaction_duration_ms -- RecordCompletedCycle copied the stats
            // before this stamping ran, leaving the fields stale in status.
            status_m.last_cycle = stats;
        }
        cumulative_erased_bytes_   = 0;
        compaction_baseline_bytes_ = after.value();
        logger_->info( "Prune compaction [generation {}]: before_bytes={}, after_bytes={}, reclaimed={}, "
                       "duration_ms={}",
                       stats.generation,
                       before_bytes,
                       after.value(),
                       reclaimed,
                       stats.compaction_duration_ms );
    }

    bool TxPruner::TickGateSettled( TransactionManager &transaction_manager, std::chrono::steady_clock::time_point now )
    {
        const bool             gate_holds = transaction_manager.GetState() == TransactionManager::State::READY &&
                                            !transaction_manager.HasMissingTxHashes();
        const std::scoped_lock lock( status_mutex_ );
        if ( !gate_holds )
        {
            // The clock restarts whenever the gate re-opens: a cycle can
            // never fire on the strength of a stale settle window, and it
            // cannot fire during initial sync (D-C2).
            gate_held_since_.reset();
            return false;
        }
        if ( !gate_held_since_.has_value() )
        {
            gate_held_since_ = now;
            return false;
        }
        return std::chrono::duration_cast<std::chrono::seconds>( now - gate_held_since_.value() ).count() >=
               static_cast<std::chrono::seconds::rep>( config_m.settle_seconds );
    }

    bool TxPruner::OutputsRemainGenuinelyConsumed( crdt::GlobalDB             &globaldb,
                                                   UTXOManager                &utxo_manager,
                                                   const PruneCandidateResult &candidate ) const
    {
        // FRESH evidence only (D-01 / T-18-0402): re-fetch by path and
        // re-check every produced outpoint immediately before the erase —
        // a spend that reverted between evaluation and erase must refuse
        // it. Each IsOutPointGenuinelyConsumed call is atomic under the
        // UTXO lock internally. NO window arithmetic and NO spender
        // resolution are recomputed here — Evaluate remains their one home.
        auto transaction = TransactionManager::FetchTransaction( globaldb, candidate.tx_path );
        if ( transaction.has_error() || !transaction.value() )
        {
            logger_->warn( "Prune re-verify: tx {} could not be re-fetched ({}) — skipped",
                           candidate.tx_path,
                           transaction.has_error() ? transaction.error().message() : std::string( "empty value" ) );
            return false;
        }

        auto outputs = transaction.value()->GetProducedUTXOs();
        if ( !outputs.has_value() || outputs->empty() )
        {
            // Fail closed: vacuous "all outputs spent" must never erase.
            logger_->warn( "Prune re-verify: tx {} no longer reports produced outputs — skipped", candidate.tx_path );
            return false;
        }

        const auto reverted = std::find_if(
            outputs->begin(),
            outputs->end(),
            [&utxo_manager]( const GeniusUTXO &output )
            { return !utxo_manager.IsOutPointGenuinelyConsumed( output.GetTxID(), output.GetOutputIdx() ); } );
        if ( reverted != outputs->end() )
        {
            logger_->warn( "Prune re-verify: output {}:{} of tx {} is no longer genuinely consumed (spend "
                           "reverted) — skipped",
                           reverted->GetTxID().toReadableString(),
                           reverted->GetOutputIdx(),
                           candidate.tx_path );
            return false;
        }
        return true;
    }

    PruneStatus TxPruner::GetPruneStatus() const
    {
        const std::scoped_lock lock( status_mutex_ );
        return status_m;
    }

    bool TxPruner::IsPermanentStop( const std::error_code &error )
    {
        // The 17-06 fail-closed error split re-hosted as ONE classification
        // home: invalid_argument (Evaluate's no-window contract) and
        // operation_not_permitted (a non-Light role at either factory) are
        // broken policy states no retry can fix — stop, never spin.
        return error == std::errc::invalid_argument || error == std::errc::operation_not_permitted;
    }

    void TxPruner::RequestCompactionCancel()
    {
        // The shutdown promptness path (the wiring calls this beside the
        // prune-timer cancel). Never reset: a released pruner never
        // compacts again.
        compaction_canceled_.store( true );
    }

    void TxPruner::SetCycleInterruptHookForTest( std::function<bool( int step )> hook )
    {
        // TEST-ONLY seam (see header): inert (nullptr) in production; a
        // fixture must clear it in its destructor so a hook armed by a
        // failing case never leaks further.
        cycle_interrupt_hook_for_test_ = std::move( hook );
    }

    bool TxPruner::InterruptRequested( int step )
    {
        if ( !cycle_interrupt_hook_for_test_ )
        {
            return false;
        }
        return cycle_interrupt_hook_for_test_( step );
    }

    TxPruner::TxPruner( NodeType node_type, PruningConfig config ) :
        config_m( std::move( config ) ), node_type_m( node_type )
    {
    }
} // namespace sgns
