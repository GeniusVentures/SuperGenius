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
#include <unordered_set>

#include <gsl/span>

#include "account/GeniusAccount.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"
#include "crdt/graphsync_dagsyncer.hpp"

namespace sgns::blockchain
{
    namespace
    {
        base::Logger validator_registry_logger()
        {
            return base::createLogger( "ValidatorRegistry" );
        }

        outcome::result<std::string> ExtractPrevRegistryCid( const ipfs_lite::ipld::IPLDNode &node )
        {
            auto            buffer = node.content();
            crdt::pb::Delta delta;
            if ( !delta.ParseFromArray( buffer.data(), buffer.size() ) )
            {
                return outcome::failure( std::errc::invalid_argument );
            }

            const std::string registry_key = std::string( ValidatorRegistry::RegistryKey() );
            for ( const auto &element : delta.elements() )
            {
                if ( element.key() != registry_key )
                {
                    continue;
                }

                validator::RegistryUpdate update;
                if ( !update.ParseFromString( element.value() ) )
                {
                    return outcome::failure( std::errc::invalid_argument );
                }

                return update.prev_registry_hash();
            }

            return outcome::failure( std::errc::no_such_file_or_directory );
        }
    }

    ValidatorRegistry::ValidatorRegistry( std::shared_ptr<crdt::GlobalDB> db,
                                          uint64_t                        quorum_numerator,
                                          uint64_t                        quorum_denominator,
                                          WeightConfig                    weight_config,
                                          std::string                     genesis_authority,
                                          BlockRequestMethod              block_request_method,
                                          InitCallback                    init_callback ) :
        db_( std::move( db ) ),
        quorum_numerator_( quorum_numerator ),
        quorum_denominator_( quorum_denominator ),
        weight_config_( std::move( weight_config ) ),
        genesis_authority_( std::move( genesis_authority ) ),
        request_block_by_cid_( std::move( block_request_method ) ),
        init_callback_( std::move( init_callback ) )
    {
        logger_->trace( "{}: constructed", __func__ );
    }

