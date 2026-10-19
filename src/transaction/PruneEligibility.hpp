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
#include <vector>

#include <rapidjson/document.h>

#include "account/NodeType.hpp"
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
     *              each one names a check whose failure must block erasure.
     */
    enum class PruneReason : uint8_t
    {
        UNSPENT_OUTPUT,       ///< An output is not genuinely consumed (D-C2); synthesized
                              ///< zero-amount tombstones land here — they are not spend evidence.
        SPENDER_NOT_RECORDED, ///< The output is genuinely consumed but no spending
                              ///< transaction identity was recorded (spent_by_txid unset).
        SPENDER_UNRESOLVED,   ///< The recorded spender's tx->CID map entry was not found,
                              ///< or its priority could not be read (fail closed; refined
                              ///< to pruned-marker semantics in 17-05). Existence MUST be
                              ///< established BEFORE any priority read: GetKeyPriority
                              ///< reports 0 for a missing key, which would satisfy the
                              ///< window vacuously.
        SPEND_WITHIN_WINDOW,  ///< The most-recent spend is not beyond the finality window
                              ///< (maxHeight − spend_height &lt; N).
        NO_OUTPUTS,           ///< The transaction produces no UTXOs (GetProducedUTXOs
                              ///< nullopt) — no spend evidence can exist (fail closed).
        OUT_OF_PREFIX,        ///< The candidate key is outside the /bc-&lt;net&gt;/tx/
                              ///< allowlist (defensive; the enumeration prefix already
                              ///< excludes such keys — reported, never dropped).
        DESERIALIZE_FAILED,   ///< The stored value could not be deserialized into a
                              ///< transaction (fail closed; the scan continues).
    };

    /**
     * @brief       Structured verdict for one candidate transaction key.
     * @details     Named-evidence discipline (PublicChainInputValidator shape):
     *              fields are recorded only after the check that produces them ran;
     *              an eligible verdict exists only with an empty reasons vector.
     */
    struct PruneCandidateResult
    {
        /// Full CRDT key of the candidate (the /bc-<net>/tx/<hash> path enumerated).
        std::string tx_path;

        /// CID of the candidate from the tx->CID map (nullopt when unresolved).
        std::optional<std::string> cid;

        /// True only when every gate passed — reasons is then empty.
        bool eligible = false;

        /// Every failed check, in evaluation order; empty iff eligible.
        std::vector<PruneReason> reasons;

        /// Diagnostic: the MOST-RECENT spend height (max spender priority over
        /// outputs); nullopt when no binding spend height could be computed.
        std::optional<uint64_t> spend_height;
    };

    /**
     * @brief       Structured result of one evaluation pass (D-03).
     * @details     Pure report: the evaluator erased nothing and wrote nothing.
     *              Production dry-run mode derives its per-cycle INFO summary
     *              from this structure; the Phase-18 TxPruner consumes the
     *              eligible subset as its work list.
     */
    struct PruneEvaluationResult
    {
        /// One entry per enumerated candidate key, eligible and ineligible alike.
        std::vector<PruneCandidateResult> candidates;

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

        /// Set when FromJSON had to fail-close an enabled request (missing
        /// finality_window); carries the operator-facing reason.
        std::optional<std::string> disabled_reason;

        /**
         * @brief       Parses and validates a "pruning" JSON object (GATE-06).
         * @details     Per-key type-checked reads with HasMember guards; absent
         *              members keep defaults. Fail-closed rule: enabled=true
         *              WITHOUT finality_window forces enabled=false and sets
         *              disabled_reason to the fixed message — N is required
         *              when enabled (D-02). A non-object value yields defaults.
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
