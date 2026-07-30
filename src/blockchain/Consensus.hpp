/**
 * @file       Consensus.hpp
 * @brief      Consensus proposal/vote/certificate helpers.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_CONSENSUS_HPP
#define SGNS_CONSENSUS_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <limits>

#include "blockchain/ValidatorRegistry.hpp"
#include "blockchain/ConsensusStateStore.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "crdt/globaldb/crdt_work_journal.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/proto/delta.pb.h"
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include "outcome/outcome.hpp"

namespace sgns
{
    static constexpr std::string_view NONCE_SUBJECT_TYPE          = "sgns.nonce.v1";
    static constexpr std::string_view TASK_RESULT_SUBJECT_TYPE    = "sgns.task_result.v1";
    static constexpr std::string_view REGISTRY_BATCH_SUBJECT_TYPE = "sgns.registry_batch.v1";

    /** Fixed consensus settings resolved before the manager starts any observable work. */
    struct ConsensusConfig
    {
        static constexpr std::chrono::milliseconds DEFAULT_VOTE_SELECTION_WINDOW{ 500 };
        /** Operator values above this bound are rejected to prevent unbounded selection delay. */
        static constexpr std::chrono::milliseconds MAX_VOTE_SELECTION_WINDOW{ 60'000 };

        explicit constexpr ConsensusConfig(
            std::chrono::milliseconds selection_window = DEFAULT_VOTE_SELECTION_WINDOW ) :
            vote_selection_window( selection_window )
        {
        }

        const std::chrono::milliseconds vote_selection_window;
    };

    /**
     * @brief      Implements Consensus with weighted voting.
     *
     *      This class implements a consensus algorithm using pubsub messages.
     * A subject needs to be created and with it a proposal as well. The proposal gets sent to the network
     * and gets voted by peers who receive it. This class has hooks to be filled by the caller to register methods
     * to handle subject and proposal. The idea is to leave out the validation of specific data (transaction, job result and etc)
     * for whomever creates the subject. It relies on @ref ValidatorRegistry class to get the voters and their weights.
     * Once consensus is reached a round scheme determines who amongst the validators will create the certificate which is
     * the finality of the subject. The certificate also enabled registry updates to register new validators according to peer who voted
     * correctly or penalize people who votes incorrectly.
     */
    class ConsensusManager : public std::enable_shared_from_this<ConsensusManager>
    {
    public:
        using Proposal    = ConsensusProposal;    ///< Alias for Consensus Proposal protobuf type
        using Vote        = ConsensusVote;        ///< Alias for Consensus Vote protobuf type
        using VoteBundle  = ConsensusVoteBundle;  ///< Alias for Consensus Vote Bundle protobuf type
        using Certificate = ConsensusCertificate; ///< Alias for Consensus Certificate protobuf type
        using Subject     = ConsensusSubject;     ///< Alias for Consensus Subject protobuf type

        /// @brief      Alias for a signer method type
        using Signer = std::function<outcome::result<std::vector<uint8_t>>( std::vector<uint8_t> payload )>;

        /**
         * @brief      Callback invoked during CreateVote to populate slot_N_hash
         *             fields before signing (Phase 6, D-01).
         * @details    Set once at init time by GeniusNode (via Blockchain::SetSlotHashPopulator)
         *             to bridge TransactionManager::GetPublicChainInputValidator
         *             into ConsensusManager's vote-creation path. When unset,
         *             CreateVote behaves exactly as before (backward compatible).
         *             The callback is invoked AFTER proposal_id/voter_id/approve/
         *             timestamp are set but BEFORE VoteSigningBytes, so the
         *             resulting signature commits to the slot hashes (T-06-01).
         */
        using SlotHashPopulator = std::function<void( ConsensusVote &vote )>;

        /**
         * @brief Creates a ConsensusManager instance.
         * @param[in] registry Validator registry used for voter set and weights.
         * @param[in] db GlobalDB instance used for persistence and CRDT interactions.
         * @param[in] pubsub PubSub transport for consensus message propagation.
         * @param[in] signer Local signing callback for outbound signed objects.
         * @param[in] address Local validator/account identifier.
         * @param[in] consensus_topic Optional topic override used to derive consensus channels.
         * @return Shared pointer to a new manager instance.
         */
        static std::shared_ptr<ConsensusManager> New( std::shared_ptr<ValidatorRegistry>         registry,
                                                      std::shared_ptr<crdt::GlobalDB>            db,
                                                      std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                      Signer                                     signer,
                                                      std::string                                address,
                                                      std::string                                consensus_topic = "",
                                                      ConsensusConfig                            config = ConsensusConfig{} );
        /**
         * @brief      Destroys the Consensus Manager object
         */
        ~ConsensusManager();

        /**
         * @brief      Close and cleanup members of the Consensus Manager
         */
        void Close();

        /**
         * @brief      Object checking values
         */
        enum class Check
        {
            Approve, ///< Object is approved
            Reject,  ///< Object is rejected
            Pending, ///< Object evaluation is pending
            Stalled  ///< Object evaluation is stalled
        };

        /**
         * @brief Typed failures for the v2 authoritative certificate store.
         */
        enum class CertificateStoreError
        {
            NotFound = 0,
            IntegrityError,
            StorageError,
            Conflict,
            InvalidInput,
            InvalidCertificate,
        };

        enum class DeliverySource : uint8_t
        {
            Local,
            PubSub,
            CRDT,
            Recovery,
        };

        enum class FinalizeResult : uint8_t
        {
            Applied,
            PendingApplication,
            AlreadyFinalized,
            Conflict,
            Invalid,
            StorageFailure,
        };

        /**
         * @brief Local-only dependency key for deferred subject validation.
         */
        struct PendingDependencyKey
        {
            enum class Type
            {
                Certificate, ///< Waiting for a certificate by subject/transaction hash.
            };

            Type        type{ Type::Certificate };
            std::string value;

            bool operator==( const PendingDependencyKey &other ) const
            {
                return type == other.type && value == other.value;
            }

            static PendingDependencyKey Certificate( std::string subject_hash )
            {
                return PendingDependencyKey{ Type::Certificate, std::move( subject_hash ) };
            }
        };

        /**
         * @brief Hash functor for PendingDependencyKey unordered containers.
         */
        struct PendingDependencyKeyHash
        {
            std::size_t operator()( const PendingDependencyKey &key ) const
            {
                const auto type_hash  = std::hash<int>{}( static_cast<int>( key.type ) );
                const auto value_hash = std::hash<std::string>{}( key.value );
                return type_hash ^ ( value_hash + 0x9e3779b97f4a7c15ULL + ( type_hash << 6 ) + ( type_hash >> 2 ) );
            }
        };

        /**
         * @brief Local structured validation result for subject handlers.
         *
         * Pending metadata is local bookkeeping only. It is not serialized,
         * broadcast, or counted toward quorum.
         */
        struct ValidationResult
        {
            Check                                    check{ Check::Reject };
            std::vector<PendingDependencyKey>        dependencies;
            std::optional<std::chrono::milliseconds> retry_after;

            ValidationResult() = default;

            ValidationResult( Check result ) : check( result )
            {
            }

            static ValidationResult Approve()
            {
                return ValidationResult{ Check::Approve };
            }

            static ValidationResult Reject()
            {
                return ValidationResult{ Check::Reject };
            }

            static ValidationResult Stalled()
            {
                return ValidationResult{ Check::Stalled };
            }

            static ValidationResult Pending( std::vector<PendingDependencyKey>        deps  = {},
                                             std::optional<std::chrono::milliseconds> retry = std::nullopt )
            {
                ValidationResult result{ Check::Pending };
                result.dependencies = std::move( deps );
                result.retry_after  = retry;
                return result;
            }
        };

        /**
         * @brief Local pending proposal lifecycle limits.
         */
        struct PendingLifecycleConfig
        {
            std::size_t                            max_pending_proposals         = 1024;
            std::size_t                            max_pending_per_proposer      = 64;
            std::size_t                            max_retained_pending_bytes    = 64ULL * 1024ULL * 1024ULL;
            std::chrono::milliseconds              pending_ttl                   = std::chrono::minutes( 3 );
            std::chrono::milliseconds              min_dependency_retry_interval = std::chrono::seconds( 1 );
            std::vector<std::chrono::milliseconds> scheduled_retry_delays        = { std::chrono::seconds( 1 ),
                                                                                     std::chrono::seconds( 2 ),
                                                                                     std::chrono::seconds( 5 ),
                                                                                     std::chrono::seconds( 10 ) };
        };

        /// @brief      Alias for a subject handler method type
        using SubjectHandler = std::function<outcome::result<ValidationResult>( const Subject &subject )>;
        /// @brief      Alias for a certificate handler method type
        using CertificateSubjectHandler =
            std::function<outcome::result<Check>( const std::string &subject_hash, const Certificate &certificate )>;
        enum class ApplicationDisposition : uint8_t
        {
            Applied,
            AlreadyApplied,
            Retryable,
            Irreconcilable,
        };
        /** Immutable exact-store authority handed to winning mint application. */
        struct FinalizedReservationApplicationHandle
        {
            FinalizedReservationApplicationHandle(
                std::shared_ptr<ConsensusStateStore> store_value,
                ConsensusStateStore::FinalizedReservationIdentity identity_value )
                : store( std::move( store_value ) ), identity( std::move( identity_value ) )
            {
            }

            const std::shared_ptr<ConsensusStateStore> store;
            const ConsensusStateStore::FinalizedReservationIdentity identity;
        };
        using CertificateApplicationHandler =
            std::function<outcome::result<ApplicationDisposition>( const std::string &subject_hash,
                                                                    const Certificate &certificate,
                                                                    FinalizedReservationApplicationHandle handle )>;
        /// @brief      Alias for a proposal cleanup handler method type
        ///             Callback invoked when a proposal slot is cleaned up due to timeout.
        ///             Receives the transaction hash so the handler can clean up associated tracking entries.
        using ProposalCleanupHandler = std::function<void( const std::string &tx_hash )>;
        /// @brief      Alias for a fail-closed canonical slot key handler.
        ///             Takes the raw subject, called from GetSlotKey by subject type hash.
        using SlotKeyHandler = std::function<outcome::result<std::string>( const Subject &subject )>;
        /// @brief Optional canonical resource descriptor returned after semantic approval.
        using ResourceAdmissionHandler =
            std::function<outcome::result<std::optional<ConsensusStateStore::BurnOutpoint>>(
                const Subject &subject, const std::string &slot_id )>;

        /**
         * @brief      Quorum tally structure
         */
        struct QuorumTally
        {
            uint64_t total_weight    = 0;     ///< The total maximum weight of the quorum
            uint64_t approved_weight = 0;     ///< The weight which was already approved
            bool     has_quorum      = false; ///< Flag indicating if quorum was reached
            // Phase 6 (D-06): populated only for bridge-mint subjects; zero for
            // non-bridge subjects (observability -- the slot-tally result).
            uint64_t qualified_sum  = 0;      ///< Slot-weighted qualified contribution.
            uint64_t slot_threshold = 0;      ///< total_voting_reputation * 0.75 (D-06).
        };

        /**
         * @brief Registers a subject validation/handling callback by canonical subject type string.
         * @param[in] subject_type Canonical subject type, e.g. "gnus.bridge_event.v1".
         * @param[in] handler Callback invoked for matching subject type hash.
         * @return `true` when registered, `false` when input is invalid.
         */
        bool RegisterSubjectHandler( std::string_view subject_type, SubjectHandler handler );
        /**
         * @brief Unregisters a subject handler by canonical subject type string.
         * @param[in] subject_type Canonical subject type associated with the handler.
         */
        void UnregisterSubjectHandler( std::string_view subject_type );
        /**
         * @brief Registers a certificate handling callback by canonical subject type string.
         * @param[in] subject_type Canonical subject type associated with certificates.
         * @param[in] handler Callback invoked for matching certificate subjects.
         * @return `true` when registered, `false` when input is invalid.
         */
        bool RegisterCertificateHandler( std::string_view subject_type, CertificateSubjectHandler handler );
        bool RegisterCertificateApplicationHandler( std::string_view subject_type,
                                                    CertificateApplicationHandler handler );
        /**
         * @brief Unregisters a certificate handler by canonical subject type string.
         * @param[in] subject_type Canonical subject type associated with the handler.
         */
        void UnregisterCertificateHandler( std::string_view subject_type );
        /**
         * @brief Registers a proposal cleanup callback by canonical subject type string.
         * @param[in] subject_type Canonical subject type to handle.
         * @param[in] handler Callback invoked when a proposal is cleaned up due to timeout.
         * @return `true` on successful registration.
         */
        bool RegisterProposalCleanupHandler( std::string_view subject_type, ProposalCleanupHandler handler );
        /**
         * @brief Unregisters all proposal cleanup handlers for a canonical subject type string.
         * @param[in] subject_type Canonical subject type to remove.
         */
        void UnregisterProposalCleanupHandler( std::string_view subject_type );

        /** Registers one post-validation resource descriptor handler. */
        bool RegisterResourceAdmissionHandler( std::string_view subject_type, ResourceAdmissionHandler handler );
        /** Removes the post-validation resource descriptor handler. */
        void UnregisterResourceAdmissionHandler( std::string_view subject_type );

        /** Strict node-local bridge reservation lookup used by relayer admission. */
        outcome::result<std::optional<ConsensusStateStore::BurnReservationRecord>> GetBurnReservation(
            const ConsensusStateStore::BurnOutpoint &outpoint ) const;

        /**
         * @brief Overrides local pending lifecycle limits for deterministic tests/configuration.
         * @param[in] config Pending lifecycle configuration.
         */
        void SetPendingLifecycleConfig( PendingLifecycleConfig config );

        /** RegisterSlotKeyHandler also changed to match subject type pattern: */
        /**
         * @brief Registers a slot key handler for a canonical subject type.
         * @param[in] subject_type Canonical subject type (e.g. "sgns.nonce.v1").
         * @param[in] handler Callback that produces a slot key from the raw subject.
         */
        static bool RegisterSlotKeyHandler( std::string_view subject_type, SlotKeyHandler handler );

        /** Installs the built-in nonce/mint resolver when the type is unowned. */
        static bool EnsureBuiltinSlotKeyHandlers();

        /**
         * @brief Unregisters the slot key handler for a canonical subject type.
         * @param[in] subject_type Canonical subject type to remove.
         */
        static void UnregisterSlotKeyHandler( std::string_view subject_type );

        /**
         * @brief Publishes a consensus envelope to pubsub.
         * @param[in] message Consensus message envelope.
         * @return outcome::success on publish success, or an error otherwise.
         */
        outcome::result<void> Publish( const ConsensusMessage &message );

        /**
         * @brief Builds and signs a proposal using the manager signer.
         * @param[in] subject Consensus subject to propose.
         * @param[in] proposer_id Validator identifier of the proposer.
         * @param[in] registry_cid CID of the validator registry snapshot.
         * @param[in] registry_epoch Epoch of the validator registry snapshot.
         * @return Signed proposal on success, otherwise an error.
         */
        outcome::result<Proposal> CreateProposal( const Subject     &subject,
                                                  const std::string &proposer_id,
                                                  const std::string &registry_cid,
                                                  uint64_t           registry_epoch );
        /**
         * @brief Builds and signs a proposal using an explicit signer.
         * @param[in] subject Consensus subject to propose.
         * @param[in] proposer_id Validator identifier of the proposer.
         * @param[in] registry_cid CID of the validator registry snapshot.
         * @param[in] registry_epoch Epoch of the validator registry snapshot.
         * @param[in] sign Signing callback.
         * @return Signed proposal on success, otherwise an error.
         */
        static outcome::result<Proposal> CreateProposal( const Subject     &subject,
                                                         const std::string &proposer_id,
                                                         const std::string &registry_cid,
                                                         uint64_t           registry_epoch,
                                                         Signer             sign );

        /**
         * @brief Builds and signs a vote for a proposal.
         * @param[in] proposal_id Proposal identifier being voted on.
         * @param[in] voter_id Validator identifier of the voter.
         * @param[in] approve `true` for approval vote, `false` for rejection vote.
         * @param[in] sign Signing callback.
         * @return Signed vote on success, otherwise an error.
         */
        outcome::result<Vote> CreateVote( const std::string &proposal_id,
                                          const std::string &voter_id,
                                          bool               approve,
                                          Signer             sign );

        /**
         * @brief      Injects the slot-hash populator used by CreateVote (Phase 6, D-01).
         * @param[in]  populator  Callback that fills slot_N_hash fields on a vote.
         * @details    Set by GeniusNode during blockchain initialization. When the
         *             callback is unset (default), CreateVote skips slot population.
         */
        void SetSlotHashPopulator( SlotHashPopulator populator )
        {
            slot_hash_populator_ = std::move( populator );
        }

        /**
         * @brief Builds and signs an aggregated vote bundle.
         * @param[in] proposal_id Proposal identifier associated with the votes.
         * @param[in] aggregator_id Validator identifier of the aggregator.
         * @param[in] votes Votes to aggregate in the bundle.
         * @param[in] sign Signing callback.
         * @return Signed vote bundle on success, otherwise an error.
         */
        outcome::result<VoteBundle> CreateVoteBundle( const std::string       &proposal_id,
                                                      const std::string       &aggregator_id,
                                                      const std::vector<Vote> &votes,
                                                      Signer                   sign );

        /**
         * @brief Creates a certificate from a proposal and votes.
         * @param[in] proposal Proposal to certify.
         * @param[in] votes Votes used for quorum/certificate construction.
         * @return Certificate on success, otherwise an error.
         */
        outcome::result<Certificate> CreateCertificate( const Proposal &proposal, const std::vector<Vote> &votes );

        /**
         * @brief Tallies votes against an explicit registry snapshot.
         * @param[in] proposal Proposal being evaluated.
         * @param[in] votes Votes to tally.
         * @param[in] registry Registry snapshot used to compute weight.
         * @param[in] registry_cid Registry CID expected by the proposal.
         * @return Quorum tally result or an error.
         */
        outcome::result<QuorumTally> TallyVotes( const Proposal                    &proposal,
                                                 const std::vector<Vote>           &votes,
                                                 const ValidatorRegistry::Registry &registry,
                                                 const std::string                 &registry_cid ) const;
        /**
         * @brief Tallies votes using the manager registry source.
         * @param[in] proposal Proposal being evaluated.
         * @param[in] votes Votes to tally.
         * @return Quorum tally result or an error.
         */
        outcome::result<QuorumTally> TallyVotes( const Proposal &proposal, const std::vector<Vote> &votes ) const;

        /**
         * @brief Phase 6 (D-06): classifies a proposal's subject as a bridge mint.
         *
         * A bridge mint is a NonceSubject whose embedded transaction case is
         * kMintV2 and whose chain_id is public-chain metadata (`public` or a
         * numeric chain id). Local/test chain ids use single-pool quorum. Any
         * decode failure returns false (fail-closed: single-pool quorum applies).
         *
         * @param[in] proposal Proposal whose subject is inspected.
         * @return `true` when the subject is a bridge mint; `false` otherwise.
         */
        static bool IsBridgeMintSubject( const Proposal &proposal );

        /**
         * @brief Phase 6 (D-06): single quorum dispatcher.
         *
         * For non-bridge subjects: computes the single-pool tally (total weight,
         * approved weight, IsQuorum) -- identical to the pre-Phase-6 behavior.
         * For bridge-mint subjects: delegates to ValidatorRegistry::EvaluateSlotQuorum
         * (cumulative slot model with 50/25/75 weighting and >=2-validator PUBLIC
         * deduplication).
         *
         * Used by BOTH TallyVotes (certificate creation) and the incremental
         * HandleVote tally so the two sites agree on bridge-mint quorum
         * (RESEARCH Pitfall 1 mitigation / T-06-10).
         *
         * @param[in] proposal Proposal being evaluated.
         * @param[in] votes   Votes to tally.
         * @param[in] registry Registry snapshot used for weight resolution.
         * @return Quorum tally result.
         */
        outcome::result<QuorumTally> EvaluateQuorum( const Proposal                    &proposal,
                                                     const std::vector<Vote>           &votes,
                                                     const ValidatorRegistry::Registry &registry ) const;

        /**
         * @brief Computes canonical bytes to sign a proposal.
         * @param[in] proposal Proposal to encode.
         * @return Signing bytes on success, otherwise an error.
         */
        static outcome::result<std::vector<uint8_t>> ProposalSigningBytes( const Proposal &proposal );
        /**
         * @brief Computes canonical bytes to sign a vote.
         * @param[in] vote Vote to encode.
         * @return Signing bytes on success, otherwise an error.
         */
        static outcome::result<std::vector<uint8_t>> VoteSigningBytes( const Vote &vote );
        /**
         * @brief Computes canonical bytes to sign a vote bundle.
         * @param[in] bundle Vote bundle to encode.
         * @return Signing bytes on success, otherwise an error.
         */
        static outcome::result<std::vector<uint8_t>> VoteBundleSigningBytes( const VoteBundle &bundle );
        /**
         * @brief Computes deterministic subject id/hash.
         * @param[in] subject Subject to hash.
         * @return Subject identifier on success, otherwise an error.
         */
        static outcome::result<std::string> ComputeSubjectId( const Subject &subject );
        /**
         * @brief Computes deterministic bytes for a canonical subject type string.
         * @param[in] subject_type Canonical subject type, e.g. "gnus.bridge_event.v1".
         * @return 32-byte subject type hash on success, otherwise an error.
         */
        static outcome::result<std::string>          ComputeSubjectTypeHash( std::string_view subject_type );
        static outcome::result<NonceSubject>         DecodeNonceSubject( const Subject &subject );
        static outcome::result<TaskResultSubject>    DecodeTaskResultSubject( const Subject &subject );
        static outcome::result<RegistryBatchSubject> DecodeRegistryBatchSubject( const Subject &subject );
        static bool SubjectTypeMatches( const Subject &subject, std::string_view subject_type );
        /**
         * @brief Creates a nonce subject.
         * @param[in] account_id Account identifier bound to the subject.
         * @param[in] nonce Account nonce.
         * @param[in] tx_hash Transaction hash associated with the nonce transition.
         * @param[in] transaction_data Full serialized transaction bytes (SerializeByteVector output).
         * @param[in] utxo_commitment Optional UTXO commitment payload.
         * @param[in] utxo_witness Optional UTXO witness payload.
         * @return Constructed subject or an error.
         */
        static outcome::result<Subject> CreateNonceSubject(
            const std::string                             &account_id,
            uint64_t                                       nonce,
            const std::string                             &tx_hash,
            const EmbeddedTransaction                     &transaction,
            const std::optional<UTXOTransitionCommitment> &utxo_commitment,
            const std::optional<UTXOWitness>              &utxo_witness );
        /**
         * @brief Creates a task-result subject.
         * @param[in] account_id Account identifier bound to the subject.
         * @param[in] escrow_path Escrow path associated with task execution.
         * @param[in] task_result_hash Result hash for the task output.
         * @param[in] result_epoch Epoch for the task result.
         * @return Constructed subject or an error.
         */
        static outcome::result<Subject> CreateTaskResultSubject( const std::string &account_id,
                                                                 const std::string &escrow_path,
                                                                 const std::string &task_result_hash,
                                                                 uint64_t           result_epoch );
        /**
         * @brief Creates a registry-batch subject.
         * @param[in] account_id Account identifier bound to the subject.
         * @param[in] base_registry_cid Base registry CID used for the batch.
         * @param[in] base_registry_epoch Base registry epoch.
         * @param[in] target_registry_epoch Target registry epoch after applying batch.
         * @param[in] certificate_count Number of certificates in the batch.
         * @param[in] batch_root Merkle/root hash of the batch payload.
         * @return Constructed subject or an error.
         */
        static outcome::result<Subject> CreateRegistryBatchSubject( const std::string &account_id,
                                                                    const std::string &base_registry_cid,
                                                                    uint64_t           base_registry_epoch,
                                                                    uint64_t           target_registry_epoch,
                                                                    uint32_t           certificate_count,
                                                                    const std::string &batch_root );
        /**
         * @brief Creates a generic typed subject for application-owned payload schemas.
         * @param[in] account_id Account identifier bound to the subject.
         * @param[in] subject_type Canonical subject type, e.g. "gnus.bridge_event.v1".
         * @param[in] payload Canonical serialized application payload.
         * @return Constructed subject or an error.
         */
        static outcome::result<Subject> CreateGenericSubject( const std::string          &account_id,
                                                              std::string_view            subject_type,
                                                              const std::vector<uint8_t> &payload );
        /**
         * @brief Returns the lexicographically better hash among two values.
         * @param[in] a First hash candidate.
         * @param[in] b Second hash candidate.
         * @return Reference to the selected hash string.
         */
        static const std::string &BestHash( const std::string &a, const std::string &b );
        /**
         * @brief Submits a proposal for local handling and broadcast.
         * @param[in] proposal Proposal to submit.
         * @param[in] self_vote Whether the local node should auto-vote for its own proposal.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> SubmitProposal( const Proposal &proposal, bool self_vote = true );
        /**
         * @brief Submits a vote for local handling and broadcast.
         * @param[in] vote Vote to submit.
         * @param[in] self_handle Whether the local node should handle the vote immediately.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> SubmitVote( const Vote &vote, bool self_handle = true );
        /**
         * @brief Submits a certificate for local handling and broadcast.
         * @param[in] certificate Certificate to submit.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> SubmitCertificate( const Certificate &certificate );
        /**
         * @brief Retries proposal handling once its subject becomes ready.
         * @param[in] subject_hash Subject hash used to locate pending proposals.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> ResumeProposalHandling( const std::string &subject_hash );
        /**
         * @brief Retries pending proposals waiting on a typed dependency key.
         * @param[in] dependency Dependency key that became available.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> WakePendingDependency( const PendingDependencyKey &dependency );
        /**
         * @brief Processes queued certificate work entries.
         */
        void ProcessCertificates();
        /**
         * @brief Configures local delayed processing for received certificates.
         * @param[in] delay Delay applied before certificate processing.
         */
        void ConfigureCertificateDelay( std::chrono::milliseconds delay );

        /**
         * @brief Retrieves the authoritative certificate for a canonical slot.
         * @param[in] slot_id Canonical 64-lowercase-hex slot identifier.
         * @return Validated certificate when present, or a typed store error.
         */
        outcome::result<Certificate> GetCertificateBySlotId( const std::string &slot_id ) const;

        /**
         * @brief Retrieves a certificate by subject hash.
         * @param[in] subject_hash Subject hash key.
         * @return Certificate when present, or an error.
         */
        outcome::result<Certificate> GetCertificateBySubjectHash( const std::string &subject_hash ) const;

        /**
         * @brief Checks whether a certificate exists for a subject hash.
         * @param[in] subject_hash Subject hash key.
         * @return `true` if a certificate exists, otherwise `false`.
         */
        bool CheckCertificateForSubject( const std::string &subject_hash ) const;
        /**
         * @brief Checks whether a certificate exists for a subject.
         * @param[in] subject Subject instance to hash and lookup.
         * @return `true` if a certificate exists, otherwise `false`.
         */
        bool CheckCertificateForSubject( const Subject &subject ) const;

    protected:
        /**
         * @brief Sets timestamp validation window for received objects.
         * @param[in] window Allowed timestamp drift window.
         */
        void ConfigureTimestampWindow( std::chrono::milliseconds window );
        /**
         * @brief Sets consensus round duration.
         * @param[in] duration Round duration.
         */
        void ConfigureRoundDuration( std::chrono::milliseconds duration );
        /**
         * @brief Sets allowable round skew tolerance.
         * @param[in] skew Allowed round skew.
         */
        void ConfigureRoundSkew( std::chrono::milliseconds skew );

    private:
        friend class ConsensusManagerTestAccess;
        friend class ConsensusPendingLifecycleTestAccess;
        friend class ConsensusSlotKeyTestAccess;
        friend class ConsensusVoteJournalTestAccess;
        friend class ConsensusFinalizationTestAccess;
        friend class ConsensusFinalityRaceTestAccess;
        friend class BridgeRaceConsensusTestAccess;
        friend class ConsensusBurnReservationTestAccess;
        friend class TransactionManagerPendingLifecycleTestAccess;
        friend class NetworkConfigPrecedenceTestAccess;

        /**
         * @brief Constructs a consensus manager.
         * @param[in] registry Validator registry dependency.
         * @param[in] db GlobalDB dependency.
         * @param[in] pubsub PubSub dependency.
         * @param[in] signer Local signing callback.
         * @param[in] address Local validator/account id.
         * @param[in] consensus_topic Consensus topic base.
         */
        explicit ConsensusManager( std::shared_ptr<ValidatorRegistry>         registry,
                                   std::shared_ptr<crdt::GlobalDB>            db,
                                   std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                   Signer                                     signer,
                                   std::string                                address,
                                   std::string                                consensus_topic,
                                   ConsensusConfig                            config );

        bool RestoreLocalState();

        struct ConsensusTraceEvent
        {
            enum class Stage : uint8_t
            {
                LocalProposalPublished,
                VotePublished,
                AuthorityEstablished,
            };

            Stage                         stage{ Stage::LocalProposalPublished };
            std::string                   validator_id;
            std::string                   slot_id;
            std::string                   proposal_id;
            std::string                   subject_hash;
            std::string                   payload_digest;
            std::optional<DeliverySource> delivery_source;
        };

        void EmitConsensusTrace( ConsensusTraceEvent event ) const noexcept;
        void ReplayRestoredVotes();
        void ProcessCandidateDeadlines( std::chrono::steady_clock::time_point steady_now );
        void ReconcileBurnReservations();
        bool ReplayDurableVote( const std::string &slot_id, uint64_t generation );
        void RecoverRestoredCertificateWork();
        outcome::result<void> PublishSerialized( std::string_view envelope_bytes );
        static uint64_t CurrentTimeMs();
        void EmitStartupEvent( std::string_view event ) const;

        /**
         * @brief Maps one raw datastore read failure to the typed certificate API.
         *
         * Relationship validation is deliberately handled by the lookup callers;
         * this helper classifies only the underlying read operation.
         */
        static CertificateStoreError MapCertificateReadError( const std::error_code &error );
        /**
         * @brief Reads one certificate preflight record with typed absence semantics.
         *
         * Only an exact datastore NOT_FOUND is returned as an empty optional.
         * Integrity and operational failures remain typed errors so callers
         * cannot mistake unknown durable state for an unoccupied key.
         */
        outcome::result<std::optional<crdt::GlobalDB::Buffer>> ReadCertificatePreflightRecord(
            const crdt::HierarchicalKey &key ) const;
        /**
         * @brief Starts the background round timer loop.
         */
        void StartRoundTimer();
        std::shared_ptr<void> BeginActivity();

        static constexpr std::string_view CONSENSUS_CHANNEL_PREFIX =
            "consensus-channel-"; ///< Prefix for pubsub consensus channels.
        static constexpr std::string_view CERTIFICATE_BASE_PATH_KEY =
            "/cert/"; ///< Datastore root used for compatibility checks.
        static constexpr std::string_view CERTIFICATE_SLOT_BASE_PATH_KEY =
            "/cert/v2/slot/"; ///< Authoritative v2 slot certificate namespace.
        static constexpr std::string_view CERTIFICATE_TX_INDEX_BASE_PATH_KEY =
            "/cert/v2/tx/"; ///< Winning transaction hash secondary index namespace.
        static constexpr std::chrono::milliseconds DEFAULT_TIMESTAMP_WINDOW = std::chrono::minutes(
            5 ); ///< Default timestamp acceptance window.
        static constexpr std::chrono::milliseconds DEFAULT_ROUND_DURATION = std::chrono::milliseconds(
            500 ); ///< Default consensus round duration.
        static constexpr std::chrono::milliseconds DEFAULT_ROUND_SKEW = std::chrono::milliseconds(
            250 ); ///< Default round skew tolerance.
        static constexpr uint64_t NO_ROUND =
            std::numeric_limits<uint64_t>::max();                             ///< Sentinel for uninitialized round.
        static constexpr std::string_view CERT_SLOT_KEY_PATTERN =
            "^/?cert/v2/slot/[0-9a-f]{64}$"; ///< Exact authoritative slot key regex.
        static constexpr std::string_view CERT_TX_INDEX_KEY_PATTERN =
            "^/?cert/v2/tx/[0-9a-f]{64}$"; ///< Exact winning transaction index regex.
        static constexpr std::string_view CERT_NAMESPACE_KEY_PATTERN =
            "^/?cert/.*$"; ///< Complete live certificate namespace routing regex.

        /**
         * @brief Result of strict certificate normalization.
         */
        struct CertificateNormalization
        {
            Check       check{ Check::Reject }; ///< Approve, Reject, or dependency Stalled.
            Certificate certificate;            ///< Fully validated canonical certificate.
            std::string deterministic_bytes;    ///< Deterministic serialization of `certificate`.
        };

        /**
         * @brief Runtime state tracked for one proposal.
         */
        struct ProposalState
        {
            Proposal                        proposal;                        ///< Proposal currently tracked.
            std::vector<Vote>               votes;                           ///< Votes accepted for the proposal.
            std::string                     slot_key;                        ///< Slot key grouping competing proposals.
            uint64_t                        total_weight    = 0;             ///< Total eligible weight for tally.
            uint64_t                        approved_weight = 0;             ///< Approved weight accumulated so far.
            std::unordered_set<std::string> seen_voters;                     ///< Voter ids already counted.
            bool                            quorum_reached       = false;    ///< Whether quorum has been reached.
            uint64_t                        quorum_reached_ts_ms = 0;        ///< Timestamp when quorum was reached.
            uint64_t                        last_attempt_round   = NO_ROUND; ///< Last certificate-attempt round.
        };

        /**
         * @brief Runtime slot arbitration state.
         */
        struct SlotState
        {
            enum class Lifecycle
            {
                Empty,
                Selecting,
                SigningPublishing,
                Voted,
                PublishingReplay,
                Retired,
                Finalizing,
                Reconciling,
                FinalizedPendingApplication,
                Applied,
                SafetyViolation,
            };

            Lifecycle lifecycle{ Lifecycle::Empty };
            uint64_t generation{ 0 };
            std::chrono::steady_clock::time_point deadline{};
            std::string best_proposal_id;
            std::string best_tx_hash;
            std::string frozen_proposal_id;
            std::string durable_proposal_id;
            uint64_t durable_generation{ 0 };
            uint64_t publication_count{ 0 };
            uint64_t last_publication_at_ms{ 0 };
            bool last_publication_succeeded{ false };
            std::string reserved_finalization_proposal_id;
            std::string reserved_finalization_digest;
            std::string reserved_finalization_winner_id;
            std::vector<std::string> late_candidate_ids;
        };

        /**
         * @brief Canonical local pending proposal entry.
         */
        struct PendingProposalEntry
        {
            Proposal                                 proposal;
            std::vector<PendingDependencyKey>        dependencies;
            std::chrono::steady_clock::time_point    admitted_at;
            std::chrono::steady_clock::time_point    expires_at;
            std::chrono::steady_clock::time_point    next_retry_at;
            std::chrono::steady_clock::time_point    last_retry_at;
            std::optional<std::chrono::milliseconds> retry_after;
            std::size_t                              retained_bytes = 0;
            std::string                              proposer_id;
            std::size_t                              scheduled_retry_count = 0;
        };

        /**
         * @brief Handles an incoming proposal.
         * @param[in] proposal Proposal to process.
         */
        void HandleProposal( const Proposal &proposal );
        /**
         * @brief Handles an incoming vote.
         * @param[in] vote Vote to process.
         */
        void HandleVote( const Vote &vote );
        /**
         * @brief Handles an incoming vote bundle.
         * @param[in] bundle Vote bundle to process.
         */
        void HandleVoteBundle( const VoteBundle &bundle );
        /**
         * @brief Handles an incoming certificate.
         * @param[in] certificate Certificate to process.
         */
        void HandleCertificate( const Certificate &certificate );
        FinalizeResult FinalizeSlot( const Certificate &certificate, DeliverySource source );
        bool RecordCertificateConflict( const CertificateNormalization &authoritative,
                                        const CertificateNormalization &incoming,
                                        const std::string              &slot_id,
                                        DeliverySource                  source,
                                        uint64_t                        now_ms );
        FinalizeResult ProcessFinalizedCertificate( const CertificateNormalization &normalized,
                                                    const std::string              &slot_id,
                                                    const std::string              &winner_id );
        /**
         * @brief Fires all registered proposal cleanup callbacks for a proposal being cleaned up.
         *        Decodes the NonceSubject payload, extracts tx_hash, and dispatches to all handlers
         *        registered for the subject type under a shared lock.
         * @param[in] proposal Proposal whose slot is about to be cleared on timeout.
         */
        void FireProposalCleanupCallbacks( const Proposal &proposal );
        /**
         * @brief Computes proposal slot key used for conflict resolution.
         * @param[in] proposal Proposal to map to a slot.
         * @return Canonical 64-lowercase-hex slot key, or invalid_argument.
         */
        static outcome::result<std::string> GetSlotKey( const Proposal &proposal );
        /**
         * @brief Computes the canonical slot directly from a subject.
         * @param[in] subject Subject to map to its finality slot.
         * @return Canonical 64-lowercase-hex slot key, or an error.
         */
        static outcome::result<std::string> GetSlotKey( const Subject &subject );
        /**
         * @brief Compares competing proposals for the same slot.
         * @param[in] candidate Candidate proposal.
         * @param[in] current Current best proposal.
         * @return `true` when candidate should replace current.
         */
        bool IsBetterProposal( const Proposal &candidate, const Proposal &current ) const;
        /**
         * @brief Validates whether a timestamp is inside acceptable drift bounds.
         * @param[in] timestamp_ms Timestamp in Unix milliseconds.
         * @return `true` when timestamp is acceptable.
         */
        bool IsTimestampSane( uint64_t timestamp_ms ) const;
        /**
         * @brief Local node's certificate aggregation role for a proposal round.
         */
        enum class AggregatorRole
        {
            NotInRegistry,          ///< Local node is not an active validator in the proposal registry.
            ActiveButNotAggregator, ///< Local node is active, but another validator owns this round.
            CurrentAggregator,      ///< Local node owns certificate creation for this round.
        };
        /**
         * @brief Evaluates the local node's aggregation role for a proposal round.
         * @param[in] proposal Proposal being evaluated.
         * @param[in] registry Active validator registry snapshot.
         * @return Local aggregation role relative to the proposal registry and current round.
         */
        AggregatorRole GetAggregatorRole( const Proposal &proposal, const ValidatorRegistry::Registry &registry ) const;
        /**
         * @brief Returns active validators in deterministic ordering.
         * @param[in] registry Validator registry snapshot.
         * @return Ordered list of validator identifiers.
         */
        std::vector<std::string> GetOrderedActiveValidators( const ValidatorRegistry::Registry &registry ) const;
        /**
         * @brief Computes current round number relative to proposal timestamp.
         * @param[in] proposal_ts_ms Proposal timestamp in Unix milliseconds.
         * @return Round number.
         */
        uint64_t GetCurrentRound( uint64_t proposal_ts_ms ) const;
        /**
         * @brief Loads proposal state associated with a certificate.
         * @param[in] certificate Certificate used to locate proposal state.
         * @return Proposal state on success, or an error.
         */
        outcome::result<ProposalState> FetchProposalState( const Certificate &certificate );
        /**
         * @brief Creates an initial proposal state from a certificate payload.
         * @param[in] certificate Certificate with base proposal data.
         * @param[in] slot_key Previously validated canonical slot key.
         * @return Initialized proposal state.
         */
        ProposalState CreateProposalState( const Certificate &certificate, const std::string &slot_key );
        /**
         * @brief Validates whether certificate points to the best known proposal in slot.
         * @param[in] state Runtime state of the referenced proposal.
         * @param[in] certificate Certificate to validate.
         * @return `true` when certificate references the best proposal.
         */
        /**
         * @brief Extracts certificate votes into normalized vote objects.
         * @param[in] certificate Certificate to inspect.
         * @return Vote list collected from certificate.
         */
        std::vector<Vote> CollectCertificateVotes( const Certificate &certificate ) const;
        /**
         * @brief Clears local slot bookkeeping for a proposal.
         * @param[in] proposal Proposal whose slot state should be cleared.
         */
        void ClearProposalSlot( const Proposal &proposal );
        /**
         * @brief Computes subject hash from a subject object.
         * @param[in] subject Subject to hash.
         * @return Subject hash on success, otherwise an error.
         */
        static outcome::result<std::string> GetSubjectHash( const Subject &subject );
        /**
         * @brief Continues deferred proposal processing after subject validation.
         * @param[in] proposal Proposal to continue processing.
         * @param[in] slot_key Previously validated canonical slot key.
         */
        void ContinueProposalAfterSubject( const Proposal &proposal, const std::string &slot_key );
        bool AdmitProposalResources( const Proposal &proposal, const std::string &slot_key );
        void ReleaseProposalAdmission( const Proposal &proposal, const std::string &slot_key );
        /**
         * @brief Stores proposal pending subject readiness.
         * @param[in] proposal Proposal to queue.
         * @param[in] subject_hash Subject hash dependency key.
         */
        bool AddPendingProposal( const Proposal                       &proposal,
                                 const std::string                    &subject_hash,
                                 const ValidationResult               &validation_result = ValidationResult::Pending(),
                                 std::size_t                           scheduled_retry_count = 0,
                                 std::chrono::steady_clock::time_point last_retry_at         = {} );
        /**
         * @brief Removes one pending proposal and its local indexes/accounting.
         * @param[in] proposal_id Proposal identifier.
         * @param[in] reason Short local reason for logging.
         * @return `true` when an entry was removed.
         */
        bool RemovePendingProposal( const std::string &proposal_id, std::string_view reason );
        /**
         * @brief Removes and returns pending proposals for a subject hash.
         * @param[in] subject_hash Subject hash key.
         * @return Pending proposals for the subject.
         */
        std::vector<Proposal> TakePendingProposals( const std::string &subject_hash );
        bool                  RemovePendingProposalLocked( const std::string &proposal_id, std::string_view reason );
        bool                  CanAdmitPendingProposalLocked( const Proposal    &proposal,
                                                             std::size_t        retained_bytes,
                                                             const std::string &proposer_id ) const;
        std::vector<PendingDependencyKey> NormalizePendingDependencies(
            const std::string      &subject_hash,
            const ValidationResult &validation_result ) const;
        std::chrono::milliseconds NextPendingRetryDelayLocked( const PendingProposalEntry &entry ) const;
        void                      RetryPendingProposal( const Proposal                       &proposal,
                                                        std::string_view                      reason,
                                                        std::size_t                           scheduled_retry_count = 0,
                                                        std::chrono::steady_clock::time_point last_retry_at = {} );
        void                      ProcessDuePendingRetries();
        void                      ExpirePendingProposals();
        /**
         * @brief Stores vote pending proposal availability.
         * @param[in] vote Vote to queue.
         */
        void AddPendingVote( const Vote &vote );
        /**
         * @brief Removes and returns pending votes for a proposal id.
         * @param[in] proposal_id Proposal identifier.
         * @return Pending votes for the proposal.
         */
        std::vector<Vote> TakePendingVotes( const std::string &proposal_id );
        /**
         * @brief Registers CRDT filter used for certificate keys.
         * @return `true` on successful registration.
         */
        bool RegisterCertificateFilter();
        /**
         * @brief Validates a complete incoming v2 certificate/index delta.
         */
        crdt::DeltaFilterResult FilterCertificateDelta( const crdt::pb::Delta &delta );
        /**
         * @brief Filters CRDT entries to certificate payloads.
         * @param[in] element CRDT element candidate.
         * @return Filtered element vector, or `std::nullopt` when rejected.
         */
        std::optional<std::vector<crdt::pb::Element>> FilterCertificate( const crdt::pb::Element &element );
        /**
         * @brief Callback for new certificate data received from CRDT.
         * @param[in] new_data New key-value pair.
         * @param[in] cid CID associated with the CRDT update.
         */
        void CertificateReceived( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid );
        /**
         * @brief Recovers unfinished certificate-processing work from journal.
         */
        void RecoverPendingCertificateWork();
        /**
         * @brief Rejects incompatible certificate state before startup side effects.
         */
        bool HasCompatibleCertificateState() const;
        /**
         * @brief Validates a certificate semantic and quorum constraints.
         * @param[in] certificate Certificate to validate.
         * @return Validation result enum.
         */
        ConsensusManager::Check ValidateCertificate( const Certificate &certificate ) const;
        /**
         * @brief Strictly validates and normalizes every certificate storage surface.
         *
         * Unknown fields and invalid votes are terminally rejected. Registry
         * unavailability is the only condition returned as `Check::Stalled`.
         */
        CertificateNormalization NormalizeCertificateStructural( const Certificate &certificate ) const;
        Check ValidateCertificateForFirstObservation( const CertificateNormalization &normalized,
                                                      uint64_t system_now_ms ) const;
        /**
         * @brief Computes deterministic proposal identifier.
         * @param[in] proposal Proposal to identify.
         * @return Proposal identifier string.
         */
        static std::string CreateProposalId( const Proposal &proposal );
        /**
         * @brief Checks if a subject has a valid type hash.
         * @param[in,out] subject Subject to check
         * @return `true` if the subject has a valid type hash, otherwise `false`.
         */
        static bool SubjectHasValidTypeHash( Subject *subject );
        /**
         * @brief Performs basic subject sanity validation.
         * @param[in] subject Subject to validate.
         * @return `true` when subject structure is valid.
         */
        static bool ValidateSubject( const Subject &subject );

        /**
         * @brief Callback for incoming consensus pubsub messages.
         * @param[in] message Incoming pubsub message.
         */
        void OnConsensusMessage( boost::optional<const ipfs_pubsub::GossipPubSub::Message &> message );
        /**
         * @brief Recomputes local pending-certificate flag.
         */
        void UpdateCertificatesPending();
        /**
         * @brief Performs lightweight subject checks.
         * @param[in] subject Subject to validate.
         * @return `true` when valid.
         */
        static bool CheckSubject( const Subject &subject );
        /**
         * @brief Performs lightweight proposal checks.
         * @param[in] proposal Proposal to validate.
         * @return `true` when valid.
         */
        static bool CheckProposal( const Proposal &proposal );
        /**
         * @brief Performs lightweight vote checks.
         * @param[in] vote Vote to validate.
         * @return `true` when valid.
         */
        static bool CheckVote( const Vote &vote );
        /**
         * @brief Produces printable subject hash representation for logs.
         * @param[in] subject Subject to format.
         * @return Printable hash string.
         */
        static std::string                     GetPrintableSubjectHash( const Subject &subject );
        std::shared_ptr<ValidatorRegistry>     registry_; ///< Validator registry dependency.
        std::shared_ptr<crdt::GlobalDB>        db_;       ///< GlobalDB dependency for persistence and CRDT operations.
        std::function<outcome::result<crdt::GlobalDB::Buffer>( const crdt::HierarchicalKey & )>
            certificate_record_reader_; ///< Private/friend-only read seam; defaults to GlobalDB::Get.
        std::function<void()> certificate_publish_observer_; ///< Private/friend-only publish-attempt observer.
        std::function<void( const ConsensusStateStore::ConflictRecord &, bool )>
            certificate_conflict_observer_; ///< Private/friend-only conflict record observer (record, unique pair).
        std::function<void( std::string_view )> finalization_stage_observer_; ///< Private/friend-only stage observer.
        std::function<void( const ConsensusTraceEvent & )>
            consensus_trace_observer_; ///< Private per-manager observer of public consensus identities.
        std::function<void( std::string_view, const std::string & )>
            burn_reconciliation_stage_observer_; ///< Private/friend-only deterministic reconciliation barrier.
        std::shared_ptr<crdt::CRDTWorkJournal> certificate_work_journal_; ///< Work journal for certificate processing.
        /**
         * Lock order for manager-owned synchronization:
         * close_mutex_ -> activity_state_->mutex -> timer_mutex_ -> proposals_mutex_
         * -> restored_state_mutex_ -> handler registries.  The local state
         * store owns its own lock and is entered only after releasing
         * proposals_mutex_.  Signers, CRDT writes, PubSub publication, and
         * subject/certificate/cleanup handlers are always invoked without
         * proposals_mutex_ or a handler-registry lock held.
         */
        struct ActivityState
        {
            std::mutex              mutex;
            std::condition_variable cv;
            bool                    closing{ false };
            std::size_t             active{ 0 };
        };

        std::shared_ptr<ActivityState> activity_state_{ std::make_shared<ActivityState>() };
        std::mutex                     close_mutex_;
        bool                           close_complete_{ false };

        std::unordered_map<std::string, SubjectHandler>
                                  subject_handlers_;       ///< Subject handlers keyed by subject type hash.
        mutable std::shared_mutex subject_handlers_mutex_; ///< Guards `subject_handlers_`.
        std::unordered_map<std::string, CertificateSubjectHandler>
                                  certificate_subject_handlers_; ///< Certificate handlers by subject type hash.
        std::unordered_map<std::string, CertificateApplicationHandler>
                                  certificate_application_handlers_; ///< Typed application handlers by subject type hash.
        mutable std::shared_mutex certificate_handlers_mutex_;   ///< Guards `certificate_subject_handlers_`.
        std::unordered_map<std::string, std::vector<ProposalCleanupHandler>>
            proposal_cleanup_handlers_; ///< Proposal cleanup handlers by subject type hash.
        std::unordered_map<std::string, ResourceAdmissionHandler>
            resource_admission_handlers_; ///< Post-validation resource descriptors by subject type hash.
        static inline std::unordered_map<std::string, SlotKeyHandler>
                                  slot_key_handlers_;                 ///< Slot key handlers keyed by subject type hash.
        static inline std::shared_mutex slot_key_handlers_mutex_;     ///< Guards `slot_key_handlers_`.
        mutable std::shared_mutex cleanup_handlers_mutex_;            ///< Guards `proposal_cleanup_handlers_`.
        mutable std::shared_mutex resource_admission_handlers_mutex_; ///< Guards resource admission handlers.
        Signer                    signer_;                            ///< Local signing callback.
        SlotHashPopulator         slot_hash_populator_;               ///< Optional slot-hash populator (Phase 6, D-01).
        std::string               account_address_;                   ///< Local validator/account id.
        std::unordered_map<std::string, ProposalState> proposals_;    ///< Proposal state map keyed by proposal id.
        std::unordered_map<std::string, SlotState>     slot_states_;  ///< Slot arbitration state keyed by slot key.
        std::unordered_map<std::string, PendingProposalEntry>
            pending_entries_; ///< Canonical pending proposals keyed by proposal id.
        std::unordered_map<PendingDependencyKey, std::unordered_set<std::string>, PendingDependencyKeyHash>
            pending_by_dependency_; ///< Proposal ids queued by typed dependency key.
        std::unordered_map<std::string, std::size_t>
                               pending_count_by_proposer_;                   ///< Pending proposal count by proposer id.
        std::size_t            pending_retained_bytes_ = 0;                  ///< Total retained pending proposal bytes.
        PendingLifecycleConfig pending_config_;                              ///< Local pending lifecycle bounds.
        std::unordered_map<std::string, std::vector<Vote>> pending_votes_;   ///< Pending votes keyed by proposal id.
        mutable std::mutex                                 proposals_mutex_; ///< Guards proposal and pending maps.
        std::unordered_set<std::string> resource_admissions_inflight_; ///< Slots between approval and activation.
        std::shared_ptr<ipfs_pubsub::GossipPubSub>         pubsub_;          ///< PubSub transport dependency.
        const ConsensusConfig config_; ///< Immutable settings fixed before factory side effects.
        std::shared_ptr<ConsensusStateStore> state_store_; ///< One live shared node-local consensus state owner.
        std::vector<ConsensusStateStore::VoteRecord> restored_votes_; ///< Validated durable vote records.
        std::unordered_map<std::string, ConsensusStateStore::ProcessRecord>
            restored_processes_; ///< Restored certificate work keyed by canonical slot.
        mutable std::mutex restored_state_mutex_; ///< Guards restored process state used by recovery callbacks.
        std::unordered_set<std::string> processing_slots_; ///< In-process leases; callbacks never overlap per slot.
        std::unordered_set<std::string> restored_final_slots_; ///< Slots with authoritative finality.
        std::unordered_set<std::string> restored_safety_slots_; ///< Slots stopped by SafetyViolation.
        std::unordered_set<std::string> restored_safety_proposal_ids_; ///< Proposal ids bound to stopped slots.
        std::atomic<uint64_t> certificate_conflict_unique_pairs_{ 0 }; ///< Unique canonical digest pairs observed.
        static inline std::function<void( std::string_view )> startup_event_observer_;
        static inline std::function<outcome::result<void>( std::string_view )> raw_publish_override_;
        static inline std::function<outcome::result<storage::rocksdb::QueryResult>( const base::Buffer & )>
            startup_local_query_override_;
        static inline std::function<outcome::result<void>( const ConsensusStateStore::VoteRecord & )>
            vote_put_override_;
        static inline std::function<std::chrono::steady_clock::time_point()> steady_now_override_;
        static inline std::function<uint64_t()> system_now_override_;
        static inline std::atomic<bool> suppress_timer_burn_reconciliation_{ false };
        static inline std::function<void( std::string_view, const std::string &, uint64_t )>
            vote_stage_observer_;

        std::string consensus_messages_topic_;  ///< PubSub topic for live consensus messages.
        std::string consensus_datastore_topic_; ///< Datastore namespace/topic for persisted data.
        std::shared_future<std::shared_ptr<ipfs_pubsub::GossipPubSub::Subscription>>
                                  consensus_subs_future_;                        ///< Async subscription handle.
        std::chrono::milliseconds timestamp_window_{ DEFAULT_TIMESTAMP_WINDOW }; ///< Accepted timestamp window.
        std::chrono::milliseconds certificate_delay_{
            std::chrono::milliseconds( 2000 ) };                             ///< Delay before certificate processing.
        std::chrono::milliseconds round_duration_{ DEFAULT_ROUND_DURATION }; ///< Consensus round duration.
        std::chrono::milliseconds round_skew_{ DEFAULT_ROUND_SKEW };         ///< Round skew tolerance.
        std::atomic<bool>         stop_timer_{ false };           ///< Signals the round timer thread to stop.
        std::atomic<bool>         certificates_pending_{ false }; ///< Indicates pending certificate processing.
        std::condition_variable   timer_cv_;                      ///< Condition variable used by the round timer.
        std::mutex                timer_mutex_;                   ///< Mutex paired with `timer_cv_`.
        std::condition_variable   slot_cv_;                       ///< Wakes finalizers after vote publication reservations.
        std::thread               round_timer_;                   ///< Background thread driving round-based retries.
    };
}

OUTCOME_HPP_DECLARE_ERROR_2( sgns, ConsensusManager::CertificateStoreError );

#endif // CONSENSUS_MANAGER_HPP
