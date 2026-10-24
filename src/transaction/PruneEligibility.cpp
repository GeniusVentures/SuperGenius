/**
 * @file       PruneEligibility.cpp
 * @brief      PRUNE-01 dry-run eligibility evaluator implementation (D-03).
 * @details    Read-only evaluation over committed state: config parse/validate
 *             (GATE-06 policy), the Light-only construction gate (GATE-01),
 *             the single allowlist home (D-C7), and the spend-recency window
 *             (D-02). Zero writes — see Evaluate() for the enumeration and
 *             window arithmetic.
 * @date       2026-10-19
 */

#include "transaction/PruneEligibility.hpp"

#include <set>

#include "account/UTXOManager.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "storage/database_error.hpp"
#include "transaction/TransactionManager.hpp"

namespace sgns
{
    namespace
    {
        /// Fixed operator-facing message when an enabled request lacks a
        /// POSITIVE finality_window and must be fail-closed (GATE-06 / D-02).
        constexpr char kWindowRequiredMessage[] =
            "pruning.enabled requires pruning.finality_window > 0; pruning disabled (fail closed)";

        /// Records @p reason on @p target once (per-category evidence, no
        /// duplicates). Works for both verdict structs — each carries a
        /// reasons vector with identical semantics.
        template <typename T>
        void RecordReason( T &target, PruneReason reason )
        {
            if ( std::find( target.reasons.begin(), target.reasons.end(), reason ) == target.reasons.end() )
            {
                target.reasons.push_back( reason );
            }
        }

        /// True when @p reasons carries any FAILURE code. Every PruneReason is
        /// a failure except the two affirmative-evidence codes,
        /// SPENDER_PRUNED_WINDOW_SATISFIED and BLOCK_ALREADY_ERASED (an
        /// eligible/erasable verdict may carry either).
        bool HasFailureReason( const std::vector<PruneReason> &reasons )
        {
            return std::any_of( reasons.begin(),
                                reasons.end(),
                                []( PruneReason reason )
                                {
                                    return reason != PruneReason::SPENDER_PRUNED_WINDOW_SATISFIED &&
                                           reason != PruneReason::BLOCK_ALREADY_ERASED;
                                } );
        }

        /// Logger for the static marker-contract functions (no instance exists
        /// in a static member, so the class member logger is out of reach).
        base::Logger MarkerLogger()
        {
            static const base::Logger logger = base::createLogger( "PruneEligibility" );
            return logger;
        }
    } // anonymous namespace

    PruningConfig PruningConfig::FromJSON( const rapidjson::Value &pruning_object )
    {
        PruningConfig config;
        if ( !pruning_object.IsObject() )
        {
            // Absent/malformed object: all defaults (enabled stays false).
            return config;
        }

        if ( pruning_object.HasMember( "enabled" ) && pruning_object["enabled"].IsBool() )
        {
            config.enabled = pruning_object["enabled"].GetBool();
        }

        if ( pruning_object.HasMember( "dry_run" ) && pruning_object["dry_run"].IsBool() )
        {
            config.dry_run = pruning_object["dry_run"].GetBool();
        }

        // WR-03: zero is rejected at READ time — N=0 would make the window
        // predicate vacuously true for every recorded spend — so a degenerate
        // window is treated exactly like an absent one.
        if ( pruning_object.HasMember( "finality_window" ) && pruning_object["finality_window"].IsUint64() &&
             pruning_object["finality_window"].GetUint64() > 0 )
        {
            config.finality_window = pruning_object["finality_window"].GetUint64();
        }

        // D-04 budget knob: a degenerate 0 is rejected at read time exactly
        // like a zero window (WR-03 rule — a 0-K cycle can never drain); the
        // member keeps its safe default.
        if ( pruning_object.HasMember( "max_erases_per_cycle" ) && pruning_object["max_erases_per_cycle"].IsUint64() &&
             pruning_object["max_erases_per_cycle"].GetUint64() > 0 )
        {
            config.max_erases_per_cycle = pruning_object["max_erases_per_cycle"].GetUint64();
        }

        // D-C2 settle knob: a degenerate 0 would let a cycle fire during
        // initial sync; rejected at read time, default retained.
        if ( pruning_object.HasMember( "settle_seconds" ) && pruning_object["settle_seconds"].IsUint64() &&
             pruning_object["settle_seconds"].GetUint64() > 0 )
        {
            config.settle_seconds = pruning_object["settle_seconds"].GetUint64();
        }

        // Compaction threshold knob (D-01): a degenerate 0 would compact
        // after every cycle (write-amp denial of service on self, V5);
        // rejected at read time, default retained.
        if ( pruning_object.HasMember( "compact_threshold_mb" ) && pruning_object["compact_threshold_mb"].IsUint64() &&
             pruning_object["compact_threshold_mb"].GetUint64() > 0 )
        {
            config.compact_threshold_mb = pruning_object["compact_threshold_mb"].GetUint64();
        }

        if ( config.enabled && !config.finality_window.has_value() )
        {
            // Fail closed (D-02): a positive N is required when enabled; there
            // is no default window, so an enabled request without one (or with
            // the degenerate 0) disables itself.
            config.enabled         = false;
            config.disabled_reason = std::string( kWindowRequiredMessage );
        }

        return config;
    }

