/**
 * @file       PruneEligibility.hpp
 * @brief      PRUNE-01 dry-run eligibility evaluator: pure structured result over
 *             transaction-key enumeration (D-03).
 * @details    Decides, READ-ONLY, which stored transactions are erasable by the
 *             Phase-18 TxPruner: every output genuinely consumed (D-C2) and the
 *             MOST-RECENT spend beyond the finality window (D-02 — spend recency,
 *             never creation age). Construction fails closed unless the node role
 *             is Light (GATE-01); the PruningConfig default (disabled) is the
 *             consent gate (GATE-06 policy half). The window arithmetic lives in
 *             Evaluate() and nowhere else — the TxPruner reuses this evaluator
 *             verbatim.
 * @date       2026-10-19
 */

#ifndef _SGNS_PRUNE_ELIGIBILITY_HPP_
#define _SGNS_PRUNE_ELIGIBILITY_HPP_

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rapidjson/document.h>

#include "account/NodeType.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    namespace crdt
    {
        class GlobalDB;
    }

    class TransactionManager;
    class UTXOManager;

    /**
     * @brief       Why one candidate is (or is not) prune-eligible (D-03 structured result).
     * @details     Every ineligible candidate carries at least one of these codes —
     *              nothing is silently dropped (repudiability). Codes are fail-closed:
     *              each one names a check whose failure must block erasure, EXCEPT
     *              SPENDER_PRUNED_WINDOW_SATISFIED, the sole affirmative-evidence
     *              code (it appears on ELIGIBLE verdicts so a satisfied-by-marker
     *              window is explicit, never silent).
     */
    enum class PruneReason : uint8_t
    {
        UNSPENT_OUTPUT,       ///< An output is not genuinely consumed (D-C2); synthesized
                              ///< zero-amount tombstones land here — they are not spend evidence.
        SPENDER_NOT_RECORDED, ///< The output is genuinely consumed but no spending
                              ///< transaction identity was recorded (spent_by_txid unset).
        SPENDER_UNRESOLVED,   ///< The recorded spender's tx->CID map entry was not found,
                              ///< or its priority could not be read (fail closed; the
                              ///< pruned-marker refinement below narrows this). Existence
                              ///< MUST be established BEFORE any priority read:
                              ///< GetKeyPriority reports 0 for a missing key, which
                              ///< would satisfy the window vacuously.
        SPEND_WITHIN_WINDOW,  ///< The most-recent spend is not beyond the finality window
                              ///< (maxHeight − spend_height &lt; N).
        NO_OUTPUTS,           ///< The transaction produces no UTXOs (GetProducedUTXOs
                              ///< nullopt) — no spend evidence can exist (fail closed).
        OUT_OF_PREFIX,        ///< The candidate key is outside the /bc-&lt;net&gt;/tx/
                              ///< allowlist (defensive; the enumeration prefix already
                              ///< excludes such keys — reported, never dropped).
        DESERIALIZE_FAILED,   ///< The stored value could not be deserialized into a
                              ///< transaction (fail closed; the scan continues).
        NON_ELIGIBLE_SIBLING, ///< PRUNE-03 block gate: a key the delta block carries
                              ///< (element OR tombstone) is allowlist-conformant but
                              ///< not in the eligible set — the block holds a sibling
                              ///< that is not itself prunable, so the BLOCK is not
                              ///< erasable (the tx can still be eligible on its own).
        CID_UNRESOLVED,       ///< PRUNE-03 block gate: an ELIGIBLE candidate's
                              ///< tx->CID map entry could not be resolved, so no
                              ///< delta-block verdict can be computed — reported
                              ///< as an explicit not-erasable block report
                              ///< (fail closed), never silently dropped from
                              ///< the report (WR-05).
        SPENDER_PRUNED_WINDOW_SATISFIED, ///< EVIDENCE code (the only non-failure
                                         ///< reason): the spender's own entries are gone because
                                         ///< the spender was pruned (marker present, value
                                         ///< absent). A pruned spender's own window elapsed by
                                         ///< construction, so this spend is older still — the
                                         ///< output's window counts as SATISFIED, explicitly
                                         ///< (D-02/B spec point 5, never silent).
    };

    /**
     * @brief       Tri-state verdict of the pruned-marker lookup (D-04 contract).
     * @details     The PRUNED-vs-NOT_FOUND distinction is Phase 19 SURV-02's
     *              answer source: "history pruned on this node" is a distinct,
     *              reportable state — never conflated with plain not-found. The
     *              invariant violation (marker AND value both present) is a
     *              state bug and is reported loudly — never guessed away.
     */
    enum class PrunedLookup : uint8_t
    {
        PRUNED,                            ///< Marker present AND value absent: the
                                           ///< transaction's history was physically
                                           ///< pruned on this node.
        NOT_FOUND,                         ///< No marker AND no value: the transaction
                                           ///< is simply unknown here.
        VALUE_PRESENT_INVARIANT_VIOLATION, ///< Marker AND value both present: violates
                                           ///< the value-XOR-marker invariant (Phase 18
                                           ///< writes both sides in ONE batch, so this
                                           ///< state signals corruption — report, never
                                           ///< guess which side is right).
    };

    /**
     * @brief       Structured verdict for one candidate transaction key.
     * @details     Named-evidence discipline (PublicChainInputValidator shape):
     *              fields are recorded only after the check that produces them
     *              ran; an eligible verdict carries no FAILURE reasons — the
     *              only permitted non-empty reasons on an eligible verdict is
     *              the sole evidence code SPENDER_PRUNED_WINDOW_SATISFIED.
     */
    struct PruneCandidateResult
    {
        /// Full CRDT key of the candidate (the /bc-<net>/tx/<hash> path enumerated).
        std::string tx_path;

        /// CID of the candidate from the tx->CID map (nullopt when unresolved).
        std::optional<std::string> cid;

        /// True only when every gate passed — reasons then carries no failure code.
        bool eligible = false;

        /// Every failed check, in evaluation order, plus (on eligible verdicts)
        /// the affirmative SPENDER_PRUNED_WINDOW_SATISFIED evidence if any.
        std::vector<PruneReason> reasons;

        /// Diagnostic: the MOST-RECENT spend height (max spender priority over
        /// outputs); nullopt when no binding spend height could be computed.
        std::optional<uint64_t> spend_height;
    };

    /**
     * @brief       Structured verdict of the multi-element block gate (PRUNE-03).
     * @details     A delta block is erasable only when EVERY key it carries —
     *              elements AND tombstones, parsed from the block itself at
     *              evaluation time — is allowlist-conformant and inside the
     *              eligible set (D-C3). Kept SEPARATE from the per-candidate
     *              eligible flag: a transaction can be eligible (PRUNE-01)
     *              while its block is held by a non-eligible sibling
     *              (PRUNE-03) — two verdicts, one report each.
     */
    struct BlockGateResult
    {
        /// CID string of the gated delta block — or the sentinel
        /// "&lt;unresolved for &lt;tx path&gt;&gt;" when an ELIGIBLE candidate's
        /// CID could not be resolved (CID_UNRESOLVED; the verdict is then
        /// not-erasable by construction, WR-05).
        std::string cid;

        /// True only when every carried key passed both checks — reasons is
        /// then empty.
        bool erasable = false;

        /// Every failed per-key check (deduplicated); empty iff erasable.
        std::vector<PruneReason> reasons;
    };

    /**
     * @brief       Structured result of one evaluation pass (D-03).
     * @details     Pure report: the evaluator erased nothing and wrote nothing.
     *              Production dry-run mode derives its per-cycle INFO summary
     *              from this structure (candidates AND block verdicts); the
     *              Phase-18 TxPruner consumes the eligible subset as its work
     *              list and consults block_reports before deleting a block.
     */
    struct PruneEvaluationResult
    {
        /// One entry per enumerated candidate key, eligible and ineligible alike.
        std::vector<PruneCandidateResult> candidates;

        /// PRUNE-03 block-gate verdicts, one per DISTINCT CID among the
        /// eligible candidates (deduplicated — batched commits put several
        /// eligible transactions into one block).
        std::vector<BlockGateResult> block_reports;

        /**
         * @brief   Number of eligible candidates in this pass.
         * @return  Count of candidates whose eligible flag is set.
         */
        size_t EligibleCount() const
        {
            return static_cast<size_t>( std::count_if( candidates.begin(),
                                                       candidates.end(),
                                                       []( const PruneCandidateResult &c ) { return c.eligible; } ) );
        }
    };

    /**
     * @brief       Pruning policy configuration (GATE-06 policy half).
     * @details     Parsed from the "pruning" object of sgns_config.json via
     *              FromJSON by GeniusNode (17-06), which delegates every rule
     *              to this contract. Default is DISABLED — the config default is
     *              the consent gate (NodeType defaulting to Light is a role gate,
     *              never consent; Pitfall 8).
     */
    struct PruningConfig
    {
        /// Whether pruning is enabled at all. Default false.
        bool enabled = false;

        /// Finality window N (D-02): required when enabled, no default value.
        /// A spend is beyond the window when maxHeight − spend_height ≥ N.
        std::optional<uint64_t> finality_window;

        /// Dry-run mode: report only, never erase (Phase 17 production mode).
        bool dry_run = false;

        /// Erase budget per cycle (D-04): at most this many eligible
        /// transactions erase per tick; the backlog self-drains across
        /// ticks. Default 100. A degenerate 0 is rejected at read time
        /// (WR-03 rule: a 0-K cycle can never drain) and the member keeps
        /// its safe default.
        uint64_t max_erases_per_cycle = 100;

        /// Settle window in seconds (D-C2): a real cycle fires only once the
        /// tick gate has held for this long, so a cycle cannot fire during
        /// initial sync. Default 60. A degenerate 0 is rejected at read time
        /// and the member keeps its safe default.
        uint64_t settle_seconds = 60;

        /// Set when FromJSON had to fail-close an enabled request (missing
        /// finality_window); carries the operator-facing reason.
        std::optional<std::string> disabled_reason;

        /**
         * @brief       Parses and validates a "pruning" JSON object (GATE-06).
         * @details     Per-key type-checked reads with HasMember guards; absent
         *              members keep defaults. Fail-closed rule: enabled=true
         *              WITHOUT a finality_window > 0 forces enabled=false and
         *              sets disabled_reason to the fixed message — a positive
         *              N is required when enabled (D-02); a degenerate 0 is
         *              rejected at read time (WR-03), exactly like an absent
         *              window. A non-object value yields defaults.
         *              max_erases_per_cycle (default 100) and settle_seconds
         *              (default 60) follow the same per-key discipline:
         *              explicit positive values parse; 0 values are degenerate
         *              and rejected at read time (a 0-K cycle can never drain,
         *              a settle-less cycle can fire during initial sync), so
         *              the members keep their safe defaults; ill-typed members
         *              keep defaults too.
         * @param[in]   pruning_object The "pruning" member of sgns_config.json.
         * @return      The validated configuration.
         */
        static PruningConfig FromJSON( const rapidjson::Value &pruning_object );
    };

    /**
     * @brief       Read-only prune-eligibility evaluator (PRUNE-01, GATE-01).
     * @details     PURE evaluator (D-C6): it runs on committed state only —
     *              driven from a timer cycle by the caller, NEVER from CRDT
     *              element callbacks (they fire pre-batch-commit). It performs
     *              ZERO writes: no Put, no Erase, no staging, no hooks. The
     *              Phase-18 TxPruner is the reuse seam — it calls Evaluate()
     *              verbatim and owns every mutation.
     *
     *              Gates:
     *              - Construction fails closed unless NodeType is Light
     *                (GATE-01 role gate; full/archive never erase history).
     *              - IsAllowlistedTxKey is the ONE allowlist home (D-C7):
     *                exact base + "tx/" prefix over monitored networks only,
     *                with the trailing slash (a slashless prefix would also
     *                match /bc-<net>/txfoo siblings).
     *              - Evaluate fails closed with std::errc::invalid_argument
     *                when no finality_window is configured.
     *              - EvaluateDeltaBlockErasable is the PRUNE-03 gate: a block
     *                erases only when every element AND tombstone key it
     *                carries is eligible and allowlist-conformant, parsed
     *                from the block itself (D-C3).
     */
    class PruneEligibility
    {
    public:
        /**
         * @brief       Fail-closed factory (the construction gate).
         * @details     Rejects any role other than NodeType::Light with
         *              std::errc::operation_not_permitted before any state is
         *              read (GATE-01). The PruningConfig default-disabled is
         *              the separate consent gate and is NOT re-checked here.
         * @param[in]   globaldb Committed-state reader for tx enumeration,
         *              priorities, and head height.
         * @param[in]   transaction_manager Owner of the tx path/allowlist
         *              contracts and the tx->CID map resolution.
         * @param[in]   utxo_manager Owner of the outpoint lifecycle (spend
         *              discriminator and spender carrier).
         * @param[in]   node_type Deployment role of the constructing node.
         * @param[in]   config Validated pruning policy.
         * @return      The evaluator, or outcome::failure for a non-Light role.
         */
        static outcome::result<std::shared_ptr<PruneEligibility>> New( crdt::GlobalDB      &globaldb,
                                                                       TransactionManager  &transaction_manager,
                                                                       UTXOManager         &utxo_manager,
                                                                       NodeType             node_type,
                                                                       const PruningConfig &config );

        /**
         * @brief       THE allowlist (D-C7): exact /bc-<net>/tx/ prefix membership.
         * @details     True only when @p key starts with
         *              GetBlockChainBase(net) + "tx/" for a net in
         *              TransactionManager::GetMonitoredNetworkIDs(). The
         *              trailing slash is mandatory (T-17-0401): cert slots,
         *              registration paths, proof keys, unmonitored networks,
         *              and slashless "txfoo" siblings all fail here.
         * @param[in]   key Candidate storage key.
         * @return      true when the key is inside the allowlisted prefix.
         */
        bool IsAllowlistedTxKey( const std::string &key ) const;

        /**
         * @brief       Enumerates and evaluates every candidate (read-only).
         * @details     Per monitored network: QueryKeyValues(base + "tx/")
         *              (WITH the trailing slash), deserialize each value, then
         *              per candidate: genuine-consumption of every output
         *              (D-C2), spender resolution with existence BEFORE any
         *              priority read, spend_height = MAX spender priority over
         *              outputs (the most-recent spend binds), and the window
         *              maxHeight − spend_height ≥ finality_window (D-02). The
         *              window arithmetic lives HERE and nowhere else. Query or
         *              per-entry failures are logged and the scan continues
         *              (never aborts); every ineligible candidate carries its
         *              PruneReason codes.
         * @return      The structured eligible/ineligible report, or
         *              outcome::failure(std::errc::invalid_argument) when no
         *              finality_window is configured (fail closed — a window
         *              without N cannot make anything eligible).
         */
        outcome::result<PruneEvaluationResult> Evaluate() const;

        /**
         * @brief       THE multi-element block gate (PRUNE-03, D-C3).
         * @details     Parses the delta block @p cid_string itself, at
         *              evaluation time, via GlobalDB::GetLocalDeltaKeys
         *              (elements AND tombstones; "what this node applied" is
         *              forbidden as an oracle — light nodes store elements
         *              they never applied). The block is erasable only when
         *              EVERY carried key is (a) IsAllowlistedTxKey-true and
         *              (b) present in @p eligible's eligible set (matched by
         *              tx path). A non-conformant key reports OUT_OF_PREFIX;
         *              an allowlisted key outside the eligible set reports
         *              NON_ELIGIBLE_SIBLING (an elements-only gate would miss
         *              tombstone keys — Pitfall 4). Parse failure fails
         *              closed: a not-erasable result carrying
         *              DESERIALIZE_FAILED, never a crash and never a guess.
         *              Read-only; writes nothing.
         * @param[in]   cid_string CID of the delta block to gate.
         * @param[in]   eligible Evaluation whose eligible set (PRUNE-01
         *              verdicts) the carried keys are matched against.
         * @return      The block verdict (erasable=false with reasons on any
         *              miss or parse failure).
         */
        outcome::result<BlockGateResult> EvaluateDeltaBlockErasable( const std::string           &cid_string,
                                                                     const PruneEvaluationResult &eligible ) const;

        //
        // Pruned-marker contract (D-04 design — Phase 17 delivers format +
        // lookup semantics; the production WRITE joins Phase 18's erase batch).
        //

        /// Node-local marker key namespace (CRDTWorkJournal prefix discipline:
        /// same shared RocksDB, direct datastore writes, never broadcast).
        static constexpr std::string_view PRUNE_MARKER_PREFIX = "/prune/tx/";

        /**
         * @brief       Builds the node-local marker key for @p tx_hash.
         * @details     PRUNE_MARKER_PREFIX + hash; the tx PATH travels inside
         *              the record value because lookups are by hash.
         * @param[in]   tx_hash Transaction hash (readable string form).
         * @return      "/prune/tx/&lt;tx_hash&gt;".
         */
        static std::string BuildPruneMarkerKey( const std::string &tx_hash );

        /**
         * @brief       Answers "was this transaction's history pruned here?" (D-04).
         * @details     Reads the marker at BuildPruneMarkerKey(tx_hash) through
         *              the node-local raw surface, then establishes value
         *              presence for the record's tx_path through the same
         *              CRDT-layer query surface the evaluator enumerates —
         *              both reads share one resolution path (T-17-0503).
         *              Verdicts: marker ∧ ¬value ⇒ PRUNED (record decoded);
         *              ¬marker ∧ ¬value ⇒ NOT_FOUND; marker ∧ value ⇒
         *              VALUE_PRESENT_INVARIANT_VIOLATION (logged at ERROR —
         *              never guessed). A malformed marker payload is an error
         *              result, never a silent PRUNED (T-17-0502), and a
         *              decoded record whose tx_path does not end with the
         *              requested tx_hash (the empty-payload degenerate case
         *              included) is an invariant-violation error too — never
         *              a silent PRUNED and never a whole-store prefix scan
         *              (WR-04). Writes nothing.
         * @param[in]   db Committed-state reader (marker + value presence).
         * @param[in]   tx_hash Transaction hash (readable string form).
         * @return      (verdict, decoded record when a marker decoded), or
         *              outcome::failure for a raw-read/decode/presence error.
         */
        static outcome::result<std::pair<PrunedLookup, std::optional<SGTransaction::PrunedTransactionRecord>>>
        LookupPrunedEntry( crdt::GlobalDB &db, const std::string &tx_hash );

        /**
         * @brief       Builds the production marker entry for @p tx_hash (D-C1).
         * @details     The production twin of WritePrunedEntryForTest: returns
         *              the key/value buffer pair — BuildPruneMarkerKey(tx_hash)
         *              plus a PrunedTransactionRecord carrying the tx path
         *              named by the hash — instead of writing it. The caller
         *              stages the pair into the SAME WriteBatch as the erase
         *              via GlobalDB::EraseEntry's staged-extra-entry
         *              parameter, so the value-XOR-marker invariant is
         *              established atomically with the erase (one commit:
         *              value absent AND marker present, never either alone).
         *              The key home and record shape live HERE — callers never
         *              rebuild them.
         * @param[in]   tx_hash Transaction hash (readable string form).
         * @return      (marker key buffer, serialized record buffer).
         */
        static std::pair<base::Buffer, base::Buffer> BuildPruneMarkerEntry( const std::string &tx_hash );

        /**
         * @brief       TEST-ONLY marker writer (the D-04 test seam).
         * @details     Serializes @p record and writes it at the marker key on
         *              the node-local raw surface. Production NEVER calls this:
         *              the real marker write joins Phase 18's erase WriteBatch
         *              so the value-XOR-marker invariant is established
         *              atomically with the erase itself.
         * @note        No production caller may invoke this method.
         * @param[in]   db Store receiving the marker.
         * @param[in]   tx_hash Transaction hash the marker is keyed by.
         * @param[in]   record Marker payload to persist.
         * @return      outcome::success, or outcome::failure on I/O error.
         */
        static outcome::result<void> WritePrunedEntryForTest( crdt::GlobalDB                               &db,
                                                              const std::string                            &tx_hash,
                                                              const SGTransaction::PrunedTransactionRecord &record );

    private:
        PruneEligibility( crdt::GlobalDB      &globaldb,
                          TransactionManager  &transaction_manager,
                          UTXOManager         &utxo_manager,
                          const PruningConfig &config );

        /**
         * @brief       Evaluates one enumerated candidate against every gate.
         * @details     Fail-closed gate order: allowlist, deserialize, outputs,
         *              then per output genuine-consumption (D-C2) -> spender
         *              recorded -> spender RESOLVED (existence) -> spender
         *              priority. Existence is established BEFORE any priority
         *              read because GetKeyPriority reports 0 for a missing key —
         *              0 would satisfy the window vacuously (T-17-0403).
         *              spend_height is the MAX spender priority over outputs
         *              (the most-recent spend binds).
         * @param[in]   key Full candidate storage key.
         * @param[in]   value Serialized transaction bytes.
         * @param[in]   allowlisted Pre-computed IsAllowlistedTxKey verdict.
         * @param[in]   max_height Head height for the window comparison.
         * @param[in]   window Configured finality window N.
         * @return      The structured per-candidate verdict.
         */
        PruneCandidateResult EvaluateCandidate( const std::string  &key,
                                                const base::Buffer &value,
                                                bool                allowlisted,
                                                uint64_t            max_height,
                                                uint64_t            window ) const;

        /// Committed-state reader (enumeration, priorities, head height). Never written.
        crdt::GlobalDB &globaldb_m;

        /// Transaction-domain owner (paths, monitored networks, tx->CID map).
        TransactionManager &transaction_manager_m;

        /// Outpoint-lifecycle owner (genuine-spend discriminator, spender carrier).
        UTXOManager &utxo_manager_m;

        /// Validated policy snapshot.
        PruningConfig config_m;

        base::Logger logger_ = base::createLogger( "PruneEligibility" );
    };
} // namespace sgns

#endif
