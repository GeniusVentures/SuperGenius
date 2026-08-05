/**
 * @file       ValidatorRegistry.hpp
 * @brief      Validator registry and quorum logic for governance.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_VALIDATOR_REGISTRY_HPP
#define SGNS_VALIDATOR_REGISTRY_HPP

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/format.h>

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "blockchain/impl/proto/Consensus.pb.h"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"
#include "crdt/crdt_callback_manager.hpp"
#include "crdt/proto/delta.pb.h"
#include "outcome/outcome.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "primitives/cid/cid.hpp"

namespace sgns
{
    class Migration3_5_0To3_6_0;
}

namespace sgns
{
    /**
     * @brief Maintains validator registry state and applies certificate-driven updates.
     *
     * Here, a certificate is a finalized `ConsensusCertificate`: the signed
     * proposal together with the validators' signed votes and the registry
     * CID/epoch against which their voting weight and quorum are evaluated.
     * It is consensus evidence that the proposal was approved, not a TLS/X.509
     * identity certificate.
     *
     * The active validator set is the subset of entries in the current registry
     * snapshot whose status is `ACTIVE`. Each entry identifies a validator and
     * its role and voting weight; only active entries contribute to quorum.
     * Suspended and blacklisted entries remain in the registry but are excluded.
     *
     * This component stores the registry in GlobalDB/CRDT, computes quorum
     * thresholds, validates registry updates, and derives next registry snapshots
     * from consensus certificates.
     */
    class ValidatorRegistry : public std::enable_shared_from_this<ValidatorRegistry>
    {
    public:
        static constexpr size_t DefaultMaxNewValidatorsPerUpdate =
            10;                                                  ///< Default cap for new validators added per update.
        static constexpr size_t DefaultCertificatesPerBatch = 5; ///< Default number of certificates grouped per batch.
        using ValidatorEntry                                = validator::ValidatorEntry;
        using Registry                                      = validator::Registry;
        using SignatureEntry                                = validator::SignatureEntry;
        using RegistryUpdate                                = validator::RegistryUpdate;
        using Role                                          = validator::Role;
        using Status                                        = validator::Status;
        using InitCallback                                  = std::function<void( bool )>;
        using BlockRequestMethod =
            std::function<void( const std::string &, std::function<void( outcome::result<std::string> )> )>;

        /**
         * @brief Weight policy used to score validators and update penalties.
         */
        struct WeightConfig
        {
            uint64_t genesis_weight_                  = 50000; ///< Base weight for genesis authority validators.
            uint64_t full_weight_                     = 1000;  ///< Base weight for full validators.
            uint64_t regular_weight_                  = 1;     ///< Base weight for regular validators.
            uint64_t sharded_weight_                  = 1;     ///< Base weight for sharded validators.
            uint64_t genesis_max_weight_              = 50000; ///< Max weight allowed for genesis validators.
            uint64_t full_max_weight_                 = 5000;  ///< Max weight allowed for full validators.
            uint64_t regular_max_weight_              = 100;   ///< Max weight allowed for regular validators.
            uint64_t sharded_max_weight_              = 100;   ///< Max weight allowed for sharded validators.
            uint64_t approval_increment_              = 1;     ///< Weight increment applied for approved behavior.
            uint32_t penalty_threshold_               = 10;    ///< Penalty score threshold before harsher actions.
            uint32_t penalty_cap_                     = 100;   ///< Maximum accumulated penalty score.
            uint32_t blacklist_bump_                  = 10;    ///< Penalty increment applied for severe failures.
            uint32_t missed_epoch_threshold_          = 500; ///< Missed-epoch threshold used for inactivity decisions.
            uint32_t inactivity_decrement_            = 1;   ///< Weight decrement for inactive validators.
            uint64_t total_weight_cap_multiplier_     = 4; ///< Multiplier controlling global weight-cap normalization.
            uint64_t certificate_timestamp_window_ms_ = 300000; ///< Allowed timestamp drift for certificates.
            // Phase 6 slot-based RPC-hash voting (D-02/D-03/D-06). Integer ratios
            // keep the tally deterministic across peers (no floating point).
            uint64_t slot_direct_numerator_   = 1; ///< Slot 0 (DIRECT_API) weight numerator.   0.50 = 1/2 (D-02).
            uint64_t slot_direct_denominator_ = 2; ///< Slot 0 (DIRECT_API) weight denominator. 0.50 = 1/2 (D-02).
            uint64_t slot_public_numerator_   = 1; ///< Slots 1-2 (PUBLIC) weight numerator.    0.25 = 1/4 (D-03).
            uint64_t slot_public_denominator_ = 4; ///< Slots 1-2 (PUBLIC) weight denominator.  0.25 = 1/4 (D-03).
            uint64_t slot_quorum_numerator_   = 3; ///< Cumulative quorum threshold numerator.  0.75 = 3/4 (D-06).
            uint64_t slot_quorum_denominator_ = 4; ///< Cumulative quorum threshold denominator.0.75 = 3/4 (D-06).
            uint64_t slot_public_min_group_   = 2; ///< D-03: minimum distinct validators per PUBLIC hash group.
            // D-08: REGULAR -> FULL promotion threshold. A REGULAR validator whose
            // accumulated weight (via ApplyVoteEffects approve increments) reaches
            // this value AND whose penalty_score is below penalty_threshold_ is
            // promoted to Role::FULL. The promoted node's weight then accumulates
            // up to full_max_weight_, flowing into EvaluateSlotQuorum via
            // validator.weight() with no tally-side special case. Equal to regular_max_weight_ so the approve-branch clamp
            // does not prevent reaching the threshold.
            uint64_t full_promotion_weight_ = 100; ///< Weight at which a REGULAR validator is promoted to FULL (D-08).
        };

        /**
         * @brief Result of the Phase 6 cumulative slot-quorum tally (D-06).
         *
         * Deterministic across peers: computed ONLY from the vote vector and a
         * registry snapshot (REQ-DETERM-01). No clocks, no local config, no node
         * state is consulted.
         */
        struct SlotQuorumResult
        {
            uint64_t qualified_sum           = 0; ///< Sum of slot-weighted contributions (D-06).
            uint64_t total_voting_reputation = 0; ///< Sum of weight of ALL approve voters.
            uint64_t threshold               = 0; ///< ceil(total * slot_quorum_numerator_ / slot_quorum_denominator_).
            bool     has_quorum              = false; ///< qualified_sum > threshold (STRICT, D-06).
        };

        /**
         * @brief Creates and initializes a validator registry instance.
         * @param[in] db GlobalDB backing store.
         * @param[in] quorum_numerator Numerator used for quorum threshold computation.
         * @param[in] quorum_denominator Denominator used for quorum threshold computation.
         * @param[in] weight_config Validator weighting and penalty configuration.
         * @param[in] genesis_authority Validator id treated as genesis authority.
         * @param[in] block_request_method Callback used to fetch blocks by CID.
         * @param[in] init_callback Optional callback notified after initialization.
         * @return Shared pointer to the created registry.
         */
        static std::shared_ptr<ValidatorRegistry> New( std::shared_ptr<crdt::GlobalDB> db,
                                                       uint64_t                        quorum_numerator,
                                                       uint64_t                        quorum_denominator,
                                                       WeightConfig                    weight_config,
                                                       std::string                     genesis_authority,
                                                       BlockRequestMethod              block_request_method,
                                                       InitCallback                    init_callback = nullptr );
        /**
         * @brief Destroys the registry instance.
         */
        ~ValidatorRegistry();

        /**
         * @brief Stops accepting registry work and waits for queued persistence to finish.
         *
         * Must be called before shutting down the backing GlobalDB. Safe to call
         * multiple times.
         */
        void Close();

        /**
         * @brief Computes default weight for a validator role.
         * @param[in] role Validator role.
         * @return Weight associated with the role.
         */
        uint64_t ComputeWeight( Role role ) const;
        /**
         * @brief Computes total effective weight in a registry snapshot.
         * @param[in] registry Registry snapshot.
         * @return Sum of validator weights.
         */
        static uint64_t TotalWeight( const Registry &registry );
        /**
         * @brief Computes minimum accumulated weight required for quorum.
         * @param[in] total_weight Total eligible weight.
         * @return Quorum threshold.
         */
        uint64_t QuorumThreshold( uint64_t total_weight ) const;
        /**
         * @brief Checks whether accumulated weight satisfies quorum.
         * @param[in] accumulated_weight Weight accumulated by votes.
         * @param[in] total_weight Total eligible weight.
         * @return `true` when quorum is reached.
         */
        bool IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const;

        /**
         * @brief Phase 6 cumulative slot-quorum tally for bridge-mint subjects (D-06).
         *
         * Reads ONLY the supplied votes and registry snapshot (REQ-DETERM-01).
         * Slot 0: each distinct approver with a non-empty slot_0_hash contributes
         * weight * slot_direct_numerator_ / slot_direct_denominator_ (D-02).
         * Slots 1-2: votes are grouped by slot_N_hash; only groups with at least
         * slot_public_min_group_ distinct validators contribute
         * sum(weight) * slot_public_numerator_ / slot_public_denominator_ (D-03).
         * Solo hashes contribute zero. Abstainers (all slot hashes empty) still
         * count toward total_voting_reputation but zero toward qualified_sum (D-05).
         * has_quorum = (qualified_sum > threshold) -- STRICT (D-06).
         *
         * @param[in] votes     Consensus votes (only approve votes are counted).
         * @param[in] registry  Registry snapshot used to resolve voter weights.
         * @return Slot tally result.
         */
        SlotQuorumResult EvaluateSlotQuorum( const std::vector<sgns::ConsensusVote> &votes,
                                             const Registry                         &registry ) const;

        /**
         * @brief Pure (stateless) slot-quorum tally for deterministic unit testing.
         *
         * Identical arithmetic to EvaluateSlotQuorum, but takes the WeightConfig
         * explicitly so it can be exercised without a GlobalDB-backed
         * ValidatorRegistry instance. The member function delegates here.
         *
         * @param[in] votes         Consensus votes (only approve votes counted).
         * @param[in] registry      Registry snapshot used to resolve voter weights.
         * @param[in] weight_config Slot ratio configuration.
         * @return Slot tally result.
         */
        static SlotQuorumResult EvaluateSlotQuorumStatic( const std::vector<sgns::ConsensusVote> &votes,
                                                          const Registry                         &registry,
                                                          const WeightConfig                     &weight_config );

        /**
         * @brief Pure (stateless) REGULAR -> FULL promotion decision (D-08).
         *
         * Returns true iff the entry is currently Role::REGULAR, its accumulated
         * weight has reached @ref full_promotion_weight_, AND its penalty_score is
         * strictly below @ref penalty_threshold_. GENESIS, SHARDED, and already-FULL
         * entries never qualify (no GENESIS demotion, idempotent on FULL). Extracted
         * as a pure static helper so the promotion decision is unit-testable without
         * a GlobalDB-backed ValidatorRegistry instance -- ApplyVoteEffects delegates
         * here. The function reads ONLY its inputs (REQ-DETERM-01), so every peer
         * mutates the entry identically.
         *
         * @param[in] entry         Validator entry under consideration.
         * @param[in] weight_config Weight policy supplying thresholds.
         * @return true if the entry should be promoted from REGULAR to FULL.
         */
        static bool EvaluateRegularPromotionStatic( const ValidatorEntry &entry, const WeightConfig &weight_config );

        /**
         * @brief Creates an in-memory genesis registry snapshot.
         * @param[in] genesis_validator_ids Validator ids for genesis authorities.
         * @return Genesis registry snapshot.
         */
        Registry CreateGenesisRegistry( const std::vector<std::string> &genesis_validator_ids ) const;
        /**
         * @brief Persists a signed genesis registry update.
         * @param[in] genesis_validator_ids Validator ids for genesis authorities.
         * @param[in] sign Signing callback used for registry-update signatures.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> StoreGenesisRegistry( const std::vector<std::string> &genesis_validator_ids,
                                                    std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign );
        /**
         * @brief Loads the currently active registry.
         * @return Registry snapshot or an error.
         */
        outcome::result<Registry> LoadCurrentRegistry() const;
        /**
         * @brief Loads a registry by CID.
         *
         * Each registry CRDT element contains a complete serialized
         * RegistryUpdate snapshot, so this reads the element from the identified
         * delta directly; reconstructing the registry does not require replaying
         * or merging ancestor deltas.
         *
         * @param[in] cid Registry CID.
         * @return Registry snapshot or an error.
         */
        outcome::result<Registry> LoadRegistryByCid( const std::string &cid ) const;
        /**
         * @brief Loads the currently active registry update payload.
         * @return Registry update or an error.
         */
        outcome::result<RegistryUpdate> LoadRegistryUpdate() const;
        /**
         * @brief Looks up validator weight by validator id.
         * @param[in] validator_id Validator identifier.
         * @return Optional weight when validator exists, or an error.
         */
        outcome::result<std::optional<uint64_t>> GetValidatorWeight( const std::string &validator_id ) const;
        /**
         * @brief Registers CRDT filter/callbacks for registry updates.
         * @return `true` when registration succeeds.
         */
        bool RegisterFilter();
        /**
         * @brief Builds a registry update from a finalized certificate.
         * @param[in] certificate Consensus certificate.
         * @return Registry update or an error.
         */
        outcome::result<RegistryUpdate> CreateUpdateFromCertificate( const sgns::ConsensusCertificate &certificate );
        /**
         * @brief Persists a registry update.
         * @param[in] update Registry update to store.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> StoreRegistryUpdate( const RegistryUpdate &update );
        /**
         * @brief Serializes a registry update protobuf.
         * @param[in] update Registry update to serialize.
         * @return Serialized bytes or an error.
         */
        outcome::result<std::vector<uint8_t>> SerializeRegistryUpdate( const RegistryUpdate &update ) const;
        /**
         * @brief Deserializes a registry update protobuf.
         * @param[in] buffer Serialized registry update bytes.
         * @return Parsed update or an error.
         */
        outcome::result<RegistryUpdate> DeserializeRegistryUpdate( const std::vector<uint8_t> &buffer ) const;
        /**
         * @brief Returns cached/current registry CID.
         * @return Registry CID string.
         */
        std::string GetRegistryCid() const;
        /**
         * @brief Returns cached/current registry epoch.
         * @return Registry epoch.
         */
        uint64_t GetRegistryEpoch() const;
        /**
         * @brief Sets certificate count threshold used when creating batch subjects.
         * @param[in] batch_size Number of certificates per batch.
         */
        void SetCertificatesPerBatch( size_t batch_size );
        /**
         * @brief Sets callback used to submit generated batch subjects.
         * @param[in] submitter Subject submitter callback.
         */
        void SetBatchSubjectSubmitter(
            std::function<outcome::result<void>( const ConsensusSubject &subject )> submitter );
        /**
         * @brief Handles a finalized consensus certificate.
         * @param[in] certificate Finalized certificate.
         */
        outcome::result<void> OnFinalizedCertificate( const sgns::ConsensusCertificate &certificate );

        /**
         * @brief Decision result when evaluating a registry-batch subject.
         */
        enum class BatchSubjectDecision
        {
            Approve, ///< Subject is valid and should proceed.
            Reject,  ///< Subject is invalid and should be rejected.
            Pending  ///< Subject cannot be decided yet.
        };
        /**
         * @brief Decision result when handling a registry-batch certificate.
         */
        enum class BatchCertificateDecision
        {
            Approve, ///< Certificate is accepted.
            Reject,  ///< Certificate is rejected.
            Pending, ///< Decision is deferred due to missing prerequisites.
            Stalled  ///< Processing is stalled and should be retried later.
        };
        /**
         * @brief Evaluates a registry-batch subject payload.
         * @param[in] subject Subject to evaluate.
         * @return Subject decision or an error.
         */
        outcome::result<BatchSubjectDecision> EvaluateBatchSubject( const ConsensusSubject &subject );
        /**
         * @brief Handles certificate associated with a registry-batch subject.
         * @param[in] subject_hash Subject hash key.
         * @param[in] certificate Certificate to process.
         * @return Certificate handling decision or an error.
         */
        outcome::result<BatchCertificateDecision> HandleBatchCertificate(
            const std::string                &subject_hash,
            const sgns::ConsensusCertificate &certificate );

        /**
         * @brief Registry object key used in datastore.
         * @return Constant registry key.
         */
        static constexpr std::string_view RegistryKey()
        {
            return "gnus-validator-registry";
        }

        /**
         * @brief Topic used to publish/subscribe validator registry updates.
         * @return Constant validator topic.
         */
        static constexpr std::string_view ValidatorTopic()
        {
            return "gnus-validator-registry";
        }

        /**
         * @brief Key used to persist the current registry CID.
         * @return Constant CID key.
         */
        static constexpr std::string_view RegistryCidKey()
        {
            return "gnus-validator-registry-cid";
        }

        /**
         * @brief Finds validator entry by id in a registry snapshot.
         * @param[in] registry Registry snapshot.
         * @param[in] validator_id Validator identifier.
         * @return Pointer to entry when found, otherwise `nullptr`.
         */
        static const ValidatorEntry *FindValidator( const Registry &registry, const std::string &validator_id );

        /**
         * @brief Re-attempts genesis-registry head-CID discovery when initialization has not
         *        yet completed (bug fix, D-01/2-of-11-nodes-start-bridge).
         *
         * InitializeCache() runs synchronously once, at construction time. If the genesis
         * registry has not yet synced into this node's local CRDT store at that instant
         * (the common case for a Light node in a large concurrent cluster), it returns
         * without requesting anything further and initialization is left to depend solely
         * on a passive CRDT broadcast (RegistryUpdateReceived) that may never arrive for
         * every node. Callers that periodically retry a deferred blockchain start (e.g.
         * Blockchain::Start()) should call this on every such retry so an active,
         * repeating head-CID request backs up the passive broadcast path.
         *
         * No-op once the cache is already initialized.
         */
        void RetryInitializationIfNeeded();

    protected:
        friend class sgns::Migration3_5_0To3_6_0;

        /**
         * @brief Migrates registry-related CIDs from old to new datastore.
         * @param[in] old_db Source GlobalDB.
         * @param[in] new_db Target GlobalDB.
         * @return outcome::success on success, otherwise an error.
         */
        static outcome::result<void> MigrateCids( const std::shared_ptr<crdt::GlobalDB> &old_db,
                                                  const std::shared_ptr<crdt::GlobalDB> &new_db );

    private:
        /**
         * @brief Partitioned vote extraction from a certificate.
         */
        struct CertificateVotes
        {
            std::unordered_set<std::string>       approved;           ///< Validators that approved the certificate.
            std::unordered_set<std::string>       unregistered;       ///< Unregistered voters observed in certificate.
            std::unordered_map<std::string, bool> registered_votes;   ///< Vote decisions by registered validators.
            std::unordered_map<std::string, bool> unregistered_votes; ///< Vote decisions by unregistered validators.
        };

        /**
         * @brief Constructs a ValidatorRegistry instance.
         * @param[in] db GlobalDB backing store.
         * @param[in] quorum_numerator Numerator used for quorum threshold computation.
         * @param[in] quorum_denominator Denominator used for quorum threshold computation.
         * @param[in] weight_config Validator weighting and penalty configuration.
         * @param[in] genesis_authority Validator id treated as genesis authority.
         * @param[in] block_request_method Callback used to fetch blocks by CID.
         * @param[in] init_callback Optional callback notified after initialization.
         */
        ValidatorRegistry( std::shared_ptr<crdt::GlobalDB> db,
                           uint64_t                        quorum_numerator,
                           uint64_t                        quorum_denominator,
                           WeightConfig                    weight_config,
                           std::string                     genesis_authority,
                           BlockRequestMethod              block_request_method,
                           InitCallback                    init_callback );

        /**
         * @brief Filters CRDT elements to registry-update entries.
         * @param[in] element Incoming CRDT element.
         * @return Aditional elements to be filtered out or nullopt when no other elements need to be removed.
         */
        std::optional<std::vector<crdt::pb::Element>> FilterRegistryUpdate( const crdt::pb::Element &element );
        /**
         * @brief Callback invoked when a registry update element is received.
         * @param[in] new_data New key/value data pair.
         * @param[in] cid CID associated with the update.
         */
        void RegistryUpdateReceived( const crdt::CRDTCallbackManager::NewDataPair &new_data, const std::string &cid );
        /**
         * @brief Computes canonical bytes used to sign a registry update.
         * @param[in] update Registry update payload.
         * @return Signing bytes or an error.
         */
        outcome::result<std::vector<uint8_t>> ComputeUpdateSigningBytes( const RegistryUpdate &update ) const;
        /**
         * @brief Verifies registry update signatures and consistency.
         * @param[in] update Update to verify.
         * @param[in] enforce_time_window Whether timestamp window checks are enforced.
         * @return `true` when update is valid.
         */
        bool VerifyUpdate( const RegistryUpdate &update, bool enforce_time_window ) const;
        /**
         * @brief Validates certificate against current registry constraints.
         * @param[in] certificate Certificate to validate.
         * @param[in] current_registry Current registry snapshot.
         * @return `true` when certificate is valid.
         */
        bool ValidateCertificate( const sgns::ConsensusCertificate &certificate,
                                  const Registry                   &current_registry,
                                  std::string_view                  expected_registry_cid = {} ) const;
        /**
         * @brief Validates certificate suitability for generating a registry update.
         * @param[in] certificate Certificate to validate.
         * @param[in] current_registry Current registry snapshot.
         * @return `true` when certificate can drive a registry update.
         */
        bool ValidateCertificateForUpdate( const sgns::ConsensusCertificate &certificate,
                                           const Registry                   &current_registry,
                                           std::string_view                  expected_registry_cid = {} ) const;
        /**
         * @brief Extracts registered/unregistered vote partitions from certificate.
         * @param[in] certificate Certificate to inspect.
         * @param[in] current_registry Current registry snapshot.
         * @return Partitioned vote representation.
         */
        CertificateVotes ExtractCertificateVotes( const sgns::ConsensusCertificate &certificate,
                                                  const Registry                   &current_registry ) const;
        /**
         * @brief Builds next registry snapshot using a certificate-derived vote set.
         * @param[in] current_registry Current registry snapshot.
         * @param[in] certificate Certificate being applied.
         * @param[in] registered_votes Vote decisions from registered validators.
         * @param[in] unregistered_votes Vote decisions from unregistered validators.
         * @return Derived registry snapshot.
         */
        Registry BuildRegistryFromCertificate( const Registry                              &current_registry,
                                               const sgns::ConsensusCertificate            &certificate,
                                               const std::unordered_map<std::string, bool> &registered_votes,
                                               const std::unordered_map<std::string, bool> &unregistered_votes ) const;
        /**
         * @brief Builds next registry snapshot from aggregated vote maps.
         * @param[in] current_registry Current registry snapshot.
         * @param[in] registered_votes Vote decisions from registered validators.
         * @param[in] unregistered_votes Vote decisions from unregistered validators.
         * @return Derived registry snapshot.
         */
        Registry BuildRegistryFromAggregatedVotes(
            const Registry                              &current_registry,
            const std::unordered_map<std::string, bool> &registered_votes,
            const std::unordered_map<std::string, bool> &unregistered_votes ) const;
        /**
         * @brief Inserts eligible unregistered validators into registry.
         * @param[in,out] registry Registry being updated.
         * @param[in] unregistered_votes Vote map for unregistered validators.
         */
        void InsertNewValidators( Registry                                    &registry,
                                  const std::unordered_map<std::string, bool> &unregistered_votes ) const;
        /**
         * @brief Applies registered vote effects (approvals/penalties) to entries.
         * @param[in,out] entries Validator entries to mutate.
         * @param[in] registered_votes Vote decisions from registered validators.
         */
        void ApplyVoteEffects( std::vector<ValidatorEntry>                 &entries,
                               const std::unordered_map<std::string, bool> &registered_votes ) const;
        /**
         * @brief Applies inactivity decay to validators absent from participants.
         * @param[in,out] entries Validator entries to mutate.
         * @param[in] participants Validators observed in current activity set.
         */
        void ApplyInactivityDecay( std::vector<ValidatorEntry>           &entries,
                                   const std::unordered_set<std::string> &participants ) const;
        /**
         * @brief Enforces total-weight cap across entries.
         * @param[in,out] entries Validator entries to normalize.
         */
        void ApplyTotalWeightCap( std::vector<ValidatorEntry> &entries ) const;
        /**
         * @brief Sorts and normalizes registry structure deterministically.
         * @param[in,out] registry Registry to normalize.
         */
        static void NormalizeRegistry( Registry &registry );

        /**
         * @brief Initializes local cache from persistent storage.
         */
        void InitializeCache();

        /**
         * @brief Builds map key for pending certificate subjects by base registry.
         * @param[in] base_registry_cid Base registry CID.
         * @param[in] base_registry_epoch Base registry epoch.
         * @return Composite batch key string.
         */
        inline static std::string BuildBatchKey( const std::string &base_registry_cid, uint64_t base_registry_epoch )
        {
            return fmt::format( "{}:{}", base_registry_cid, base_registry_epoch );
        }

        /**
         * @brief Computes deterministic batch root from subject hashes.
         * @param[in] subject_hashes Subject hashes included in the batch.
         * @return Batch root hash or an error.
         */
        outcome::result<std::string> ComputeBatchRoot( const std::vector<std::string> &subject_hashes ) const;
        /**
         * @brief Selects subjects eligible for a registry batch proposal.
         * @param[in] base_registry_cid Base registry CID.
         * @param[in] base_registry_epoch Base registry epoch.
         * @param[in] certificate_count Required number of certificates.
         * @param[in] expected_root Optional expected batch root constraint.
         * @return Selected subject-hash list or an error.
         */
        outcome::result<std::vector<std::string>> SelectBatchSubjects( const std::string         &base_registry_cid,
                                                                       uint64_t                   base_registry_epoch,
                                                                       uint32_t                   certificate_count,
                                                                       std::optional<std::string> expected_root ) const;
        /**
         * @brief Loads certificate referenced by subject hash.
         * @param[in] subject_hash Subject hash key.
         * @return Loaded certificate or an error.
         */
        outcome::result<sgns::ConsensusCertificate> LoadCertificateBySubjectHash(
            const std::string &subject_hash ) const;
        /**
         * @brief Attempts to create and submit a registry batch proposal.
         * @param[in] base_registry_cid Base registry CID.
         * @param[in] base_registry_epoch Base registry epoch.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> TryCreateAndSubmitBatchProposal( const std::string &base_registry_cid,
                                                               uint64_t           base_registry_epoch );
        /**
         * @brief Notifies initialization completion to callback.
         * @param[in] success Initialization result.
         */
        void NotifyInitialized( bool success ) const;
        /**
         * @brief Persists local cache metadata/state.
         * @param[in] cid Registry CID to persist locally.
         */
        void PersistLocalState( const std::string &cid ) const;
        /**
         * @brief Requests head blocks for the provided CIDs.
         * @param[in] cids Set of CIDs to request.
         */
        void RequestHeadCids( const std::set<CID> &cids );

        struct PendingRegistryWrite
        {
            std::string    subject_hash;
            RegistryUpdate update;
        };

        class ActiveBatchHandlerGuard
        {
        public:
            explicit ActiveBatchHandlerGuard( ValidatorRegistry &registry );
            ~ActiveBatchHandlerGuard();

            explicit operator bool() const
            {
                return active_;
            }

        private:
            ValidatorRegistry &registry_;
            bool               active_ = false;
        };

        void PersistenceWorkerLoop();
        bool EnqueueRegistryWrite( std::string subject_hash, RegistryUpdate update );

        std::shared_ptr<crdt::GlobalDB> db_;                 ///< Backing GlobalDB instance.
        uint64_t                        quorum_numerator_;   ///< Quorum numerator.
        uint64_t                        quorum_denominator_; ///< Quorum denominator.
        WeightConfig                    weight_config_;      ///< Weight and penalty configuration.
        std::string                     genesis_authority_;  ///< Genesis authority validator id.
        base::Logger                    logger_ = base::createLogger( "ValidatorRegistry" ); ///< Component logger.
        mutable std::shared_mutex       cache_mutex_;               ///< Guards cached registry/update state.
        std::optional<Registry>         cached_registry_;           ///< Cached active registry snapshot.
        std::optional<RegistryUpdate>   cached_update_;             ///< Cached active registry update.
        std::string                     cached_registry_id_;        ///< Cached active registry CID.
        bool                            cache_initialized_ = false; ///< Indicates whether cache has been initialized.
        size_t                          max_new_validators_per_update_ =
            DefaultMaxNewValidatorsPerUpdate;                         ///< Cap for new validators per update.
        size_t certificates_per_batch_ = DefaultCertificatesPerBatch; ///< Certificates required per batch subject.
        mutable std::mutex batch_mutex_;                              ///< Guards batch-tracking collections.
        std::unordered_map<std::string, std::set<std::string>>
            pending_certificate_subjects_by_base_;                  ///< Pending subject hashes keyed by base registry.
        std::unordered_set<std::string> pending_batch_subject_ids_; ///< Batch subject ids pending finalization.
        std::unordered_set<std::string> finalized_batch_subject_ids_; ///< Batch subject ids already finalized.
        std::unordered_set<std::string> applying_batch_subject_ids_;  ///< Batch subject ids currently being applied.
        std::function<outcome::result<void>( const ConsensusSubject &subject )>
            submit_batch_subject_; ///< Callback used to submit batch subjects.

        std::mutex                       persistence_mutex_; ///< Guards the persistence queue and shutdown state.
        std::condition_variable          persistence_cv_;    ///< Wakes the persistence worker during work/shutdown.
        std::deque<PendingRegistryWrite> persistence_queue_; ///< Registry updates waiting to be persisted.
        bool                             persistence_stopping_  = false; ///< Rejects work after Close starts.
        size_t                           active_batch_handlers_ = 0;     ///< Batch handlers still using GlobalDB.
        std::thread                      persistence_worker_;            ///< Owned registry persistence worker.
        std::mutex                       close_mutex_;                   ///< Serializes idempotent Close calls.
        bool                             close_started_ = false;         ///< Makes Close one-shot.

        InitCallback init_callback_; ///< Optional initialization callback.
        std::function<void( const std::string &cid, std::function<void( outcome::result<std::string> )> callback )>
            request_block_by_cid_; ///< Callback to request blocks by CID.
    };

}

#endif // SGNS_VALIDATOR_REGISTRY_HPP