    outcome::result<std::shared_ptr<PruneEligibility>> PruneEligibility::New( crdt::GlobalDB      &globaldb,
                                                                              TransactionManager  &transaction_manager,
                                                                              UTXOManager         &utxo_manager,
                                                                              NodeType             node_type,
                                                                              const PruningConfig &config )
    {
        if ( node_type != NodeType::Light )
        {
            // GATE-01 role gate: full/archive nodes never erase history. The
            // PruningConfig default-disabled is the separate consent gate and
            // is deliberately not consulted here.
            return outcome::failure( std::errc::operation_not_permitted );
        }
        return std::shared_ptr<PruneEligibility>(
            new PruneEligibility( globaldb, transaction_manager, utxo_manager, config ) );
    }

    bool PruneEligibility::IsAllowlistedTxKey( const std::string &key ) const
    {
        // THE one allowlist home (D-C7): exact base + "tx/" startswith over the
        // monitored networks only. The trailing slash is load-bearing — the
        // slashless form also matches "txfoo"-style siblings (T-17-0401).
        for ( auto network_id : transaction_manager_m.GetMonitoredNetworkIDs() )
        {
            const std::string prefix = TransactionManager::GetBlockChainBase( network_id ) + "tx/";
            if ( key.rfind( prefix, 0 ) == 0 )
            {
                return true;
            }
        }
        return false;
    }

    std::string PruneEligibility::BuildPruneMarkerKey( const std::string &tx_hash )
    {
        // Node-local prefix discipline (CRDTWorkJournal shape): the hash keys
        // the entry, the tx PATH travels inside the record value.
        return std::string( PRUNE_MARKER_PREFIX ) + tx_hash;
    }

