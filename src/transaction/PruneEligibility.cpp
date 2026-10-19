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

#include "account/UTXOManager.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "transaction/TransactionManager.hpp"

namespace sgns
{
    namespace
    {
        /// Fixed operator-facing message when an enabled request lacks
        /// finality_window and must be fail-closed (GATE-06 / D-02).
        constexpr char kWindowRequiredMessage[] =
            "pruning.enabled requires pruning.finality_window; pruning disabled (fail closed)";

        /// Records @p reason on @p candidate once (per-category evidence, no duplicates).
        void RecordReason( PruneCandidateResult &candidate, PruneReason reason )
        {
            if ( std::find( candidate.reasons.begin(), candidate.reasons.end(), reason ) == candidate.reasons.end() )
            {
                candidate.reasons.push_back( reason );
            }
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

        if ( pruning_object.HasMember( "finality_window" ) && pruning_object["finality_window"].IsUint64() )
        {
            config.finality_window = pruning_object["finality_window"].GetUint64();
        }

        if ( config.enabled && !config.finality_window.has_value() )
        {
            // Fail closed (D-02): N is required when enabled; there is no default
            // window, so an enabled request without one disables itself.
            config.enabled        = false;
            config.disabled_reason = std::string( kWindowRequiredMessage );
        }

        return config;
    }

    outcome::result<std::shared_ptr<PruneEligibility>> PruneEligibility::New(
        crdt::GlobalDB       &globaldb,
        TransactionManager   &transaction_manager,
        UTXOManager          &utxo_manager,
        NodeType              node_type,
        const PruningConfig  &config )
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

        logger_->info( "Prune evaluation: {} candidates, {} eligible",
                       result.candidates.size(),
                       result.EligibleCount() );
        return result;
    }

    PruneCandidateResult PruneEligibility::EvaluateCandidate( const std::string  &key,
                                                              const base::Buffer &value,
                                                              bool                allowlisted,
                                                              uint64_t            max_height,
                                                              uint64_t            window ) const
    {
        PruneCandidateResult candidate;
        candidate.tx_path = key;

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

        if ( !candidate.reasons.empty() )
        {
            // A per-output gate already failed; spend_height stays as the
            // diagnostic of whatever partial evidence existed.
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
                                        const PruningConfig &config )
        : globaldb_m( globaldb ),
          transaction_manager_m( transaction_manager ),
          utxo_manager_m( utxo_manager ),
          config_m( config )
    {
    }
} // namespace sgns
