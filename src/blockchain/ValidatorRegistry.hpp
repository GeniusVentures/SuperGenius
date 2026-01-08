/**
 * @file       ValidatorRegistry.hpp
 * @brief      Validator registry and quorum logic for governance.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <gsl/span>

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "crdt/proto/delta.pb.h"
#include "outcome/outcome.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "scale/scale_decoder_stream.hpp"
#include "scale/scale_encoder_stream.hpp"

namespace sgns::blockchain
{
    class ValidatorRegistry : public std::enable_shared_from_this<ValidatorRegistry>
    {
    public:
        enum class Role : uint8_t
        {
            Genesis = 0,
            Full    = 1,
            Regular = 2,
            Sharded = 3
        };

        enum class Status : uint8_t
        {
            Active      = 0,
            Suspended   = 1,
            Blacklisted = 2
        };

        struct ValidatorEntry
        {
            std::string validator_id_;
            uint64_t    weight_ = 0;
            Role        role_   = Role::Regular;
            Status      status_ = Status::Active;
        };

        struct Registry
        {
            uint64_t                  epoch_ = 0;
            std::vector<ValidatorEntry> validators_;
        };

        struct SignatureEntry
        {
            std::string validator_id_;
            std::string signature_;
        };

        struct RegistryUpdate
        {
            Registry                  registry_;
            std::string               prev_registry_hash_;
            std::vector<SignatureEntry> signatures_;
        };

        struct WeightConfig
        {
            uint64_t base_weight_       = 1;
            uint64_t full_multiplier_   = 3;
            uint64_t genesis_multiplier_ = 5;
            uint64_t sharded_multiplier_ = 1;
            uint64_t max_weight_        = 10;
        };

        ValidatorRegistry( std::shared_ptr<crdt::GlobalDB> db,
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

        outcome::result<base::Buffer> SerializeRegistry( const Registry &registry ) const;
        outcome::result<Registry>     DeserializeRegistry( const base::Buffer &buffer ) const;
        outcome::result<base::Buffer> SerializeRegistryUpdate( const RegistryUpdate &update ) const;
        outcome::result<RegistryUpdate> DeserializeRegistryUpdate( const base::Buffer &buffer ) const;

        static constexpr std::string_view RegistryKey()
        {
            return "gnus-validator-registry";
        }

        static constexpr std::string_view ValidatorTopic()
        {
            return "gnus-validator-registry";
        }

    private:
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

    template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
    Stream &operator<<( Stream &s, const ValidatorRegistry::SignatureEntry &entry )
    {
        return s << entry.validator_id_ << entry.signature_;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
    Stream &operator>>( Stream &s, ValidatorRegistry::SignatureEntry &entry )
    {
        return s >> entry.validator_id_ >> entry.signature_;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
    Stream &operator<<( Stream &s, const ValidatorRegistry::ValidatorEntry &entry )
    {
        return s << entry.validator_id_ << entry.weight_ << static_cast<uint8_t>( entry.role_ )
                 << static_cast<uint8_t>( entry.status_ );
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
    Stream &operator>>( Stream &s, ValidatorRegistry::ValidatorEntry &entry )
    {
        uint8_t role = 0;
        uint8_t status = 0;
        s >> entry.validator_id_ >> entry.weight_ >> role >> status;
        entry.role_   = static_cast<ValidatorRegistry::Role>( role );
        entry.status_ = static_cast<ValidatorRegistry::Status>( status );
        return s;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
    Stream &operator<<( Stream &s, const ValidatorRegistry::Registry &registry )
    {
        return s << registry.epoch_ << registry.validators_;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
    Stream &operator>>( Stream &s, ValidatorRegistry::Registry &registry )
    {
        return s >> registry.epoch_ >> registry.validators_;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_encoder_stream>>
    Stream &operator<<( Stream &s, const ValidatorRegistry::RegistryUpdate &update )
    {
        return s << update.registry_ << update.prev_registry_hash_ << update.signatures_;
    }

    template <class Stream, typename = std::enable_if_t<Stream::is_decoder_stream>>
    Stream &operator>>( Stream &s, ValidatorRegistry::RegistryUpdate &update )
    {
        return s >> update.registry_ >> update.prev_registry_hash_ >> update.signatures_;
    }
}
