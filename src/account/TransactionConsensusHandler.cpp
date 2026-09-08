/**
 * @file       TransactionConsensusHandler.cpp
 * @brief      Consensus-facing validation and finalization split out of TransactionManager.
 * @date       2026-09-08
 * @author     Eduardo Menges Mattje (emenges@gnus.ai)
 */
#include "account/TransactionConsensusHandler.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include "account/TransactionManager.hpp"
#include "account/InputValidators.hpp"
#include "account/MigrationAllowList.hpp"
#include "account/MigrationTransaction.hpp"
#include "account/UTXOMerkle.hpp"
#include "crypto/hasher.hpp"

namespace sgns
{
    namespace
    {
        using TransactionStatus = TransactionManager::TransactionStatus;

        using input_validator_constants::HASH256_BYTES;
        using input_validator_constants::SERIALIZED_UINT32_BYTES;
        using utxo_merkle::HashLeaf;
        using utxo_merkle::OutPointKey;
        using utxo_merkle::SerializeUTXOLeafPayload;
    }

    void TransactionConsensusHandler::OnProposalTimeoutCleanup( const std::string &tx_hash )
    {
        auto tx = owner_.GetTransactionByHash( tx_hash );
        if ( !tx )
        {
            // D-10: Entry not found — silently return, nothing to clean up.
            return;
        }

        const auto tracked = owner_.GetTrackedTxByHash( tx_hash );
        if ( !tracked || tracked->status != TransactionStatus::VERIFYING )
        {
            // D-10: Entry not in map OR entry status is not VERIFYING → silently skip.
            return;
        }

        if ( tx->GetSrcAddress() == owner_.account_m->GetAddress() )
        {
            logger_->info( "{}: Proposal timeout — transitioning local tx to UNCONFIRMED tx={}", __func__, tx_hash );
            (void) owner_.ChangeTransactionState( tx, TransactionStatus::UNCONFIRMED );
            return;
        }

        logger_->info( "{}: Proposal timeout — removing remote temp entry tx={}", __func__, tx_hash );
        (void) owner_.RemoveTrackedIfVerifying( tx_hash );
    }