    outcome::result<std::pair<PrunedLookup, std::optional<SGTransaction::PrunedTransactionRecord>>> PruneEligibility::
        LookupPrunedEntry( crdt::GlobalDB &db, const std::string &tx_hash )
    {
        crdt::GlobalDB::Buffer marker_key;
        marker_key.put( BuildPruneMarkerKey( tx_hash ) );
        auto marker = db.GetRaw( marker_key );
        if ( marker.has_error() )
        {
            if ( marker.error() == storage::DatabaseError::NOT_FOUND )
            {
                // No marker: this node never pruned the transaction.
                return std::make_pair( PrunedLookup::NOT_FOUND, std::nullopt );
            }
            return outcome::failure( marker.error() );
        }

        SGTransaction::PrunedTransactionRecord record;
        if ( !record.ParseFromArray( marker.value().data(), static_cast<int>( marker.value().size() ) ) )
        {
            // T-17-0502: a malformed marker is an ERROR, never a silent PRUNED.
            MarkerLogger()->error( "Pruned marker for tx {} is malformed", tx_hash );
            return outcome::failure( std::errc::bad_message );
        }

        // WR-04: the record's tx_path must be THE path this hash names (the
        // hash is the path's terminal segment). A marker pointing elsewhere —
        // or an empty payload whose tx_path is empty — would otherwise answer
        // PRUNED for the wrong key (granting SPENDER_PRUNED_WINDOW_SATISFIED)
        // or prefix-scan the entire store. Fail closed, never guess.
        const std::string &marker_path = record.tx_path();
        if ( marker_path.empty() || marker_path.size() < tx_hash.size() ||
             marker_path.compare( marker_path.size() - tx_hash.size(), tx_hash.size(), tx_hash ) != 0 )
        {
            MarkerLogger()->error( "Pruned marker for tx {} carries mismatched path {}", tx_hash, marker_path );
            return outcome::failure( std::errc::bad_message );
        }

        // Value presence through the same CRDT-layer query surface the
        // evaluator enumerates (T-17-0503: one resolution path for both reads).
        // Exact-key match — a prefix scan alone would also catch longer
        // sibling keys sharing the path as a prefix.
        bool value_present = false;
        if ( auto entries = db.QueryKeyValues( record.tx_path() ); !entries.has_error() )
        {
            for ( const auto &[key, value] : entries.value() )
            {
                (void) value; // presence only, the bytes are irrelevant here
                auto key_string = db.KeyToString( key );
                if ( !key_string.has_error() && key_string.value() == record.tx_path() )
                {
                    value_present = true;
                    break;
                }
            }
        }
        else
        {
            return outcome::failure( entries.error() );
        }

        if ( value_present )
        {
            // T-17-0503: report loudly, never guess which side is right. The
            // Phase-18 same-batch write makes this state atomically impossible.
            MarkerLogger()->error( "Pruned marker AND value both present for tx {} at {} — invariant violation",
                                   tx_hash,
                                   record.tx_path() );
            return std::make_pair( PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION, record );
        }

        return std::make_pair( PrunedLookup::PRUNED, record );
    }

    std::pair<base::Buffer, base::Buffer> PruneEligibility::BuildPruneMarkerEntry( const std::string &tx_hash,
                                                                                   const std::string &resolved_tx_path )
    {
        // The production twin of WritePrunedEntryForTest's body: the SAME key
        // home (BuildPruneMarkerKey) and record shape, returned as buffers for
        // the caller to stage into the erase WriteBatch (value XOR marker in
        // one commit) instead of written directly. The record names the
        // RESOLVED path the caller's erase targets (WR-02) — never a
        // local-network formatting of the hash alone, so a cross-network
        // entry is marked with ITS network's path.
        SGTransaction::PrunedTransactionRecord record;
        record.set_tx_path( resolved_tx_path );

        base::Buffer key_buffer;
        key_buffer.put( BuildPruneMarkerKey( tx_hash ) );
        base::Buffer value_buffer;
        value_buffer.put( record.SerializeAsString() );
        return { std::move( key_buffer ), std::move( value_buffer ) };
    }

    outcome::result<void> PruneEligibility::WritePrunedEntryForTest(
        crdt::GlobalDB                               &db,
        const std::string                            &tx_hash,
        const SGTransaction::PrunedTransactionRecord &record )
    {
        // TEST-ONLY (see header): the production write joins Phase 18's erase
        // WriteBatch so value XOR marker holds atomically.
        crdt::GlobalDB::Buffer key_buffer;
        key_buffer.put( BuildPruneMarkerKey( tx_hash ) );
        crdt::GlobalDB::Buffer value_buffer;
        value_buffer.put( record.SerializeAsString() );
        return db.PutRaw( key_buffer, value_buffer );
    }

