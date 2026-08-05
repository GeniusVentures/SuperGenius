/**
 * @file       ValidatorRegistry.cpp
 * @brief      Validator registry and quorum logic for governance.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "blockchain/ValidatorRegistry.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <set>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <gsl/span>

#include "account/GeniusAccount.hpp"
#include "base/hexutil.hpp"
#include "blockchain/Consensus.hpp"
#include "blockchain/ConsensusAuth.hpp"
#include "blockchain/impl/proto/ValidatorRegistry.pb.h"
#include "crypto/hasher.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "outcome/outcome.hpp"

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

        outcome::result<std::string> ExtractConsensusSubjectHash( const ConsensusSubject &subject )
        {
            auto nonce_payload = ConsensusManager::DecodeNonceSubject( subject );
            if ( nonce_payload.has_value() )
            {
                if ( nonce_payload.value().tx_hash().empty() )
                {
                    return outcome::failure( std::errc::invalid_argument );
                }
                return nonce_payload.value().tx_hash();
            }
            auto task_payload = ConsensusManager::DecodeTaskResultSubject( subject );
            if ( task_payload.has_value() )
            {
                if ( task_payload.value().task_result_hash().empty() )
                {
                    return outcome::failure( std::errc::invalid_argument );
                }
                return task_payload.value().task_result_hash();
            }
            auto batch_payload = ConsensusManager::DecodeRegistryBatchSubject( subject );
            if ( batch_payload.has_value() )
            {
                if ( batch_payload.value().batch_root().empty() )
                {
                    return outcome::failure( std::errc::invalid_argument );
                }
                return std::string( batch_payload.value().batch_root() );
            }
            return outcome::failure( std::errc::invalid_argument );
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
        persistence_worker_ = std::thread( [this]() { PersistenceWorkerLoop(); } );
    }

    ValidatorRegistry::~ValidatorRegistry()
    {
        Close();
        logger_->trace( "{}: destroyed", __func__ );
    }

    void ValidatorRegistry::Close()
    {
        std::lock_guard<std::mutex> close_lock( close_mutex_ );
        if ( close_started_ )
        {
            return;
        }
        close_started_ = true;

        const std::string pattern = "/?" + std::string( RegistryKey() );
        if ( db_ )
        {
            db_->UnregisterNewElementCallback( pattern );
            db_->UnregisterElementFilter( pattern );
        }

        {
            std::lock_guard<std::mutex> lock( batch_mutex_ );
            submit_batch_subject_ = nullptr;
        }
        {
            std::unique_lock<std::mutex> lock( persistence_mutex_ );
            persistence_stopping_ = true;
            persistence_cv_.wait( lock, [this]() { return active_batch_handlers_ == 0; } );
        }
        persistence_cv_.notify_all();

        if ( persistence_worker_.joinable() )
        {
            persistence_worker_.join();
        }
    }

    ValidatorRegistry::ActiveBatchHandlerGuard::ActiveBatchHandlerGuard( ValidatorRegistry &registry ) :
        registry_( registry )
    {
        std::lock_guard<std::mutex> lock( registry_.persistence_mutex_ );
        if ( !registry_.persistence_stopping_ )
        {
            ++registry_.active_batch_handlers_;
            active_ = true;
        }
    }

    ValidatorRegistry::ActiveBatchHandlerGuard::~ActiveBatchHandlerGuard()
    {
        if ( !active_ )
        {
            return;
        }
        std::lock_guard<std::mutex> lock( registry_.persistence_mutex_ );
        --registry_.active_batch_handlers_;
        registry_.persistence_cv_.notify_all();
    }

    bool ValidatorRegistry::EnqueueRegistryWrite( std::string subject_hash, RegistryUpdate update )
    {
        {
            std::lock_guard<std::mutex> lock( persistence_mutex_ );
            if ( persistence_stopping_ )
            {
                return false;
            }
            persistence_queue_.push_back( { std::move( subject_hash ), std::move( update ) } );
        }
        persistence_cv_.notify_one();
        return true;
    }

    void ValidatorRegistry::PersistenceWorkerLoop()
    {
        while ( true )
        {
            PendingRegistryWrite write;
            {
                std::unique_lock<std::mutex> lock( persistence_mutex_ );
                persistence_cv_.wait( lock, [this]() { return persistence_stopping_ || !persistence_queue_.empty(); } );
                if ( persistence_queue_.empty() )
                {
                    if ( persistence_stopping_ )
                    {
                        return;
                    }
                    continue;
                }
                write = std::move( persistence_queue_.front() );
                persistence_queue_.pop_front();
            }

            auto                        store_result = StoreRegistryUpdate( write.update );
            std::lock_guard<std::mutex> lock( batch_mutex_ );
            applying_batch_subject_ids_.erase( write.subject_hash );
            if ( store_result.has_error() )
            {
                logger_->error( "{}: failed storing batch registry update subject_hash={} error={}",
                                __func__,
                                write.subject_hash.substr( 0, 8 ),
                                store_result.error().message() );
                continue;
            }
            pending_batch_subject_ids_.erase( write.subject_hash );
            finalized_batch_subject_ids_.insert( write.subject_hash );
        }
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
                BOOST_OUTCOME_TRY( auto cid, CID::fromString( current_cid ) );
                BOOST_OUTCOME_TRY( auto node, old_syncer->GetNodeFromMerkleDAG( cid ) );
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
                (void) new_store->put( registry_cid_key, std::move( registry_cid_value ) );

                BOOST_OUTCOME_TRY( new_crdt->AddDAGNode( node ) );
            }
        }
        ValidatorRegistryLogger()->debug( "{}: Finished migrating validator registry: ", __func__ );
        return outcome::success();
    }

    uint64_t ValidatorRegistry::ComputeWeight( Role role ) const
    {
        logger_->trace( "{}: entry role={}", __func__, static_cast<int>( role ) );
        uint64_t weight = weight_config_.regular_weight_;

        switch ( role )
        {
            case Role::GENESIS:
                weight = weight_config_.genesis_weight_;
                break;
            case Role::FULL:
                weight = weight_config_.full_weight_;
                break;
            case Role::SHARDED:
                weight = weight_config_.sharded_weight_;
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

        const uint64_t cap = MaxWeight( role );
        if ( weight > cap )
        {
            logger_->debug( "{}: weight clamped to max {}", __func__, cap );
            return cap;
        }

        logger_->debug( "{}: computed weight={}", __func__, weight );
        return weight;
    }

    uint64_t ValidatorRegistry::TotalWeight( const Registry &registry )
    {
        ValidatorRegistryLogger()->trace( "{}: entry validators={}", __func__, registry.validators().size() );
        uint64_t total_weight = 0;
        for ( const auto &entry : registry.validators() )
        {
            if ( entry.status() != Status::ACTIVE )
            {
                continue;
            }
            total_weight += entry.weight();
        }
        ValidatorRegistryLogger()->debug( "{}: total_weight={}", __func__, total_weight );
        return total_weight;
    }

    uint64_t ValidatorRegistry::QuorumThreshold( uint64_t total_weight ) const
    {
        ValidatorRegistryLogger()->trace( "{}: entry total_weight={}", __func__, total_weight );
        if ( total_weight == 0 )
        {
            ValidatorRegistryLogger()->debug( "{}: total_weight is zero, threshold=0", __func__ );
            return 0;
        }
        const uint64_t numerator = total_weight * quorum_numerator_;
        const uint64_t threshold = ( numerator + quorum_denominator_ - 1 ) / quorum_denominator_;
        ValidatorRegistryLogger()->debug( "{}: threshold={}", __func__, threshold );
        return threshold;
    }

    bool ValidatorRegistry::IsQuorum( uint64_t accumulated_weight, uint64_t total_weight ) const
    {
        ValidatorRegistryLogger()->trace( "{}: entry accumulated={} total={}",
                                          __func__,
                                          accumulated_weight,
                                          total_weight );
        const bool is_quorum = accumulated_weight >= QuorumThreshold( total_weight );
        ValidatorRegistryLogger()->debug( "{}: is_quorum={}", __func__, is_quorum );
        return is_quorum;
    }

    ValidatorRegistry::SlotQuorumResult ValidatorRegistry::EvaluateSlotQuorum(
        const std::vector<sgns::ConsensusVote> &votes,
        const Registry                         &registry ) const
    {
        return EvaluateSlotQuorumStatic( votes, registry, weight_config_ );
    }

    // REQ-DETERM-01: this function reads ONLY `votes` and `registry` (plus the
    // caller-supplied weight_config). No clocks, no local node state, no network.
    // All peers with identical inputs compute an identical result. Integer math
    // throughout -- no floating point -- to guarantee cross-platform determinism.
    ValidatorRegistry::SlotQuorumResult ValidatorRegistry::EvaluateSlotQuorumStatic(
        const std::vector<sgns::ConsensusVote> &votes,
        const Registry                         &registry,
        const WeightConfig                     &weight_config )
    {
        ValidatorRegistryLogger()->trace( "{}: entry votes={}", __func__, votes.size() );

        struct PublicHashGroup
        {
            size_t   voter_count  = 0;
            uint64_t total_weight = 0;
        };

        using PublicHashGroups = std::unordered_map<std::string, PublicHashGroup>;

        SlotQuorumResult                result;
        std::unordered_set<std::string> seen_voters;
        PublicHashGroups                slot1_groups;
        PublicHashGroups                slot2_groups;
        uint64_t                        slot0_contribution = 0;

        const auto add_public_vote = []( PublicHashGroups &groups, const std::string &hash, uint64_t weight )
        {
            if ( hash.empty() )
            {
                return;
            }
            auto &group = groups[hash];
            ++group.voter_count;
            group.total_weight += weight;
        };

        for ( const auto &vote : votes )
        {
            // Only the first approval from each active validator participates.
            if ( !vote.approve() || !seen_voters.insert( vote.voter_id() ).second )
            {
                continue;
            }
            const auto *validator = ValidatorRegistry::FindValidator( registry, vote.voter_id() );
            if ( !validator || validator->status() != Status::ACTIVE )
            {
                continue;
            }

            const uint64_t weight           = validator->weight();
            result.total_voting_reputation += weight;

            if ( !vote.slot_0_hash().empty() && weight_config.slot_direct_denominator_ > 0 )
            {
                slot0_contribution += ( weight * weight_config.slot_direct_numerator_ ) /
                                      weight_config.slot_direct_denominator_;
            }
            add_public_vote( slot1_groups, vote.slot_1_hash(), weight );
            add_public_vote( slot2_groups, vote.slot_2_hash(), weight );
        }

        if ( result.total_voting_reputation > 0 && weight_config.slot_quorum_denominator_ > 0 )
        {
            const uint64_t numerator = result.total_voting_reputation * weight_config.slot_quorum_numerator_;
            result.threshold         = ( numerator + weight_config.slot_quorum_denominator_ - 1 ) /
                                       weight_config.slot_quorum_denominator_;
        }

        const auto public_contribution = [&]( const PublicHashGroups &groups )
        {
            if ( weight_config.slot_public_denominator_ == 0 )
            {
                return uint64_t{ 0 };
            }

            uint64_t contribution = 0;
            for ( const auto &entry : groups )
            {
                const auto &group = entry.second;
                if ( group.voter_count >= weight_config.slot_public_min_group_ )
                {
                    contribution += ( group.total_weight * weight_config.slot_public_numerator_ ) /
                                    weight_config.slot_public_denominator_;
                }
            }
            return contribution;
        };

        const uint64_t slot1_contribution = public_contribution( slot1_groups );
        const uint64_t slot2_contribution = public_contribution( slot2_groups );

        result.qualified_sum = slot0_contribution + slot1_contribution + slot2_contribution;
        result.has_quorum    = result.qualified_sum > result.threshold;

        ValidatorRegistryLogger()->debug(
            "{}: slot0={} slot1={} slot2={} qualified_sum={} total_voting_rep={} threshold={} has_quorum={}",
            __func__,
            slot0_contribution,
            slot1_contribution,
            slot2_contribution,
            result.qualified_sum,
            result.total_voting_reputation,
            result.threshold,
            result.has_quorum );

        return result;
    }

    // D-08: REGULAR -> FULL promotion decision (REQ-DETERM-01). Pure function of
    // (entry, weight_config): every peer with identical inputs computes an
    // identical promotion decision. ApplyVoteEffects delegates here after the
    // approve-branch weight clamp so the just-clamped weight and just-updated
    // penalty_score are considered.
    bool ValidatorRegistry::EvaluateRegularPromotionStatic( const ValidatorEntry &entry,
                                                            const WeightConfig   &weight_config )
    {
        // GENESIS is never demoted to FULL; SHARDED is not promoted by this rule;
        // an already-FULL entry is not re-promoted (idempotent).
        if ( entry.role() != Role::REGULAR )
        {
            return false;
        }
        // Weight must reach the promotion threshold AND penalty must be strictly
        // below the threshold (a penalized node must earn back reputation).
        return entry.weight() >= weight_config.full_promotion_weight_ &&
               entry.penalty_score() < weight_config.penalty_threshold_;
    }

    ValidatorRegistry::Registry ValidatorRegistry::CreateGenesisRegistry(
        const std::vector<std::string> &genesis_validator_ids ) const
    {
        logger_->trace( "{}: entry count={}", __func__, genesis_validator_ids.size() );
        Registry registry;
        registry.set_epoch( 0 );
        for ( const auto &id : genesis_validator_ids )
        {
            auto *entry = registry.add_validators();
            entry->set_validator_id( id );
            entry->set_role( Role::GENESIS );
            entry->set_status( Status::ACTIVE );
            entry->set_weight( ComputeWeight( entry->role() ) );
            entry->set_penalty_score( 0 );
            entry->set_missed_epochs( 0 );
            logger_->debug( "{}: registered genesis validator id={} weight={}",
                            __func__,
                            id.substr( 0, 8 ),
                            entry->weight() );
        }
        return registry;
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
        const std::vector<std::string>                             &genesis_validator_ids,
        std::function<std::vector<uint8_t>( std::vector<uint8_t> )> sign )
    {
        logger_->trace( "{}: entry count={}", __func__, genesis_validator_ids.size() );
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
        *update.mutable_registry() = CreateGenesisRegistry( genesis_validator_ids );
        update.clear_prev_registry_hash();

        auto signing_bytes = ComputeUpdateSigningBytes( update );
        if ( signing_bytes.has_error() )
        {
            logger_->error( "{}: failed to compute signing bytes", __func__ );
            return outcome::failure( signing_bytes.error() );
        }

        SignatureEntry signature_entry;
        signature_entry.set_validator_id( genesis_validator_ids.front() );
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
        if ( !cid_string.has_value() )
        {
            logger_->error( "{}: registry stored but CID missing", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        // Make local creation authoritative before returning instead of depending on CRDT callback timing.
        RegistryUpdateReceived( { registry_key.GetKey(), std::move( update_buffer ) }, cid_string.value() );

        logger_->info( "{}: stored and initialized genesis registry CID {}", __func__, cid_string.value() );
        return outcome::success();
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::LoadCurrentRegistry() const
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
            logger_->error( "{}: failed to load registry update", __func__ );
            return outcome::failure( update_result.error() );
        }
        logger_->debug( "{}: returning registry from update", __func__ );
        return update_result.value().registry();
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::LoadRegistryByCid( const std::string &cid ) const
    {
        ValidatorRegistryLogger()->trace( "{}: entry cid={}", __func__, cid );

        // A registry write stores the complete RegistryUpdate as one CRDT element
        // value. The delta at this CID therefore contains the requested snapshot;
        // ancestor deltas are not needed to reconstruct it.
        BOOST_OUTCOME_TRY( auto delta_key_values, db_->GetLocalDeltaKeyValues( cid ) );
        ValidatorRegistryLogger()->trace( "{}: Got local delta with {} entries ", __func__, delta_key_values.size() );

        crdt::HierarchicalKey registry_key{ std::string( RegistryKey() ) };
        for ( const auto &[key, registry_update_buffer] : delta_key_values )
        {
            ValidatorRegistryLogger()->trace( "{}: Processing delta element key={}", __func__, key );
            if ( key != registry_key.GetKey() )
            {
                ValidatorRegistryLogger()->debug( "{}: Skipping non-registry content key={}, registry_key={}",
                                                  __func__,
                                                  key,
                                                  registry_key.GetKey() );
                continue;
            }
            std::vector<uint8_t> serialized_update( registry_update_buffer.begin(), registry_update_buffer.end() );
            auto                 update = DeserializeRegistryUpdate( serialized_update );
            if ( update.has_error() )
            {
                ValidatorRegistryLogger()->error( "{}: failed to parse registry update ", __func__ );
                continue;
            }

            ValidatorRegistryLogger()->debug( "{}: Grabbing registry from cid {} and key={}", __func__, cid, key );
            return update.value().registry();
        }

        return outcome::failure( std::errc::no_such_file_or_directory );
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
        auto registry_result = LoadCurrentRegistry();
        if ( registry_result.has_error() )
        {
            logger_->error( "{}: failed to load registry: {}", __func__, registry_result.error().message() );
            return outcome::failure( registry_result.error() );
        }

        auto       current_registry = registry_result.value();
        const auto current_cid      = GetRegistryCid();
        if ( !ValidateCertificateForUpdate( certificate, current_registry, current_cid ) )
        {
            logger_->error( "{}: invalid certificate", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        BOOST_OUTCOME_TRY( auto votes, ExtractCertificateVotes( certificate, current_registry ) );

        RegistryUpdate update;
        update.set_prev_registry_hash( current_cid );
        *update.mutable_registry() = BuildRegistryFromAggregatedVotes( current_registry, votes );

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

    void ValidatorRegistry::SetCertificatesPerBatch( size_t batch_size )
    {
        if ( batch_size == 0 )
        {
            logger_->warn( "{}: ignored zero batch size", __func__ );
            return;
        }
        std::lock_guard<std::mutex> lock( batch_mutex_ );
        certificates_per_batch_ = batch_size;
    }

    void ValidatorRegistry::SetBatchSubjectSubmitter(
        std::function<outcome::result<void>( const ConsensusSubject &subject )> submitter )
    {
        std::lock_guard<std::mutex> lock( batch_mutex_ );
        submit_batch_subject_ = std::move( submitter );
    }

    outcome::result<std::string> ValidatorRegistry::ComputeBatchRoot(
        const std::vector<std::string> &subject_hashes ) const
    {
        if ( subject_hashes.empty() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        std::string payload( subject_hashes[0] );
        for ( size_t i = 1; i < subject_hashes.size(); ++i )
        {
            payload.push_back( '\n' );
            payload += subject_hashes[i];
        }
        auto hash = sgns::crypto::sha2_256( payload.data(), payload.size() );
        return base::hex_lower( gsl::span<const uint8_t>( hash.data(), hash.size() ) );
    }

    outcome::result<std::vector<std::string>> ValidatorRegistry::SelectBatchSubjects(
        const std::string         &base_registry_cid,
        uint64_t                   base_registry_epoch,
        uint32_t                   certificate_count,
        std::optional<std::string> expected_root ) const
    {
        if ( certificate_count == 0 )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        std::vector<std::string> selected;
        {
            std::lock_guard<std::mutex> lock( batch_mutex_ );
            const auto                  key = BuildBatchKey( base_registry_cid, base_registry_epoch );
            auto                        it  = pending_certificate_subjects_by_base_.find( key );
            if ( it == pending_certificate_subjects_by_base_.end() ||
                 it->second.size() < static_cast<size_t>( certificate_count ) )
            {
                return outcome::failure( std::errc::resource_unavailable_try_again );
            }
            selected.assign( it->second.begin(), it->second.end() );
        }
        if ( selected.size() > static_cast<size_t>( certificate_count ) )
        {
            selected.resize( certificate_count );
        }
        BOOST_OUTCOME_TRY( auto root_result, ComputeBatchRoot( selected ) );
        if ( expected_root.has_value() && root_result != expected_root.value() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return selected;
    }

    outcome::result<sgns::ConsensusCertificate> ValidatorRegistry::LoadCertificateBySubjectHash(
        const std::string &subject_hash ) const
    {
        const auto cert_key = std::string( "/cert/" ) + subject_hash;
        BOOST_OUTCOME_TRY( auto cert_get, db_->Get( crdt::HierarchicalKey( cert_key ) ) );

        sgns::ConsensusCertificate certificate;
        if ( !certificate.ParseFromString( cert_get.toString() ) )
        {
            return outcome::failure( std::errc::invalid_argument );
        }
        return certificate;
    }

    outcome::result<void> ValidatorRegistry::OnFinalizedCertificate( const sgns::ConsensusCertificate &certificate )
    {
        if ( !certificate.has_proposal() )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        if ( ConsensusManager::DecodeRegistryBatchSubject( certificate.proposal().subject() ).has_value() )
        {
            return outcome::success();
        }

        BOOST_OUTCOME_TRY( auto subject_hash_result, ExtractConsensusSubjectHash( certificate.proposal().subject() ) );

        const auto key = BuildBatchKey( certificate.registry_cid(), certificate.registry_epoch() );
        {
            std::lock_guard<std::mutex> lock( batch_mutex_ );
            pending_certificate_subjects_by_base_[key].insert( subject_hash_result );
        }

        return TryCreateAndSubmitBatchProposal( certificate.registry_cid(), certificate.registry_epoch() );
    }

    outcome::result<void> ValidatorRegistry::TryCreateAndSubmitBatchProposal( const std::string &base_registry_cid,
                                                                              uint64_t           base_registry_epoch )
    {
        std::function<outcome::result<void>( const ConsensusSubject &subject )> submitter;
        size_t                                                                  threshold = 0;
        {
            std::lock_guard<std::mutex> lock( batch_mutex_ );
            submitter = submit_batch_subject_;
            threshold = certificates_per_batch_;
        }
        if ( !submitter || threshold == 0 )
        {
            return outcome::success();
        }

        BOOST_OUTCOME_TRY( auto base_registry, LoadRegistryByCid( base_registry_cid ) );
        if ( base_registry.epoch() != base_registry_epoch )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        BOOST_OUTCOME_TRY( auto selected,
                           SelectBatchSubjects( base_registry_cid,
                                                base_registry_epoch,
                                                static_cast<uint32_t>( threshold ),
                                                std::nullopt ) );

        BOOST_OUTCOME_TRY( auto root, ComputeBatchRoot( selected ) );

        BOOST_OUTCOME_TRY( auto subject,
                           ConsensusManager::CreateRegistryBatchSubject( genesis_authority_,
                                                                         base_registry_cid,
                                                                         base_registry_epoch,
                                                                         base_registry_epoch + 1,
                                                                         static_cast<uint32_t>( threshold ),
                                                                         root ) );

        {
            std::lock_guard<std::mutex> lock( batch_mutex_ );
            BOOST_OUTCOME_TRY( auto batch_hash, ExtractConsensusSubjectHash( subject ) );
            if ( pending_batch_subject_ids_.find( batch_hash ) != pending_batch_subject_ids_.end() )
            {
                return outcome::success();
            }
            pending_batch_subject_ids_.insert( batch_hash );
        }

        return submitter( subject );
    }

    ValidatorRegistry::BatchSubjectDecision ValidatorRegistry::EvaluateBatchSubject( const ConsensusSubject &subject )
    {
        auto payload_result = ConsensusManager::DecodeRegistryBatchSubject( subject );
        if ( payload_result.has_error() )
        {
            return BatchSubjectDecision::Reject;
        }

        const auto &payload         = payload_result.value();
        auto        selected_result = SelectBatchSubjects( payload.base_registry_cid(),
                                                           payload.base_registry_epoch(),
                                                           payload.certificate_count(),
                                                           std::string( payload.batch_root() ) );
        if ( selected_result.has_error() )
        {
            if ( selected_result.error() == std::errc::resource_unavailable_try_again )
            {
                return BatchSubjectDecision::Pending;
            }
            return BatchSubjectDecision::Reject;
        }

        auto registry_result = LoadRegistryByCid( payload.base_registry_cid() );
        if ( registry_result.has_error() )
        {
            return BatchSubjectDecision::Pending;
        }

        if ( registry_result.value().epoch() != payload.base_registry_epoch() )
        {
            return BatchSubjectDecision::Reject;
        }

        return BatchSubjectDecision::Approve;
    }

    ValidatorRegistry::BatchCertificateDecision ValidatorRegistry::HandleBatchCertificate(
        const std::string                &subject_hash,
        const sgns::ConsensusCertificate &certificate )
    {
        ActiveBatchHandlerGuard active_handler( *this );
        if ( !active_handler )
        {
            return BatchCertificateDecision::Stalled;
        }

        {
            std::lock_guard<std::mutex> lock( batch_mutex_ );
            if ( finalized_batch_subject_ids_.find( subject_hash ) != finalized_batch_subject_ids_.end() ||
                 applying_batch_subject_ids_.find( subject_hash ) != applying_batch_subject_ids_.end() )
            {
                return BatchCertificateDecision::Approve;
            }
            applying_batch_subject_ids_.insert( subject_hash );
        }
        const auto stop_applying = [this, &subject_hash]( BatchCertificateDecision decision )
        {
            std::lock_guard<std::mutex> lock( batch_mutex_ );
            applying_batch_subject_ids_.erase( subject_hash );
            return decision;
        };

        auto payload_result = ConsensusManager::DecodeRegistryBatchSubject( certificate.proposal().subject() );
        if ( payload_result.has_error() )
        {
            return stop_applying( BatchCertificateDecision::Reject );
        }

        const auto &payload              = payload_result.value();
        auto        base_registry_result = LoadRegistryByCid( payload.base_registry_cid() );
        if ( base_registry_result.has_error() )
        {
            return stop_applying( BatchCertificateDecision::Stalled );
        }
        if ( base_registry_result.value().epoch() != payload.base_registry_epoch() ||
             !ValidateCertificate( certificate, base_registry_result.value(), payload.base_registry_cid() ) )
        {
            return stop_applying( BatchCertificateDecision::Reject );
        }

        auto selected_result = SelectBatchSubjects( payload.base_registry_cid(),
                                                    payload.base_registry_epoch(),
                                                    payload.certificate_count(),
                                                    std::string( payload.batch_root() ) );
        if ( selected_result.has_error() )
        {
            return stop_applying( BatchCertificateDecision::Reject );
        }

        auto registry_result = BuildRegistryFromBatchCertificates( base_registry_result.value(),
                                                                   payload,
                                                                   selected_result.value() );
        if ( registry_result.has_error() )
        {
            return stop_applying( BatchCertificateDecision::Reject );
        }

        RegistryUpdate update;
        update.set_prev_registry_hash( payload.base_registry_cid() );
        *update.mutable_registry() = registry_result.value();
        std::string serialized_cert;
        if ( !certificate.SerializeToString( &serialized_cert ) )
        {
            logger_->error( "{}: failed to serialize certificate", __func__ );
            return stop_applying( BatchCertificateDecision::Reject );
        }
        update.set_certificate( serialized_cert );
        for ( const auto &tx_subject_hash : selected_result.value() )
        {
            update.add_batch_certificate_subject_hashes( tx_subject_hash );
        }

        if ( !EnqueueRegistryWrite( subject_hash, std::move( update ) ) )
        {
            return stop_applying( BatchCertificateDecision::Stalled );
        }
        return BatchCertificateDecision::Approve;
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

    bool ValidatorRegistry::IsActiveValidator( const std::string &validator_id ) const
    {
        auto weight = GetValidatorWeight( validator_id );
        return weight.has_value() && weight.value().has_value();
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
            [weak_self]( const crdt::CRDTCallbackManager::NewDataPair &new_data, const std::string &cid )
            {
                if ( auto strong = weak_self.lock() )
                {
                    strong->RegistryUpdateReceived( new_data, cid );
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

        RegistryUpdate update = decoded_update.value();
        if ( !VerifyUpdate( update, false ) )
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
            if ( cached_registry_ )
            {
                const auto current_epoch           = cached_registry_->epoch();
                const auto incoming_epoch          = decoded.value().registry().epoch();
                const bool same_epoch_noncanonical = incoming_epoch == current_epoch && !cached_registry_id_.empty() &&
                                                     cached_registry_id_ <= cid;
                if ( incoming_epoch < current_epoch || same_epoch_noncanonical )
                {
                    logger_->debug(
                        "{}: ignoring non-canonical registry update cid={} epoch={} current_cid={} current_epoch={}",
                        __func__,
                        cid,
                        incoming_epoch,
                        cached_registry_id_,
                        current_epoch );
                    return;
                }
            }
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

    bool ValidatorRegistry::VerifyUpdate( const RegistryUpdate &update, bool enforce_time_window ) const
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

        const std::string      &prev_registry_cid = update.prev_registry_hash();
        std::optional<Registry> base_registry_snapshot;
        std::string             current_id;
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            base_registry_snapshot = cached_registry_;
            current_id             = cached_registry_id_;
        }

        const bool needs_to_fetch_registry = !base_registry_snapshot || current_id.empty() ||
                                             prev_registry_cid != current_id;
        if ( !prev_registry_cid.empty() && needs_to_fetch_registry )
        {
            auto base_registry_result = LoadRegistryByCid( prev_registry_cid );
            if ( base_registry_result.has_error() )
            {
                logger_->error( "{}: base registry unavailable cid={}", __func__, prev_registry_cid );
                return false;
            }
            base_registry_snapshot = base_registry_result.value();
        }

        if ( prev_registry_cid.empty() )
        {
            if ( base_registry_snapshot )
            {
                logger_->error( "{}: missing base registry for update", __func__ );
                return false;
            }

            logger_->debug( "{}: verifying genesis update", __func__ );
            for ( const auto &signature : update.signatures() )
            {
                if ( signature.validator_id() == genesis_authority_ &&
                     GeniusAccount::VerifySignature( signature.validator_id(),
                                                     signature.signature(),
                                                     signing_bytes.value() ) )
                {
                    logger_->info( "{}: genesis update verified", __func__ );
                    return true;
                }
            }
            logger_->error( "{}: genesis update verification failed", __func__ );
            return false;
        }

        if ( !base_registry_snapshot )
        {
            logger_->error( "{}: missing base registry for update", __func__ );
            return false;
        }
        const Registry &base_registry = base_registry_snapshot.value();

        if ( update.registry().epoch() != base_registry.epoch() + 1 )
        {
            logger_->error( "{}: epoch not next expected", __func__ );
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

            const bool certificate_valid = enforce_time_window
                                               ? ValidateCertificateForUpdate( certificate,
                                                                               base_registry,
                                                                               prev_registry_cid )
                                               : ValidateCertificate( certificate, base_registry, prev_registry_cid );
            if ( !certificate_valid )
            {
                logger_->error( "{}: certificate verification failed", __func__ );
                return false;
            }

            auto expected_result = BuildExpectedRegistryFromCertificate( update, certificate, base_registry );
            if ( expected_result.has_error() )
            {
                return false;
            }

            Registry provided = update.registry();
            Registry expected = expected_result.value();
            const auto sort_validators = []( Registry &registry )
            {
                auto *validators = registry.mutable_validators();
                std::sort( validators->begin(),
                           validators->end(),
                           []( const ValidatorEntry &a, const ValidatorEntry &b )
                           { return a.validator_id() < b.validator_id(); } );
            };
            sort_validators( provided );
            sort_validators( expected );

            if ( provided.SerializeAsString() != expected.SerializeAsString() )
            {
                logger_->error( "{}: registry mismatch against certificate", __func__ );
                return false;
            }

            logger_->info( "{}: certificate-based update verified", __func__ );
            return true;
        }

        uint64_t              total_weight       = TotalWeight( base_registry );
        uint64_t              accumulated_weight = 0;
        std::set<std::string> seen;

        for ( const auto &signature : update.signatures() )
        {
            if ( !seen.insert( signature.validator_id() ).second )
            {
                continue;
            }

            const auto *validator = FindValidator( base_registry, signature.validator_id() );
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

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::BuildExpectedRegistryFromCertificate(
        const RegistryUpdate             &update,
        const sgns::ConsensusCertificate &certificate,
        const Registry                   &base_registry ) const
    {
        auto batch_payload = ConsensusManager::DecodeRegistryBatchSubject( certificate.proposal().subject() );
        if ( batch_payload.has_error() )
        {
            BOOST_OUTCOME_TRY( auto votes, ExtractCertificateVotes( certificate, base_registry ) );
            return BuildRegistryFromAggregatedVotes( base_registry, votes );
        }

        const auto &payload = batch_payload.value();
        if ( payload.base_registry_cid() != update.prev_registry_hash() ||
             payload.base_registry_epoch() != base_registry.epoch() ||
             payload.target_registry_epoch() != base_registry.epoch() + 1 )
        {
            logger_->error( "{}: batch subject metadata mismatch", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }
        if ( update.batch_certificate_subject_hashes_size() != static_cast<int>( payload.certificate_count() ) )
        {
            logger_->error( "{}: batch subject certificate count mismatch", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        std::vector<std::string> subject_hashes( update.batch_certificate_subject_hashes().begin(),
                                                 update.batch_certificate_subject_hashes().end() );
        std::sort( subject_hashes.begin(), subject_hashes.end() );
        auto root_result = ComputeBatchRoot( subject_hashes );
        if ( root_result.has_error() || std::string( payload.batch_root() ) != root_result.value() )
        {
            logger_->error( "{}: batch root mismatch", __func__ );
            return outcome::failure( std::errc::invalid_argument );
        }

        return BuildRegistryFromBatchCertificates( base_registry, payload, subject_hashes );
    }

    bool ValidatorRegistry::ValidateCertificate( const sgns::ConsensusCertificate &certificate,
                                                 const Registry                   &current_registry,
                                                 std::string_view                  expected_registry_cid ) const
    {
        logger_->trace( "{}: entry proposal_id={}", __func__, certificate.proposal_id() );
        if ( !certificate.has_proposal() )
        {
            logger_->error( "{}: missing proposal in certificate", __func__ );
            return false;
        }

        const auto &proposal = certificate.proposal();
        if ( !ValidateProposal( proposal ) )
        {
            logger_->error( "{}: invalid proposal signature", __func__ );
            return false;
        }
        if ( proposal.proposal_id() != certificate.proposal_id() )
        {
            logger_->error( "{}: proposal_id mismatch cert={} proposal={}",
                            __func__,
                            certificate.proposal_id(),
                            proposal.proposal_id() );
            return false;
        }
        if ( proposal.registry_epoch() != certificate.registry_epoch() ||
             proposal.registry_cid() != certificate.registry_cid() )
        {
            logger_->error( "{}: registry metadata mismatch proposal_id={}", __func__, proposal.proposal_id() );
            return false;
        }
        if ( proposal.registry_epoch() != current_registry.epoch() )
        {
            logger_->error( "{}: registry epoch mismatch cert={} registry={}",
                            __func__,
                            proposal.registry_epoch(),
                            current_registry.epoch() );
            return false;
        }

        if ( !expected_registry_cid.empty() && !proposal.registry_cid().empty() &&
             proposal.registry_cid() != expected_registry_cid )
        {
            logger_->error( "{}: registry CID mismatch cert={} registry={}",
                            __func__,
                            proposal.registry_cid(),
                            expected_registry_cid );
            return false;
        }

        if ( certificate.proposal_id().empty() )
        {
            logger_->error( "{}: empty proposal_id", __func__ );
            return false;
        }

        return true;
    }

    bool ValidatorRegistry::ValidateCertificateForUpdate( const sgns::ConsensusCertificate &certificate,
                                                          const Registry                   &current_registry,
                                                          std::string_view expected_registry_cid ) const
    {
        const uint64_t window_ms = weight_config_.certificate_timestamp_window_ms_;
        if ( window_ms > 0 )
        {
            const auto now_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch() )
                                     .count();
            const auto cert_ms = static_cast<int64_t>( certificate.timestamp() );
            const auto diff    = std::llabs( now_ms - cert_ms );
            if ( cert_ms == 0 || static_cast<uint64_t>( diff ) > window_ms )
            {
                logger_->error( "{}: certificate timestamp outside window", __func__ );
                return false;
            }
        }
        return ValidateCertificate( certificate, current_registry, expected_registry_cid );
    }

    outcome::result<ValidatorRegistry::CertificateVotes> ValidatorRegistry::ExtractCertificateVotes(
        const sgns::ConsensusCertificate &certificate,
        const Registry                   &current_registry ) const
    {
        CertificateVotes                result;
        uint64_t                        total_weight    = TotalWeight( current_registry );
        uint64_t                        approved_weight = 0;
        std::unordered_set<std::string> seen;

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
                result.unregistered_votes[vote.voter_id()] = vote.approve();
                continue;
            }

            result.registered_votes[vote.voter_id()] = vote.approve();

            if ( vote.approve() && validator->status() == Status::ACTIVE )
            {
                approved_weight += validator->weight();
            }
        }

        if ( !IsQuorum( approved_weight, total_weight ) )
        {
            logger_->error( "{}: quorum not reached approved={} total={}", __func__, approved_weight, total_weight );
            return outcome::failure( std::errc::invalid_argument );
        }

        logger_->debug( "{}: quorum verified approved={} total={}", __func__, approved_weight, total_weight );
        return result;
    }

    ValidatorRegistry::Registry ValidatorRegistry::BuildRegistryFromAggregatedVotes(
        const Registry         &current_registry,
        const CertificateVotes &votes ) const
    {
        Registry next = current_registry;
        next.set_epoch( current_registry.epoch() + 1 );

        InsertNewValidators( next, votes.unregistered_votes );

        std::vector<ValidatorEntry> entries;
        entries.reserve( static_cast<size_t>( next.validators_size() ) );
        for ( const auto &entry : next.validators() )
        {
            entries.push_back( entry );
        }

        ApplyVoteEffects( entries, votes.registered_votes );
        std::unordered_set<std::string> participants;
        participants.reserve( votes.registered_votes.size() + votes.unregistered_votes.size() );
        for ( const auto &pair : votes.registered_votes )
        {
            participants.insert( pair.first );
        }
        for ( const auto &pair : votes.unregistered_votes )
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
        return next;
    }

    outcome::result<ValidatorRegistry::Registry> ValidatorRegistry::BuildRegistryFromBatchCertificates(
        const Registry                 &current_registry,
        const RegistryBatchSubject     &payload,
        const std::vector<std::string> &subject_hashes ) const
    {
        std::unordered_map<std::string, int64_t> registered_scores;
        std::unordered_map<std::string, int64_t> unregistered_scores;
        for ( const auto &subject_hash : subject_hashes )
        {
            auto certificate_result = LoadCertificateBySubjectHash( subject_hash );
            if ( certificate_result.has_error() )
            {
                logger_->error( "{}: missing certificate for batch hash={}", __func__, subject_hash.substr( 0, 8 ) );
                return outcome::failure( certificate_result.error() );
            }
            const auto &certificate = certificate_result.value();
            if ( certificate.registry_cid() != payload.base_registry_cid() ||
                 certificate.registry_epoch() != payload.base_registry_epoch() )
            {
                logger_->error( "{}: batch certificate registry mismatch", __func__ );
                return outcome::failure( std::errc::invalid_argument );
            }
            BOOST_OUTCOME_TRY( auto votes, ExtractCertificateVotes( certificate, current_registry ) );
            for ( const auto &[validator_id, approve] : votes.registered_votes )
            {
                registered_scores[validator_id] += approve ? 1 : -1;
            }
            for ( const auto &[validator_id, approve] : votes.unregistered_votes )
            {
                unregistered_scores[validator_id] += approve ? 1 : -1;
            }
        }

        CertificateVotes votes;
        for ( const auto &[validator_id, score] : registered_scores )
        {
            votes.registered_votes[validator_id] = score >= 0;
        }
        for ( const auto &[validator_id, score] : unregistered_scores )
        {
            votes.unregistered_votes[validator_id] = score >= 0;
        }
        return BuildRegistryFromAggregatedVotes( current_registry, votes );
    }

    void ValidatorRegistry::InsertNewValidators( Registry                                    &registry,
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
            auto       it      = unregistered_votes.find( validator_id );
            const bool approve = ( it != unregistered_votes.end() ) ? it->second : true;
            entry->set_penalty_score( approve ? 0 : 1 );
            entry->set_missed_epochs( 0 );
            logger_->debug( "{}: added validator id={} weight={} approve={} penalty={} status={}",
                            __func__,
                            validator_id.substr( 0, 8 ),
                            entry->weight(),
                            approve,
                            entry->penalty_score(),
                            static_cast<int>( entry->status() ) );
            ++added;
        }
    }

    void ValidatorRegistry::ApplyVoteEffects( std::vector<ValidatorEntry>                 &entries,
                                              const std::unordered_map<std::string, bool> &registered_votes ) const
    {
        for ( auto &entry : entries )
        {
            const auto vote_it = registered_votes.find( entry.validator_id() );
            if ( vote_it == registered_votes.end() )
            {
                continue;
            }

            entry.set_missed_epochs( 0 );
            uint32_t penalty = entry.penalty_score();

            if ( const bool reject = !vote_it->second; reject )
            {
                const uint32_t cap = weight_config_.penalty_cap_;
                if ( entry.status() != Status::BLACKLISTED )
                {
                    if ( penalty < cap )
                    {
                        ++penalty;
                    }
                    if ( penalty >= weight_config_.penalty_threshold_ )
                    {
                        entry.set_status( Status::BLACKLISTED );
                    }
                }
                if ( entry.status() == Status::BLACKLISTED )
                {
                    penalty  = std::min( penalty, cap );
                    penalty += std::min( weight_config_.blacklist_bump_, cap - penalty );
                }
                entry.set_penalty_score( penalty );
                continue;
            }

            if ( penalty > 0 )
            {
                --penalty;
            }
            entry.set_penalty_score( penalty );

            if ( entry.status() != Status::ACTIVE )
            {
                if ( penalty == 0 )
                {
                    entry.set_status( Status::ACTIVE );
                }
                continue;
            }

            const uint64_t increment = weight_config_.approval_increment_;
            if ( increment > 0 )
            {
                const uint64_t cap    = MaxWeight( entry.role() );
                const uint64_t weight = std::min( entry.weight(), cap );
                entry.set_weight( weight + std::min( increment, cap - weight ) );
            }

            if ( EvaluateRegularPromotionStatic( entry, weight_config_ ) )
            {
                entry.set_role( Role::FULL );
            }
        }
    }

    uint64_t ValidatorRegistry::MaxWeight( Role role ) const
    {
        switch ( role )
        {
            case Role::GENESIS:
                return weight_config_.genesis_max_weight_;
            case Role::FULL:
                return weight_config_.full_max_weight_;
            case Role::SHARDED:
                return weight_config_.sharded_max_weight_;
            case Role::REGULAR:
            default:
                return weight_config_.regular_max_weight_;
        }
    }

    void ValidatorRegistry::ApplyInactivityDecay( std::vector<ValidatorEntry>           &entries,
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
            uint32_t missed = entry.missed_epochs();
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
                    const uint64_t old_weight = entry.weight();
                    const uint64_t new_weight = ( entry.weight() > dec ) ? ( entry.weight() - dec ) : 0;
                    entry.set_weight( new_weight );
                    if ( new_weight == 0 )
                    {
                        entry.set_status( Status::SUSPENDED );
                    }
                    logger_->debug( "{}: inactivity decay id={} missed={} weight {}->{} status={}",
                                    __func__,
                                    entry.validator_id().substr( 0, 8 ),
                                    missed,
                                    old_weight,
                                    new_weight,
                                    static_cast<int>( entry.status() ) );
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

        const uint64_t weight_cap = weight_config_.genesis_weight_ * weight_config_.total_weight_cap_multiplier_;
        if ( weight_cap == 0 || total_active <= weight_cap )
        {
            return;
        }

        logger_->debug( "{}: applying total weight cap total_active={} cap={}", __func__, total_active, weight_cap );

        uint64_t            scaled_sum = 0;
        std::vector<size_t> active_indices;
        active_indices.reserve( entries.size() );
        for ( size_t i = 0; i < entries.size(); ++i )
        {
            if ( entries[i].status() != Status::ACTIVE )
            {
                continue;
            }
            const uint64_t old_weight = entries[i].weight();
            const uint64_t scaled     = ( entries[i].weight() * weight_cap ) / total_active;
            entries[i].set_weight( scaled );
            scaled_sum += scaled;
            active_indices.push_back( i );
            logger_->debug( "{}: cap scale id={} weight {}->{}",
                            __func__,
                            entries[i].validator_id().substr( 0, 8 ),
                            old_weight,
                            scaled );
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
            idx        = ( idx + 1 ) % active_indices.size();
        }

        for ( const auto active_idx : active_indices )
        {
            logger_->debug( "{}: cap final id={} weight={}",
                            __func__,
                            entries[active_idx].validator_id().substr( 0, 8 ),
                            entries[active_idx].weight() );
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

    void ValidatorRegistry::RetryInitializationIfNeeded()
    {
        {
            std::shared_lock<std::shared_mutex> lock( cache_mutex_ );
            if ( cache_initialized_ )
            {
                return;
            }
        }

        logger_->debug( "{}: cache not yet initialized, retrying head-CID discovery", __func__ );

        std::set<CID> heads_to_request;
        auto          heads_result = db_->GetCRDTHeadList();
        if ( heads_result.has_value() )
        {
            const auto &heads_map = heads_result.value().first;
            auto        it        = heads_map.find( std::string( ValidatorTopic() ) );
            if ( it != heads_map.end() )
            {
                heads_to_request = it->second;
            }
        }

        if ( !heads_to_request.empty() )
        {
            logger_->debug( "{}: retry found {} head(s) to request", __func__, heads_to_request.size() );
            RequestHeadCids( heads_to_request );
        }
        else
        {
            logger_->debug( "{}: retry found no heads yet available", __func__ );
        }
    }

    void ValidatorRegistry::PersistLocalState( const std::string &cid ) const
    {
        logger_->trace( "{}: entry cid={}", __func__, cid );
        crdt::GlobalDB::Buffer registry_cid_key;
        registry_cid_key.put( std::string( RegistryCidKey() ) );
        crdt::GlobalDB::Buffer registry_cid;
        registry_cid.put( cid );
        (void) db_->GetDataStore()->put( registry_cid_key, registry_cid );
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

            explicit RequestState( size_t remaining_count ) : remaining( remaining_count )
            {
            }
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