    outcome::result<ConsensusManager::Check> TransactionConsensusHandler::OnConsensusCertificate(
        const std::string          &tx_hash,
        const ConsensusCertificate &certificate )
    {
        logger_->debug( "{}: Consensus certificate arrived for transaction {}", __func__, tx_hash );
        auto tx                             = owner_.GetTransactionByHash( tx_hash );
        bool reconstructed_from_certificate = false;
        if ( !tx )
        {
            // CONFLICT-01 / NONCE-01: Standalone validator without local transaction state.
            // Deserialize from the certificate's embedded proposal (Phase 1 transaction).
            auto nonce_subject_result = ConsensusManager::DecodeNonceSubject( certificate.proposal().subject() );
            if ( nonce_subject_result.has_error() )
            {
                logger_->warn( "{}: Certificate for hash {} has no decodable NonceSubject, "
                                "accepting",
                                __func__,
                                tx_hash );
                // METRICS-01: Certificate fallback deserialization failure
                metrics_cert_fallback_failure_.fetch_add( 1, std::memory_order_relaxed );
                return ConsensusManager::Check::Approve;
            }
            const auto &nonce_subject = nonce_subject_result.value();

            if ( nonce_subject.transaction().transaction_case() == EmbeddedTransaction::TRANSACTION_NOT_SET )
            {
                logger_->warn( "{}: Certificate for hash {} has no embedded transaction "
                                "(pre-Phase-1 certificate), accepting",
                                __func__,
                                tx_hash );
                return ConsensusManager::Check::Approve;
            }

            auto tx_result = TransactionManager::DeSerializeEmbeddedTransaction( nonce_subject.transaction() );
            if ( tx_result.has_error() )
            {
                logger_->warn( "{}: Failed to deserialize tx from certificate for hash {}, "
                                "accepting certificate",
                                __func__,
                                tx_hash );
                metrics_cert_fallback_failure_.fetch_add( 1, std::memory_order_relaxed );
                return ConsensusManager::Check::Approve;
            }
            tx = tx_result.value();

            // Verify hash binding — deserialized tx must match certificate's tx_hash
            if ( tx->GetHash() != tx_hash || !tx->CheckHash() )
            {
                logger_->warn( "{}: Certificate-embedded tx hash mismatch for {}, "
                                "accepting certificate without processing embedded data",
                                __func__,
                                tx_hash );
                metrics_cert_fallback_failure_.fetch_add( 1, std::memory_order_relaxed );
                return ConsensusManager::Check::Approve;
            }
            reconstructed_from_certificate = true;
        }

        auto conflicting_txs = owner_.GetConflictingTransactions( *tx );
        for ( const auto &conflict : conflicting_txs )
        {
            auto tracked = owner_.GetTrackedTxByHash( conflict->GetHash() );
            if ( tracked.has_value() && tracked->status == TransactionStatus::CONFIRMED )
            {
                logger_->critical( "{}: Conflicting transaction {} is already CONFIRMED while processing "
                                    "certificate winner {}; refusing contradictory finality",
                                    __func__,
                                    conflict->GetHash(),
                                    tx_hash );
                return ConsensusManager::Check::Stalled;
            }
        }

        for ( const auto &conflict : conflicting_txs )
        {
            auto tracked = owner_.GetTrackedTxByHash( conflict->GetHash() );
            if ( tracked.has_value() && tracked->status == TransactionStatus::FAILED )
            {
                continue;
            }
            logger_->warn( "{}: Failing transaction {} superseded by certified transaction {}",
                            __func__,
                            conflict->GetHash(),
                            tx_hash );
            if ( auto result = owner_.ChangeTransactionState( conflict, TransactionStatus::FAILED );
                 result.has_error() )
            {
                logger_->error( "{}: Failed to mark superseded transaction {} as FAILED: {}",
                                 __func__,
                                 conflict->GetHash(),
                                 result.error().message() );
                return outcome::failure( result.error() );
            }
        }

        if ( auto result = owner_.ChangeTransactionState( tx, TransactionStatus::CONFIRMED ); result.has_error() )
        {
            logger_->error( "{}: Failed to confirm certified transaction {}: {}",
                             __func__,
                             tx_hash,
                             result.error().message() );
            if ( reconstructed_from_certificate )
            {
                metrics_cert_fallback_failure_.fetch_add( 1, std::memory_order_relaxed );
            }
            return outcome::failure( result.error() );
        }

        if ( reconstructed_from_certificate )
        {
            metrics_cert_fallback_success_.fetch_add( 1, std::memory_order_relaxed );
            logger_->info( "{}: Standalone validator confirmed tx {} from certificate proposal_id={}",
                            __func__,
                            tx_hash,
                            certificate.proposal_id() );
        }
        else
        {
            logger_->debug( "{}: Transaction {} confirmed by consensus", __func__, tx_hash );
        }

        auto tx_hash_bin = base::Hash256::fromReadableString( tx_hash );
        if ( tx_hash_bin.has_error() )
        {
            logger_->error( "{}: Could not parse tx hash for checkpoint tx={}", __func__, tx_hash );
            return outcome::failure( tx_hash_bin.error() );
        }

        auto validator_registry = owner_.blockchain_->GetValidatorRegistry();
        if ( !validator_registry )
        {
            logger_->error( "{}: No validator registry, skipping checkpoint", __func__ );
            return outcome::failure( std::errc::no_such_device );
        }

        const uint64_t registry_epoch = validator_registry->GetRegistryEpoch();
        const auto     registry_cid   = validator_registry->GetRegistryCid();
        auto           registry_hash  = crypto::sha2_256( registry_cid.data(), registry_cid.size() );

        if ( auto checkpoint_res = owner_.account_m->GetUTXOManager().CreateCheckpoint( registry_epoch,
                                                                                        tx_hash_bin.value(),
                                                                                        registry_hash );
             checkpoint_res.has_error() )
        {
            logger_->error( "{}: Failed to create UTXO checkpoint tx={} epoch={} err={}",
                             __func__,
                             tx_hash,
                             registry_epoch,
                             checkpoint_res.error().message() );
        }
        logger_->debug( "{}: Transaction approved: {:.8}", __func__, tx_hash );
        return ConsensusManager::Check::Approve;
    }