    std::shared_ptr<ValidatorRegistry> ValidatorRegistry::New( std::shared_ptr<crdt::GlobalDB> db,
                                                               uint64_t                        quorum_numerator,
                                                               uint64_t                        quorum_denominator,
                                                               WeightConfig                    weight_config,
                                                               std::string                     genesis_authority,
                                                               BlockRequestMethod              block_request_method,
                                                               InitCallback                    init_callback )
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
                                                                                   std::move( genesis_authority ),
                                                                                   std::move( block_request_method ),
                                                                                   std::move( init_callback ) ) );

        instance->logger_->trace( "{}: instance created", __func__ );
        instance->InitializeCache();

        if ( !instance->RegisterFilter() )
        {
            instance->logger_->error( "{}: failed to register filters", __func__ );
            return nullptr;
        }

        instance->logger_->info( "{}: instance ready", __func__ );
        return instance;
    }

    outcome::result<void> ValidatorRegistry::MigrateCids( const std::shared_ptr<crdt::GlobalDB> &old_db,
                                                          const std::shared_ptr<crdt::GlobalDB> &new_db )
    {
        if ( !old_db || !new_db )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        auto old_syncer = std::static_pointer_cast<crdt::GraphsyncDAGSyncer>(
            old_db->GetBroadcaster()->GetDagSyncer() );
        auto new_crdt = new_db->GetCRDTDataStore();
        if ( !new_crdt )
        {
            validator_registry_logger()->error( "{}: Missing broadcaster while migrating Validator CIDs", __func__ );
            return outcome::failure( std::errc::no_such_device );
        }
        if ( !old_syncer )
        {
            validator_registry_logger()->error( "{}: Missing DAG syncer while migrating Validator CIDs", __func__ );
            return outcome::failure( std::errc::no_such_device );
        }

        auto old_store = old_db->GetDataStore();
        auto new_store = new_db->GetDataStore();

        validator_registry_logger()->debug( "{}: Getting the registry CID from the datastore", __func__ );

        crdt::GlobalDB::Buffer registry_cid_key;
        registry_cid_key.put( std::string( RegistryCidKey() ) );
        auto registry_cid = old_store->get( registry_cid_key );
        if ( registry_cid.has_value() )
        {
            validator_registry_logger()->debug( "{}: Latest Validator CID: ",
                                                __func__,
                                                registry_cid.value().toString() );

            std::vector<std::string>                 registry_chain;
            std::vector<ipfs_lite::ipld::IPLDNode &> nodes;
            auto                                     current_cid = std::string( registry_cid.value().toString() );

            while ( !current_cid.empty() )
            {
                registry_chain.push_back( current_cid );
                OUTCOME_TRY( auto &&cid, CID::fromString( current_cid ) );
                OUTCOME_TRY( auto &&node, old_syncer->GetNodeFromMerkleDAG( cid ) );
                nodes.push_back( *node );
                auto prev_result = ExtractPrevRegistryCid( *node );
                if ( prev_result.has_error() )
                {
                    validator_registry_logger()->error( "{}: Failed to extract previous registry CID from {}",
                                                        __func__,
                                                        current_cid );
                    break;
                }
                current_cid = prev_result.value();
            }
            std::reverse( registry_chain.begin(), registry_chain.end() );
            std::reverse( nodes.begin(), nodes.end() );
            for ( auto i = 0; i < registry_chain.size(); ++i )
            {
                const auto &cid_string = registry_chain[i];
                const auto &node       = nodes[i];

                if ( cid_string.empty() )
                {
                    continue;
                }
                validator_registry_logger()->debug( "{}: Adding Validator CID: ",
                                                    __func__,
                                                    registry_cid.value().toString() );
                crdt::GlobalDB::Buffer registry_cid_value;
                registry_cid_value.putBuffer( cid_string.value() );
                (void)new_store->put( registry_cid_key, std::move( registry_cid_value ) );

                OUTCOME_TRY( new_crdt->AddDAGNode( node ) );
            }
        }
        validator_registry_logger()->debug( "{}: Finished migrating validator registry: ", __func__ );
        return outcome::success();
    }

    uint64_t ValidatorRegistry::ComputeWeight( Role role ) const
    {
        logger_->trace( "{}: entry role={}", __func__, static_cast<int>( role ) );
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
            logger_->debug( "{}: multiplier is zero, weight=0", __func__ );
            return 0;
        }

        if ( base_weight > weight_config_.max_weight_ / multiplier )
        {
            logger_->debug( "{}: weight clamped to max {}", __func__, weight_config_.max_weight_ );
            return weight_config_.max_weight_;
        }

        const uint64_t weighted = base_weight * multiplier;
        const uint64_t result   = std::min( weighted, weight_config_.max_weight_ );
        logger_->debug( "{}: computed weight={}", __func__, result );
        return result;
    }

    uint64_t ValidatorRegistry::TotalWeight( const Registry &registry ) const
    {
        logger_->trace( "{}: entry validators={}", __func__, registry.validators().size() );
        uint64_t total_weight = 0;
        for ( const auto &entry : registry.validators() )
        {
            if ( entry.status() != Status::ACTIVE )
            {
                continue;
            }
            total_weight += entry.weight();
        }
        logger_->debug( "{}: total_weight={}", __func__, total_weight );
        return total_weight;
    }

    uint64_t ValidatorRegistry::QuorumThreshold( uint64_t total_weight ) const
    {
        logger_->trace( "{}: entry total_weight={}", __func__, total_weight );
        if ( total_weight == 0 )
        {
            logger_->debug( "{}: total_weight is zero, threshold=0", __func__ );
            return 0;
        }
        const uint64_t numerator = total_weight * quorum_numerator_;
        const uint64_t threshold = ( numerator + quorum_denominator_ - 1 ) / quorum_denominator_;
        logger_->debug( "{}: threshold={}", __func__, threshold );
        return threshold;
    }

    bool ValidatorRegistry::IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const
    {
        logger_->trace( "{}: entry accumulated={} total={}", __func__, accumulated_weight, total_weight );
        const bool is_quorum = accumulated_weight >= QuorumThreshold( total_weight );
        logger_->debug( "{}: is_quorum={}", __func__, is_quorum );
        return is_quorum;
    }

    ValidatorRegistry::Registry ValidatorRegistry::CreateGenesisRegistry(
        const std::string &genesis_validator_id ) const
    {
        logger_->trace( "{}: entry genesis_id={}", __func__, genesis_validator_id.substr( 0, 8 ) );
        Registry registry;
        registry.set_epoch( 0 );
        auto *entry = registry.add_validators();
        entry->set_validator_id( genesis_validator_id );
        entry->set_role( Role::GENESIS );
        entry->set_status( Status::ACTIVE );
        entry->set_weight( ComputeWeight( entry->role() ) );
        logger_->debug( "{}: registry created with weight={}", __func__, entry->weight() );
        return registry;
    }

    outcome::result<std::vector<uint8_t>> ValidatorRegistry::SerializeRegistry( const Registry &registry ) const
    {
        logger_->trace( "{}: entry validators={}", __func__, registry.validators().size() );
        std::string serialized;
        if ( !registry.SerializeToString( &serialized ) )
        {
            logger_->error( "{}: serialization failed", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        logger_->debug( "{}: serialized size={}", __func__, serialized.size() );
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::DeserializeRegistry(
        const std::vector<uint8_t> &buffer ) const
    {
        logger_->trace( "{}: entry size={}", __func__, buffer.size() );
        Registry proto;
        if ( !proto.ParseFromArray( buffer.data(), static_cast<int>( buffer.size() ) ) )
        {
            logger_->error( "{}: parse failed", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        logger_->debug( "{}: parsed validators={}", __func__, proto.validators().size() );
        return proto;
    }

    outcome::result<std::vector<uint8_t>> ValidatorRegistry::SerializeRegistryUpdate(
        const RegistryUpdate &update ) const
    {
        logger_->trace( "{}: entry validators={}", __func__, update.registry().validators().size() );
        std::string serialized;
        if ( !update.SerializeToString( &serialized ) )
        {
            logger_->error( "{}: serialization failed", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        logger_->debug( "{}: serialized size={}", __func__, serialized.size() );
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    outcome::result<ValidatorRegistry::RegistryUpdate> ValidatorRegistry::DeserializeRegistryUpdate(
        const std::vector<uint8_t> &buffer ) const
    {
        logger_->trace( "{}: entry size={}", __func__, buffer.size() );
        RegistryUpdate proto;
        if ( !proto.ParseFromArray( buffer.data(), static_cast<int>( buffer.size() ) ) )
        {
            logger_->error( "{}: parse failed", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        logger_->debug( "{}: parsed validators={}", __func__, proto.registry().validators().size() );
        return proto;
    }

    outcome::result<void> ValidatorRegistry::StoreGenesisRegistry(
        const std::string                                          &genesis_validator_id,
        std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign )
    {
        logger_->trace( "{}: entry genesis_id={}", __func__, genesis_validator_id.substr( 0, 8 ) );
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            if ( cache_initialized_ && cached_registry_ && !cached_registry_->validators().empty() )
            {
                logger_->info( "{}: registry already initialized, skipping", __func__ );
                return outcome::success();
            }
        }

        if ( !sign )
        {
            logger_->error( "{}: missing sign callback", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        logger_->debug( "{}: creating genesis registry", __func__ );
        RegistryUpdate update;
        *update.mutable_registry() = CreateGenesisRegistry( genesis_validator_id );
        update.clear_prev_registry_hash();

        auto signing_bytes = ComputeUpdateSigningBytes( update );
        if ( signing_bytes.has_error() )
        {
            logger_->error( "{}: failed to compute signing bytes", __func__ );
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
            logger_->error( "{}: failed to serialize registry update", __func__ );
            return outcome::failure( serialized_update.error() );
        }

        base::Buffer update_buffer(
            gsl::span<const uint8_t>( serialized_update.value().data(), serialized_update.value().size() ) );

        crdt::HierarchicalKey registry_key{ std::string( RegistryKey() ) };

        auto registry_put = db_->Put( registry_key, update_buffer, { std::string( ValidatorTopic() ) } );
        if ( registry_put.has_error() )
        {
            logger_->error( "{}: failed to store registry in CRDT", __func__ );
            return outcome::failure( registry_put.error() );
        }

        auto cid_string = registry_put.value().toString();
        if ( cid_string.has_value() )
        {
            logger_->info( "{}: stored genesis registry CID {}", __func__, cid_string.value() );
        }
        else
        {
            logger_->error( "{}: registry stored but CID missing", __func__ );
        }

        logger_->info( "{}: success", __func__ );
        return outcome::success();
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::LoadRegistry() const
    {
        logger_->trace( "{}: entry", __func__ );
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            if ( cached_registry_ )
            {
                logger_->debug( "{}: returning cached registry", __func__ );
                return cached_registry_.value();
            }
        }

        auto update_result = LoadRegistryUpdate();
        if ( update_result.has_error() )
        {
            logger_->error( "{}: failed to load registry update", __func__ );
            return outcome::failure( update_result.error() );
        }
        logger_->debug( "{}: returning registry from update", __func__ );
        return update_result.value().registry();
    }

    outcome::result<ValidatorRegistry::RegistryUpdate> ValidatorRegistry::LoadRegistryUpdate() const
    {
        logger_->trace( "{}: entry", __func__ );
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            if ( cached_update_ )
            {
                logger_->debug( "{}: returning cached update", __func__ );
                return cached_update_.value();
            }
        }

        logger_->error( "{}: registry update not available", __func__ );
        return outcome::failure( std::errc::no_such_file_or_directory );
    }

    bool ValidatorRegistry::RegisterFilter()
    {
        logger_->trace( "{}: entry", __func__ );
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

        const bool result = filter_registered && callback_registered;
        logger_->info( "{}: result={}", __func__, result );
        return result;
    }

    std::optional<std::vector<crdt::pb::Element>> ValidatorRegistry::FilterRegistryUpdate(
        const crdt::pb::Element &element )
    {
        logger_->trace( "{}: entry key={}", __func__, element.key() );
        std::vector<uint8_t> bytes( element.value().begin(), element.value().end() );
        auto                 decoded_update = DeserializeRegistryUpdate( bytes );
        if ( decoded_update.has_error() )
        {
            logger_->error( "{}: parse failed, rejecting: {}", __func__, element.key() );
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
            logger_->error( "{}: verification failed, rejecting: {}", __func__, element.key() );
            return std::vector<crdt::pb::Element>{};
        }

        logger_->debug( "{}: update accepted", __func__ );
        return std::nullopt;
    }

    void ValidatorRegistry::RegistryUpdateReceived( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                    const std::string                     &cid )
    {
        logger_->trace( "{}: entry cid={}", __func__, cid );
        const auto          &buffer = new_data.second;
        std::vector<uint8_t> bytes( buffer.data(), buffer.data() + buffer.size() );
        auto                 decoded = DeserializeRegistryUpdate( bytes );
        if ( decoded.has_error() )
        {
            logger_->error( "{}: failed to parse registry update for cache refresh", __func__ );
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
        logger_->info( "{}: cache updated and initialized", __func__ );
    }

    outcome::result<std::vector<uint8_t>> ValidatorRegistry::ComputeUpdateSigningBytes(
        const RegistryUpdate &update ) const
    {
        logger_->trace( "{}: entry validators={}", __func__, update.registry().validators().size() );
        sgns::blockchain::validator::RegistrySigningPayload payload;
        *payload.mutable_registry() = update.registry();
        payload.set_prev_registry_hash( update.prev_registry_hash() );

        std::string serialized;
        if ( !payload.SerializeToString( &serialized ) )
        {
            logger_->error( "{}: serialization failed", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        logger_->debug( "{}: payload size={}", __func__, serialized.size() );
        return std::vector<uint8_t>( serialized.begin(), serialized.end() );
    }

    bool ValidatorRegistry::VerifyUpdate( const RegistryUpdate &update, const Registry *current_registry ) const
    {
        logger_->trace( "{}: entry validators={}", __func__, update.registry().validators().size() );
        if ( update.registry().validators().empty() )
        {
            logger_->error( "{}: empty registry update", __func__ );
            return false;
        }

        auto signing_bytes = ComputeUpdateSigningBytes( update );
        if ( signing_bytes.has_error() )
        {
            logger_->error( "{}: signing bytes computation failed", __func__ );
            return false;
        }

        if ( !current_registry )
        {
            logger_->debug( "{}: verifying genesis update", __func__ );
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
                        logger_->info( "{}: genesis update verified", __func__ );
                        return true;
                    }
                }
            }
            logger_->error( "{}: genesis update verification failed", __func__ );
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
            //TODO - Check if the CID checking is necessary, because we could receive out-of-order updates
            logger_->error( "{}: prev registry CID mismatch", __func__ );
            return false;
        }

        if ( update.registry().epoch() <= current_registry->epoch() )
        {
            logger_->error( "{}: epoch not increasing", __func__ );
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
                logger_->info( "{}: quorum reached", __func__ );
                return true;
            }
        }

        logger_->error( "{}: quorum not reached", __func__ );
        return false;
    }

    const ValidatorRegistry::ValidatorEntry *ValidatorRegistry::FindValidator( const Registry    &registry,
                                                                               const std::string &validator_id ) const
    {
        logger_->trace( "{}: entry id={}", __func__, validator_id.substr( 0, 8 ) );
        for ( const auto &validator : registry.validators() )
        {
            if ( validator.validator_id() == validator_id )
            {
                logger_->debug( "{}: validator found", __func__ );
                return &validator;
            }
        }
        logger_->debug( "{}: validator not found", __func__ );
        return nullptr;
    }

    void ValidatorRegistry::InitializeCache()
    {
        logger_->trace( "{}: entry", __func__ );
        std::unique_lock<std::shared_mutex> lock( cache_mutex_ );
        if ( cache_initialized_ )
        {
            logger_->error( "{}: cache already initialized", __func__ );
            return;
        }
        logger_->trace( "{}: grabbing validator registry from CRDT", __func__ );

        crdt::HierarchicalKey registry_key{ std::string( RegistryKey() ) };
        auto                  registry_get    = db_->Get( registry_key );
        bool                  content_present = registry_get.has_value();
        if ( !content_present )
        {
            logger_->error( "{}: registry content not found during cache init", __func__ );
            return;
        }
        const auto          &buffer = registry_get.value();
        std::vector<uint8_t> bytes( buffer.data(), buffer.data() + buffer.size() );
        auto                 decoded = DeserializeRegistryUpdate( bytes );
        if ( !decoded.has_value() )
        {
            logger_->error( "{}: failed to parse registry content during cache init", __func__ );
            return;
        }

        cached_update_   = decoded.value();
        cached_registry_ = decoded.value().registry();
        logger_->debug( "{}: cache populated validators={}", __func__, cached_registry_->validators().size() );

        cache_initialized_ = true;

        sgns::crdt::GlobalDB::Buffer registry_cid_key;
        registry_cid_key.put( std::string( RegistryCidKey() ) );
        auto registry_cid = db_->GetDataStore()->get( registry_cid_key );
        if ( registry_cid.has_value() )
        {
            cached_registry_id_ = registry_cid.value().toString();
            logger_->info( "{}: cache initialized with CID {}", __func__, cached_registry_id_ );
            NotifyInitialized( true );
            return;
        }

        std::set<CID> heads_to_request;

        logger_->error( "{}: registry content found but CID missing, requesting heads", __func__ );

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
        logger_->debug( "{}: heads to request={}", __func__, heads_to_request.size() );

        lock.unlock();

        if ( !heads_to_request.empty() )
        {
            RequestHeadCids( heads_to_request );
        }
        else
        {
            logger_->error( "{}: no heads available to request", __func__ );
        }
    }

    void ValidatorRegistry::PersistLocalState( const std::string &cid )
    {
        logger_->trace( "{}: entry cid={}", __func__, cid );
        sgns::crdt::GlobalDB::Buffer registry_cid_key;
        registry_cid_key.put( std::string( RegistryCidKey() ) );
        sgns::crdt::GlobalDB::Buffer registry_cid;
        registry_cid.put( cid );
        (void)db_->GetDataStore()->put( registry_cid_key, registry_cid );
        logger_->debug( "{}: persisted CID", __func__ );
    }

    void ValidatorRegistry::RequestHeadCids( const std::set<CID> &cids )
    {
        logger_->trace( "{}: entry count={}", __func__, cids.size() );
        const char *func = __func__;
        if ( cids.empty() )
        {
            logger_->error( "{}: empty CID set", __func__ );
            return;
        }

        struct RequestState
        {
            std::atomic<size_t> remaining;
            std::atomic<bool>   success_reported{ false };

            explicit RequestState( size_t remaining_count ) : remaining( remaining_count ) {}
        };

        auto state = std::make_shared<RequestState>( cids.size() );

        for ( const auto &cid : cids )
        {
            auto cid_string = cid.toString();
            if ( !cid_string.has_value() )
            {
                logger_->error( "{}: failed to convert CID to string", __func__ );
                if ( state->remaining.fetch_sub( 1 ) == 1 && !state->success_reported.load() )
                {
                    NotifyInitialized( false );
                }
                continue;
            }

            logger_->debug( "{}: requesting CID {}", __func__, cid_string.value() );
            request_block_by_cid_(
                cid_string.value(),
                [weak_self = weak_from_this(), state, func]( outcome::result<std::string> result )
                {
                    if ( auto self = weak_self.lock() )
                    {
                        if ( !result.has_error() )
                        {
                            if ( !state->success_reported.exchange( true ) )
                            {
                                self->logger_->info( "{}: head request succeeded", func );
                                self->NotifyInitialized( true );
                            }
                        }

                        if ( state->remaining.fetch_sub( 1 ) == 1 && !state->success_reported.load() )
                        {
                            self->logger_->error( "{}: all head requests failed", func );
                            self->NotifyInitialized( false );
                        }
                    }
                } );
        }
    }

    void ValidatorRegistry::NotifyInitialized( bool success )
    {
        logger_->trace( "{}: entry success={}", __func__, success );
        if ( init_callback_ )
        {
            init_callback_( success );
            logger_->debug( "{}: callback dispatched", __func__ );
        }
        else
        {
            logger_->debug( "{}: no callback registered", __func__ );
        }
    }
}
