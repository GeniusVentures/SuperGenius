/**
 * @file       ValidatorRegistry.cpp
 * @brief      Validator registry and quorum logic for governance.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "blockchain/ValidatorRegistry.hpp"

#include <algorithm>
#include <set>
#include <system_error>

#include <gsl/span>

#include "account/GeniusAccount.hpp"
#include "base/hexutil.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"
#include "crypto/hasher/hasher_impl.hpp"

namespace sgns::blockchain
{
    ValidatorRegistry::ValidatorRegistry( std::shared_ptr<crdt::GlobalDB> db,
                                          uint64_t                        quorum_numerator,
                                          uint64_t                        quorum_denominator,
                                          WeightConfig                    weight_config,
                                          std::string                     genesis_authority )
        : db_( std::move( db ) )
        , quorum_numerator_( quorum_numerator )
        , quorum_denominator_( quorum_denominator )
        , weight_config_( std::move( weight_config ) )
        , genesis_authority_( std::move( genesis_authority ) )
    {

    }

    std::shared_ptr<ValidatorRegistry> ValidatorRegistry::New( std::shared_ptr<crdt::GlobalDB> db,
                                                               uint64_t                        quorum_numerator,
                                                               uint64_t                        quorum_denominator,
                                                               WeightConfig                    weight_config,
                                                               std::string                     genesis_authority )
    {
        if ( !db )
        {
            return nullptr;
        }
        if ( quorum_denominator == 0 )
        {
            quorum_denominator = 1;
        }
        return std::shared_ptr<ValidatorRegistry>(
            new ValidatorRegistry( std::move( db ),
                                    quorum_numerator,
                                    quorum_denominator,
                                    std::move( weight_config ),
                                    std::move( genesis_authority ) ) );
    }

    uint64_t ValidatorRegistry::ComputeWeight( Role role ) const
    {
        const uint64_t base_weight = weight_config_.base_weight_;
        uint64_t       multiplier  = 1;

        switch ( role )
        {
            case Role::GENESIS:
                multiplier = weight_config_.genesis_multiplier_;
                break;
            case Role::FULL:
                multiplier = weight_config_.full_multiplier_;
                break;
            case Role::SHARDED:
                multiplier = weight_config_.sharded_multiplier_;
                break;
            case Role::REGULAR:
            default:
                multiplier = 1;
                break;
        }

        if ( multiplier == 0 )
        {
            return 0;
        }

        if ( base_weight > weight_config_.max_weight_ / multiplier )
        {
            return weight_config_.max_weight_;
        }

        const uint64_t weighted = base_weight * multiplier;
        return std::min( weighted, weight_config_.max_weight_ );
    }

    uint64_t ValidatorRegistry::TotalWeight( const Registry &registry ) const
    {
        uint64_t total_weight = 0;
        for ( const auto &entry : registry.validators() )
        {
            if ( entry.status() != Status::ACTIVE )
            {
                continue;
            }
            total_weight += entry.weight();
        }
        return total_weight;
    }

    uint64_t ValidatorRegistry::QuorumThreshold( uint64_t total_weight ) const
    {
        if ( total_weight == 0 )
        {
            return 0;
        }
        const uint64_t numerator = total_weight * quorum_numerator_;
        return ( numerator + quorum_denominator_ - 1 ) / quorum_denominator_;
    }

    bool ValidatorRegistry::IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const
    {
        return accumulated_weight >= QuorumThreshold( total_weight );
    }

    ValidatorRegistry::Registry ValidatorRegistry::CreateGenesisRegistry(
        const std::string &genesis_validator_id ) const
    {
        Registry registry;
        registry.set_epoch( 0 );
        auto *entry = registry.add_validators();
        entry->set_validator_id( genesis_validator_id );
        entry->set_role( Role::GENESIS );
        entry->set_status( Status::ACTIVE );
        entry->set_weight( ComputeWeight( entry->role() ) );
        return registry;
    }

    outcome::result<std::vector<uint8_t>> ValidatorRegistry::SerializeRegistry( const Registry &registry ) const
    {
        std::string serialized;
        if ( !registry.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::DeserializeRegistry(
        const std::vector<uint8_t> &buffer ) const
    {
        Registry proto;
        if ( !proto.ParseFromArray( buffer.data(), static_cast<int>( buffer.size() ) ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return proto;
    }

    outcome::result<std::vector<uint8_t>> ValidatorRegistry::SerializeRegistryUpdate( const RegistryUpdate &update ) const
    {
        std::string serialized;
        if ( !update.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<ValidatorRegistry::RegistryUpdate> ValidatorRegistry::DeserializeRegistryUpdate(
        const std::vector<uint8_t> &buffer ) const
    {
        RegistryUpdate proto;
        if ( !proto.ParseFromArray( buffer.data(), static_cast<int>( buffer.size() ) ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return proto;
    }

    outcome::result<void> ValidatorRegistry::StoreGenesisRegistry(
        const std::string &genesis_validator_id,
        std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign )
    {
        const crdt::HierarchicalKey registry_key( std::string( RegistryKey() ) );
        const auto                  registry_get = db_->Get( registry_key );
        bool                        registry_empty = true;

        if ( registry_get.has_value() )
        {
            const auto &buffer = registry_get.value();
            std::vector<uint8_t> bytes( buffer.data(), buffer.data() + buffer.size() );
            auto decoded_update = DeserializeRegistryUpdate( bytes );
            if ( decoded_update.has_value() )
            {
                registry_empty = decoded_update.value().registry().validators().empty();
            }
        }

        if ( !registry_empty )
        {
            return outcome::success();
        }

        if ( !sign )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        RegistryUpdate update;
        *update.mutable_registry() = CreateGenesisRegistry( genesis_validator_id );
        update.clear_prev_registry_hash();

        auto signing_bytes = ComputeUpdateSigningBytes( update );
        if ( signing_bytes.has_error() )
        {
            return outcome::failure( signing_bytes.error() );
        }

        SignatureEntry signature_entry;
        signature_entry.set_validator_id( genesis_validator_id );
        auto signature = sign( signing_bytes.value() );
        signature_entry.set_signature( signature.data(), signature.size() );
        *update.add_signatures() = signature_entry;

        auto serialized_update = SerializeRegistryUpdate( update );
        if ( serialized_update.has_error() )
        {
            return outcome::failure( serialized_update.error() );
        }

        base::Buffer update_buffer( gsl::span<const uint8_t>(
            serialized_update.value().data(),
            serialized_update.value().size() ) );

        auto registry_put = db_->Put( registry_key,
                                      update_buffer,
                                      { std::string( ValidatorTopic() ) } );
        if ( registry_put.has_error() )
        {
            return outcome::failure( registry_put.error() );
        }

        return outcome::success();
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::LoadRegistry() const
    {
        auto update_result = LoadRegistryUpdate();
        if ( update_result.has_error() )
        {
            return outcome::failure( update_result.error() );
        }
        return update_result.value().registry();
    }

    outcome::result<ValidatorRegistry::RegistryUpdate> ValidatorRegistry::LoadRegistryUpdate() const
    {
        const crdt::HierarchicalKey registry_key( std::string( RegistryKey() ) );
        auto                        registry_get = db_->Get( registry_key );
        if ( registry_get.has_error() )
        {
            return outcome::failure( registry_get.error() );
        }

        const auto &buffer = registry_get.value();
        std::vector<uint8_t> bytes( buffer.data(), buffer.data() + buffer.size() );
        return DeserializeRegistryUpdate( bytes );
    }

    bool ValidatorRegistry::RegisterFilter()
    {
        const std::string pattern = "/?" + std::string( RegistryKey() );
        auto              weak_self = weak_from_this();
        return db_->RegisterElementFilter(
            pattern,
            [weak_self]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_self.lock() )
                {
                    return strong->FilterRegistryUpdate( element );
                }
                return std::nullopt;
            } );
    }

    std::optional<std::vector<crdt::pb::Element>> ValidatorRegistry::FilterRegistryUpdate(
        const crdt::pb::Element &element )
    {
        std::vector<uint8_t> bytes( element.value().begin(), element.value().end() );
        auto decoded_update = DeserializeRegistryUpdate( bytes );
        if ( decoded_update.has_error() )
        {
            logger_->warn( "Failed to parse validator registry update, rejecting: {}", element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        RegistryUpdate update = decoded_update.value();
        std::optional<Registry> current_registry;
        if ( db_ )
        {
            const crdt::HierarchicalKey registry_key( std::string( RegistryKey() ) );
            auto                        registry_get = db_->Get( registry_key );
            if ( registry_get.has_value() )
            {
                const auto &buffer = registry_get.value();
                std::vector<uint8_t> stored_bytes( buffer.data(), buffer.data() + buffer.size() );
                auto stored_update = DeserializeRegistryUpdate( stored_bytes );
                if ( stored_update.has_value() )
                {
                    current_registry = stored_update.value().registry();
                }
            }
        }

        const Registry *current_ptr = current_registry ? &current_registry.value() : nullptr;
        if ( !VerifyUpdate( update, current_ptr ) )
        {
            logger_->warn( "Validator registry update failed verification, rejecting: {}", element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        return std::nullopt;
    }

    outcome::result<std::vector<uint8_t>> ValidatorRegistry::ComputeUpdateSigningBytes(
        const RegistryUpdate &update ) const
    {
        sgns::blockchain::validator::RegistrySigningPayload payload;
        *payload.mutable_registry() = update.registry();
        payload.set_prev_registry_hash( update.prev_registry_hash() );

        std::string serialized;
        if ( !payload.SerializeToString( &serialized ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    std::string ValidatorRegistry::ComputeRegistryHash( const Registry &registry ) const
    {
        auto encoded = SerializeRegistry( registry );
        if ( encoded.has_error() )
        {
            return {};
        }

        sgns::crypto::HasherImpl hasher;
        auto hash = hasher.sha2_256( gsl::span<const uint8_t>( encoded.value().data(),
                                                              encoded.value().size() ) );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    bool ValidatorRegistry::VerifyUpdate( const RegistryUpdate &update, const Registry *current_registry ) const
    {
        if ( update.registry().validators().empty() )
        {
            return false;
        }

        auto signing_bytes = ComputeUpdateSigningBytes( update );
        if ( signing_bytes.has_error() )
        {
            return false;
        }

        if ( !current_registry )
        {
            if ( update.prev_registry_hash().empty() && !genesis_authority_.empty() )
            {
                for ( const auto &signature : update.signatures() )
                {
                    if ( signature.validator_id() != genesis_authority_ )
                    {
                        continue;
                    }
                    if ( GeniusAccount::VerifySignature( signature.validator_id(),
                                                         signature.signature(),
                                                         signing_bytes.value() ) )
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        const std::string current_hash = ComputeRegistryHash( *current_registry );
        if ( current_hash.empty() || update.prev_registry_hash() != current_hash )
        {
            return false;
        }

        if ( update.registry().epoch() <= current_registry->epoch() )
        {
            return false;
        }

        uint64_t total_weight = TotalWeight( *current_registry );
        uint64_t accumulated_weight = 0;
        std::set<std::string> seen;

        for ( const auto &signature : update.signatures() )
        {
            if ( !seen.insert( signature.validator_id() ).second )
            {
                continue;
            }

            const auto *validator = FindValidator( *current_registry, signature.validator_id() );
            if ( !validator || validator->status() != Status::ACTIVE )
            {
                continue;
            }

            if ( !GeniusAccount::VerifySignature( signature.validator_id(),
                                                  signature.signature(),
                                                  signing_bytes.value() ) )
            {
                continue;
            }

            accumulated_weight += validator->weight();
            if ( IsQuorum( accumulated_weight, total_weight ) )
            {
                return true;
            }
        }

        return false;
    }

    const ValidatorRegistry::ValidatorEntry *ValidatorRegistry::FindValidator(
        const Registry &registry,
        const std::string &validator_id ) const
    {
        for ( const auto &validator : registry.validators() )
        {
            if ( validator.validator_id() == validator_id )
            {
                return &validator;
            }
        }
        return nullptr;
    }
}
