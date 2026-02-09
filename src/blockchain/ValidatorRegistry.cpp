/**
 * @file       ValidatorRegistry.cpp
 * @brief      Validator registry and quorum logic for governance.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "blockchain/ValidatorRegistry.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include <gsl/span>

#include "account/GeniusAccount.hpp"
#include "blockchain/ConsensusSigning.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"
#include "crdt/graphsync_dagsyncer.hpp"

namespace sgns
{
    namespace
    {
        base::Logger ValidatorRegistryLogger()
        {
            return base::createLogger( "ValidatorRegistry" );
        }

        outcome::result<std::string> ExtractPrevRegistryCid( const ipfs_lite::ipld::IPLDNode &node )
        {
            auto            buffer = node.content();
            crdt::pb::Delta delta;
            if ( !delta.ParseFromArray( buffer.data(), buffer.size() ) )
            {
                ValidatorRegistryLogger()->error( "{}: Failed to parse Delta from IPLD node", __func__ );
                return outcome::failure( std::errc::invalid_argument );
            }

            const std::string registry_key = std::string( ValidatorRegistry::RegistryKey() );
            for ( const auto &element : delta.elements() )
            {
                validator::RegistryUpdate update;
                if ( !update.ParseFromString( element.value() ) )
                {
                    ValidatorRegistryLogger()->error( "{}: Can't parse the registry update {}",
                                                      __func__,
                                                      element.key() );
                    return outcome::failure( std::errc::invalid_argument );
                }

                return update.prev_registry_hash();
            }
            ValidatorRegistryLogger()->error( "{}: NO SUCH FILE ", __func__ );
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
        init_callback_( std::move( init_callback ) ),
        request_block_by_cid_( std::move( block_request_method ) )
    {
        logger_->trace( "{}: constructed", __func__ );
    }

    ValidatorRegistry::~ValidatorRegistry()
    {
        const std::string pattern = "/?" + std::string( RegistryKey() );
        if ( db_ )
        {
            db_->UnregisterNewElementCallback( pattern );
            db_->UnregisterElementFilter( pattern );
        }
        logger_->trace( "{}: destroyed", __func__ );
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
            ValidatorRegistryLogger()->error( "{}: Missing broadcaster while migrating Validator CIDs", __func__ );
            return outcome::failure( std::errc::no_such_device );
        }
        if ( !old_syncer )
        {
            ValidatorRegistryLogger()->error( "{}: Missing DAG syncer while migrating Validator CIDs", __func__ );
            return outcome::failure( std::errc::no_such_device );
        }

        auto old_store = old_db->GetDataStore();
        auto new_store = new_db->GetDataStore();

        ValidatorRegistryLogger()->debug( "{}: Getting the registry CID from the datastore", __func__ );

        crdt::GlobalDB::Buffer registry_cid_key;
        registry_cid_key.put( std::string( RegistryCidKey() ) );
        auto registry_cid = old_store->get( registry_cid_key );
        if ( registry_cid.has_value() )
        {
            ValidatorRegistryLogger()->debug( "{}: Latest Validator CID: {}",
                                              __func__,
                                              registry_cid.value().toString() );

            std::vector<std::string>                                registry_chain;
            std::vector<std::shared_ptr<ipfs_lite::ipld::IPLDNode>> nodes;
            auto current_cid = std::string( registry_cid.value().toString() );

            while ( !current_cid.empty() )
            {
                registry_chain.push_back( current_cid );
                OUTCOME_TRY( auto &&cid, CID::fromString( current_cid ) );
                OUTCOME_TRY( auto &&node, old_syncer->GetNodeFromMerkleDAG( cid ) );
                auto prev_result = ExtractPrevRegistryCid( *node );
                nodes.push_back( std::move( node ) );
                if ( prev_result.has_error() )
                {
                    ValidatorRegistryLogger()->error( "{}: Failed to extract previous registry CID from {}",
                                                      __func__,
                                                      current_cid );
                    break;
                }
                current_cid = prev_result.value();
            }
            for ( size_t i = registry_chain.size(); i-- > 0; )
            {
                const auto &cid_string = registry_chain[i];
                const auto &node       = nodes[i];

                if ( cid_string.empty() )
                {
                    continue;
                }
                ValidatorRegistryLogger()->debug( "{}: Adding Validator CID: {}",
                                                  __func__,
                                                  registry_cid.value().toString() );
                crdt::GlobalDB::Buffer registry_cid_value;
                registry_cid_value.put( cid_string );
                (void)new_store->put( registry_cid_key, std::move( registry_cid_value ) );

                OUTCOME_TRY( new_crdt->AddDAGNode( node ) );
            }
        }
        ValidatorRegistryLogger()->debug( "{}: Finished migrating validator registry: ", __func__ );
        return outcome::success();
    }

    uint64_t ValidatorRegistry::ComputeWeight( Role role ) const
    {
        logger_->trace( "{}: entry role={}", __func__, static_cast<int>( role ) );
        uint64_t weight = weight_config_.regular_weight_;
        uint64_t cap    = weight_config_.regular_max_weight_;

        switch ( role )
        {
            case Role::GENESIS:
                weight = weight_config_.genesis_weight_;
                cap    = weight_config_.genesis_max_weight_;
                break;
            case Role::FULL:
                weight = weight_config_.full_weight_;
                cap    = weight_config_.full_max_weight_;
                break;
            case Role::SHARDED:
                weight = weight_config_.sharded_weight_;
                cap    = weight_config_.sharded_max_weight_;
                break;
            case Role::REGULAR:
            default:
                break;
        }

        if ( weight == 0 )
        {
            logger_->debug( "{}: weight is zero", __func__ );
            return 0;
        }

        if ( weight > cap )
        {
            logger_->debug( "{}: weight clamped to max {}", __func__, cap );
            return cap;
        }

        logger_->debug( "{}: computed weight={}", __func__, weight );
        return weight;
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
        entry->set_penalty_score( 0 );
        entry->set_missed_epochs( 0 );
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
            std::shared_lock lock( cache_mutex_ );
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

    outcome::result<ValidatorRegistry::RegistryUpdate> ValidatorRegistry::CreateUpdateFromCertificate(
        const sgns::ConsensusCertificate &certificate )
    {
        logger_->trace( "{}: entry proposal_id={}", __func__, certificate.proposal_id() );
        auto registry_result = LoadRegistry();
        if ( registry_result.has_error() )
        {
            logger_->error( "{}: failed to load registry: {}", __func__, registry_result.error().message() );
            return outcome::failure( registry_result.error() );
        }

        auto                                  current_registry = registry_result.value();
        std::unordered_set<std::string>       approved;
        std::unordered_set<std::string>       unregistered;
        std::unordered_map<std::string, bool> registered_votes;
        std::unordered_map<std::string, bool> unregistered_votes;
        if ( !VerifyCertificateForUpdate( certificate, current_registry, approved, unregistered, registered_votes, unregistered_votes ) )
        {
            logger_->error( "{}: invalid certificate", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        RegistryUpdate update;
        update.set_prev_registry_hash( GetRegistryCid() );
        *update.mutable_registry() = BuildRegistryFromCertificate( current_registry,
                                                                   certificate,
                                                                   registered_votes,
                                                                   unregistered_votes );

        std::string serialized_cert;
        if ( !certificate.SerializeToString( &serialized_cert ) )
        {
            logger_->error( "{}: failed to serialize certificate", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        update.set_certificate( serialized_cert );

        logger_->debug( "{}: update created epoch={}", __func__, update.registry().epoch() );
        return update;
    }

    outcome::result<void> ValidatorRegistry::StoreRegistryUpdate( const RegistryUpdate &update )
    {
        logger_->trace( "{}: entry epoch={}", __func__, update.registry().epoch() );
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
            logger_->error( "{}: failed to store registry update in CRDT", __func__ );
            return outcome::failure( registry_put.error() );
        }

        auto cid_string = registry_put.value().toString();
        if ( cid_string.has_value() )
        {
            logger_->info( "{}: stored registry update CID {}", __func__, cid_string.value() );
        }
        else
        {
            logger_->error( "{}: registry update stored but CID missing", __func__ );
        }

        logger_->info( "{}: success", __func__ );
        return outcome::success();
    }

    void ValidatorRegistry::SetMaxNewValidatorsPerUpdate( size_t max_new )
    {
        logger_->trace( "{}: entry max_new={}", __func__, max_new );
        max_new_validators_per_update_ = max_new;
    }

    std::string ValidatorRegistry::GetRegistryCid() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        return cached_registry_id_;
    }

    uint64_t ValidatorRegistry::GetRegistryEpoch() const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        if ( cached_registry_ )
        {
            return cached_registry_->epoch();
        }
        return 0;
    }

    outcome::result<std::optional<uint64_t>> ValidatorRegistry::GetValidatorWeight(
        const std::string &validator_id ) const
    {
        std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
        if ( !cache_initialized_ || !cached_registry_ )
        {
            return outcome::success( std::optional<uint64_t>{} );
        }

        const auto *validator = FindValidator( cached_registry_.value(), validator_id );
        if ( !validator || validator->status() != Status::ACTIVE )
        {
            return outcome::success( std::optional<uint64_t>{} );
        }

        return outcome::success( std::optional<uint64_t>{ validator->weight() } );
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

    void ValidatorRegistry::RegistryUpdateReceived( const crdt::CRDTCallbackManager::NewDataPair &new_data,
                                                    const std::string                            &cid )
    {
        logger_->trace( "{}: entry cid={}", __func__, cid );
        const auto &buffer  = new_data.second;
        auto        decoded = DeserializeRegistryUpdate( buffer.toVector() );
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
        validator::RegistrySigningPayload payload;
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

        if ( !update.certificate().empty() )
        {
            sgns::ConsensusCertificate certificate;
            if ( !certificate.ParseFromString( update.certificate() ) )
            {
                logger_->error( "{}: invalid certificate payload", __func__ );
                return false;
            }

            std::unordered_set<std::string>       approved;
            std::unordered_set<std::string>       unregistered;
            std::unordered_map<std::string, bool> registered_votes;
            std::unordered_map<std::string, bool> unregistered_votes;
            if ( !VerifyCertificateForUpdate( certificate,
                                              *current_registry,
                                              approved,
                                              unregistered,
                                              registered_votes,
                                              unregistered_votes ) )
            {
                logger_->error( "{}: certificate verification failed", __func__ );
                return false;
            }

            Registry expected = BuildRegistryFromCertificate( *current_registry,
                                                              certificate,
                                                              registered_votes,
                                                              unregistered_votes );
            Registry provided = update.registry();
            NormalizeRegistry( provided );
            NormalizeRegistry( expected );

            if ( provided.epoch() <= current_registry->epoch() )
            {
                logger_->error( "{}: epoch not increasing", __func__ );
                return false;
            }

            if ( provided.SerializeAsString() != expected.SerializeAsString() )
            {
                logger_->error( "{}: registry mismatch against certificate", __func__ );
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
                logger_->error( "{}: prev registry CID mismatch", __func__ );
                return false;
            }

            logger_->info( "{}: certificate-based update verified", __func__ );
            return true;
        }

        const std::string prev_registry_cid = update.prev_registry_hash();
        std::string       current_id;
        {
            std::shared_lock lock( cache_mutex_ );
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

    bool ValidatorRegistry::VerifyCertificateForUpdate(
        const sgns::ConsensusCertificate      &certificate,
        const Registry                        &current_registry,
        std::unordered_set<std::string>       &approved_out,
        std::unordered_set<std::string>       &unregistered_out,
        std::unordered_map<std::string, bool> &registered_votes_out,
        std::unordered_map<std::string, bool> &unregistered_votes_out ) const
    {
        logger_->trace( "{}: entry proposal_id={}", __func__, certificate.proposal_id() );
        if ( certificate.proposal_id().empty() )
        {
            logger_->error( "{}: empty proposal_id", __func__ );
            return false;
        }

        if ( certificate.registry_epoch() != current_registry.epoch() )
        {
            logger_->error( "{}: registry epoch mismatch cert={} registry={}",
                            __func__,
                            certificate.registry_epoch(),
                            current_registry.epoch() );
            return false;
        }

        const std::string current_id = GetRegistryCid();
        if ( !current_id.empty() && !certificate.registry_cid().empty() && certificate.registry_cid() != current_id )
        {
            logger_->error( "{}: registry CID mismatch cert={} registry={}",
                            __func__,
                            certificate.registry_cid(),
                            current_id );
            return false;
        }

        uint64_t                        total_weight    = TotalWeight( current_registry );
        uint64_t                        approved_weight = 0;
        std::unordered_set<std::string> seen;

        approved_out.clear();
        unregistered_out.clear();
        registered_votes_out.clear();
        unregistered_votes_out.clear();

        for ( const auto &vote : certificate.votes() )
        {
            if ( vote.proposal_id() != certificate.proposal_id() )
            {
                continue;
            }
            if ( !seen.insert( vote.voter_id() ).second )
            {
                continue;
            }

            auto signing_bytes = VoteSigningBytes( vote );
            if ( signing_bytes.has_error() )
            {
                continue;
            }

            if ( !GeniusAccount::VerifySignature( vote.voter_id(), vote.signature(), signing_bytes.value() ) )
            {
                continue;
            }
            const auto *validator = FindValidator( current_registry, vote.voter_id() );
            if ( !validator )
            {
                unregistered_out.insert( vote.voter_id() );
                unregistered_votes_out[vote.voter_id()] = vote.approve();
                continue;
            }

            registered_votes_out[vote.voter_id()] = vote.approve();

            if ( vote.approve() && validator->status() == Status::ACTIVE )
            {
                approved_weight += validator->weight();
                approved_out.insert( vote.voter_id() );
            }
        }

        if ( !IsQuorum( approved_weight, total_weight ) )
        {
            logger_->error( "{}: quorum not reached approved={} total={}", __func__, approved_weight, total_weight );
            return false;
        }

        logger_->debug( "{}: quorum verified approved={} total={}", __func__, approved_weight, total_weight );
        return true;
    }

    ValidatorRegistry::Registry ValidatorRegistry::BuildRegistryFromCertificate(
        const Registry                              &current_registry,
        const sgns::ConsensusCertificate            &certificate,
        const std::unordered_map<std::string, bool> &registered_votes,
        const std::unordered_map<std::string, bool> &unregistered_votes ) const
    {
        Registry next = current_registry;
        next.set_epoch( current_registry.epoch() + 1 );

        InsertNewValidators( next, unregistered_votes );

        std::vector<ValidatorEntry> entries;
        entries.reserve( static_cast<size_t>( next.validators_size() ) );
        for ( const auto &entry : next.validators() )
        {
            entries.push_back( entry );
        }

        ApplyVoteEffects( entries, registered_votes );
        std::unordered_set<std::string> participants;
        participants.reserve( registered_votes.size() + unregistered_votes.size() );
        for ( const auto &pair : registered_votes )
        {
            participants.insert( pair.first );
        }
        for ( const auto &pair : unregistered_votes )
        {
            participants.insert( pair.first );
        }
        ApplyInactivityDecay( entries, participants );
        ApplyTotalWeightCap( entries );

        std::sort( entries.begin(),
                   entries.end(),
                   []( const ValidatorEntry &a, const ValidatorEntry &b )
                   { return a.validator_id() < b.validator_id(); } );

        next.clear_validators();
        for ( const auto &entry : entries )
        {
            *next.add_validators() = entry;
        }

        logger_->debug( "{}: built registry from certificate proposal_id={} epoch={}",
                        __func__,
                        certificate.proposal_id(),
                        next.epoch() );
        return next;
    }

    void ValidatorRegistry::InsertNewValidators( Registry &registry,
                                                 const std::unordered_map<std::string, bool> &unregistered_votes ) const
    {
        std::vector<std::string> new_ids;
        new_ids.reserve( unregistered_votes.size() );
        for ( const auto &pair : unregistered_votes )
        {
            new_ids.push_back( pair.first );
        }
        std::sort( new_ids.begin(), new_ids.end() );
        size_t added = 0;
        for ( const auto &validator_id : new_ids )
        {
            if ( added >= max_new_validators_per_update_ )
            {
                logger_->debug( "{}: new validator cap reached {}", __func__, max_new_validators_per_update_ );
                break;
            }
            if ( FindValidator( registry, validator_id ) )
            {
                continue;
            }
            auto *entry = registry.add_validators();
            entry->set_validator_id( validator_id );
            entry->set_role( Role::REGULAR );
            entry->set_status( Status::ACTIVE );
            entry->set_weight( ComputeWeight( entry->role() ) );
            auto it = unregistered_votes.find( validator_id );
            const bool approve = ( it != unregistered_votes.end() ) ? it->second : true;
            entry->set_penalty_score( approve ? 0 : 1 );
            entry->set_missed_epochs( 0 );
            ++added;
        }
    }

    void ValidatorRegistry::ApplyVoteEffects(
        std::vector<ValidatorEntry>                  &entries,
        const std::unordered_map<std::string, bool> &registered_votes ) const
    {
        for ( auto &entry : entries )
        {
            auto vote_it = registered_votes.find( entry.validator_id() );
            if ( vote_it == registered_votes.end() )
            {
                continue;
            }

            const bool     approve = vote_it->second;
            uint32_t       penalty = static_cast<uint32_t>( entry.penalty_score() );
            const uint32_t cap     = weight_config_.penalty_cap_;
            entry.set_missed_epochs( 0 );

            if ( approve )
            {
                if ( penalty > 0 )
                {
                    penalty -= 1;
                }
                entry.set_penalty_score( penalty );

                if ( entry.status() == Status::ACTIVE )
                {
                    const uint64_t increment = weight_config_.approval_increment_;
                    if ( increment > 0 )
                    {
                        uint64_t role_cap = weight_config_.regular_max_weight_;
                        switch ( entry.role() )
                        {
                            case Role::GENESIS:
                                role_cap = weight_config_.genesis_max_weight_;
                                break;
                            case Role::FULL:
                                role_cap = weight_config_.full_max_weight_;
                                break;
                            case Role::SHARDED:
                                role_cap = weight_config_.sharded_max_weight_;
                                break;
                            case Role::REGULAR:
                            default:
                                role_cap = weight_config_.regular_max_weight_;
                                break;
                        }
                        const uint64_t clamped = std::min( entry.weight() + increment, role_cap );
                        entry.set_weight( clamped );
                    }
                }
                else if ( penalty == 0 )
                {
                    entry.set_status( Status::ACTIVE );
                }
            }
            else
            {
                if ( entry.status() == Status::BLACKLISTED )
                {
                    const uint32_t bumped =
                        std::min( cap, static_cast<uint32_t>( penalty + weight_config_.blacklist_bump_ ) );
                    penalty = bumped;
                }
                else
                {
                    if ( penalty < cap )
                    {
                        penalty += 1;
                    }
                    if ( penalty >= weight_config_.penalty_threshold_ )
                    {
                        entry.set_status( Status::BLACKLISTED );
                        const uint32_t bumped =
                            std::min( cap, static_cast<uint32_t>( penalty + weight_config_.blacklist_bump_ ) );
                        penalty = bumped;
                    }
                }
                entry.set_penalty_score( penalty );
            }
        }
    }

    void ValidatorRegistry::ApplyInactivityDecay( std::vector<ValidatorEntry> &entries,
                                                  const std::unordered_set<std::string> &participants ) const
    {
        for ( auto &entry : entries )
        {
            if ( entry.status() != Status::ACTIVE )
            {
                continue;
            }
            if ( participants.find( entry.validator_id() ) != participants.end() )
            {
                continue;
            }
            uint32_t missed = static_cast<uint32_t>( entry.missed_epochs() );
            if ( missed < std::numeric_limits<uint32_t>::max() )
            {
                missed += 1;
            }
            entry.set_missed_epochs( missed );

            if ( missed >= weight_config_.missed_epoch_threshold_ )
            {
                const uint32_t dec = weight_config_.inactivity_decrement_;
                if ( dec > 0 && entry.weight() > 0 )
                {
                    const uint64_t new_weight = ( entry.weight() > dec ) ? ( entry.weight() - dec ) : 0;
                    entry.set_weight( new_weight );
                    if ( new_weight == 0 )
                    {
                        entry.set_status( Status::SUSPENDED );
                    }
                }
            }
        }
    }

    void ValidatorRegistry::ApplyTotalWeightCap( std::vector<ValidatorEntry> &entries ) const
    {
        uint64_t total_active = 0;
        for ( const auto &entry : entries )
        {
            if ( entry.status() == Status::ACTIVE )
            {
                total_active += entry.weight();
            }
        }

        const uint64_t weight_cap =
            weight_config_.genesis_weight_ * weight_config_.total_weight_cap_multiplier_;
        if ( weight_cap == 0 || total_active <= weight_cap )
        {
            return;
        }

        uint64_t scaled_sum = 0;
        std::vector<size_t> active_indices;
        active_indices.reserve( entries.size() );
        for ( size_t i = 0; i < entries.size(); ++i )
        {
            if ( entries[i].status() != Status::ACTIVE )
            {
                continue;
            }
            const uint64_t scaled = ( entries[i].weight() * weight_cap ) / total_active;
            entries[i].set_weight( scaled );
            scaled_sum += scaled;
            active_indices.push_back( i );
        }

        uint64_t remainder = ( scaled_sum <= weight_cap ) ? ( weight_cap - scaled_sum ) : 0;
        if ( remainder == 0 || active_indices.empty() )
        {
            return;
        }

        std::sort( active_indices.begin(),
                   active_indices.end(),
                   [&entries]( size_t a, size_t b )
                   {
                       if ( entries[a].weight() != entries[b].weight() )
                       {
                           return entries[a].weight() > entries[b].weight();
                       }
                       return entries[a].validator_id() < entries[b].validator_id();
                   } );
        size_t idx = 0;
        while ( remainder > 0 )
        {
            entries[active_indices[idx]].set_weight( entries[active_indices[idx]].weight() + 1 );
            remainder -= 1;
            idx = ( idx + 1 ) % active_indices.size();
        }
    }

void ValidatorRegistry::NormalizeRegistry( Registry &registry )
    {
        std::vector<ValidatorEntry> entries;
        entries.reserve( static_cast<size_t>( registry.validators_size() ) );
        for ( const auto &entry : registry.validators() )
        {
            entries.push_back( entry );
        }

        std::sort( entries.begin(),
                   entries.end(),
                   []( const ValidatorEntry &a, const ValidatorEntry &b )
                   { return a.validator_id() < b.validator_id(); } );

        registry.clear_validators();
        for ( const auto &entry : entries )
        {
            *registry.add_validators() = entry;
        }
    }

    const ValidatorRegistry::ValidatorEntry *ValidatorRegistry::FindValidator( const Registry    &registry,
                                                                               const std::string &validator_id )
    {
        ValidatorRegistryLogger()->trace( "{}: entry id={}", __func__, validator_id.substr( 0, 8 ) );
        for ( const auto &validator : registry.validators() )
        {
            if ( validator.validator_id() == validator_id )
            {
                ValidatorRegistryLogger()->debug( "{}: validator found", __func__ );
                return &validator;
            }
        }
        ValidatorRegistryLogger()->debug( "{}: validator not found", __func__ );
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
        const auto &buffer  = registry_get.value();
        auto        decoded = DeserializeRegistryUpdate( buffer.toVector() );
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

    void ValidatorRegistry::PersistLocalState( const std::string &cid ) const
    {
        logger_->trace( "{}: entry cid={}", __func__, cid );
        crdt::GlobalDB::Buffer registry_cid_key;
        registry_cid_key.put( std::string( RegistryCidKey() ) );
        crdt::GlobalDB::Buffer registry_cid;
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

    void ValidatorRegistry::NotifyInitialized( bool success ) const
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