    outcome::result<PruneEvaluationResult> PruneEligibility::Evaluate() const
    {
        if ( !config_m.finality_window.has_value() )
        {
            // Fail closed: a window without N cannot make anything eligible.
            logger_->warn( "Prune evaluation requested without a finality window" );
            return outcome::failure( std::errc::invalid_argument );
        }
        const uint64_t window = config_m.finality_window.value();

        auto head_list = globaldb_m.GetCRDTHeadList();
        if ( head_list.has_error() )
        {
            logger_->error( "Unable to read CRDT head list for prune evaluation: {}", head_list.error().message() );
            return outcome::failure( head_list.error() );
        }
        const uint64_t max_height = head_list.value().second;

        PruneEvaluationResult result;
        for ( auto network_id : transaction_manager_m.GetMonitoredNetworkIDs() )
        {
            // Trailing slash is load-bearing (Phase-16 lesson): the slashless
            // form also matches "txfoo"-style siblings of the tx/ namespace.
            const std::string query_path       = TransactionManager::GetBlockChainBase( network_id ) + "tx/";
            auto              transaction_list = globaldb_m.QueryKeyValues( query_path );
            if ( transaction_list.has_error() )
            {
                logger_->error( "Unable to query transactions on {}", query_path );
                continue; // never abort the whole scan for one network
            }

            for ( const auto &[key, value] : transaction_list.value() )
            {
                auto transaction_key = globaldb_m.KeyToString( key );
                if ( transaction_key.has_error() )
                {
                    logger_->error( "Unable to convert a key to string" );
                    continue;
                }
                result.candidates.push_back( EvaluateCandidate( transaction_key.value(),
                                                                value,
                                                                IsAllowlistedTxKey( transaction_key.value() ),
                                                                max_height,
                                                                window ) );
            }
        }

        // PRUNE-03 block gate: one verdict per DISTINCT CID among the eligible
        // candidates (batched commits share one block, so several eligible
        // transactions often map to the same CID). Tx eligibility (PRUNE-01)
        // and block erasability (PRUNE-03) stay SEPARATE verdicts — a block
        // held by a non-eligible sibling never flips a candidate's flag.
        std::set<std::string> gated_cids;
        for ( const auto &candidate : result.candidates )
        {
            if ( !candidate.eligible )
            {
                continue;
            }
            const auto &candidate_cid = candidate.cid;
            if ( !candidate_cid.has_value() )
            {
                // WR-05: an eligible candidate whose tx->CID map entry could
                // not be resolved still gets an explicit block verdict —
                // fail closed, never silently dropped from the report.
                BlockGateResult ungated;
                ungated.cid = "<unresolved for " + candidate.tx_path + ">";
                RecordReason( ungated, PruneReason::CID_UNRESOLVED );
                logger_->error( "Eligible candidate {} has no resolvable CID — reporting an ungated block verdict",
                                candidate.tx_path );
                result.block_reports.push_back( ungated );
                continue;
            }
            const std::string &cid = candidate_cid.value();
            if ( !gated_cids.insert( cid ).second )
            {
                continue; // this block already gated (shared CID)
            }
            auto gate = EvaluateDeltaBlockErasable( cid, result );
            if ( gate.has_error() )
            {
                logger_->error( "Block gate failed for cid {}", cid );
                continue; // never abort the report for one block
            }
            result.block_reports.push_back( gate.value() );
        }

        const auto erasable_blocks = static_cast<size_t>( std::count_if( result.block_reports.begin(),
                                                                         result.block_reports.end(),
                                                                         []( const BlockGateResult &report )
                                                                         { return report.erasable; } ) );
        logger_->info( "Prune evaluation: {} candidates, {} eligible, {}/{} blocks erasable",
                       result.candidates.size(),
                       result.EligibleCount(),
                       erasable_blocks,
                       result.block_reports.size() );
        return result;
    }

