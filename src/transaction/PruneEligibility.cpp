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
