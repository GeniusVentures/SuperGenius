/**
 * @file       ValidatorRegistry.hpp
 * @brief      Validator registry and quorum logic for governance.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#ifndef SGNS_VALIDATOR_REGISTRY_HPP
#define SGNS_VALIDATOR_REGISTRY_HPP

#include <cstdint>
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
     * This component stores the active validator set in GlobalDB/CRDT, computes
     * quorum thresholds, validates registry updates, and derives next registry
     * snapshots from consensus certificates.
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
         * @brief Creates an in-memory genesis registry snapshot.
         * @param[in] genesis_validator_id Validator id for the genesis authority.
         * @return Genesis registry snapshot.
         */
        Registry CreateGenesisRegistry( const std::string &genesis_validator_id ) const;
        /**
         * @brief Persists a signed genesis registry update.
         * @param[in] genesis_validator_id Validator id for the genesis authority.
         * @param[in] sign Signing callback used for registry-update signatures.
         * @return outcome::success on success, otherwise an error.
         */
        outcome::result<void> StoreGenesisRegistry( const std::string &genesis_validator_id,
                                                    std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign );
        /**
         * @brief Loads the currently active registry.
         * @return Registry snapshot or an error.
         */
        outcome::result<Registry> LoadCurrentRegistry() const;
        /**
         * @brief Loads a registry by CID.
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
         * @brief Starts an atomic transaction to apply a registry update.
         * @param[in] update Registry update being applied.
         * @return Transaction handle or an error.
         */
        outcome::result<std::shared_ptr<crdt::AtomicTransaction>> BeginRegistryUpdateTransaction(
            const RegistryUpdate &update );
        /**
         * @brief Sets the maximum number of unregistered validators added per update.
         * @param[in] max_new New cap value.
         */
        void SetMaxNewValidatorsPerUpdate( size_t max_new );

        /**
         * @brief Serializes a registry protobuf.
         * @param[in] registry Registry to serialize.
         * @return Serialized bytes or an error.
         */
        outcome::result<std::vector<uint8_t>> SerializeRegistry( const Registry &registry ) const;
        /**
         * @brief Deserializes a registry protobuf.
         * @param[in] buffer Serialized registry bytes.
         * @return Parsed registry or an error.
         */
        outcome::result<Registry> DeserializeRegistry( const std::vector<uint8_t> &buffer ) const;
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

        InitCallback init_callback_; ///< Optional initialization callback.
        std::function<void( const std::string &cid, std::function<void( outcome::result<std::string> )> callback )>
            request_block_by_cid_; ///< Callback to request blocks by CID.
    };

}

#endif // SGNS_VALIDATOR_REGISTRY_HPP