    outcome::result<BlockGateResult> PruneEligibility::EvaluateDeltaBlockErasable(
        const std::string           &cid_string,
        const PruneEvaluationResult &eligible ) const
    {
        BlockGateResult report;
        report.cid = cid_string;

        // Sharing count (the block-share divisor): derived from the same
        // report the gate already received -- no store reads.
        report.sharing_tx_count = static_cast<uint64_t>( std::count_if(
            eligible.candidates.begin(),
            eligible.candidates.end(),
            [&cid_string]( const PruneCandidateResult &candidate )
            { return candidate.eligible && candidate.cid.has_value() && *candidate.cid == cid_string; } ) );

        // The block is the source of truth (D-C3): parse it NOW, keys only,
        // elements AND tombstones — "what this node applied" is forbidden as
        // an oracle (light nodes store elements they never applied).
        auto keys = globaldb_m.GetLocalDeltaKeys( cid_string );
        if ( keys.has_error() )
        {
            // Discriminate ABSENT from corrupt: byte presence (no parse) —
            // a corrupt-but-present block still fails closed below.
            auto present = globaldb_m.HasLocalBlock( cid_string );
            if ( present.has_value() && !present.value() )
            {
                // The block is ABSENT. Soundness: raw block bytes disappear
                // ONLY through the sanctioned erase path (EraseEntry's
                // staged CID::toBytes() removal is the sole production
                // writer of that deletion — the Phase-16 inventory; the seal
                // marker is a different key and SURVIVES, so isResolved
                // still guards re-delivery, GATE-03). Absence therefore
                // means the sibling group already passed this gate and was
                // erased; the verdict COMPLETES a stranded sibling's erase
                // (value + map entry + marker — block staging is an
                // idempotent no-op on the absent key). It fires ONLY when
                // this CID is named by an otherwise-ELIGIBLE candidate: the
                // evidence completes an erase the caller already justified.
                if ( report.sharing_tx_count > 0 )
                {
                    RecordReason( report, PruneReason::BLOCK_ALREADY_ERASED );
                    report.erasable = true;
                    return report;
                }
            }
            // Corrupt-but-present bytes, an unreadable store, or an absent
            // block NOT tied to an eligible candidate: parse failure fails
            // closed — not erasable, never a crash.
            logger_->error( "Unable to parse delta block {} for the block gate: {}",
                            cid_string,
                            keys.error().message() );
            RecordReason( report, PruneReason::DESERIALIZE_FAILED );
            return report;
        }

        for ( const auto &key : keys.value() )
        {
            // Threshold-estimate carry (research A5): the parse above is the
            // only read -- the carried-key byte total rides this same loop.
            report.block_bytes += static_cast<uint64_t>( key.size() );
            if ( !IsAllowlistedTxKey( key ) )
            {
                RecordReason( report, PruneReason::OUT_OF_PREFIX );
                continue;
            }
            const bool in_eligible_set = std::any_of( eligible.candidates.begin(),
                                                      eligible.candidates.end(),
                                                      [&key]( const PruneCandidateResult &candidate )
                                                      { return candidate.eligible && candidate.tx_path == key; } );
            if ( !in_eligible_set )
            {
                RecordReason( report, PruneReason::NON_ELIGIBLE_SIBLING );
            }
        }

        report.erasable = report.reasons.empty();
        return report;
    }

