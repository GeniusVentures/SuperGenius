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
#include <optional>
#include <string>
#include <vector>

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"
#include "crdt/proto/delta.pb.h"
#include "outcome/outcome.hpp"
#include "crdt/globaldb/globaldb.hpp"

namespace sgns::blockchain
{
    class ValidatorRegistry : public std::enable_shared_from_this<ValidatorRegistry>
    {
    public:
        using ValidatorEntry = validator::ValidatorEntry;
        using Registry = validator::Registry;
        using SignatureEntry = validator::SignatureEntry;
        using RegistryUpdate = validator::RegistryUpdate;
        using Role = validator::Role;
        using Status = validator::Status;

        struct WeightConfig
        {
            uint64_t base_weight_       = 1;
            uint64_t full_multiplier_   = 3;
            uint64_t genesis_multiplier_ = 5;
            uint64_t sharded_multiplier_ = 1;
            uint64_t max_weight_        = 10;
        };

        static std::shared_ptr<ValidatorRegistry> New( std::shared_ptr<crdt::GlobalDB> db,
                                                       uint64_t                        quorum_numerator = 2,
                                                       uint64_t                        quorum_denominator = 3,
                                                       WeightConfig                    weight_config = {},
                                                       std::string                     genesis_authority = {} );

        uint64_t ComputeWeight( Role role ) const;
        uint64_t TotalWeight( const Registry &registry ) const;
        uint64_t QuorumThreshold( uint64_t total_weight ) const;
        bool     IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const;

        Registry CreateGenesisRegistry( const std::string &genesis_validator_id ) const;
        outcome::result<void> StoreGenesisRegistry( const std::string &genesis_validator_id,
                                                    std::function<std::vector<uint8_t>( std::vector<uint8_t> )>
                                                        sign );
        outcome::result<Registry> LoadRegistry() const;
        outcome::result<RegistryUpdate> LoadRegistryUpdate() const;
        bool RegisterFilter();

        outcome::result<std::vector<uint8_t>> SerializeRegistry( const Registry &registry ) const;
        outcome::result<Registry>             DeserializeRegistry( const std::vector<uint8_t> &buffer ) const;
        outcome::result<std::vector<uint8_t>> SerializeRegistryUpdate( const RegistryUpdate &update ) const;
        outcome::result<RegistryUpdate>       DeserializeRegistryUpdate( const std::vector<uint8_t> &buffer ) const;

        static constexpr std::string_view RegistryKey()
        {
            return "gnus-validator-registry";
        }

        static constexpr std::string_view ValidatorTopic()
        {
            return "gnus-validator-registry";
        }

    private:
        ValidatorRegistry( std::shared_ptr<crdt::GlobalDB> db,
                           uint64_t                        quorum_numerator,
                           uint64_t                        quorum_denominator,
                           WeightConfig                    weight_config,
                           std::string                     genesis_authority );

        std::optional<std::vector<crdt::pb::Element>> FilterRegistryUpdate( const crdt::pb::Element &element );
        outcome::result<std::vector<uint8_t>>         ComputeUpdateSigningBytes( const RegistryUpdate &update ) const;
        std::string                                   ComputeRegistryHash( const Registry &registry ) const;
        bool                                          VerifyUpdate( const RegistryUpdate &update,
                                                                      const Registry *current_registry ) const;
        const ValidatorEntry *FindValidator( const Registry &registry, const std::string &validator_id ) const;

        std::shared_ptr<crdt::GlobalDB> db_;
        uint64_t                        quorum_numerator_;
        uint64_t                        quorum_denominator_;
        WeightConfig                    weight_config_;
        std::string                     genesis_authority_;
        base::Logger                    logger_ = base::createLogger( "ValidatorRegistry" );
    };

}
