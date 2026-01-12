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
#include <string>
#include <vector>

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"
#include "crdt/crdt_callback_manager.hpp"
#include "crdt/proto/delta.pb.h"
#include "outcome/outcome.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "primitives/cid/cid.hpp"

namespace sgns::blockchain
{
    class ValidatorRegistry : public std::enable_shared_from_this<ValidatorRegistry>
    {
    public:
        using ValidatorEntry = validator::ValidatorEntry;
        using Registry       = validator::Registry;
        using SignatureEntry = validator::SignatureEntry;
        using RegistryUpdate = validator::RegistryUpdate;
        using Role           = validator::Role;
        using Status         = validator::Status;
        using InitCallback   = std::function<void( bool )>;
        using BlockRequestMethod =
            std::function<void( const std::string &, std::function<void( outcome::result<std::string> )> )>;

        struct WeightConfig
        {
            uint64_t base_weight_        = 1;
            uint64_t full_multiplier_    = 3;
            uint64_t genesis_multiplier_ = 5;
            uint64_t sharded_multiplier_ = 1;
            uint64_t max_weight_         = 10;
        };

        static std::shared_ptr<ValidatorRegistry> New( std::shared_ptr<crdt::GlobalDB> db,
                                                       uint64_t                        quorum_numerator   = 2,
                                                       uint64_t                        quorum_denominator = 3,
                                                       WeightConfig                    weight_config      = {},
                                                       std::string                     genesis_authority,
                                                       InitCallback                    init_callback = nullptr,
                                                       BlockRequestMethod              block_request_method );

        uint64_t ComputeWeight( Role role ) const;
        uint64_t TotalWeight( const Registry &registry ) const;
        uint64_t QuorumThreshold( uint64_t total_weight ) const;
        bool     IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const;

        Registry                        CreateGenesisRegistry( const std::string &genesis_validator_id ) const;
        outcome::result<void>           StoreGenesisRegistry( const std::string &genesis_validator_id,
                                                              std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign );
        outcome::result<Registry>       LoadRegistry() const;
        outcome::result<RegistryUpdate> LoadRegistryUpdate() const;
        bool                            RegisterFilter();

        outcome::result<std::vector<uint8_t>> SerializeRegistry( const Registry &registry ) const;
        outcome::result<Registry>             DeserializeRegistry( const std::vector<uint8_t> &buffer ) const;
        outcome::result<std::vector<uint8_t>> SerializeRegistryUpdate( const RegistryUpdate &update ) const;
        outcome::result<RegistryUpdate>       DeserializeRegistryUpdate( const std::vector<uint8_t> &buffer ) const;

        void SetRequestBlockByCidMethod(
            std::function<void( const std::string &cid, std::function<void( outcome::result<std::string> )> callback )>
                method );

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

    private:
        ValidatorRegistry( std::shared_ptr<crdt::GlobalDB> db,
                           uint64_t                        quorum_numerator,
                           uint64_t                        quorum_denominator,
                           WeightConfig                    weight_config,
                           std::string                     genesis_authority,
                           InitCallback                    init_callback,
                           BlockRequestMethod              block_request_method );

        std::optional<std::vector<crdt::pb::Element>> FilterRegistryUpdate( const crdt::pb::Element &element );
        void RegistryUpdateReceived( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid );
        outcome::result<std::vector<uint8_t>> ComputeUpdateSigningBytes( const RegistryUpdate &update ) const;
        bool                  VerifyUpdate( const RegistryUpdate &update, const Registry *current_registry ) const;
        const ValidatorEntry *FindValidator( const Registry &registry, const std::string &validator_id ) const;
        void                  InitializeCache();
        void                  NotifyInitialized( bool success );
        void                  PersistLocalState( const std::string &cid );
        void                  RequestHeadCids( const std::set<CID> &cids );

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
        bool                            cache_initialized_ = false;

        InitCallback init_callback_;
        std::function<void( const std::string &cid, std::function<void( outcome::result<std::string> )> callback )>
            request_block_by_cid_;
    };

}