    PruneCandidateResult PruneEligibility::EvaluateCandidate( const std::string  &key,
                                                              const base::Buffer &value,
                                                              bool                allowlisted,
                                                              uint64_t            max_height,
                                                              uint64_t            window ) const
    {
        PruneCandidateResult candidate;
        candidate.tx_path     = key;
        candidate.value_bytes = static_cast<uint64_t>( value.size() );

        // Defensive allowlist (belt-and-braces): out-of-prefix keys are
        // reported with a reason, never silently dropped.
        if ( !allowlisted )
        {
            RecordReason( candidate, PruneReason::OUT_OF_PREFIX );
            return candidate;
        }

        auto transaction = TransactionManager::DeSerializeTransaction( value );
        if ( transaction.has_error() || !transaction.value() )
        {
            // Malformed stored bytes never crash the scan and never become
            // eligible (T-17-0405).
            RecordReason( candidate, PruneReason::DESERIALIZE_FAILED );
            return candidate;
        }

        if ( auto cid = transaction_manager_m.GetTransactionCID( transaction.value()->GetHash() ); !cid.has_error() )
        {
            candidate.cid = cid.value();
        }

        auto outputs = transaction.value()->GetProducedUTXOs();
        if ( !outputs.has_value() || outputs->empty() )
        {
            // No (or zero) produced outputs: no spend evidence can exist —
            // vacuous "all outputs spent" must not make a tx eligible.
            RecordReason( candidate, PruneReason::NO_OUTPUTS );
            return candidate;
        }

        std::optional<uint64_t> spend_height;
        for ( const auto &output : *outputs )
        {
            if ( !utxo_manager_m.IsOutPointGenuinelyConsumed( output.GetTxID(), output.GetOutputIdx() ) )
            {
                // Synthesized zero-amount tombstones fail here (D-C2).
                RecordReason( candidate, PruneReason::UNSPENT_OUTPUT );
                continue;
            }

            auto spender = utxo_manager_m.GetOutPointSpender( output.GetTxID(), output.GetOutputIdx() );
            if ( !spender.has_value() )
            {
                RecordReason( candidate, PruneReason::SPENDER_NOT_RECORDED );
                continue;
            }

            // Existence BEFORE priority (T-17-0403): resolve the spender's
            // tx->CID map entry first — a missing key would read priority 0,
            // which satisfies the window vacuously.
            auto resolved = transaction_manager_m.ResolveTransactionPathAndCID( spender->toReadableString() );
            if ( resolved.has_error() )
            {
                // Pruned-marker refinement (D-02/B spec point 5): the spender's
                // own entries may be gone because the spender was ITSELF pruned
                // (marker present, value absent). A pruned spender's own window
                // elapsed by construction, so this spend is older still — the
                // output's window counts as SATISFIED, with the explicit
                // evidence code (never silent). NOT_FOUND or an invariant
                // violation keeps SPENDER_UNRESOLVED (fail closed).
                auto pruned = LookupPrunedEntry( globaldb_m, spender->toReadableString() );
                if ( !pruned.has_error() && pruned.value().first == PrunedLookup::PRUNED )
                {
                    RecordReason( candidate, PruneReason::SPENDER_PRUNED_WINDOW_SATISFIED );
                    // Effective spend height 0 — the earliest DAG height. The
                    // max-over-outputs binding is preserved (0 never displaces
                    // a resolved height), and the window check below still
                    // requires max_height >= N (fail closed under it).
                    if ( !spend_height.has_value() )
                    {
                        spend_height = 0;
                    }
                    continue;
                }
                RecordReason( candidate, PruneReason::SPENDER_UNRESOLVED );
                continue;
            }

            auto priority = globaldb_m.GetKeyPriority( resolved.value().first );
            if ( priority.has_error() )
            {
                RecordReason( candidate, PruneReason::SPENDER_UNRESOLVED );
                continue;
            }

            // The MOST-RECENT spend binds (Pitfall 3): outputs of one
            // transaction can be spent at different times — max, never first.
            if ( !spend_height.has_value() || priority.value() > spend_height.value() )
            {
                spend_height = priority.value();
            }
        }

        candidate.spend_height = spend_height;

        if ( HasFailureReason( candidate.reasons ) )
        {
            // A per-output gate already failed; spend_height stays as the
            // diagnostic of whatever partial evidence existed. (The evidence
            // code alone does not fail the candidate.)
            return candidate;
        }

        // The one home of the window arithmetic (D-02): spend recency, never
        // creation age. An anomalous spend_height above max_height fails too.
        const bool window_passed = spend_height.has_value() && max_height >= spend_height.value() &&
                                   ( max_height - spend_height.value() ) >= window;
        if ( !window_passed )
        {
            RecordReason( candidate, PruneReason::SPEND_WITHIN_WINDOW );
            return candidate;
        }

        candidate.eligible = true;
        return candidate;
    }

    PruneEligibility::PruneEligibility( crdt::GlobalDB      &globaldb,
                                        TransactionManager  &transaction_manager,
                                        UTXOManager         &utxo_manager,
                                        const PruningConfig &config ) :
        globaldb_m( globaldb ),
        transaction_manager_m( transaction_manager ),
        utxo_manager_m( utxo_manager ),
        config_m( config )
    {
    }
} // namespace sgns
