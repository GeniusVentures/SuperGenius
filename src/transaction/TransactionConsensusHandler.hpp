/**
 * @file       TransactionConsensusHandler.hpp
 * @brief      Consensus-facing half of TransactionManager: subject validation, certificate
 *             finalization, proposal-timeout cleanup and UTXO commitment/witness payloads.
 * @date       2026-09-08
 * @author     Eduardo Menges Mattje (emenges@gnus.ai)
 */
#ifndef SGNS_ACCOUNT_TRANSACTION_CONSENSUS_HANDLER_HPP
#define SGNS_ACCOUNT_TRANSACTION_CONSENSUS_HANDLER_HPP

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include "base/logger.hpp"
#include "blockchain/Consensus.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    class GeniusTransaction;
    class TransactionManager;

    /**
     * @brief Validates and finalizes transactions on behalf of consensus.
     *
     * Owned by the TransactionManager that constructs it, whose lifetime strictly contains this
     * object's. Tracking state stays in the manager and is reached through its accessors, so this
     * class holds no transaction map and takes no lock of its own.
     */
    class TransactionConsensusHandler
    {
    public:
        enum class WitnessValidationResult : uint8_t
        {
            VALID,
            DRIFT,
            INVALID
        };

        /// Snapshot of the consensus counters, for the manager's shutdown log.
        struct Metrics
        {
            uint64_t cert_fallback_success_;
            uint64_t cert_fallback_failure_;
            uint64_t validation_approve_;
            uint64_t validation_reject_;
        };

        /**
         * @param[in] owner  Manager owning this handler; must outlive it.
         * @param[in] logger Shared with the owner so log lines stay attributed to the node.
         */
        TransactionConsensusHandler( TransactionManager &owner, base::Logger logger ) :
            owner_( owner ), logger_( std::move( logger ) )
        {
        }

        /**
         * @brief Finalizes a transaction once consensus produced a certificate for it,
         *        reconstructing the transaction from the certificate when it is not tracked locally.
         */
        outcome::result<ConsensusManager::Check> OnConsensusCertificate( const std::string          &tx_hash,
                                                                         const ConsensusCertificate &certificate );

        /**
         * @brief Validates a nonce consensus subject: well-formedness, authorization, timestamp,
         *        replay protection, type rules and the UTXO witness.
         */
        outcome::result<ConsensusManager::ValidationResult> HandleNonceConsensusSubject(
            const ConsensusManager::Subject &subject );

        /**
         * @brief Handles proposal timeout cleanup for VERIFYING tracking entries.
         *        Local outgoing entries become UNCONFIRMED; remote temporary entries are removed.
         *        CONFIRMED entries and missing entries are left untouched.
         */
        void OnProposalTimeoutCleanup( const std::string &tx_hash );

        /// @brief Builds the consumed/produced Merkle commitment a nonce subject carries.
        std::optional<UTXOTransitionCommitment> BuildUTXOTransitionCommitment( const GeniusTransaction &tx ) const;

        /// @brief Builds the Merkle inclusion proofs backing a transition commitment.
        std::optional<UTXOWitness> BuildUTXOWitness( const GeniusTransaction &tx ) const;

        [[nodiscard]] Metrics SnapshotMetrics() const
        {
            return { metrics_cert_fallback_success_.load(),
                     metrics_cert_fallback_failure_.load(),
                     metrics_validation_approve_.load(),
                     metrics_validation_reject_.load() };
        }

    private:
        struct ReplayProtectionResult
        {
            ConsensusManager::ValidationResult validation_ = ConsensusManager::ValidationResult::Approve();
        };

        ConsensusManager::ValidationResult ValidateTransactionForConsensus( const GeniusTransaction &tx ) const;
        bool                               CheckTransactionWellFormed( const GeniusTransaction &tx ) const;
        bool                               CheckTransactionTimestamp( const GeniusTransaction &tx ) const;
        ReplayProtectionResult             EvaluateTransactionReplayProtection( const GeniusTransaction &tx ) const;
        bool                               CheckTransactionTypeRules( const GeniusTransaction &tx ) const;
        WitnessValidationResult            ValidateWitnessForConsensus( const ConsensusSubject  &subject,
                                                                        const GeniusTransaction &tx ) const;

        TransactionManager &owner_;
        base::Logger        logger_;

        /// Number of nonces ahead of the confirmed one that a subject may claim.
        static constexpr uint64_t NONCE_WINDOW = 5;

        std::atomic<uint64_t> metrics_cert_fallback_success_{ 0 };
        std::atomic<uint64_t> metrics_cert_fallback_failure_{ 0 };
        std::atomic<uint64_t> metrics_validation_approve_{ 0 };
        std::atomic<uint64_t> metrics_validation_reject_{ 0 };
    };
}

#endif // SGNS_ACCOUNT_TRANSACTION_CONSENSUS_HANDLER_HPP
