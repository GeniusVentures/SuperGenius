/**
 * @file       ValidatorRegistry.hpp
 * @brief      Validator registry and quorum logic for governance.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

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
#include <vector>

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
    class ValidatorRegistry : public std::enable_shared_from_this<ValidatorRegistry>
    {
    public:
        static constexpr size_t DefaultMaxNewValidatorsPerUpdate = 10;
        using ValidatorEntry                                     = validator::ValidatorEntry;
        using Registry                                           = validator::Registry;
        using SignatureEntry                                     = validator::SignatureEntry;
        using RegistryUpdate                                     = validator::RegistryUpdate;
        using Role                                               = validator::Role;
        using Status                                             = validator::Status;
        using InitCallback                                       = std::function<void( bool )>;
        using BlockRequestMethod =
            std::function<void( const std::string &, std::function<void( outcome::result<std::string> )> )>;

        struct WeightConfig
        {
            uint64_t genesis_weight_                  = 50000;
            uint64_t full_weight_                     = 1000;
            uint64_t regular_weight_                  = 1;
            uint64_t sharded_weight_                  = 1;
            uint64_t genesis_max_weight_              = 50000;
            uint64_t full_max_weight_                 = 5000;
            uint64_t regular_max_weight_              = 100;
            uint64_t sharded_max_weight_              = 100;
            uint64_t approval_increment_              = 1;
            uint32_t penalty_threshold_               = 10;
            uint32_t penalty_cap_                     = 100;
            uint32_t blacklist_bump_                  = 10;
            uint32_t missed_epoch_threshold_          = 5;
            uint32_t inactivity_decrement_            = 1;
            uint64_t total_weight_cap_multiplier_     = 4;
            uint64_t certificate_timestamp_window_ms_ = 300000;
        };

        static std::shared_ptr<ValidatorRegistry> New( std::shared_ptr<crdt::GlobalDB> db,
                                                       uint64_t                        quorum_numerator,
                                                       uint64_t                        quorum_denominator,
                                                       WeightConfig                    weight_config,
                                                       std::string                     genesis_authority,
                                                       BlockRequestMethod              block_request_method,
                                                       InitCallback                    init_callback = nullptr );
        ~ValidatorRegistry();

        uint64_t        ComputeWeight( Role role ) const;
        static uint64_t TotalWeight( const Registry &registry );
        uint64_t        QuorumThreshold( uint64_t total_weight ) const;
        bool            IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const;

        Registry                        CreateGenesisRegistry( const std::string &genesis_validator_id ) const;
        outcome::result<void>           StoreGenesisRegistry( const std::string &genesis_validator_id,
                                                              std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign );
        outcome::result<Registry>       LoadRegistry() const;
        outcome::result<Registry>       LoadRegistry( const std::string &cid ) const;
        outcome::result<RegistryUpdate> LoadRegistryUpdate() const;
        outcome::result<std::optional<uint64_t>> GetValidatorWeight( const std::string &validator_id ) const;
        bool                            RegisterFilter();
        outcome::result<RegistryUpdate> CreateUpdateFromCertificate( const sgns::ConsensusCertificate &certificate );
        outcome::result<void>           StoreRegistryUpdate( const RegistryUpdate &update );
        outcome::result<std::shared_ptr<crdt::AtomicTransaction>> BeginRegistryUpdateTransaction(
            const RegistryUpdate &update );
        void SetMaxNewValidatorsPerUpdate( size_t max_new );

        outcome::result<std::vector<uint8_t>> SerializeRegistry( const Registry &registry ) const;
        outcome::result<Registry>             DeserializeRegistry( const std::vector<uint8_t> &buffer ) const;
        outcome::result<std::vector<uint8_t>> SerializeRegistryUpdate( const RegistryUpdate &update ) const;
        outcome::result<RegistryUpdate>       DeserializeRegistryUpdate( const std::vector<uint8_t> &buffer ) const;
        std::string                           GetRegistryCid() const;
        uint64_t                              GetRegistryEpoch() const;

        static constexpr std::string_view RegistryKey()
        {
            return "gnus-validator-registry";
        }

        static constexpr std::string_view ValidatorTopic()
        {
            return "gnus-validator-registry";
        }

        static constexpr std::string_view RegistryCidKey()
        {
            return "gnus-validator-registry-cid";
        }

        static const ValidatorEntry *FindValidator( const Registry &registry, const std::string &validator_id );

    protected:
        friend class sgns::Migration3_5_0To3_6_0;

        static outcome::result<void> MigrateCids( const std::shared_ptr<crdt::GlobalDB> &old_db,
                                                  const std::shared_ptr<crdt::GlobalDB> &new_db );

    private:
        struct CertificateVotes
        {
            std::unordered_set<std::string>       approved;
            std::unordered_set<std::string>       unregistered;
            std::unordered_map<std::string, bool> registered_votes;
            std::unordered_map<std::string, bool> unregistered_votes;
        };

        ValidatorRegistry( std::shared_ptr<crdt::GlobalDB> db,
                           uint64_t                        quorum_numerator,
                           uint64_t                        quorum_denominator,
                           WeightConfig                    weight_config,
                           std::string                     genesis_authority,
                           BlockRequestMethod              block_request_method,
                           InitCallback                    init_callback );

        std::optional<std::vector<crdt::pb::Element>> FilterRegistryUpdate( const crdt::pb::Element &element );
        void RegistryUpdateReceived( const crdt::CRDTCallbackManager::NewDataPair &new_data, const std::string &cid );
        outcome::result<std::vector<uint8_t>> ComputeUpdateSigningBytes( const RegistryUpdate &update ) const;
        bool                                  VerifyUpdate( const RegistryUpdate &update,
                                                            const Registry       *current_registry,
                                                            bool                  enforce_time_window ) const;
        bool                                  ValidateCertificate( const sgns::ConsensusCertificate &certificate,
                                                                   const Registry                   &current_registry ) const;
        bool             ValidateCertificateForUpdate( const sgns::ConsensusCertificate &certificate,
                                                       const Registry                   &current_registry ) const;
        CertificateVotes ExtractCertificateVotes( const sgns::ConsensusCertificate &certificate,
                                                  const Registry                   &current_registry ) const;
        Registry         BuildRegistryFromCertificate( const Registry                              &current_registry,
                                                       const sgns::ConsensusCertificate            &certificate,
                                                       const std::unordered_map<std::string, bool> &registered_votes,
                                                       const std::unordered_map<std::string, bool> &unregistered_votes ) const;
        void             InsertNewValidators( Registry                                    &registry,
                                              const std::unordered_map<std::string, bool> &unregistered_votes ) const;
        void             ApplyVoteEffects( std::vector<ValidatorEntry>                 &entries,
                                           const std::unordered_map<std::string, bool> &registered_votes ) const;
        void             ApplyInactivityDecay( std::vector<ValidatorEntry>           &entries,
                                               const std::unordered_set<std::string> &participants ) const;
        void             ApplyTotalWeightCap( std::vector<ValidatorEntry> &entries ) const;
        static void      NormalizeRegistry( Registry &registry );

        void InitializeCache();
        void NotifyInitialized( bool success ) const;
        void PersistLocalState( const std::string &cid ) const;
        void RequestHeadCids( const std::set<CID> &cids );

        std::shared_ptr<crdt::GlobalDB> db_;
        uint64_t                        quorum_numerator_;
        uint64_t                        quorum_denominator_;
        WeightConfig                    weight_config_;
        std::string                     genesis_authority_;
        base::Logger                    logger_ = base::createLogger( "ValidatorRegistry" );
        mutable std::shared_mutex       cache_mutex_;
        std::optional<Registry>         cached_registry_;
        std::optional<RegistryUpdate>   cached_update_;
        std::string                     cached_registry_id_;
        bool                            cache_initialized_             = false;
        size_t                          max_new_validators_per_update_ = DefaultMaxNewValidatorsPerUpdate;

        InitCallback init_callback_;
        std::function<void( const std::string &cid, std::function<void( outcome::result<std::string> )> callback )>
            request_block_by_cid_;
    };

}
