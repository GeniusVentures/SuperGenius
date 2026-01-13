/**
 * @file       ValidatorRegistry.cpp
 * @brief      Validator registry and quorum logic for governance.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "blockchain/ValidatorRegistry.hpp"

#include <algorithm>
#include <atomic>
#include <set>
#include <system_error>

#include <gsl/span>

#include "account/GeniusAccount.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"

namespace sgns::blockchain
{
    ValidatorRegistry::ValidatorRegistry( std::shared_ptr<crdt::GlobalDB> db,
                                          uint64_t                        quorum_numerator,
                                          uint64_t                        quorum_denominator,
                                          WeightConfig                    weight_config,
                                          std::string                     genesis_authority,
                                          InitCallback                    init_callback,
                                          BlockRequestMethod              block_request_method ) :
        db_( std::move( db ) ),
        quorum_numerator_( quorum_numerator ),
        quorum_denominator_( quorum_denominator ),
        weight_config_( std::move( weight_config ) ),
        genesis_authority_( std::move( genesis_authority ) ),
        init_callback_( std::move( init_callback ) ),
        request_block_by_cid_( std::move( block_request_method ) )
    {
    }

    std::shared_ptr<ValidatorRegistry> ValidatorRegistry::New( std::shared_ptr<crdt::GlobalDB> db,
                                                               uint64_t                        quorum_numerator,
                                                               uint64_t                        quorum_denominator,
                                                               WeightConfig                    weight_config,
                                                               std::string                     genesis_authority,
                                                               InitCallback                    init_callback,
                                                               BlockRequestMethod              block_request_method )
    {
        if ( !db )
        {
            return nullptr;
        }
        if ( genesis_authority.empty() )
        {
            return nullptr;
        }
        if ( block_request_method == nullptr )
        {
            return nullptr;
        }
        if ( quorum_denominator == 0 )
        {
            quorum_denominator = 1;
        }
        auto instance = std::shared_ptr<ValidatorRegistry>( new ValidatorRegistry( std::move( db ),
                                                                                   quorum_numerator,
                                                                                   quorum_denominator,
                                                                                   std::move( weight_config ),
                                                                                   std::move( genesis_authority ) ) );
        instance->InitializeCache();

        if ( !RegisterFilter() )
        {
            return nullptr;
        }

        return instance;
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

    outcome::result<std::vector<uint8_t>> ValidatorRegistry::SerializeRegistryUpdate(
        const RegistryUpdate &update ) const
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
        const std::string                                          &genesis_validator_id,
        std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign )
    {
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            if ( cache_initialized_ && cached_registry_ && !cached_registry_->validators().empty() )
            {
                return outcome::success();
            }
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

        base::Buffer update_buffer(
            gsl::span<const uint8_t>( serialized_update.value().data(), serialized_update.value().size() ) );

        const crdt::HierarchicalKey registry_key( std::string( RegistryKey() ) );

        auto registry_put = db_->Put( registry_key, update_buffer, { std::string( ValidatorTopic() ) } );
        if ( registry_put.has_error() )
        {
            return outcome::failure( registry_put.error() );
        }

        return outcome::success();
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::LoadRegistry() const
    {
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            if ( cached_registry_ )
            {
                return cached_registry_.value();
            }
        }

        auto update_result = LoadRegistryUpdate();
        if ( update_result.has_error() )
        {
            return outcome::failure( update_result.error() );
        }
        return update_result.value().registry();
    }

    outcome::result<ValidatorRegistry::RegistryUpdate> ValidatorRegistry::LoadRegistryUpdate() const
    {
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            if ( cached_update_ )
            {
                return cached_update_.value();
            }
        }

        return outcome::failure( std::errc::no_such_file_or_directory );
    }

    bool ValidatorRegistry::RegisterFilter()
    {
        const std::string pattern           = "/?" + std::string( RegistryKey() );
        auto              weak_self         = weak_from_this();
        const bool        filter_registered = db_->RegisterElementFilter(
            pattern,
            [weak_self]( const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_self.lock() )
                {
                    return strong->FilterRegistryUpdate( element );
                }
                return std::nullopt;
            } );
        const bool callback_registered = db_->RegisterNewElementCallback(
            pattern,
            [weak_self]( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
            {
                if ( auto strong = weak_self.lock() )
                {
                    strong->RegistryUpdateReceived( std::move( new_data ), cid );
                }
            } );

        db_->AddListenTopic( std::string( ValidatorTopic() ) );

        return filter_registered && callback_registered;
    }

    std::optional<std::vector<crdt::pb::Element>> ValidatorRegistry::FilterRegistryUpdate(
        const crdt::pb::Element &element )
    {
        std::vector<uint8_t> bytes( element.value().begin(), element.value().end() );
        auto                 decoded_update = DeserializeRegistryUpdate( bytes );
        if ( decoded_update.has_error() )
        {
            logger_->warn( "Failed to parse validator registry update, rejecting: {}", element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        RegistryUpdate  update      = decoded_update.value();
        const Registry *current_ptr = nullptr;

        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            if ( cached_registry_ )
            {
                current_ptr = &cached_registry_.value();
            }
        }

        if ( !VerifyUpdate( update, current_ptr ) )
        {
            logger_->warn( "Validator registry update failed verification, rejecting: {}", element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        return std::nullopt;
    }

    void ValidatorRegistry::RegistryUpdateReceived( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                    const std::string                     &cid )
    {
        const auto          &buffer = new_data.second;
        std::vector<uint8_t> bytes( buffer.data(), buffer.data() + buffer.size() );
        auto                 decoded = DeserializeRegistryUpdate( bytes );
        if ( decoded.has_error() )
        {
            logger_->error( "Failed to parse registry update for cache refresh" );
            return;
        }

        {
            std::unique_lock<std::shared_mutex> lock( cache_mutex_ );
            cached_update_      = decoded.value();
            cached_registry_    = decoded.value().registry();
            cached_registry_id_ = cid;
            cache_initialized_  = true;
        }

        PersistLocalState( cid );
        NotifyInitialized( true );
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
            if ( update.prev_registry_hash().empty() )
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

        const std::string prev_registry_cid = update.prev_registry_hash();
        std::string       current_id;
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            current_id = cached_registry_id_;
        }
        if ( current_id.empty() || prev_registry_cid != current_id )
        {
            return false;
        }

        if ( update.registry().epoch() <= current_registry->epoch() )
        {
            return false;
        }

        uint64_t              total_weight       = TotalWeight( *current_registry );
        uint64_t              accumulated_weight = 0;
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

    const ValidatorRegistry::ValidatorEntry *ValidatorRegistry::FindValidator( const Registry    &registry,
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

    void ValidatorRegistry::InitializeCache()
    {
        std::unique_lock<std::shared_mutex> lock( cache_mutex_ );
        if ( cache_initialized_ )
        {
            return;
        }

        const crdt::HierarchicalKey registry_key( std::string( RegistryKey() ) );
        auto                        registry_get    = db_->Get( registry_key );
        bool                        content_present = registry_get.has_value();
        if ( !content_present )
        {
            logger_->warn( "Registry content not found during cache init" );
            return;
        }
        const auto          &buffer = registry_get.value();
        std::vector<uint8_t> bytes( buffer.data(), buffer.data() + buffer.size() );
        auto                 decoded = DeserializeRegistryUpdate( bytes );
        if ( !decoded.has_value() )
        {
            logger_->warn( "Failed to parse registry content during cache init" );
            return;
        }

        cached_update_   = decoded.value();
        cached_registry_ = decoded.value().registry();

        cache_initialized_ = true;

        sgns::crdt::GlobalDB::Buffer registry_cid_key;
        registry_cid_key.put( std::string( RegistryCidKey() ) );
        auto registry_cid = db_->GetDataStore()->get( registry_cid_key );
        if ( registry_cid.has_value() )
        {
            cached_registry_id_ = registry_cid.value().toString();
            NotifyInitialized( true );
            return;
        }

        std::set<CID> heads_to_request;

        logger_->warn( "Registry content found, but CID is missing; requesting heads" );

        auto heads_result = db_->GetCRDTHeadList();
        if ( heads_result.has_value() )
        {
            const auto &heads_map = heads_result.value().first;
            auto        it        = heads_map.find( std::string( ValidatorTopic() ) );
            if ( it != heads_map.end() )
            {
                heads_to_request = it->second;
            }
        }

        lock.unlock();

        if ( !heads_to_request.empty() )
        {
            RequestHeadCids( heads_to_request );
        }
    }

    void ValidatorRegistry::PersistLocalState( const std::string &cid )
    {
        sgns::crdt::GlobalDB::Buffer registry_cid_key;
        registry_cid_key.put( std::string( RegistryCidKey() ) );
        sgns::crdt::GlobalDB::Buffer registry_cid;
        registry_cid.put( cid );
        (void)db_->GetDataStore()->put( registry_cid_key, registry_cid );
    }

    void ValidatorRegistry::RequestHeadCids( const std::set<CID> &cids )
    {
        if ( cids.empty() )
        {
            return;
        }

        struct RequestState
        {
            std::atomic<size_t> remaining;
            std::atomic<bool>   success_reported{ false };
        };

        auto state = std::make_shared<RequestState>( RequestState{ cids.size() } );

        for ( const auto &cid : cids )
        {
            auto cid_string = cid.toString();
            if ( !cid_string.has_value() )
            {
                if ( state->remaining.fetch_sub( 1 ) == 1 && !state->success_reported.load() )
                {
                    NotifyInitialized( false );
                }
                continue;
            }

            request_block_by_cid_(
                cid_string.value(),
                [weak_self = weak_from_this(), state]( outcome::result<std::string> result )
                {
                    if ( auto self = weak_self.lock() )
                    {
                        if ( !result.has_error() )
                        {
                            if ( !state->success_reported.exchange( true ) )
                            {
                                self->NotifyInitialized( true );
                            }
                        }

                        if ( state->remaining.fetch_sub( 1 ) == 1 && !state->success_reported.load() )
                        {
                            self->NotifyInitialized( false );
                        }
                    }
                } );
        }
    }

    void ValidatorRegistry::NotifyInitialized( bool success )
    {
        if ( init_callback_ )
        {
            init_callback_( success );
        }
    }
}
