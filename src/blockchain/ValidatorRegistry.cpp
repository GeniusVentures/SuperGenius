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

#include "account/GeniusAccount.hpp"
#include "base/hexutil.hpp"
#include "crypto/hasher/hasher_impl.hpp"
#include "scale/scale.hpp"

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
        if ( quorum_denominator_ == 0 )
        {
            quorum_denominator_ = 1;
        }
    }

    uint64_t ValidatorRegistry::ComputeWeight( Role role ) const
    {
        const uint64_t base_weight = weight_config_.base_weight_;
        uint64_t       multiplier  = 1;

        switch ( role )
        {
            case Role::Genesis:
                multiplier = weight_config_.genesis_multiplier_;
                break;
            case Role::Full:
                multiplier = weight_config_.full_multiplier_;
                break;
            case Role::Sharded:
                multiplier = weight_config_.sharded_multiplier_;
                break;
            case Role::Regular:
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
        for ( const auto &entry : registry.validators_ )
        {
            if ( entry.status_ != Status::Active )
            {
                continue;
            }
            total_weight += entry.weight_;
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
        ValidatorEntry entry;
        entry.validator_id_ = genesis_validator_id;
        entry.role_          = Role::Genesis;
        entry.status_        = Status::Active;
        entry.weight_        = ComputeWeight( entry.role_ );

        registry.validators_.push_back( std::move( entry ) );
        return registry;
    }

    outcome::result<base::Buffer> ValidatorRegistry::SerializeRegistry( const Registry &registry ) const
    {
        auto encoded = scale::encode( registry );
        if ( encoded.has_error() )
        {
            return outcome::failure( encoded.error() );
        }
        return base::Buffer( std::move( encoded.value() ) );
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::DeserializeRegistry(
        const base::Buffer &buffer ) const
    {
        auto decoded = scale::decode<Registry>( gsl::span<const uint8_t>( buffer.data(), buffer.size() ) );
        if ( decoded.has_error() )
        {
            return outcome::failure( decoded.error() );
        }
        return decoded.value();
    }

    outcome::result<base::Buffer> ValidatorRegistry::SerializeRegistryUpdate( const RegistryUpdate &update ) const
    {
        auto encoded = scale::encode( update );
        if ( encoded.has_error() )
        {
            return outcome::failure( encoded.error() );
        }
        return base::Buffer( std::move( encoded.value() ) );
    }

    outcome::result<ValidatorRegistry::RegistryUpdate> ValidatorRegistry::DeserializeRegistryUpdate(
        const base::Buffer &buffer ) const
    {
        auto decoded = scale::decode<RegistryUpdate>( gsl::span<const uint8_t>( buffer.data(), buffer.size() ) );
        if ( decoded.has_error() )
        {
            return outcome::failure( decoded.error() );
        }
        return decoded.value();
    }

    outcome::result<void> ValidatorRegistry::StoreGenesisRegistry(
        const std::string &genesis_validator_id,
        std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign )
    {
        if ( !db_ )
        {
            return outcome::failure( std::errc::bad_address );
        }

        const crdt::HierarchicalKey registry_key( std::string( RegistryKey() ) );
        const auto                  registry_get = db_->Get( registry_key );
        bool                        registry_empty = true;

        if ( registry_get.has_value() )
        {
            auto decoded_update = DeserializeRegistryUpdate( registry_get.value() );
            if ( decoded_update.has_value() )
            {
                registry_empty = decoded_update.value().registry_.validators_.empty();
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
        update.registry_ = CreateGenesisRegistry( genesis_validator_id );
        update.prev_registry_hash_.clear();

        auto signing_bytes = ComputeUpdateSigningBytes( update );
        if ( signing_bytes.has_error() )
        {
            return outcome::failure( signing_bytes.error() );
        }

        SignatureEntry signature_entry;
        signature_entry.validator_id_ = genesis_validator_id;
        auto signature = sign( signing_bytes.value() );
        signature_entry.signature_ = std::string( signature.begin(), signature.end() );
        update.signatures_.push_back( std::move( signature_entry ) );

        auto serialized_update = SerializeRegistryUpdate( update );
        if ( serialized_update.has_error() )
        {
            return outcome::failure( serialized_update.error() );
        }

        auto registry_put = db_->Put( registry_key,
                                      serialized_update.value(),
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
        return update_result.value().registry_;
    }

    outcome::result<ValidatorRegistry::RegistryUpdate> ValidatorRegistry::LoadRegistryUpdate() const
    {
        if ( !db_ )
        {
            return outcome::failure( std::errc::bad_address );
        }

        const crdt::HierarchicalKey registry_key( std::string( RegistryKey() ) );
        auto                        registry_get = db_->Get( registry_key );
        if ( registry_get.has_error() )
        {
            return outcome::failure( registry_get.error() );
        }

        return DeserializeRegistryUpdate( registry_get.value() );
    }

    bool ValidatorRegistry::RegisterFilter()
    {
        if ( !db_ )
        {
            return false;
        }

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
        auto decoded_update = DeserializeRegistryUpdate( base::Buffer{
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( element.value().data() ),
                                      element.value().size() ) } );
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
                auto stored_update = DeserializeRegistryUpdate( registry_get.value() );
                if ( stored_update.has_value() )
                {
                    current_registry = stored_update.value().registry_;
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
        return scale::encode( update.prev_registry_hash_, update.registry_ );
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
        if ( update.registry_.validators_.empty() )
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
            if ( update.prev_registry_hash_.empty() && !genesis_authority_.empty() )
            {
                for ( const auto &signature : update.signatures_ )
                {
                    if ( signature.validator_id_ != genesis_authority_ )
                    {
                        continue;
                    }
                    if ( GeniusAccount::VerifySignature( signature.validator_id_,
                                                         signature.signature_,
                                                         signing_bytes.value() ) )
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        const std::string current_hash = ComputeRegistryHash( *current_registry );
        if ( current_hash.empty() || update.prev_registry_hash_ != current_hash )
        {
            return false;
        }

        if ( update.registry_.epoch_ <= current_registry->epoch_ )
        {
            return false;
        }

        uint64_t total_weight = TotalWeight( *current_registry );
        uint64_t accumulated_weight = 0;
        std::set<std::string> seen;

        for ( const auto &signature : update.signatures_ )
        {
            if ( !seen.insert( signature.validator_id_ ).second )
            {
                continue;
            }

            const auto *validator = FindValidator( *current_registry, signature.validator_id_ );
            if ( !validator || validator->status_ != Status::Active )
            {
                continue;
            }

            if ( !GeniusAccount::VerifySignature( signature.validator_id_,
                                                  signature.signature_,
                                                  signing_bytes.value() ) )
            {
                continue;
            }

            accumulated_weight += validator->weight_;
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
        for ( const auto &validator : registry.validators_ )
        {
            if ( validator.validator_id_ == validator_id )
            {
                return &validator;
            }
        }
        return nullptr;
    }
}
