/**
 * @file       Blockchain.cpp
 * @brief      Header file for the Blockchain class, which provides an interface for block storage operations.
 * @date       2025-10-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <chrono>
#include <system_error>
#include <unordered_set>
#include "blockchain/Blockchain.hpp"
#include "blockchain/ValidatorRegistry.hpp"
#include <primitives/cid/cid.hpp>
#include "crdt/graphsync_dagsyncer.hpp"
#include "outcome/outcome.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns, Blockchain::Error, err )
{
    using Error = sgns::Blockchain::Error;
    switch ( err )
    {
        case Error::GENESIS_BLOCK_CREATION_FAILED:
            return "Couldn't create genesis block";
        case Error::GENESIS_BLOCK_INVALID_SIGNATURE:
            return "Genesis block has invalid signature";
        case Error::GENESIS_BLOCK_UNAUTHORIZED_CREATOR:
            return "Genesis block created by unauthorized user";
        case Error::GENESIS_BLOCK_SERIALIZATION_FAILED:
            return "Failed to serialize/deserialize genesis block";
        case Error::GENESIS_BLOCK_MISSING:
            return "Genesis block was not received";
        case Error::ACCOUNT_CREATION_BLOCK_MISSING:
            return "Account creation block was not received";
        case Error::ACCOUNT_CREATION_BLOCK_CREATION_FAILED:
            return "Couldn't create account creation block";
        case Error::ACCOUNT_CREATION_BLOCK_INVALID_SIGNATURE:
            return "Account creation block has invalid signature";
        case Error::ACCOUNT_CREATION_BLOCK_SERIALIZATION_FAILED:
            return "Failed to serialize/deserialize account creation block";
        case Error::ACCOUNT_CREATION_BLOCK_INVALID_GENESIS_LINK:
            return "Account creation block not properly linked to genesis";
        case Error::VALIDATOR_REGISTRY_CREATION_FAILED:
            return "Couldn't create validator registry";
        case Error::BLOCKCHAIN_NOT_INITIALIZED:
            return "Blockchain not fully initialized";
    }
    return "Unknown error";
}

namespace sgns
{
    namespace
    {
        base::Logger blockchain_logger()
        {
            // Always call base::createLogger to get the current logger
            // This will return existing logger or create new one as needed
            return base::createLogger( "Blockchain" );
        }
    }

    std::string &Blockchain::AuthorizedFullNodeAddressStorage()
    {
        static std::string address( DEFAULT_FULL_NODE_PUB_ADDRESS );
        return address;
    }

    std::shared_ptr<Blockchain> Blockchain::New( std::shared_ptr<crdt::GlobalDB>            global_db,
                                                 std::shared_ptr<GeniusAccount>             account,
                                                 std::shared_ptr<ipfs_pubsub::GossipPubSub> pubsub,
                                                 BlockchainCallback                         callback )
    {
        auto instance = std::shared_ptr<Blockchain>(
            new Blockchain( std::move( global_db ), std::move( account ), std::move( callback ) ) );
        auto request_validator_registry = []( const std::shared_ptr<Blockchain> &self )
        {
            if ( !self )
            {
                return;
            }
            auto request_result = self->account_->RequestValidatorRegistry(
                TIMEOUT_GENESIS_BLOCK_MS,
                [weak_ptr( std::weak_ptr<Blockchain>( self ) )]( outcome::result<std::string> registry_cid_res )
                {
                    if ( auto strong = weak_ptr.lock() )
                    {
                        if ( registry_cid_res.has_error() )
                        {
                            strong->logger_->warn( "[{}] Validator registry request finished with error",
                                                   strong->account_->GetAddress().substr( 0, 8 ) );
                            return;
                        }
                        strong->logger_->debug( "[{}] Validator registry request finished with CID {}",
                                                strong->account_->GetAddress().substr( 0, 8 ),
                                                registry_cid_res.value().substr( 0, 8 ) );
                    }
                } );
            if ( request_result.has_error() )
            {
                self->logger_->warn( "[{}] Failed to request validator registry during blockchain init",
                                     self->account_->GetAddress().substr( 0, 8 ) );
            }
        };

        instance->logger_->info( "[{}] Blockchain instance created with authorized full node: {}",
                                 instance->account_->GetAddress().substr( 0, 8 ),
                                 GetAuthorizedFullNodeAddress().substr( 0, 8 ) );

        const bool genesis_filter_initialized = instance->db_->RegisterElementFilter(
            "/?" + std::string( GENESIS_KEY ),
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )](
                const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->FilterGenesis( element );
                }
                return std::nullopt;
            } );
        const bool account_creation_filter_initialized = instance->db_->RegisterElementFilter(
            "/?" + std::string( ACCOUNT_CREATION_KEY_PREFIX ) + ".*",
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )](
                const crdt::pb::Element &element ) -> std::optional<std::vector<crdt::pb::Element>>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->FilterAccountCreation( element );
                }
                return std::nullopt;
            } );

        instance->validator_registry_ = ValidatorRegistry::New(
            instance->db_,
            2,
            3,
            ValidatorRegistry::WeightConfig{},
            GetAuthorizedFullNodeAddress(),

            [weak_ptr( std::weak_ptr<Blockchain>(
                instance ) )]( const std::string &cid, std::function<void( outcome::result<std::string> )> callback )
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    (void) strong->account_->RequestRegularBlock( 8000, cid, std::move( callback ) );
                }
            },
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) ), request_validator_registry]( bool initialized )
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    strong->validator_registry_initialized_.store( initialized );
                    if ( !initialized )
                    {
                        strong->logger_->error( "[{}] Validator registry not initialized yet",
                                                strong->account_->GetAddress().substr( 0, 8 ) );
                        request_validator_registry( strong );
                    }
                }
            } );

        if ( !instance->validator_registry_ )
        {
            instance->logger_->error( "[{}] Failed to create validator registry",
                                      instance->account_->GetAddress().substr( 0, 8 ) );
            return nullptr;
        }

        instance->consensus_manager_ = ConsensusManager::New(
            instance->validator_registry_,
            instance->db_,
            std::move( pubsub ),
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )](
                std::vector<uint8_t> payload ) -> outcome::result<std::vector<uint8_t>>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->account_->Sign( std::move( payload ) );
                }
                return outcome::failure( std::errc::owner_dead );
            },
            instance->account_->GetAddress() );

        instance->validator_registry_->SetBatchSubjectSubmitter(
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )](
                const ConsensusSubject &subject ) -> outcome::result<void>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    auto weight_result = strong->validator_registry_->GetValidatorWeight(
                        strong->account_->GetAddress() );
                    if ( weight_result.has_error() )
                    {
                        return outcome::failure( weight_result.error() );
                    }
                    if ( !weight_result.value().has_value() )
                    {
                        return outcome::success();
                    }
                    std::string registry_cid   = strong->validator_registry_->GetRegistryCid();
                    uint64_t    registry_epoch = strong->validator_registry_->GetRegistryEpoch();
                    auto        batch_payload  = ConsensusManager::DecodeRegistryBatchSubject( subject );
                    // In case the batch has already started, use the current CID and epoch
                    if ( batch_payload.has_value() )
                    {
                        registry_cid   = batch_payload.value().base_registry_cid();
                        registry_epoch = batch_payload.value().base_registry_epoch();
                    }
                    auto proposal_result = strong->consensus_manager_->CreateProposal( subject,
                                                                                       strong->account_->GetAddress(),
                                                                                       registry_cid,
                                                                                       registry_epoch );
                    if ( proposal_result.has_error() )
                    {
                        return outcome::failure( proposal_result.error() );
                    }
                    return strong->consensus_manager_->SubmitProposal( proposal_result.value(), true );
                }
                return outcome::failure( std::errc::owner_dead );
            } );

        instance->consensus_manager_->RegisterSubjectHandler(
            REGISTRY_BATCH_SUBJECT_TYPE,
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )](
                const ConsensusManager::Subject &subject ) -> outcome::result<ConsensusManager::ValidationResult>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    auto decision_result = strong->validator_registry_->EvaluateBatchSubject( subject );
                    if ( decision_result.has_error() )
                    {
                        return outcome::failure( decision_result.error() );
                    }
                    switch ( decision_result.value() )
                    {
                        case ValidatorRegistry::BatchSubjectDecision::Approve:
                            return ConsensusManager::ValidationResult::Approve();
                        case ValidatorRegistry::BatchSubjectDecision::Pending:
                            return ConsensusManager::ValidationResult::Pending();
                        case ValidatorRegistry::BatchSubjectDecision::Reject:
                        default:
                            return ConsensusManager::ValidationResult::Reject();
                    }
                }
                return outcome::failure( std::errc::owner_dead );
            } );

        instance->consensus_manager_->RegisterCertificateHandler(
            REGISTRY_BATCH_SUBJECT_TYPE,
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )](
                const std::string          &subject_hash,
                const ConsensusCertificate &certificate ) -> outcome::result<ConsensusManager::Check>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    auto decision = strong->validator_registry_->HandleBatchCertificate( subject_hash, certificate );
                    if ( decision.has_error() )
                    {
                        return outcome::failure( decision.error() );
                    }
                    switch ( decision.value() )
                    {
                        case ValidatorRegistry::BatchCertificateDecision::Approve:
                            return ConsensusManager::Check::Approve;
                        case ValidatorRegistry::BatchCertificateDecision::Pending:
                            return ConsensusManager::Check::Pending;
                        case ValidatorRegistry::BatchCertificateDecision::Stalled:
                            return ConsensusManager::Check::Stalled;
                        case ValidatorRegistry::BatchCertificateDecision::Reject:
                        default:
                            return ConsensusManager::Check::Reject;
                    }
                }
                return outcome::failure( std::errc::owner_dead );
            } );

        auto ensure_registry_result = instance->EnsureValidatorRegistry();
        if ( ensure_registry_result.has_error() )
        {
            instance->logger_->error( "[{}] Failed to ensure validator registry during init",
                                      instance->account_->GetAddress().substr( 0, 8 ) );
        }
        if ( !instance->validator_registry_initialized_.load() )
        {
            request_validator_registry( instance );
        }

        if ( !genesis_filter_initialized )
        {
            instance->logger_->error( "[{}] Failed to initialize genesis filter",
                                      instance->account_->GetAddress().substr( 0, 8 ) );
        }
        if ( !account_creation_filter_initialized )
        {
            instance->logger_->error( "[{}] Failed to initialize account creation filter",
                                      instance->account_->GetAddress().substr( 0, 8 ) );
        }

        const bool genesis_callback_registered = instance->db_->RegisterNewElementCallback(
            "/?" + std::string( GENESIS_KEY ),
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )]( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                                 const std::string                     &cid )
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    strong->GenesisReceivedCallback( std::move( new_data ), cid );
                }
            } );

        const bool account_creation_callback_registered = instance->db_->RegisterNewElementCallback(
            "/?" + std::string( ACCOUNT_CREATION_KEY_PREFIX ) + ".*",
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )]( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                                 const std::string                     &cid )
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    strong->AccountCreationReceivedCallback( std::move( new_data ), cid );
                }
            } );

        instance->filters_registered_   = genesis_filter_initialized && account_creation_filter_initialized;
        instance->callbacks_registered_ = genesis_callback_registered && account_creation_callback_registered;
        instance->created_successfully_ = instance->filters_registered_ && instance->callbacks_registered_ &&
                                          instance->validator_registry_;
        instance->account_->SetGetBlockChainCIDMethod(
            [weak_ptr( std::weak_ptr<Blockchain>(
                instance ) )]( uint8_t block_index, const std::string &address ) -> outcome::result<std::string>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    switch ( block_index )
                    {
                        case 0:
                            if ( strong->cids_.hasGenesis() )
                            {
                                return strong->cids_.genesis_.value();
                            }
                            break;
                        case 1:
                        {
                            auto cid_it = strong->cids_.account_creation_.find( address );
                            if ( cid_it != strong->cids_.account_creation_.end() )
                            {
                                return cid_it->second;
                            }
                            break;
                        }
                        case 2:
                        {
                            sgns::crdt::GlobalDB::Buffer registry_cid_key;
                            registry_cid_key.put( std::string( ValidatorRegistry::RegistryCidKey() ) );
                            auto registry_cid = strong->db_->GetDataStore()->get( registry_cid_key );
                            if ( registry_cid.has_value() )
                            {
                                return std::string( registry_cid.value().toString() );
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    return outcome::failure( std::errc::invalid_argument );
                }
                return outcome::failure( std::errc::owner_dead );
            } );

        instance->account_->SetGetValidatorWeightMethod(
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )](
                const std::string &address ) -> outcome::result<std::optional<uint64_t>>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->validator_registry_->GetValidatorWeight( address );
                }
                return outcome::failure( std::errc::owner_dead );
            } );

        instance->logger_->debug( "[{}] Block callback registered", instance->account_->GetAddress().substr( 0, 8 ) );

        return instance;
    }

    outcome::result<void> Blockchain::MigrateCids( const std::shared_ptr<crdt::GlobalDB> &old_db,
                                                   const std::shared_ptr<crdt::GlobalDB> &new_db )
    {
        if ( !old_db || !new_db )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        auto new_crdt   = new_db->GetCRDTDataStore();
        auto old_syncer = std::static_pointer_cast<crdt::GraphsyncDAGSyncer>(
            old_db->GetBroadcaster()->GetDagSyncer() );
        if ( !new_crdt )
        {
            blockchain_logger()->error( "Missing broadcaster while migrating blockchain CIDs" );
            return outcome::failure( std::errc::no_such_device );
        }
        if ( !old_syncer )
        {
            blockchain_logger()->error( "Missing DAG syncer while migrating blockchain CIDs" );
            return outcome::failure( std::errc::no_such_device );
        }

        std::unordered_set<std::string> migrated_cids;
        auto MigrateCIDToNewDB = [&]( const std::string &cid_string ) -> outcome::result<void>
        {
            if ( cid_string.empty() || !migrated_cids.emplace( cid_string ).second )
            {
                return outcome::success();
            }
            blockchain_logger()->debug( " Migrating CID: {}", cid_string );

            BOOST_OUTCOME_TRY( auto cid, CID::fromString( cid_string ) );
            BOOST_OUTCOME_TRY( auto node, old_syncer->GetNodeFromMerkleDAG( cid ) );
            BOOST_OUTCOME_TRY( new_crdt->AddDAGNode( node ) );
            return outcome::success();
        };

        auto old_store = old_db->GetDataStore();
        auto new_store = new_db->GetDataStore();

        blockchain_logger()->debug( "{}: Getting the genesis CID from old database", __func__ );

        crdt::GlobalDB::Buffer genesis_cid_key;
        genesis_cid_key.put( std::string( GENESIS_CID_KEY ) );
        auto genesis_cid = old_store->get( genesis_cid_key );
        if ( genesis_cid.has_value() )
        {
            blockchain_logger()->debug( "{}: Genesis CID: {}", __func__, genesis_cid.value().toString() );
            crdt::GlobalDB::Buffer genesis_cid_value;
            genesis_cid_value.putBuffer( genesis_cid.value() );
            (void) new_store->put( genesis_cid_key, std::move( genesis_cid_value ) );
            BOOST_OUTCOME_TRY( MigrateCIDToNewDB( std::string( genesis_cid.value().toString() ) ) );
        }

        blockchain_logger()->debug( "{}: Getting the account creation CIDs from old database", __func__ );
        crdt::GlobalDB::Buffer account_creation_prefix;
        account_creation_prefix.put( std::string( ACCOUNT_CREATION_CID_KEY_PREFIX ) );
        auto account_creation_cids = old_store->query( account_creation_prefix );
        if ( account_creation_cids.has_value() )
        {
            for ( const auto &entry : account_creation_cids.value() )
            {
                blockchain_logger()->debug( "{}: Account creation CID: {}", __func__, entry.second.toString() );

                (void) new_store->put( entry.first, entry.second );
                BOOST_OUTCOME_TRY( MigrateCIDToNewDB( std::string( entry.second.toString() ) ) );
            }
        }
        blockchain_logger()->debug( "{}: Finalized migrating the blockchain", __func__ );
        for ( auto cid_string : migrated_cids )
        {
            blockchain_logger()->debug( "{}: Migrated CID: {}", __func__, cid_string );
        }

        return outcome::success();
    }

    // Private constructor
    Blockchain::Blockchain( std::shared_ptr<crdt::GlobalDB> global_db,
                            std::shared_ptr<GeniusAccount>  account,
                            BlockchainCallback              callback ) :
        db_( std::move( global_db ) ),                          //
        account_( std::move( account ) ),                       //
        blockchain_processed_callback_( std::move( callback ) ) //
    {
        logger_->debug( "[{}] Blockchain constructor called", account_->GetAddress().substr( 0, 8 ) );
    }

    Blockchain::~Blockchain()
    {
        logger_->debug( "[{}] ~Blockchain destructor called", account_->GetAddress().substr( 0, 8 ) );
        if ( consensus_manager_ )
        {
            consensus_manager_->Close();
        }
        if ( db_ )
        {
            const std::string genesis_pattern = "/?" + std::string( GENESIS_KEY );
            const std::string account_pattern = "/?" + std::string( ACCOUNT_CREATION_KEY_PREFIX ) + ".*";
            db_->UnregisterNewElementCallback( genesis_pattern );
            db_->UnregisterElementFilter( genesis_pattern );
            db_->UnregisterNewElementCallback( account_pattern );
            db_->UnregisterElementFilter( account_pattern );
        }
        account_->ClearGetBlockChainCIDMethod();
        account_->ClearGetValidatorWeightMethod();
    }

    void Blockchain::SetAuthorizedFullNodeAddress( const std::string &pub_address )
    {
        auto  logger  = base::createLogger( "Blockchain" );
        auto &address = AuthorizedFullNodeAddressStorage();
        logger->info( "Setting authorized full node address from {} to {}",
                      address.substr( 0, 8 ),
                      pub_address.substr( 0, 8 ) );
        address = pub_address;
    }

    const std::string &Blockchain::GetAuthorizedFullNodeAddress()
    {
        return AuthorizedFullNodeAddressStorage();
    }

    outcome::result<void> Blockchain::Start()
    {
        if ( !created_successfully_ || !filters_registered_ || !callbacks_registered_ ||
             !validator_registry_initialized_.load() )
        {
            logger_->warn(
                "[{}] Blockchain start deferred (created: {}, filters: {}, callbacks: {}, validator_registry: {})",
                account_->GetAddress().substr( 0, 8 ),
                created_successfully_,
                filters_registered_,
                callbacks_registered_,
                validator_registry_initialized_.load() );
            return InformBlockchainResult( outcome::failure( Error::BLOCKCHAIN_NOT_INITIALIZED ) );
        }

        logger_->info( "[{}] Starting blockchain with authorized full node: {}",
                       account_->GetAddress().substr( 0, 8 ),
                       GetAuthorizedFullNodeAddress().substr( 0, 8 ) );

        auto get_account_creation_result = db_->Get(
            crdt::HierarchicalKey( std::string( ACCOUNT_CREATION_KEY_PREFIX ) + account_->GetAddress() ) );

        if ( !get_account_creation_result.has_error() )
        {
            logger_->info( "[{}] Account creation block found locally, sanity checking if Genesis is also present",
                           account_->GetAddress().substr( 0, 8 ) );
            auto get_genesis_result = db_->Get( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ) );
            if ( get_genesis_result.has_error() )
            {
                logger_->error( "[{}] Account creation block found but genesis block missing locally, invalid state",
                                account_->GetAddress().substr( 0, 8 ) );
                return outcome::failure( Error::GENESIS_BLOCK_MISSING );
            }
            logger_->info( "[{}] Genesis block also found locally, verifying account creation block",
                           account_->GetAddress().substr( 0, 8 ) );
            BOOST_OUTCOME_TRY( OnGenesisBlockReceived( get_genesis_result.value() ) );
            BOOST_OUTCOME_TRY( InitGenesisCID() );
            BOOST_OUTCOME_TRY( EnsureValidatorRegistry() );
            BOOST_OUTCOME_TRY( OnAccountCreationBlockReceived( get_account_creation_result.value() ) );
            BOOST_OUTCOME_TRY( InitAccountCreationCID( account_->GetAddress() ) );

            auto query_result = db_->QueryKeyValues( std::string( ACCOUNT_CREATION_KEY_PREFIX ) );
            if ( query_result.has_error() )
            {
                logger_->debug( "[{}] Could not query other account creation CIDs: {}",
                                account_->GetAddress().substr( 0, 8 ),
                                query_result.error().message() );
            }
            else
            {
                const auto prefix      = std::string( ACCOUNT_CREATION_KEY_PREFIX );
                const auto prefix_size = prefix.size();
                logger_->debug( "[{}] Found account creation CIDS for other accounts",
                                account_->GetAddress().substr( 0, 8 ) );
                for ( const auto &entry : query_result.value() )
                {
                    auto key_str_res = db_->KeyToString( entry.first );
                    if ( key_str_res.has_error() )
                    {
                        logger_->debug( "[{}] Failed to convert account creation key to string",
                                        account_->GetAddress().substr( 0, 8 ) );
                        continue;
                    }
                    const auto &key_str = key_str_res.value();
                    logger_->debug( "[{}] Creation CID key found: {}", account_->GetAddress().substr( 0, 8 ), key_str );

                    if ( key_str.size() <= prefix_size + 1 || key_str.front() != '/' ||
                         key_str.rfind( prefix, 1 ) != 1 )
                    {
                        logger_->error( "[{}] Creation CID key INVALID: {}",
                                        account_->GetAddress().substr( 0, 8 ),
                                        key_str );

                        continue;
                    }
                    auto address = key_str.substr( prefix_size + 1 );
                    if ( address == account_->GetAddress() )
                    {
                        logger_->error( "[{}] Creation CID key same as OWN: {}",
                                        account_->GetAddress().substr( 0, 8 ),
                                        key_str );
                        continue;
                    }
                    (void) InitAccountCreationCID( address );
                }
            }

            logger_->info( "[{}] Account creation block verification completed successfully",
                           account_->GetAddress().substr( 0, 8 ) );

            return InformBlockchainResult( outcome::success() );
        }
        logger_->info( "[{}] Account creation block not found locally, proceeding to check genesis block",
                       account_->GetAddress().substr( 0, 8 ) );
        // Try to get genesis block first
        auto get_genesis_result = db_->Get( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ) );
        if ( !get_genesis_result.has_error() )
        {
            logger_->info( "[{}] Genesis block found locally, verifying", account_->GetAddress().substr( 0, 8 ) );
            BOOST_OUTCOME_TRY( OnGenesisBlockReceived( get_genesis_result.value() ) );
            BOOST_OUTCOME_TRY( InitGenesisCID() );
            BOOST_OUTCOME_TRY( EnsureValidatorRegistry() );

            logger_->info( "[{}] Genesis block verification completed successfully",
                           account_->GetAddress().substr( 0, 8 ) );
            logger_->info( "[{}] Requesting account creation block via pubsub", account_->GetAddress().substr( 0, 8 ) );

            return account_->RequestAccountCreation(
                TIMEOUT_ACC_CREATION_BLOCK_MS,
                [weakptr( weak_from_this() )]( outcome::result<std::string> creation_cid_res )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        self->logger_->debug( "[{}] Account creation request finished",
                                              self->account_->GetAddress().substr( 0, 8 ) );

                        self->InformAccountCreationResponse( std::move( creation_cid_res ) );
                    }
                } );
        }

        logger_->info( "[{}] Genesis block not found locally, proceeding to creation/request",
                       account_->GetAddress().substr( 0, 8 ) );
        // Genesis block not found locally
        if ( account_->GetAddress() == GetAuthorizedFullNodeAddress() )
        {
            logger_->info( "[{}] Full node detected, creating genesis block", account_->GetAddress().substr( 0, 8 ) );
            auto create_result = CreateGenesisBlock();
            return create_result;
        }
        logger_->info( "[{}] Regular node detected, requesting genesis block via pubsub",
                       account_->GetAddress().substr( 0, 8 ) );
        auto genesis_request_result = account_->RequestGenesis(
            TIMEOUT_GENESIS_BLOCK_MS,
            [weakptr( weak_from_this() )]( outcome::result<std::string> genesis_cid_res )
            {
                if ( auto self = weakptr.lock() )
                {
                    self->logger_->debug( "[{}] Genesis request finished",
                                          self->account_->GetAddress().substr( 0, 8 ) );
                    self->InformGenesisResult( std::move( genesis_cid_res ) );
                }
            } );
        if ( genesis_request_result.has_error() )
        {
            logger_->error( "[{}] Genesis request failed: no response received",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_MISSING );
        }
        logger_->info( "[{}] Request succeeded for Genesis", account_->GetAddress().substr( 0, 8 ) );

        return outcome::success();
    }

    outcome::result<void> Blockchain::InitGenesisCID()
    {
        sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_key;
        genesis_cid_buffer_key.put( std::string( GENESIS_CID_KEY ) );
        auto genesis_cid = db_->GetDataStore()->get( genesis_cid_buffer_key );
        if ( genesis_cid.has_value() )
        {
            cids_.genesis_ = std::string( genesis_cid.value().toString() );
            return outcome::success();
        }
        return outcome::failure( std::errc::no_such_file_or_directory );
    }

    outcome::result<void> Blockchain::EnsureValidatorRegistry() const
    {
        if ( account_->GetAddress() != GetAuthorizedFullNodeAddress() )
        {
            return outcome::success();
        }

        auto registry_result = validator_registry_->StoreGenesisRegistry( GetAuthorizedFullNodeAddress(),
                                                                          [this]( const std::vector<uint8_t> &data )
                                                                          { return account_->Sign( data ); } );
        if ( registry_result.has_error() )
        {
            logger_->error( "[{}] Failed to ensure validator registry", account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::VALIDATOR_REGISTRY_CREATION_FAILED );
        }

        return outcome::success();
    }

    outcome::result<void> Blockchain::InitAccountCreationCID( const std::string &address )
    {
        sgns::crdt::GlobalDB::Buffer account_creation_cid_buffer_key;
        account_creation_cid_buffer_key.put( std::string( ACCOUNT_CREATION_CID_KEY_PREFIX ) + address );
        logger_->debug( "[{}] Init account creation CID for {}", account_->GetAddress().substr( 0, 8 ), address );
        auto account_creation_cid = db_->GetDataStore()->get( account_creation_cid_buffer_key );
        if ( account_creation_cid.has_value() )
        {
            logger_->debug( "[{}] Account creation CID for {}: {}",
                            account_->GetAddress().substr( 0, 8 ),
                            address,
                            account_creation_cid.value().toString() );
            cids_.account_creation_[address] = std::string( account_creation_cid.value().toString() );
            return outcome::success();
        }
        return outcome::failure( std::errc::no_such_file_or_directory );
    }

    outcome::result<void> Blockchain::SaveGenesisCID( const std::string &cid )
    {
        sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_key;
        genesis_cid_buffer_key.put( std::string( GENESIS_CID_KEY ) );

        sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_value;
        genesis_cid_buffer_value.put( cid );

        auto put_result = db_->GetDataStore()->put( genesis_cid_buffer_key, genesis_cid_buffer_value );
        if ( put_result.has_error() )
        {
            logger_->error( "[{}] Failed to store genesis CID: {}",
                            account_->GetAddress().substr( 0, 8 ),
                            put_result.error().message() );
            return outcome::failure( put_result.error() );
        }
        cids_.genesis_ = cid;
        logger_->debug( "[{}] Genesis CID stored: {}", account_->GetAddress().substr( 0, 8 ), cid );
        return outcome::success();
    }

    outcome::result<void> Blockchain::SaveAccountCreationCID( const std::string &address, const std::string &cid )
    {
        sgns::crdt::GlobalDB::Buffer account_creation_cid_buffer_key;
        account_creation_cid_buffer_key.put( std::string( ACCOUNT_CREATION_CID_KEY_PREFIX ) + address );

        sgns::crdt::GlobalDB::Buffer account_creation_cid_buffer_value;
        account_creation_cid_buffer_value.put( cid );

        auto put_result = db_->GetDataStore()->put( account_creation_cid_buffer_key,
                                                    account_creation_cid_buffer_value );
        if ( put_result.has_error() )
        {
            logger_->error( "[{}] Failed to store account creation CID: {}",
                            account_->GetAddress().substr( 0, 8 ),
                            put_result.error().message() );
            return outcome::failure( put_result.error() );
        }
        cids_.account_creation_[address] = cid;
        logger_->debug( "[{}] Account creation CID saved for {}: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        address.substr( 0, 8 ),
                        cid );
        return outcome::success();
    }

    outcome::result<void> Blockchain::OnGenesisBlockReceived( const base::Buffer &serialized_genesis )
    {
        logger_->debug( "[{}] Processing received genesis block (size: {} bytes)",
                        account_->GetAddress().substr( 0, 8 ),
                        serialized_genesis.size() );

        // Store the received genesis block in member variable
        std::vector<uint8_t> data( serialized_genesis.begin(), serialized_genesis.end() );
        if ( !genesis_block_.ParseFromArray( data.data(), data.size() ) )
        {
            logger_->error( "[{}] Failed to parse genesis block from received data",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_SERIALIZATION_FAILED );
        }

        logger_->debug( "[{}] Genesis block parsed successfully, chain_id: {}, version: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        genesis_block_.chain_id(),
                        genesis_block_.version() );

        return VerifyGenesisBlock( std::string( serialized_genesis.toString() ) );
    }

    outcome::result<void> Blockchain::InformBlockchainResult( outcome::result<void> result ) const
    {
        // Only inform if both genesis and account creation are ready
        if ( blockchain_processed_callback_ )
        {
            logger_->info( "[{}] Finishing blockchain processing, informing caller",
                           account_->GetAddress().substr( 0, 8 ) );
            blockchain_processed_callback_( result );
        }
        return result;
    }

    outcome::result<void> Blockchain::InformGenesisResult( outcome::result<std::string> genesis_result )
    {
        if ( genesis_result.has_error() )
        {
            logger_->debug( "[{}] Genesis block not found", account_->GetAddress().substr( 0, 8 ) );

            return InformBlockchainResult( outcome::failure( Error::GENESIS_BLOCK_MISSING ) );
        }
        logger_->debug( "[{}] Informing genesis result response with CID: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        genesis_result.value() );
        WatchCIDDownload( genesis_result.value(), Error::GENESIS_BLOCK_MISSING, TIMEOUT_GENESIS_BLOCK_MS );
        return outcome::success();
    }

    outcome::result<void> Blockchain::InformAccountCreationResponse( outcome::result<std::string> creation_result )
    {
        if ( creation_result.has_error() )
        {
            logger_->debug( "[{}] Received empty account creation CID, no account created yet",
                            account_->GetAddress().substr( 0, 8 ) );

            return CreateAccountCreationBlock();
        }

        logger_->debug( "[{}] Informing account creation response with CID: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        creation_result.value() );
        WatchCIDDownload( creation_result.value(),
                          Error::ACCOUNT_CREATION_BLOCK_MISSING,
                          TIMEOUT_ACC_CREATION_BLOCK_MS );

        return outcome::success();
    }

    void Blockchain::WatchCIDDownload( const std::string &cid, Error error_on_failure, uint64_t timeout_ms )
    {
        std::thread(
            [weakptr = weak_from_this(), cid, error_on_failure, timeout_ms]
            {
                auto cid_result = CID::fromString( cid );
                if ( cid_result.has_failure() )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        self->logger_->error( "[{}] Invalid CID received: {}",
                                              self->account_->GetAddress().substr( 0, 8 ),
                                              cid );
                    }
                    return;
                }

                auto deadline       = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeout_ms );
                auto sleep_interval = std::chrono::milliseconds( 200 );

                while ( std::chrono::steady_clock::now() < deadline )
                {
                    if ( auto self = weakptr.lock() )
                    {
                        // Exit if block already processed via normal flow
                        if ( ( error_on_failure == Error::GENESIS_BLOCK_MISSING && self->cids_.hasGenesis() ) ||
                             ( error_on_failure == Error::ACCOUNT_CREATION_BLOCK_MISSING &&
                               self->cids_.hasAccount( self->account_->GetAddress() ) ) )
                        {
                            return;
                        }

                        auto status = self->db_->GetCIDJobStatus( cid_result.value() );
                        if ( status.has_value() && status.value() == crdt::CrdtDatastore::JobStatus::COMPLETED )
                        {
                            return;
                        }
                        if ( status.has_value() && status.value() == crdt::CrdtDatastore::JobStatus::FAILED )
                        {
                            self->logger_->error( "[{}] CID {} failed to download via GraphSync",
                                                  self->account_->GetAddress().substr( 0, 8 ),
                                                  cid.substr( 0, 8 ) );
                            self->InformBlockchainResult( outcome::failure( error_on_failure ) );
                            return;
                        }
                    }
                    std::this_thread::sleep_for( sleep_interval );
                }

                if ( auto self = weakptr.lock() )
                {
                    auto status = self->db_->GetCIDJobStatus( cid_result.value() );
                    bool done   = status.has_value() && status.value() == crdt::CrdtDatastore::JobStatus::COMPLETED;
                    bool local_state = ( error_on_failure == Error::GENESIS_BLOCK_MISSING &&
                                         self->cids_.hasGenesis() ) ||
                                       ( error_on_failure == Error::ACCOUNT_CREATION_BLOCK_MISSING &&
                                         self->cids_.hasAccount( self->account_->GetAddress() ) );

                    if ( done || local_state )
                    {
                        return;
                    }

                    self->logger_->error( "[{}] Timeout waiting for CID {} to be processed",
                                          self->account_->GetAddress().substr( 0, 8 ),
                                          cid.substr( 0, 8 ) );
                    self->InformBlockchainResult( outcome::failure( error_on_failure ) );
                }
            } )
            .detach();
    }

    outcome::result<void> Blockchain::GenesisReceivedCallback( const crdt::CRDTCallbackManager::NewDataPair &new_data,
                                                               const std::string                            &cid )
    {
        logger_->debug( "[{}] Genesis received callback triggered with CID: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        cid );

        outcome::result<void> new_genesis_return = outcome::success();

        do
        {
            logger_->info( "[{}] New genesis block CID received, processing", account_->GetAddress().substr( 0, 8 ) );
            auto [genesis_key, serialized_genesis] = new_data;

            auto genesis_validation_result = OnGenesisBlockReceived( serialized_genesis );
            if ( genesis_validation_result.has_error() )
            {
                logger_->error( "[{}] Genesis block validation failed", account_->GetAddress().substr( 0, 8 ) );
                new_genesis_return = genesis_validation_result;
                break;
            }
            logger_->info( "[{}] Genesis block validation successful, storing CID",
                           account_->GetAddress().substr( 0, 8 ) );

            auto save_genesis_result = SaveGenesisCID( cid );
            if ( save_genesis_result.has_error() )
            {
                logger_->error( "[{}] Failed to save genesis CID", account_->GetAddress().substr( 0, 8 ) );
                new_genesis_return = save_genesis_result;
                break;
            }
            logger_->debug( "[{}] Genesis CID stored: {}", account_->GetAddress().substr( 0, 8 ), cid );

        } while ( 0 );

        if ( new_genesis_return.has_error() )
        {
            return InformBlockchainResult( new_genesis_return );
        }

        logger_->info( "[{}] Requesting account creation block via pubsub (async)",
                       account_->GetAddress().substr( 0, 8 ) );

        auto result = account_->RequestAccountCreation(
            TIMEOUT_ACC_CREATION_BLOCK_MS,
            [weakself = weak_from_this()]( outcome::result<std::string> creation_cid_res )
            {
                if ( auto s = weakself.lock() )
                {
                    s->InformAccountCreationResponse( std::move( creation_cid_res ) );
                }
            } );
        if ( result.has_error() )
        {
            logger_->error( "[{}] Account creation request failed {}. Creating account...",
                            account_->GetAddress().substr( 0, 8 ),
                            result.error().message() );
            return InformAccountCreationResponse( outcome::failure( Error::ACCOUNT_CREATION_BLOCK_CREATION_FAILED ) );
        }
        logger_->info( "[{}] Triggered Request account creation successfully", account_->GetAddress().substr( 0, 8 ) );
        return outcome::success();
    }

    outcome::result<void> Blockchain::CreateGenesisBlock()
    {
        logger_->info( "[{}] Creating genesis block with authorized creator: {}",
                       account_->GetAddress().substr( 0, 8 ),
                       GetAuthorizedFullNodeAddress().substr( 0, 8 ) );

        GenesisBlock g;
        auto         timestamp = std::chrono::system_clock::now();

        g.set_chain_id( "supergenius" );
        g.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );
        g.set_version( version::SuperGeniusVersionFullString() );

        logger_->debug( "[{}] Genesis block fields set - chain_id: {}, version: {}, timestamp: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        g.chain_id(),
                        g.version(),
                        g.timestamp() );

        // compute or fill hash as needed (placeholder empty for now)
        g.set_hash( std::string{} );

        // creator public key - store authorized creator pub (as bytes)
        g.set_creator_public_key( GetAuthorizedFullNodeAddress() );

        logger_->debug( "[{}] Computing signature for genesis block", account_->GetAddress().substr( 0, 8 ) );

        // compute signature data
        auto sig_data = ComputeSignatureData( g );

        // Sign using account
        auto signature_bytes = account_->Sign( sig_data );
        g.set_signature( std::string( signature_bytes.begin(), signature_bytes.end() ) );

        logger_->debug( "[{}] Genesis block signature computed (size: {} bytes)",
                        account_->GetAddress().substr( 0, 8 ),
                        signature_bytes.size() );

        // serialize using SerializeToArray like EscrowTransaction
        size_t               size = g.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !g.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            logger_->error( "[{}] Failed to serialize genesis block", account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_SERIALIZATION_FAILED );
        }

        logger_->debug( "[{}] Genesis block serialized (size: {} bytes)", account_->GetAddress().substr( 0, 8 ), size );

        // Convert to string for storage
        crdt::GlobalDB::Buffer serialized;
        serialized.put( serialized_proto );
        auto put_res = db_->Put( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ),
                                 serialized,
                                 { std::string( BLOCKCHAIN_TOPIC ) } );
        if ( put_res.has_error() )
        {
            logger_->error( "[{}] Failed to store genesis block in CRDT", account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_CREATION_FAILED );
        }

        logger_->debug( "[{}] Genesis block stored in CRDT with CID: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        put_res.value().toString().value() );

        auto save_cid_result = SaveGenesisCID( put_res.value().toString().value() );

        if ( save_cid_result.has_error() )
        {
            logger_->error( "[{}] Failed to store genesis CID, rolling back genesis block",
                            account_->GetAddress().substr( 0, 8 ) );

            (void) db_->Remove( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ),
                                { std::string( BLOCKCHAIN_TOPIC ) } );

            return outcome::failure( Error::GENESIS_BLOCK_CREATION_FAILED );
        }

        auto registry_result = validator_registry_->StoreGenesisRegistry( GetAuthorizedFullNodeAddress(),
                                                                          [this]( const std::vector<uint8_t> &data )
                                                                          { return account_->Sign( data ); } );
        if ( registry_result.has_error() )
        {
            logger_->error( "[{}] Failed to store validator registry in CRDT", account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::VALIDATOR_REGISTRY_CREATION_FAILED );
        }
        logger_->info( "[{}] Validator registry initialized", account_->GetAddress().substr( 0, 8 ) );

        logger_->info( "[{}] Genesis block created and stored successfully", account_->GetAddress().substr( 0, 8 ) );
        return outcome::success();
    }

    outcome::result<void> Blockchain::VerifyGenesisBlock( const std::string &serialized_genesis )
    {
        logger_->debug( "[{}] Verifying genesis block against authorized creator: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        GetAuthorizedFullNodeAddress().substr( 0, 8 ) );

        GenesisBlock g;

        // Convert string back to byte vector for ParseFromArray
        std::vector<uint8_t> data( serialized_genesis.begin(), serialized_genesis.end() );

        if ( !g.ParseFromArray( data.data(), data.size() ) )
        {
            logger_->error( "[{}] Failed to parse genesis block during verification",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_SERIALIZATION_FAILED );
        }

        logger_->debug( "[{}] Checking genesis block creator authorization", account_->GetAddress().substr( 0, 8 ) );

        // check authorized creator (compare as raw bytes)
        if ( g.creator_public_key() != GetAuthorizedFullNodeAddress() )
        {
            logger_->error( "[{}] Genesis block created by unauthorized key: {} (expected: {})",
                            account_->GetAddress().substr( 0, 8 ),
                            g.creator_public_key().substr( 0, 8 ),
                            GetAuthorizedFullNodeAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_UNAUTHORIZED_CREATOR );
        }

        logger_->debug( "[{}] Creator authorization verified, checking signature",
                        account_->GetAddress().substr( 0, 8 ) );

        if ( !VerifySignature( g ) )
        {
            logger_->error( "[{}] Genesis block signature verification failed", account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_INVALID_SIGNATURE );
        }

        logger_->info( "[{}] Genesis block verification completed successfully",
                       account_->GetAddress().substr( 0, 8 ) );
        return outcome::success();
    }

    std::vector<uint8_t> Blockchain::ComputeSignatureData( const GenesisBlock &g ) const
    {
        logger_->trace( "[{}] Computing signature data for genesis block", account_->GetAddress().substr( 0, 8 ) );

        // Create a copy without signature for deterministic signing
        GenesisBlock g_copy = g;
        g_copy.clear_signature();

        // Serialize the unsigned block
        size_t               size = g_copy.ByteSizeLong();
        std::vector<uint8_t> signature_data( size );

        if ( !g_copy.SerializeToArray( signature_data.data(), signature_data.size() ) )
        {
            logger_->error( "Failed to serialize signature into array" );
        }

        logger_->trace( "[{}] Signature data computed (size: {} bytes)", account_->GetAddress().substr( 0, 8 ), size );

        return signature_data;
    }

    std::vector<uint8_t> Blockchain::ComputeSignatureData( const AccountCreationBlock &ac ) const
    {
        // Create a copy without signature for deterministic signing
        AccountCreationBlock ac_copy = ac;
        ac_copy.clear_signature();

        size_t               size = ac_copy.ByteSizeLong();
        std::vector<uint8_t> signature_data( size );
        if ( !ac_copy.SerializeToArray( signature_data.data(), signature_data.size() ) )
        {
            logger_->error( "Failed to serialize signature into array" );
        }

        return signature_data;
    }

    bool Blockchain::VerifySignature( const GenesisBlock &g ) const
    {
        logger_->trace( "[{}] Verifying genesis block signature", account_->GetAddress().substr( 0, 8 ) );

        auto sig_data = ComputeSignatureData( g );

        // Convert signature from string to vector<uint8_t>
        std::vector<uint8_t> signature_bytes( g.signature().begin(), g.signature().end() );

        logger_->trace( "[{}] Signature verification - sig size: {} bytes, data size: {} bytes",
                        account_->GetAddress().substr( 0, 8 ),
                        signature_bytes.size(),
                        sig_data.size() );

        // Use GeniusAccount static method for signature verification
        bool verification_result = GeniusAccount::VerifySignature(
            g.creator_public_key(),                                        // address (public key)
            std::string( signature_bytes.begin(), signature_bytes.end() ), // signature as string
            sig_data                                                       // data to verify
        );

        if ( verification_result )
        {
            logger_->debug( "[{}] Genesis block signature verification successful",
                            account_->GetAddress().substr( 0, 8 ) );
        }
        else
        {
            logger_->debug( "[{}] Genesis block signature verification failed", account_->GetAddress().substr( 0, 8 ) );
        }

        return verification_result;
    }

    bool Blockchain::VerifySignature( const AccountCreationBlock &ac ) const
    {
        logger_->trace( "[{}] Verifying account creation block signature", account_->GetAddress().substr( 0, 8 ) );

        auto sig_data = ComputeSignatureData( ac );

        // Convert signature from string to vector<uint8_t>
        std::vector<uint8_t> signature_bytes( ac.signature().begin(), ac.signature().end() );

        logger_->trace( "[{}] Account creation signature verification - sig size: {} bytes, data size: {} bytes",
                        account_->GetAddress().substr( 0, 8 ),
                        signature_bytes.size(),
                        sig_data.size() );

        // Use GeniusAccount static method for signature verification
        bool verification_result = GeniusAccount::VerifySignature(
            ac.account_address(),                                          // address (public key)
            std::string( signature_bytes.begin(), signature_bytes.end() ), // signature as string
            sig_data                                                       // data to verify
        );

        if ( verification_result )
        {
            logger_->debug( "[{}] Account creation block signature verification successful",
                            account_->GetAddress().substr( 0, 8 ) );
        }
        else
        {
            logger_->debug( "[{}] Account creation block signature verification failed",
                            account_->GetAddress().substr( 0, 8 ) );
        }

        return verification_result;
    }

    outcome::result<void> Blockchain::CreateAccountCreationBlock()
    {
        if ( !cids_.hasGenesis() )
        {
            logger_->error( "[{}] Cannot create account creation block without genesis CID",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_MISSING );
        }

        logger_->info( "[{}] Creating account creation block linked to genesis CID: {}",
                       account_->GetAddress().substr( 0, 8 ),
                       cids_.genesis_.value() );

        AccountCreationBlock ac;
        auto                 timestamp = std::chrono::system_clock::now();

        ac.set_account_address( account_->GetAddress() );
        ac.set_genesis_block_cid( cids_.genesis_.value() );
        ac.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );
        ac.set_version( version::SuperGeniusVersionFullString() );
        ac.set_hash( std::string{} ); // placeholder

        // Compute signature data (without signature field)
        auto sig_data        = ComputeSignatureData( ac );
        auto signature_bytes = account_->Sign( sig_data );
        ac.set_signature( std::string( signature_bytes.begin(), signature_bytes.end() ) );

        // Serialize
        size_t               size = ac.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !ac.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            logger_->error( "[{}] Failed to serialize account creation block", account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::ACCOUNT_CREATION_BLOCK_SERIALIZATION_FAILED );
        }

        // Store in CRDT
        crdt::GlobalDB::Buffer serialized;
        serialized.put( serialized_proto );
        std::string account_creation_key = std::string( ACCOUNT_CREATION_KEY_PREFIX ) + account_->GetAddress();

        auto put_res = db_->Put( crdt::HierarchicalKey( account_creation_key ),
                                 serialized,
                                 { std::string( BLOCKCHAIN_TOPIC ), account_->GetAddress() } );
        if ( put_res.has_error() )
        {
            logger_->error( "[{}] Failed to store account creation block in CRDT",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::ACCOUNT_CREATION_BLOCK_CREATION_FAILED );
        }

        logger_->info( "[{}] Account creation block created and stored successfully",
                       account_->GetAddress().substr( 0, 8 ) );
        return outcome::success();
    }

    outcome::result<void> Blockchain::OnAccountCreationBlockReceived( const base::Buffer &serialized_account_creation )
    {
        logger_->debug( "[{}] Processing received account creation block (size: {} bytes)",
                        account_->GetAddress().substr( 0, 8 ),
                        serialized_account_creation.size() );

        // Store the received account creation block in member variable
        std::vector<uint8_t> data( serialized_account_creation.begin(), serialized_account_creation.end() );
        if ( !account_creation_block_.ParseFromArray( data.data(), data.size() ) )
        {
            logger_->error( "[{}] Failed to parse account creation block from received data",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::ACCOUNT_CREATION_BLOCK_SERIALIZATION_FAILED );
        }

        logger_->debug( "[{}] Account creation block parsed successfully, version: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        account_creation_block_.version() );

        return VerifyAccountCreationBlock( std::string( serialized_account_creation.toString() ) );
    }

    outcome::result<void> Blockchain::VerifyAccountCreationBlock( const std::string &serialized_account_creation )
    {
        logger_->debug( "[{}] Verifying account creation block", account_->GetAddress().substr( 0, 8 ) );

        AccountCreationBlock ac;

        // Convert string back to byte vector for ParseFromArray
        std::vector<uint8_t> data( serialized_account_creation.begin(), serialized_account_creation.end() );

        if ( !ac.ParseFromArray( data.data(), data.size() ) )
        {
            logger_->error( "[{}] Failed to parse account creation block during verification",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::ACCOUNT_CREATION_BLOCK_SERIALIZATION_FAILED );
        }

        logger_->debug( "[{}] Checking account creation block genesis CID link",
                        account_->GetAddress().substr( 0, 8 ) );

        if ( !cids_.hasGenesis() )
        {
            logger_->error( "[{}] Account creation received without any genesis. Linked genesis CID: {} ",
                            account_->GetAddress().substr( 0, 8 ),
                            ac.genesis_block_cid().substr( 0, 8 ) );
            return outcome::failure( Error::ACCOUNT_CREATION_BLOCK_INVALID_GENESIS_LINK );
        }

        // check genesis block CID link
        if ( ac.genesis_block_cid() != cids_.genesis_.value() )
        {
            logger_->error( "[{}] Account creation block linked to wrong genesis CID: {} (expected: {})",
                            account_->GetAddress().substr( 0, 8 ),
                            ac.genesis_block_cid().substr( 0, 8 ),
                            cids_.genesis_.value().substr( 0, 8 ) );
            return outcome::failure( Error::ACCOUNT_CREATION_BLOCK_INVALID_GENESIS_LINK );
        }

        if ( !VerifySignature( ac ) )
        {
            logger_->error( "[{}] Account creation block signature verification failed",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::ACCOUNT_CREATION_BLOCK_INVALID_SIGNATURE );
        }

        logger_->info( "[{}] Account creation block verification completed successfully",
                       account_->GetAddress().substr( 0, 8 ) );
        return outcome::success();
    }

    std::optional<std::vector<crdt::pb::Element>> Blockchain::FilterGenesis( const crdt::pb::Element &element )
    {
        bool reject = false;

        do
        {
            GenesisBlock new_genesis;
            if ( !new_genesis.ParseFromArray( reinterpret_cast<const uint8_t *>( element.value().data() ),
                                              static_cast<int>( element.value().size() ) ) )
            {
                logger_->warn( "[{}] Failed to parse incoming genesis block, rejecting: {}",
                               account_->GetAddress().substr( 0, 8 ),
                               element.key() );
                reject = true;
                break;
            }

            if ( VerifyGenesisBlock( element.value() ).has_error() )
            {
                logger_->warn( "[{}] Incoming genesis block failed verification, rejecting",
                               account_->GetAddress().substr( 0, 8 ) );
                reject = true;
                break;
            }

            auto maybe_existing_value = db_->Get( element.key() );
            if ( !maybe_existing_value.has_value() )
            {
                break; // no existing genesis locally, accept the new one
            }

            std::string existing_serialized( maybe_existing_value.value().toString() );
            if ( existing_serialized == element.value() )
            {
                logger_->debug( "[{}] Duplicate genesis block with identical content, ignoring new value",
                                account_->GetAddress().substr( 0, 8 ) );
                reject = true;
                break;
            }

            GenesisBlock existing_genesis;
            if ( !existing_genesis.ParseFromArray( reinterpret_cast<const uint8_t *>( existing_serialized.data() ),
                                                   static_cast<int>( existing_serialized.size() ) ) )
            {
                logger_->warn( "[{}] Stored genesis block is not parsable, accepting new candidate",
                               account_->GetAddress().substr( 0, 8 ) );
                break;
            }

            if ( VerifyGenesisBlock( existing_serialized ).has_error() )
            {
                logger_->warn( "[{}] Stored genesis block failed verification, accepting replacement",
                               account_->GetAddress().substr( 0, 8 ) );
                break;
            }

            const bool replace = ShouldReplaceGenesis( existing_genesis, new_genesis );
            if ( !replace )
            {
                logger_->info( "[{}] Keeping existing genesis (ts: {}) over new candidate (ts: {})",
                               account_->GetAddress().substr( 0, 8 ),
                               existing_genesis.timestamp(),
                               new_genesis.timestamp() );
                reject = true;
            }
            else
            {
                logger_->info( "[{}] Replacing stored genesis (ts: {}) with earlier candidate (ts: {})",
                               account_->GetAddress().substr( 0, 8 ),
                               existing_genesis.timestamp(),
                               new_genesis.timestamp() );
            }

        } while ( 0 );

        if ( reject )
        {
            return std::vector<crdt::pb::Element>{};
        }
        return std::nullopt;
    }

    std::optional<std::vector<crdt::pb::Element>> Blockchain::FilterAccountCreation( const crdt::pb::Element &element )
    {
        bool reject = false;

        do
        {
            AccountCreationBlock new_block;
            if ( !new_block.ParseFromArray( reinterpret_cast<const uint8_t *>( element.value().data() ),
                                            static_cast<int>( element.value().size() ) ) )
            {
                logger_->warn( "[{}] Failed to parse incoming account creation block, rejecting: {}",
                               account_->GetAddress().substr( 0, 8 ),
                               element.key() );
                reject = true;
                break;
            }

            if ( new_block.account_address().empty() )
            {
                logger_->warn( "[{}] Account creation block without address, rejecting",
                               account_->GetAddress().substr( 0, 8 ) );
                reject = true;
                break;
            }

            const bool genesis_known = cids_.hasGenesis();
            if ( genesis_known && new_block.genesis_block_cid() != cids_.genesis_.value() )
            {
                logger_->warn( "[{}] Account creation genesis CID mismatch ({} vs {}), rejecting",
                               account_->GetAddress().substr( 0, 8 ),
                               new_block.genesis_block_cid().substr( 0, 8 ),
                               cids_.genesis_.value().substr( 0, 8 ) );
                reject = true;
                break;
            }

            if ( !VerifySignature( new_block ) )
            {
                logger_->warn( "[{}] Account creation block failed signature verification, rejecting",
                               account_->GetAddress().substr( 0, 8 ) );
                reject = true;
                break;
            }

            auto maybe_existing_value = db_->Get( element.key() );
            if ( !maybe_existing_value.has_value() )
            {
                break; // no existing block for this account yet
            }

            std::string existing_serialized( maybe_existing_value.value().toString() );
            if ( existing_serialized == element.value() )
            {
                logger_->debug( "[{}] Duplicate account creation block for {}, ignoring identical value",
                                account_->GetAddress().substr( 0, 8 ),
                                new_block.account_address().substr( 0, 8 ) );
                reject = true;
                break;
            }

            AccountCreationBlock existing_block;
            if ( !existing_block.ParseFromArray( reinterpret_cast<const uint8_t *>( existing_serialized.data() ),
                                                 static_cast<int>( existing_serialized.size() ) ) )
            {
                logger_->warn( "[{}] Stored account creation block for {} is not parsable, accepting new candidate",
                               account_->GetAddress().substr( 0, 8 ),
                               new_block.account_address().substr( 0, 8 ) );
                break;
            }

            const bool existing_genesis_ok   = !genesis_known ||
                                               existing_block.genesis_block_cid() == cids_.genesis_.value();
            const bool existing_signature_ok = VerifySignature( existing_block );

            if ( !( existing_genesis_ok && existing_signature_ok ) )
            {
                logger_->info( "[{}] Existing account creation for {} is invalid, accepting new candidate",
                               account_->GetAddress().substr( 0, 8 ),
                               new_block.account_address().substr( 0, 8 ) );
                break;
            }

            const bool replace = ShouldReplaceAccountCreation( existing_block, new_block );
            if ( !replace )
            {
                logger_->info( "[{}] Keeping existing account creation for {} (ts: {}) over new (ts: {})",
                               account_->GetAddress().substr( 0, 8 ),
                               new_block.account_address().substr( 0, 8 ),
                               existing_block.timestamp(),
                               new_block.timestamp() );
                reject = true;
            }
            else
            {
                logger_->info( "[{}] Replacing account creation for {} (ts: {} -> ts: {})",
                               account_->GetAddress().substr( 0, 8 ),
                               new_block.account_address().substr( 0, 8 ),
                               existing_block.timestamp(),
                               new_block.timestamp() );
            }

        } while ( 0 );

        if ( reject )
        {
            return std::vector<crdt::pb::Element>{};
        }
        return std::nullopt;
    }

    bool Blockchain::ShouldReplaceGenesis( const GenesisBlock &existing, const GenesisBlock &candidate ) const
    {
        if ( candidate.timestamp() == existing.timestamp() )
        {
            return candidate.SerializeAsString() < existing.SerializeAsString();
        }
        return candidate.timestamp() < existing.timestamp();
    }

    bool Blockchain::ShouldReplaceAccountCreation( const AccountCreationBlock &existing,
                                                   const AccountCreationBlock &candidate ) const
    {
        if ( candidate.timestamp() == existing.timestamp() )
        {
            return candidate.SerializeAsString() < existing.SerializeAsString();
        }
        return candidate.timestamp() < existing.timestamp();
    }

    outcome::result<void> Blockchain::Stop()
    {
        logger_->info( "[{}] Stopping blockchain", account_->GetAddress().substr( 0, 8 ) );
        if ( consensus_manager_ )
        {
            consensus_manager_->Close();
        }
        //db_->RemoveListenTopic( std::string( BLOCKCHAIN_TOPIC ) );
        return outcome::success();
    }

    outcome::result<void> Blockchain::AccountCreationReceivedCallback(
        const crdt::CRDTCallbackManager::NewDataPair &new_data,
        const std::string                            &cid )
    {
        logger_->debug( "[{}] Account creation received callback triggered with CID: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        cid );

        bool                  notify_blockchain  = false;
        outcome::result<void> new_account_return = outcome::success();
        do
        {
            logger_->info( "[{}] New account creation block CID received, processing",
                           account_->GetAddress().substr( 0, 8 ) );
            auto [account_creation_key, serialized_account_creation] = new_data;

            std::string key_address = account_creation_key;
            if ( !key_address.empty() && key_address.front() == '/' )
            {
                key_address.erase( key_address.begin() );
            }
            if ( key_address.rfind( ACCOUNT_CREATION_KEY_PREFIX, 0 ) == 0 )
            {
                key_address = key_address.substr( ACCOUNT_CREATION_KEY_PREFIX.size() );
            }
            else
            {
                key_address.clear();
            }

            if ( key_address == account_->GetAddress() )
            {
                notify_blockchain = true;
            }

            auto account_creation_validation_result = OnAccountCreationBlockReceived( serialized_account_creation );
            if ( account_creation_validation_result.has_error() )
            {
                logger_->error( "[{}] Account creation block validation failed",
                                account_->GetAddress().substr( 0, 8 ) );
                new_account_return = account_creation_validation_result;
                break;
            }

            std::string creation_address = account_creation_block_.account_address();
            if ( creation_address.empty() )
            {
                logger_->error( "[{}] Account creation block with empty address",
                                account_->GetAddress().substr( 0, 8 ) );
                new_account_return = outcome::failure( std::errc::invalid_argument );
                break;
            }

            if ( !key_address.empty() && key_address != creation_address )
            {
                logger_->warn( "[{}] Account creation key address {} does not match block address {}",
                               account_->GetAddress().substr( 0, 8 ),
                               key_address.substr( 0, 8 ),
                               creation_address.substr( 0, 8 ) );
            }

            if ( creation_address == account_->GetAddress() )
            {
                notify_blockchain = true;
            }

            logger_->info( "[{}] Account creation block validated", account_->GetAddress().substr( 0, 8 ) );

            auto save_account_creation_result = SaveAccountCreationCID( creation_address, cid );
            if ( save_account_creation_result.has_error() )
            {
                logger_->error( "[{}] Failed to save account creation CID", account_->GetAddress().substr( 0, 8 ) );
                new_account_return = save_account_creation_result;
                break;
            }

            logger_->info( "[{}] Account creation block processed successfully",
                           account_->GetAddress().substr( 0, 8 ) );
        } while ( 0 );

        if ( notify_blockchain )
        {
            return InformBlockchainResult( new_account_return );
        }

        return outcome::success();
    }

    outcome::result<std::string> Blockchain::GetGenesisCID() const
    {
        if ( !cids_.hasGenesis() )
        {
            logger_->error( "[{}] Trying to grab Genesis CID, but no local information",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::GENESIS_BLOCK_MISSING );
        }
        return cids_.genesis_.value();
    }

    outcome::result<std::string> Blockchain::GetAccountCreationCID() const
    {
        auto it = cids_.account_creation_.find( account_->GetAddress() );
        if ( it == cids_.account_creation_.end() )
        {
            logger_->error( "[{}] Trying to grab Account Creation, but no local information",
                            account_->GetAddress().substr( 0, 8 ) );
            return outcome::failure( Error::ACCOUNT_CREATION_BLOCK_MISSING );
        }
        return it->second;
    }

    std::shared_ptr<ValidatorRegistry> Blockchain::GetValidatorRegistry() const
    {
        return validator_registry_;
    }

    void Blockchain::SetFullNodeMode()
    {
        db_->AddListenTopic(
            std::string( BLOCKCHAIN_TOPIC ) ); //This will not trigger the broadcaster, but it will grab links on CRDT
    }

    bool Blockchain::RegisterSubjectHandler( std::string_view subject_type, ConsensusManager::SubjectHandler handler )
    {
        return consensus_manager_->RegisterSubjectHandler( subject_type, std::move( handler ) );
    }

    void Blockchain::UnregisterSubjectHandler( std::string_view subject_type )
    {
        consensus_manager_->UnregisterSubjectHandler( subject_type );
    }

    bool Blockchain::RegisterCertificateHandler( std::string_view                            subject_type,
                                                 ConsensusManager::CertificateSubjectHandler handler )
    {
        return consensus_manager_->RegisterCertificateHandler( subject_type, std::move( handler ) );
    }

    void Blockchain::UnregisterCertificateHandler( std::string_view subject_type )
    {
        consensus_manager_->UnregisterCertificateHandler( subject_type );
    }

    bool Blockchain::RegisterProposalCleanupHandler( std::string_view                         subject_type,
                                                     ConsensusManager::ProposalCleanupHandler handler )
    {
        return consensus_manager_->RegisterProposalCleanupHandler( subject_type, std::move( handler ) );
    }

    void Blockchain::RegisterSlotKeyHandler( std::string_view subject_type, ConsensusManager::SlotKeyHandler handler )
    {
        ConsensusManager::RegisterSlotKeyHandler( subject_type, std::move( handler ) );
    }

    void Blockchain::UnregisterSlotKeyHandler( std::string_view subject_type )
    {
        ConsensusManager::UnregisterSlotKeyHandler( subject_type );
    }

    outcome::result<ConsensusManager::Subject> Blockchain::CreateConsensusNonceSubject(
        const std::string                             &account_id,
        uint64_t                                       nonce,
        const std::string                             &tx_hash,
        const EmbeddedTransaction                     &transaction,
        const std::optional<UTXOTransitionCommitment> &utxo_commitment,
        const std::optional<UTXOWitness>              &utxo_witness )
    {
        return ConsensusManager::CreateNonceSubject( account_id,
                                                     nonce,
                                                     tx_hash,
                                                     transaction,
                                                     utxo_commitment,
                                                     utxo_witness );
    }

    outcome::result<ConsensusManager::Proposal> Blockchain::CreateConsensusProposal(
        const std::string                             &account_id,
        uint64_t                                       nonce,
        const std::string                             &tx_hash,
        const EmbeddedTransaction                     &transaction,
        const std::optional<UTXOTransitionCommitment> &utxo_commitment,
        const std::optional<UTXOWitness>              &utxo_witness )
    {
        BOOST_OUTCOME_TRY(
            auto &&nonce_subject,
            CreateConsensusNonceSubject( account_id, nonce, tx_hash, transaction, utxo_commitment, utxo_witness ) );
        BOOST_OUTCOME_TRY( auto &&nonce_proposal,
                           consensus_manager_->CreateProposal( nonce_subject,
                                                               account_id,
                                                               validator_registry_->GetRegistryCid(),
                                                               validator_registry_->GetRegistryEpoch() ) );

        return nonce_proposal;
    }

    outcome::result<void> Blockchain::SubmitProposal( const ConsensusManager::Proposal &proposal )
    {
        return consensus_manager_->SubmitProposal( std::move( proposal ) );
    }

    outcome::result<void> Blockchain::TryResumeProposal( const std::string &hash )
    {
        return consensus_manager_->ResumeProposalHandling( hash );
    }

    outcome::result<void> Blockchain::TryResumePendingDependency(
        const ConsensusManager::PendingDependencyKey &dependency )
    {
        return consensus_manager_->WakePendingDependency( dependency );
    }

    bool Blockchain::CheckCertificateForSlot( const std::string &slot_key ) const
    {
        return consensus_manager_->CheckCertificateForSlot( slot_key );
    }

    bool Blockchain::CheckCertificate( const std::string &slot_key ) const
    {
        return CheckCertificateForSlot( slot_key );
    }

    bool Blockchain::CheckCertificateStrict( const ConsensusManager::Subject &subject ) const
    {
        return consensus_manager_->CheckCertificateForSubject( subject );
    }

    outcome::result<ConsensusManager::Certificate> Blockchain::GetCertificateBySlot( const std::string &slot_key ) const
    {
        return consensus_manager_->GetCertificateBySlot( slot_key );
    }

    outcome::result<ConsensusManager::Certificate> Blockchain::GetCertificateBySubjectHash(
        const std::string &slot_key ) const
    {
        return GetCertificateBySlot( slot_key );
    }

    const std::string &Blockchain::BestHash( const std::string &a, const std::string &b ) const
    {
        return consensus_manager_->BestHash( a, b );
    }

}