    outcome::result<ConsensusManager::ValidationResult> TransactionConsensusHandler::HandleNonceConsensusSubject(
        const ConsensusManager::Subject &subject )
    {
        constexpr std::string_view FUNC{__func__};
        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        if ( nonce_subject.has_error() )
        {
            logger_->error( "{}: Received unexpected subject payload", __func__ );
            return nonce_subject.error();
        }

        const std::string tx_hash = nonce_subject.value().tx_hash();

        // DESER-01: Deserialize from EmbeddedTransaction oneof field
        if ( nonce_subject.value().transaction().transaction_case() == EmbeddedTransaction::TRANSACTION_NOT_SET )
        {
            logger_->error( "{}: No embedded transaction set, rejecting", __func__ );
            return ConsensusManager::ValidationResult::Reject();
        }

        auto tx_result = TransactionManager::DeSerializeEmbeddedTransaction( nonce_subject.value().transaction() );
        if ( tx_result.has_error() )
        {
            logger_->error( "{}: Failed to deserialize embedded tx for hash {}", __func__, tx_hash );
            return ConsensusManager::ValidationResult::Reject();
        }
        auto tx = tx_result.value();

        // Hash binding verification — cryptographic integrity gate (defense-in-depth)
        if ( tx->GetHash() != tx_hash )
        {
            logger_->error( "{}: Hash binding mismatch, tx->GetHash() != subject.tx_hash for {}", __func__, tx_hash );
            return ConsensusManager::ValidationResult::Reject();
        }

        // BIND-01: Commitment-tx binding cross-check
        if ( nonce_subject.value().has_utxo_commitment() )
        {
            if ( !tx->HasUTXOParameters() )
            {
                logger_->error( "{}: Subject has UTXO commitment but deserialized tx lacks "
                                 "UTXO parameters — possible malicious embedding, rejecting tx={}",
                                 __func__,
                                 tx_hash );
                return ConsensusManager::ValidationResult::Reject();
            }

            auto reconstructed = BuildUTXOTransitionCommitment( *tx );
            if ( !reconstructed.has_value() ||
                 reconstructed->consumed_outpoints_root() !=
                     nonce_subject.value().utxo_commitment().consumed_outpoints_root() ||
                 reconstructed->produced_outputs_root() !=
                     nonce_subject.value().utxo_commitment().produced_outputs_root() )
            {
                logger_->error( "{}: Commitment-tx binding mismatch — "
                                 "reconstructed commitment differs from subject claim for tx={}",
                                 __func__,
                                 tx_hash );
                return ConsensusManager::ValidationResult::Reject();
            }
        }

        // TRACK-01: Insert temporary tracking entry via ChangeTransactionState lifecycle
        uint64_t tracked_nonce  = tx->GetNonce();
        auto     tracked_entry  = owner_.GetTrackedTxByHash( tx_hash );
        if ( !tracked_entry )
        {
            // Go through the state machine so the manager owns every tracking-map write.
            auto create_result = owner_.ChangeTransactionState( tx, TransactionStatus::CREATED );
            if ( create_result.has_error() )
            {
                logger_->warn( "{}: CREATE failed for embedded tx {}, entry may exist via race: {}",
                                __func__,
                                tx_hash,
                                create_result.error().message() );
                // Re-read in case another thread inserted it
                auto raced_entry = owner_.GetTrackedTxByHash( tx_hash );
                if ( raced_entry )
                {
                    if ( raced_entry->status == TransactionStatus::FAILED )
                    {
                        return ConsensusManager::ValidationResult::Reject();
                    }
                    tracked_nonce  = raced_entry->cached_nonce;
                }
            }
            else
            {
                owner_.ChangeTransactionState( tx, TransactionStatus::VERIFYING );
            }
        }
        else if ( tracked_entry->status == TransactionStatus::FAILED )
        {
            logger_->debug( "{}: Transaction {} previously FAILED, rejecting", __func__, tx_hash );
            return ConsensusManager::ValidationResult::Reject();
        }
        else
        {
            // Entry already exists with higher-status — use its values for downstream checks
            tracked_nonce  = tracked_entry->cached_nonce;
        }

        auto reject_and_maybe_fail_local = [&]( const char *reason ) -> ConsensusManager::ValidationResult
        {
            // METRICS-01: Validation reject counter with reason logged at info level
            metrics_validation_reject_.fetch_add( 1, std::memory_order_relaxed );
            logger_->info( "{}: Proposal rejected for hash {}: {}", FUNC, tx_hash, reason );

            logger_->error( "{}: Rejecting nonce subject for hash {}: {}", FUNC, tx_hash, reason );

            // Ensure local outgoing invalid transactions don't stay in VERIFYING forever.
            if ( tx->GetSrcAddress() == owner_.account_m->GetAddress() )
            {
                auto current_out_status = owner_.GetOutgoingStatusByTxId( tx->GetHash() );
                if ( current_out_status != TransactionStatus::FAILED &&
                     current_out_status != TransactionStatus::CONFIRMED )
                {
                    if ( auto fail_result = owner_.ChangeTransactionState( tx, TransactionStatus::FAILED );
                         fail_result.has_error() )
                    {
                        logger_->error( "{}: Failed to mark rejected local tx as FAILED for hash {}: {}",
                                         FUNC,
                                         tx_hash,
                                         fail_result.error().message() );
                    }
                }
            }
            else
            {
                // TRACK-01 per D-02: Mark remote embedded temp entry as FAILED via ChangeTransactionState
                {
                    const auto remote_entry = owner_.GetTrackedTxByHash( tx_hash );
                    if ( remote_entry && remote_entry->status == TransactionStatus::VERIFYING )
                    {
                        owner_.ChangeTransactionState( tx, TransactionStatus::FAILED );
                        logger_->debug( "{}: Marked rejected embedded tx as FAILED for {}", FUNC, tx_hash );
                    }
                }
            }

            return ConsensusManager::ValidationResult::Reject();
        };

        if ( tracked_nonce != nonce_subject.value().nonce() )
        {
            logger_->error( "{}: Nonce mismatch for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "nonce mismatch" );
        }

        if ( !subject.account_id().empty() && tx->GetSrcAddress() != subject.account_id() )
        {
            logger_->error( "{}: Account mismatch for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "account mismatch" );
        }

        if ( owner_.HasConfirmedInputConflict( *tx ) )
        {
            logger_->error( "{}: Outpoint conflict against finalized transaction for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "input outpoint already finalized by another transaction" );
        }

        const auto witness_validation = ValidateWitnessForConsensus( subject, *tx );
        if ( witness_validation == WitnessValidationResult::INVALID )
        {
            logger_->error( "{}: Witness validation failed for hash {}", __func__, tx_hash );
            return reject_and_maybe_fail_local( "witness validation failed" );
        }

        if ( auto migration_tx = std::dynamic_pointer_cast<MigrationTransaction>( tx ) )
        {
            MigrationAllowList allow_list( owner_.globaldb_m->GetDataStore(), migration_tx->GetFromVersion() );
            auto eligibility_result = allow_list.IsEligible( migration_tx->GetSrcAddress(), migration_tx->GetAmount() );
            if ( eligibility_result.has_error() )
            {
                logger_->warn( "{}: Failed to evaluate local migration allowlist tx={} src={} err={}, pending",
                                __func__,
                                tx_hash,
                                migration_tx->GetSrcAddress(),
                                eligibility_result.error().message() );
                return ConsensusManager::ValidationResult::Pending();
            }
            if ( !eligibility_result.value() )
            {
                return reject_and_maybe_fail_local( "migration source address not locally eligible" );
            }
        }

        auto validate_result = ValidateTransactionForConsensus( *tx );

        if ( validate_result.check == ConsensusManager::Check::Pending )
        {
            return validate_result;
        }
        if ( validate_result.check != ConsensusManager::Check::Approve )
        {
            return reject_and_maybe_fail_local( "transaction validation failed" );
        }

        // METRICS-01: Validation approve counter
        metrics_validation_approve_.fetch_add( 1, std::memory_order_relaxed );
        return ConsensusManager::ValidationResult::Approve();
    }

    ConsensusManager::ValidationResult TransactionConsensusHandler::ValidateTransactionForConsensus(
        const GeniusTransaction &tx ) const
    {
        logger_->debug( "{}: Validating transaction", __func__ );
        if ( !CheckTransactionWellFormed( tx ) )
        {
            logger_->error( "{}: Well-formed check failed tx={}", __func__, tx.GetHash() );
            return ConsensusManager::ValidationResult::Reject();
        }
        if ( !owner_.CheckTransactionAuthorization( tx ) )
        {
            logger_->error( "{}: Authorization check failed tx={}", __func__, tx.GetHash() );
            return ConsensusManager::ValidationResult::Reject();
        }
        if ( !CheckTransactionTimestamp( tx ) )
        {
            logger_->error( "{}: Timestamp check failed tx={}", __func__, tx.GetHash() );
            return ConsensusManager::ValidationResult::Reject();
        }
        auto replay_result = EvaluateTransactionReplayProtection( tx );
        if ( replay_result.validation_.check != ConsensusManager::Check::Approve )
        {
            logger_->error( "{}: Replay protection failed tx={}", __func__, tx.GetHash() );
            return replay_result.validation_;
        }
        //TODO - Deal with checking the Mint
        if ( !CheckTransactionTypeRules( tx ) )
        {
            logger_->error( "{}: Type rules failed tx={}", __func__, tx.GetHash() );
            return ConsensusManager::ValidationResult::Reject();
        }

        logger_->debug( "{}: Transaction valid tx={}", __func__, tx.GetHash() );
        return ConsensusManager::ValidationResult::Approve();
    }

    bool TransactionConsensusHandler::CheckTransactionWellFormed( const GeniusTransaction &tx ) const
    {
        logger_->debug( "{}: Checking well-formed tx={}", __func__, tx.GetHash() );
        if ( tx.GetHash().empty() || !tx.CheckHash() )
        {
            logger_->error( "{}: Hash invalid tx={}", __func__, tx.GetHash() );
            return false;
        }

        if ( tx.GetSrcAddress().empty() )
        {
            logger_->error( "{}: Empty source address tx={}", __func__, tx.GetHash() );
            return false;
        }

        if ( tx.GetTimestamp() == 0 )
        {
            logger_->error( "{}: Missing timestamp tx={}", __func__, tx.GetHash() );
            return false;
        }

        if ( TransactionManager::transaction_parsers.find( tx.GetType() ) ==
             TransactionManager::transaction_parsers.end() )
        {
            logger_->error( "{}: Unknown tx type {}", __func__, tx.GetType() );
            return false;
        }

        logger_->debug( "{}: Well-formed ok tx={}", __func__, tx.GetHash() );
        return true;
    }

    bool TransactionConsensusHandler::CheckTransactionTimestamp( const GeniusTransaction &tx ) const
    {
        logger_->debug( "{}: Checking timestamp tx={}", __func__, tx.GetHash() );
        const auto ts = tx.GetTimestamp();
        if ( ts == 0 )
        {
            logger_->error( "{}: Missing timestamp tx={}", __func__, tx.GetHash() );
            return false;
        }

        const auto elapsed      = owner_.GetElapsedTime( ts );
        const auto tolerance_ms = static_cast<int64_t>( owner_.timestamp_tolerance_m.count() );
        const auto drift_ms     = elapsed >= 0 ? elapsed : -elapsed;

        if ( tolerance_ms > 0 && drift_ms > tolerance_ms )
        {
            logger_->error( "{}: Timestamp out of tolerance tx={} (elapsed: {} ms, tolerance: {} ms)",
                             __func__,
                             tx.GetHash(),
                             elapsed,
                             tolerance_ms );
            return false;
        }

        logger_->debug( "{}: Timestamp ok tx={}", __func__, tx.GetHash() );
        return true;
    }

    TransactionConsensusHandler::ReplayProtectionResult TransactionConsensusHandler::
        EvaluateTransactionReplayProtection( const GeniusTransaction &tx ) const
    {
        logger_->debug( "{}: Checking replay protection tx={}", __func__, tx.GetHash() );

        if ( tx.GetNonce() > 0 )
        {
            const auto previous_hash = tx.GetPreviousHash();
            if ( previous_hash.empty() )
            {
                logger_->error( "{}: Missing previous hash tx={}", __func__, tx.GetHash() );
                return { ConsensusManager::ValidationResult::Reject() };
            }
            if ( tx.GetSrcAddress() == owner_.account_m->GetAddress() )
            {
                const auto expected_previous_hash = owner_.GetOutgoingPreviousHash( tx.GetNonce() );
                if ( !expected_previous_hash.empty() && previous_hash != expected_previous_hash )
                {
                    logger_->error( "{}: Previous hash does not match local account head tx={}",
                                     __func__,
                                     tx.GetHash() );
                    return { ConsensusManager::ValidationResult::Reject() };
                }
            }
            auto previous_cert_result = owner_.blockchain_->GetCertificateBySubjectHash( previous_hash );
            if ( previous_cert_result.has_error() )
            {
                logger_->error( "{}: Missing previous certificate for hash {}", __func__, previous_hash );
                return { ConsensusManager::ValidationResult::Pending(
                    { ConsensusManager::PendingDependencyKey::Certificate( previous_hash ) } ) };
            }
            const auto &previous_subject = previous_cert_result.value().proposal().subject();
            auto        previous_nonce   = ConsensusManager::DecodeNonceSubject( previous_subject );
            if ( previous_nonce.has_error() )
            {
                return { ConsensusManager::ValidationResult::Reject() };
            }
            if ( previous_subject.account_id() != tx.GetSrcAddress() )
            {
                return { ConsensusManager::ValidationResult::Reject() };
            }
            if ( ( previous_nonce.value().nonce() + 1 ) != tx.GetNonce() )
            {
                return { ConsensusManager::ValidationResult::Reject() };
            }
        }

        auto nonce_result = owner_.account_m->GetPeerNonce( tx.GetSrcAddress() );
        if ( nonce_result.has_error() )
        {
            logger_->debug( "{}: No confirmed nonce for address {}", __func__, tx.GetSrcAddress() );
            return { ConsensusManager::ValidationResult::Approve() };
        }

        const auto confirmed_nonce = nonce_result.value();
        const auto tx_nonce        = tx.GetNonce();

        if ( tx_nonce <= confirmed_nonce )
        {
            logger_->error( "{}: Nonce too low tx={} nonce={} confirmed={}",
                             __func__,
                             tx.GetHash(),
                             tx_nonce,
                             confirmed_nonce );
            return { ConsensusManager::ValidationResult::Reject() };
        }

        if ( tx_nonce > confirmed_nonce + NONCE_WINDOW )
        {
            logger_->error( "{}: Nonce too high tx={} nonce={} confirmed={} window={}",
                             __func__,
                             tx.GetHash(),
                             tx_nonce,
                             confirmed_nonce,
                             NONCE_WINDOW );
            return { ConsensusManager::ValidationResult::Reject() };
        }

        if ( tx_nonce > confirmed_nonce + 1 )
        {
            for ( uint64_t n = confirmed_nonce + 1; n < tx_nonce; ++n )
            {
                auto tracked = owner_.GetTrackedTxByNonceAndAddress( n, tx.GetSrcAddress() );
                if ( !tracked.has_value() )
                {
                    logger_->error( "{}: Missing intermediate nonce {} for address {}",
                                     __func__,
                                     n,
                                     tx.GetSrcAddress() );
                    return { ConsensusManager::ValidationResult::Reject() };
                }
                if ( tracked->status == TransactionStatus::FAILED )
                {
                    logger_->error( "{}: Intermediate nonce {} invalid for address {}",
                                     __func__,
                                     n,
                                     tx.GetSrcAddress() );
                    return { ConsensusManager::ValidationResult::Reject() };
                }
            }
        }
        logger_->debug( "{}: Replay protection ok tx={}", __func__, tx.GetHash() );
        return { ConsensusManager::ValidationResult::Approve() };
    }

    bool TransactionConsensusHandler::CheckTransactionTypeRules( const GeniusTransaction &tx ) const
    {
        logger_->debug( "{}: Checking type rules", __func__ );
        if ( tx.HasUTXOParameters() )
        {
            auto params_opt = tx.GetUTXOParametersOpt();
            if ( !params_opt.has_value() )
            {
                logger_->error( "{}: Missing UTXO parameters for tx={}", __func__, tx.GetHash() );
                return false;
            }
            const auto &[_, validator] = owner_.SelectInputValidator( tx );
            return validator.ValidateUTXOParameters( params_opt.value(),
                                                     tx.GetSrcAddress(),
                                                     owner_.account_m->GetUTXOManager() );
        }

        return true;
    }

    TransactionConsensusHandler::WitnessValidationResult TransactionConsensusHandler::ValidateWitnessForConsensus(
        const ConsensusSubject  &subject,
        const GeniusTransaction &tx ) const
    {
        auto nonce_subject = ConsensusManager::DecodeNonceSubject( subject );
        logger_->debug( "{}: Start tx={} src={} nonce={} subject_nonce={} has_nonce={} "
                         "has_utxo_params={} has_commitment={} has_witness={}",
                         __func__,
                         tx.GetHash(),
                         tx.GetSrcAddress(),
                         tx.GetNonce(),
                         nonce_subject.has_value() ? nonce_subject.value().nonce() : 0,
                         nonce_subject.has_value(),
                         tx.HasUTXOParameters(),
                         nonce_subject.has_value() && nonce_subject.value().has_utxo_commitment(),
                         nonce_subject.has_value() && nonce_subject.value().has_utxo_witness() );

        if ( nonce_subject.has_error() )
        {
            logger_->debug( "{}: Subject has no nonce payload, accepting tx={}", __func__, tx.GetHash() );
            return WitnessValidationResult::VALID;
        }

        const auto [chain_id, validator] = owner_.SelectInputValidator( tx );

        if ( !tx.HasUTXOParameters() )
        {
            // BIND-01: Hardened early-return — if subject claims UTXO commitment
            // but tx lacks UTXO params, this is Pitfall 5 bypass → reject as INVALID
            if ( nonce_subject.has_value() && nonce_subject.value().has_utxo_commitment() )
            {
                logger_->error( "{}: Subject has UTXO commitment "
                                 "but tx has no UTXO params — rejecting tx={}",
                                 __func__,
                                 tx.GetHash() );
                return WitnessValidationResult::INVALID;
            }
            logger_->debug( "{}: Tx has no UTXO params, accepting tx={}", __func__, tx.GetHash() );
            return WitnessValidationResult::VALID;
        }

        if ( !nonce_subject.value().has_utxo_commitment() )
        {
            logger_->error( "{}: Missing UTXO commitment tx={}", __func__, tx.GetHash() );
            return WitnessValidationResult::INVALID;
        }

        const auto &commitment = nonce_subject.value().utxo_commitment();
        if ( commitment.consumed_outpoints_root().size() != base::Hash256::size() ||
             commitment.produced_outputs_root().size() != base::Hash256::size() )
        {
            logger_->error( "{}: Invalid commitment root sizes tx={} consumed_size={} "
                             "produced_size={} expected={}",
                             __func__,
                             tx.GetHash(),
                             commitment.consumed_outpoints_root().size(),
                             commitment.produced_outputs_root().size(),
                             base::Hash256::size() );
            return WitnessValidationResult::INVALID;
        }
        if ( validator.RequiresConsensusUTXOData() && !nonce_subject.value().has_utxo_witness() )
        {
            logger_->error( "{}: Missing required UTXO witness tx={} chain_id={} validator_requires_witness={}",
                             __func__,
                             tx.GetHash(),
                             chain_id,
                             validator.RequiresConsensusUTXOData() );
            return WitnessValidationResult::INVALID;
        }

        auto params_opt = tx.GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            logger_->error( "{}: Missing UTXO params payload tx={}", __func__, tx.GetHash() );
            return WitnessValidationResult::INVALID;
        }
        const bool witness_ok = validator.ValidateWitness( subject, tx, params_opt.value(), *owner_.blockchain_ );
        logger_->debug( "{}: Validator witness result tx={} chain_id={} result={}",
                         __func__,
                         tx.GetHash(),
                         chain_id,
                         witness_ok );
        return witness_ok ? WitnessValidationResult::VALID : WitnessValidationResult::INVALID;
    }

    std::optional<UTXOTransitionCommitment> TransactionConsensusHandler::BuildUTXOTransitionCommitment(
        const GeniusTransaction &tx ) const
    {
        if ( !tx.HasUTXOParameters() )
        {
            return std::nullopt;
        }
        auto params_opt = tx.GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            return std::nullopt;
        }
        const auto &inputs = params_opt->first;
        if ( inputs.empty() )
        {
            return std::nullopt;
        }
        UTXOTransitionCommitment          commitment;
        std::vector<std::vector<uint8_t>> consumed_payloads;
        consumed_payloads.reserve( inputs.size() );
        for ( const auto &input : inputs )
        {
            auto *committed_input = commitment.add_consumed_outpoints();
            committed_input->set_tx_id_hash( input.txid_hash_.data(), input.txid_hash_.size() );
            committed_input->set_output_index( input.output_idx_ );

            std::vector<uint8_t> leaf_payload;
            leaf_payload.reserve( HASH256_BYTES + SERIALIZED_UINT32_BYTES );
            leaf_payload.insert( leaf_payload.end(), input.txid_hash_.begin(), input.txid_hash_.end() );
            utxo_merkle::AppendUInt32BE( leaf_payload, input.output_idx_ );
            consumed_payloads.push_back( std::move( leaf_payload ) );
        }
        const auto consumed_outpoints_root = utxo_merkle::ComputeMerkleRootFromPayloads(
            std::move( consumed_payloads ) );

        const auto produced_outputs = tx.GetProducedUTXOs();
        if ( !produced_outputs )
        {
            logger_->warn( "{}: Could not extract produced outputs for tx={}", __func__, tx.GetHash() );
            return std::nullopt;
        }
        for ( const auto &produced_output : *produced_outputs )
        {
            const auto produced_tx_hash = produced_output.GetTxID();
            auto      *committed_output = commitment.add_produced_outputs();
            committed_output->set_tx_id_hash( produced_tx_hash.data(), produced_tx_hash.size() );
            committed_output->set_output_index( produced_output.GetOutputIdx() );
            committed_output->set_owner_address( produced_output.GetOwnerAddress() );
            const auto token_bytes = produced_output.GetTokenID().bytes();
            committed_output->set_token_id( token_bytes.data(), token_bytes.size() );
            committed_output->set_amount( produced_output.GetAmount() );
        }

        const auto produced_outputs_root = utxo_merkle::ComputeMerkleRootFromUTXOs( *produced_outputs );
        commitment.set_consumed_outpoints_root( consumed_outpoints_root.data(), consumed_outpoints_root.size() );
        commitment.set_produced_outputs_root( produced_outputs_root.data(), produced_outputs_root.size() );
        return commitment;
    }

    std::optional<UTXOWitness> TransactionConsensusHandler::BuildUTXOWitness( const GeniusTransaction &tx ) const
    {
        if ( !tx.HasUTXOParameters() )
        {
            logger_->error( "{}: No UTXO parameters for transaction {}", __func__, tx.GetHash() );
            return std::nullopt;
        }

        auto params_opt = tx.GetUTXOParametersOpt();
        if ( !params_opt.has_value() )
        {
            logger_->error( "{}: Unexpected missing UTXO parameters for transaction {}", __func__, tx.GetHash() );
            return std::nullopt;
        }
        const auto &inputs = params_opt->first;

        struct SnapshotLeaf
        {
            std::string          outpoint_key;
            std::vector<uint8_t> payload;
        };

        std::vector<SnapshotLeaf> leaves;
        leaves.reserve( inputs.size() );
        for ( const auto &input : inputs )
        {
            auto utxo = owner_.account_m->GetUTXOManager().GetUnconsumedUTXO( input.txid_hash_, input.output_idx_ );
            if ( !utxo.has_value() )
            {
                logger_->error( "{}: Missing input UTXO for transaction {} and key {}",
                                 __func__,
                                 tx.GetHash(),
                                 OutPointKey( input.txid_hash_, input.output_idx_ ) );
                return std::nullopt;
            }
            leaves.push_back(
                { OutPointKey( utxo->GetTxID(), utxo->GetOutputIdx() ), SerializeUTXOLeafPayload( utxo.value() ) } );
        }

        std::sort( leaves.begin(),
                   leaves.end(),
                   []( const SnapshotLeaf &a, const SnapshotLeaf &b ) { return a.payload < b.payload; } );

        std::unordered_map<std::string, size_t> outpoint_to_index;
        outpoint_to_index.reserve( leaves.size() );
        std::vector<base::Hash256> level_hashes;
        level_hashes.reserve( leaves.size() );
        for ( size_t i = 0; i < leaves.size(); ++i )
        {
            outpoint_to_index.emplace( leaves[i].outpoint_key, i );
            level_hashes.push_back( HashLeaf( leaves[i].payload ) );
        }

        UTXOWitness witness;
        for ( const auto &input : inputs )
        {
            const auto key = OutPointKey( input.txid_hash_, input.output_idx_ );
            auto       it  = outpoint_to_index.find( key );
            if ( it == outpoint_to_index.end() )
            {
                logger_->error( "{}: Missing outpoint for transaction {} and key {}", __func__, tx.GetHash(), key );
                return std::nullopt;
            }

            const size_t leaf_index = it->second;
            auto        *proof      = witness.add_consumed_inputs();
            proof->set_tx_id_hash( input.txid_hash_.data(), input.txid_hash_.size() );
            proof->set_output_index( input.output_idx_ );
            proof->set_leaf_payload( leaves[leaf_index].payload.data(), leaves[leaf_index].payload.size() );

            utxo_merkle::AppendMerkleBranch( level_hashes,
                                             leaf_index,
                                             [proof]( const base::Hash256 &sibling, bool is_left_sibling )
                                             {
                                                 auto *step = proof->add_branch();
                                                 step->set_sibling_hash( sibling.data(), sibling.size() );
                                                 step->set_is_left_sibling( is_left_sibling );
                                             } );

            auto producer_tx = owner_.GetTransactionByHash( input.txid_hash_.toReadableString() );
            if ( !producer_tx )
            {
                logger_->error( "{}: Missing producer transaction for input {}",
                                 __func__,
                                 input.txid_hash_.toReadableString() );
                return std::nullopt;
            }
            const auto produced_outputs = producer_tx->GetProducedUTXOs();
            if ( !produced_outputs )
            {
                logger_->error( "{}: Could not extract produced outputs for producer transaction {}",
                                 __func__,
                                 producer_tx->GetHash() );
                return std::nullopt;
            }

            std::vector<SnapshotLeaf> produced_leaves;
            produced_leaves.reserve( produced_outputs->size() );
            for ( const auto &output_utxo : *produced_outputs )
            {
                produced_leaves.push_back( { OutPointKey( output_utxo.GetTxID(), output_utxo.GetOutputIdx() ),
                                             SerializeUTXOLeafPayload( output_utxo ) } );
            }
            std::sort( produced_leaves.begin(),
                       produced_leaves.end(),
                       []( const SnapshotLeaf &a, const SnapshotLeaf &b ) { return a.payload < b.payload; } );

            std::unordered_map<std::string, size_t> produced_outpoint_to_index;
            produced_outpoint_to_index.reserve( produced_leaves.size() );
            std::vector<base::Hash256> produced_level_hashes;
            produced_level_hashes.reserve( produced_leaves.size() );
            for ( size_t i = 0; i < produced_leaves.size(); ++i )
            {
                produced_outpoint_to_index.emplace( produced_leaves[i].outpoint_key, i );
                produced_level_hashes.push_back( HashLeaf( produced_leaves[i].payload ) );
            }

            auto produced_it = produced_outpoint_to_index.find( key );
            if ( produced_it == produced_outpoint_to_index.end() )
            {
                logger_->error( "{}: Missing produced UTXO for transaction {} and key {}",
                                 __func__,
                                 tx.GetHash(),
                                 key );
                return std::nullopt;
            }
            if ( produced_leaves[produced_it->second].payload != leaves[leaf_index].payload )
            {
                logger_->error( "{}: Payload mismatch for produced UTXO for transaction {} and key {}",
                                 __func__,
                                 tx.GetHash(),
                                 key );
                return std::nullopt;
            }

            utxo_merkle::AppendMerkleBranch( produced_level_hashes,
                                             produced_it->second,
                                             [proof]( const base::Hash256 &sibling, bool is_left_sibling )
                                             {
                                                 auto *step = proof->add_produced_branch();
                                                 step->set_sibling_hash( sibling.data(), sibling.size() );
                                                 step->set_is_left_sibling( is_left_sibling );
                                             } );
        }

        return witness;
    }

}
