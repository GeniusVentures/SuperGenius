/**
 * @file       Blockchain.cpp
 * @brief      Header file for the Blockchain class, which provides an interface for block storage operations.
 * @date       2025-10-28
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <chrono>
#include "blockchain/Blockchain.hpp"

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
        case Error::ACCOUNT_CREATION_BLOCK_CREATION_FAILED:
            return "Couldn't create account creation block";
        case Error::ACCOUNT_CREATION_BLOCK_INVALID_SIGNATURE:
            return "Account creation block has invalid signature";
        case Error::ACCOUNT_CREATION_BLOCK_SERIALIZATION_FAILED:
            return "Failed to serialize/deserialize account creation block";
        case Error::ACCOUNT_CREATION_BLOCK_INVALID_GENESIS_LINK:
            return "Account creation block not properly linked to genesis";
    }
    return "Unknown error";
}

namespace sgns
{

    std::shared_ptr<Blockchain> Blockchain::New( std::shared_ptr<crdt::GlobalDB> global_db,
                                                 std::shared_ptr<GeniusAccount>  account,
                                                 BlockchainCallback              callback )
    {
        auto instance = std::shared_ptr<Blockchain>(
            new Blockchain( std::move( global_db ), std::move( account ), std::move( callback ) ) );

        instance->logger_->info( "[{}] Blockchain instance created with authorized full node: {}",
                                 instance->account_->GetAddress().substr( 0, 8 ),
                                 instance->authorized_full_node_address_.substr( 0, 8 ) );

        (void)instance->db_->RegisterNewElementCallback(
            "/?" + std::string( GENESIS_KEY ),
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )]( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                                 const std::string                     &cid )
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    strong->GenesisReceivedCallback( std::move( new_data ), cid );
                }
            } );

        std::string account_creation_key = std::string( ACCOUNT_CREATION_KEY_PREFIX ) +
                                           instance->account_->GetAddress();
        (void)instance->db_->RegisterNewElementCallback(
            "/?" + account_creation_key,
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )]( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                                 const std::string                     &cid )
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    strong->AccountCreationReceivedCallback( std::move( new_data ), cid );
                }
            } );
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
                            if ( address != strong->account_->GetAddress() )
                            {
                                break;
                            }
                            if ( strong->cids_.hasAccount() )
                            {
                                return strong->cids_.account_.value();
                            }
                            break;
                        default:
                            break;
                    }
                    return outcome::failure( std::errc::invalid_argument );
                }
                return outcome::failure( std::errc::owner_dead );
            } );

        instance->logger_->debug( "[{}] Block callback registered", instance->account_->GetAddress().substr( 0, 8 ) );

        return instance;
    }

    // Private constructor
    Blockchain::Blockchain( std::shared_ptr<crdt::GlobalDB> global_db,
                            std::shared_ptr<GeniusAccount>  account,
                            BlockchainCallback              callback ) :
        db_( std::move( global_db ) ),                                 //
        account_( std::move( account ) ),                              //
        blockchain_processed_callback_( std::move( callback ) ),       //
        authorized_full_node_address_( DEFAULT_FULL_NODE_PUB_ADDRESS ) //
    {
        logger_->debug( "[{}] Blockchain constructor called", account_->GetAddress().substr( 0, 8 ) );
    }

    Blockchain::~Blockchain()
    {
        logger_->debug( "[{}] ~Blockchain destructor called", account_->GetAddress().substr( 0, 8 ) );
        account_->ClearGetBlockChainCIDMethod();
    }

    void Blockchain::SetAuthorizedFullNodeAddress( const std::string &pub_address )
    {
        logger_->info( "[{}] Setting authorized full node address from {} to {}",
                       account_->GetAddress().substr( 0, 8 ),
                       authorized_full_node_address_.substr( 0, 8 ),
                       pub_address.substr( 0, 8 ) );
        authorized_full_node_address_ = pub_address;
    }

    const std::string &Blockchain::GetAuthorizedFullNodeAddress() const
    {
        return authorized_full_node_address_;
    }

    outcome::result<void> Blockchain::Start()
    {
        logger_->info( "[{}] Starting blockchain with authorized full node: {}",
                       account_->GetAddress().substr( 0, 8 ),
                       authorized_full_node_address_.substr( 0, 8 ) );
        //TODO - Uncomment when a node wants to grab other node's account creation block (full node probably)
        //db_->AddTopicName( std::string( BLOCKCHAIN_TOPIC ) ); //This will not trigger the broadcaster, but it will grab links on CRDT

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
            OUTCOME_TRY( OnGenesisBlockReceived( get_genesis_result.value() ) );
            OUTCOME_TRY( InitGenesisCID() );
            OUTCOME_TRY( OnAccountCreationBlockReceived( get_account_creation_result.value() ) );
            OUTCOME_TRY( InitAccountCreationCID() );

            logger_->info( "[{}] Account creation block verification completed successfully",
                           account_->GetAddress().substr( 0, 8 ) );

            InformBlockchainResult( outcome::success() );
            return outcome::success();
        }
        else
        {
            logger_->info( "[{}] Account creation block not found locally, proceeding to check genesis block",
                           account_->GetAddress().substr( 0, 8 ) );
            // Try to get genesis block first
            auto get_genesis_result = db_->Get( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ) );
            if ( !get_genesis_result.has_error() )
            {
                logger_->info( "[{}] Genesis block found locally, verifying", account_->GetAddress().substr( 0, 8 ) );
                OUTCOME_TRY( OnGenesisBlockReceived( get_genesis_result.value() ) );
                OUTCOME_TRY( InitGenesisCID() );

                logger_->info( "[{}] Genesis block verification completed successfully",
                               account_->GetAddress().substr( 0, 8 ) );
                logger_->info( "[{}] Requesting account creation block via pubsub",
                               account_->GetAddress().substr( 0, 8 ) );

                account_->RequestAccountCreation( TIMEOUT_ACC_CREATION_BLOCK_MS,
                                                  [weakptr( weak_from_this() )]( std::string creation_cid )
                                                  {
                                                      if ( auto self = weakptr.lock() )
                                                      {
                                                          self->InformAccountCreationResponse( creation_cid );
                                                      }
                                                  } );
            }
            else
            {
                logger_->info( "[{}] Genesis block not found locally, proceeding to creation/request",
                               account_->GetAddress().substr( 0, 8 ) );
                // Genesis block not found locally
                if ( account_->GetAddress() == authorized_full_node_address_ )
                {
                    logger_->info( "[{}] Full node detected, creating genesis block",
                                   account_->GetAddress().substr( 0, 8 ) );
                    auto create_result = CreateGenesisBlock();
                    return create_result;
                }
                else
                {
                    logger_->info( "[{}] Regular node detected, requesting genesis block via pubsub",
                                   account_->GetAddress().substr( 0, 8 ) );
                    account_->RequestGenesis();
                }
            }
        }

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

    outcome::result<void> Blockchain::InitAccountCreationCID()
    {
        sgns::crdt::GlobalDB::Buffer account_creation_cid_buffer_key;
        account_creation_cid_buffer_key.put( std::string( ACCOUNT_CREATION_CID_KEY_PREFIX ) );
        auto account_creation_cid = db_->GetDataStore()->get( account_creation_cid_buffer_key );
        if ( account_creation_cid.has_value() )
        {
            cids_.account_ = std::string( account_creation_cid.value().toString() );
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
        logger_->debug( "[{}] Genesis CID stored: {}", account_->GetAddress().substr( 0, 8 ), genesis_cid_ );
        return outcome::success();
    }

    outcome::result<void> Blockchain::SaveAccountCreationCID( const std::string &cid )
    {
        sgns::crdt::GlobalDB::Buffer account_creation_cid_buffer_key;
        account_creation_cid_buffer_key.put( std::string( ACCOUNT_CREATION_CID_KEY_PREFIX ) );

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
        cids_.account_ = cid;
        logger_->debug( "[{}] Account creation CID stored: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        account_creation_cid_ );
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

    outcome::result<void> Blockchain::InformBlockchainResult( outcome::result<void> result )
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

    void Blockchain::GenesisReceivedCallback( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
    {
        logger_->debug( "[{}] Genesis received callback triggered with CID: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        cid );

        outcome::result<void> new_genesis_return = outcome::success();

        do
        {
            auto init_genesis_cid_result = InitGenesisCID();
            if ( !init_genesis_cid_result.has_error() )
            {
                logger_->error( "[{}] Genesis CID already initialized, ignoring new Genesis block",
                                account_->GetAddress().substr( 0, 8 ) );
                break;
            }
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
            InformBlockchainResult( new_genesis_return );
            return;
        }

        std::thread(
            [weakptr = weak_from_this()]
            {
                if ( auto self = weakptr.lock() )
                {
                    self->logger_->info( "[{}] Requesting account creation block via pubsub (async)",
                                         self->account_->GetAddress().substr( 0, 8 ) );

                    auto result = self->account_->RequestAccountCreation(
                        TIMEOUT_ACC_CREATION_BLOCK_MS,
                        [weakself = weakptr]( std::string creation_cid )
                        {
                            if ( auto s = weakself.lock() )
                            {
                                s->InformAccountCreationResponse( creation_cid );
                            }
                        } );

                    if ( result.has_error() )
                    {
                        self->logger_->error( "[{}] Account creation request failed asynchronously: {}",
                                              self->account_->GetAddress().substr( 0, 8 ),
                                              result.error().message() );
                        self->InformBlockchainResult( result );
                    }
                }
            } )
            .detach();
    }

    outcome::result<void> Blockchain::CreateGenesisBlock()
    {
        logger_->info( "[{}] Creating genesis block with authorized creator: {}",
                       account_->GetAddress().substr( 0, 8 ),
                       authorized_full_node_address_.substr( 0, 8 ) );

        sgns::blockchain::GenesisBlock g;
        auto                           timestamp = std::chrono::system_clock::now();

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
        g.set_creator_public_key( authorized_full_node_address_ );

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

            (void)db_->Remove( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ),
                               { std::string( BLOCKCHAIN_TOPIC ) } );

            return outcome::failure( Error::GENESIS_BLOCK_CREATION_FAILED );
        }

        logger_->info( "[{}] Genesis block created and stored successfully", account_->GetAddress().substr( 0, 8 ) );
        return outcome::success();
    }

    outcome::result<void> Blockchain::VerifyGenesisBlock( const std::string &serialized_genesis )
    {
        logger_->debug( "[{}] Verifying genesis block against authorized creator: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        authorized_full_node_address_.substr( 0, 8 ) );

        sgns::blockchain::GenesisBlock g;

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
        if ( g.creator_public_key() != authorized_full_node_address_ )
        {
            logger_->error( "[{}] Genesis block created by unauthorized key: {} (expected: {})",
                            account_->GetAddress().substr( 0, 8 ),
                            g.creator_public_key().substr( 0, 8 ),
                            authorized_full_node_address_.substr( 0, 8 ) );
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

    std::vector<uint8_t> Blockchain::ComputeSignatureData( const sgns::blockchain::GenesisBlock &g )
    {
        logger_->trace( "[{}] Computing signature data for genesis block", account_->GetAddress().substr( 0, 8 ) );

        // Create a copy without signature for deterministic signing
        sgns::blockchain::GenesisBlock g_copy = g;
        g_copy.clear_signature();

        // Serialize the unsigned block
        size_t               size = g_copy.ByteSizeLong();
        std::vector<uint8_t> signature_data( size );

        g_copy.SerializeToArray( signature_data.data(), signature_data.size() );

        logger_->trace( "[{}] Signature data computed (size: {} bytes)", account_->GetAddress().substr( 0, 8 ), size );

        return signature_data;
    }

    std::vector<uint8_t> Blockchain::ComputeSignatureData( const sgns::blockchain::AccountCreationBlock &ac )
    {
        // Create a copy without signature for deterministic signing
        sgns::blockchain::AccountCreationBlock ac_copy = ac;
        ac_copy.clear_signature();

        size_t               size = ac_copy.ByteSizeLong();
        std::vector<uint8_t> signature_data( size );
        ac_copy.SerializeToArray( signature_data.data(), signature_data.size() );

        return signature_data;
    }

    bool Blockchain::VerifySignature( const sgns::blockchain::GenesisBlock &g )
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

    bool Blockchain::VerifySignature( const sgns::blockchain::AccountCreationBlock &ac )
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

    void Blockchain::InformAccountCreationResponse( const std::string creation_cid )
    {
        if ( creation_cid.empty() )
        {
            logger_->debug( "[{}] Received empty account creation CID, no account created yet",
                            account_->GetAddress().substr( 0, 8 ) );

            auto account_creation_result = CreateAccountCreationBlock();
            InformBlockchainResult( account_creation_result );
        }
        else
        {
            logger_->debug( "[{}] Informing account creation response with CID: {}",
                            account_->GetAddress().substr( 0, 8 ),
                            creation_cid );
            //TODO - REQUEST THE BLOCK USING THE CID? GeniusAccount will do it I guess
        }
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

        sgns::blockchain::AccountCreationBlock ac;
        auto                                   timestamp = std::chrono::system_clock::now();

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

        auto save_account_cid_result = SaveAccountCreationCID( put_res.value().toString().value() );

        if ( save_account_cid_result.has_error() )
        {
            logger_->error( "[{}] Failed to store genesis CID, rolling back genesis block",
                            account_->GetAddress().substr( 0, 8 ) );

            (void)db_->Remove( crdt::HierarchicalKey( account_creation_key ),
                               { std::string( BLOCKCHAIN_TOPIC ), account_->GetAddress() } );

            return outcome::failure( Error::GENESIS_BLOCK_CREATION_FAILED );
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

        sgns::blockchain::AccountCreationBlock ac;

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
            logger_->error( "[{}] Account creation block linked to wrong genesis CID: {} (expected: {})",
                            account_->GetAddress().substr( 0, 8 ),
                            ac.genesis_block_cid().substr( 0, 8 ),
                            genesis_cid_.substr( 0, 8 ) );
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

    outcome::result<void> Blockchain::Stop()
    {
        logger_->info( "[{}] Stopping blockchain", account_->GetAddress().substr( 0, 8 ) );
        //db_->RemoveListenTopic( std::string( BLOCKCHAIN_TOPIC ) );
        return outcome::success();
    }

    void Blockchain::AccountCreationReceivedCallback( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                      const std::string                     &cid )
    {
        logger_->debug( "[{}] Account creation received callback triggered with CID: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        cid );

        outcome::result<void> new_account_return = outcome::success();
        do
        {
            auto init_account_cid_result = InitAccountCreationCID();
            if ( !init_account_cid_result.has_error() )
            {
                logger_->error( "[{}] Account already created, ignoring new account creation",
                                account_->GetAddress().substr( 0, 8 ) );
                break;
            }
            logger_->info( "[{}] New account creation block CID received, processing",
                           account_->GetAddress().substr( 0, 8 ) );
            auto [account_creation_key, serialized_account_creation] = new_data;

            auto account_creation_validation_result = OnAccountCreationBlockReceived( serialized_account_creation );
            if ( account_creation_validation_result.has_error() )
            {
                logger_->error( "[{}] Account creation block validation failed",
                                account_->GetAddress().substr( 0, 8 ) );
                new_account_return = account_creation_validation_result;
                break;
            }

            logger_->info( "[{}] Account creation block validated", account_->GetAddress().substr( 0, 8 ) );

            auto save_account_creation_result = SaveAccountCreationCID( cid );
            if ( save_account_creation_result.has_error() )
            {
                logger_->error( "[{}] Failed to save account creation CID", account_->GetAddress().substr( 0, 8 ) );
                new_account_return = save_account_creation_result;
                break;
            }

            logger_->debug( "[{}] Account creation CID stored: {}", account_->GetAddress().substr( 0, 8 ), cid );
            logger_->info( "[{}] Account creation block processed successfully",
                           account_->GetAddress().substr( 0, 8 ) );

        } while ( 0 );

        InformBlockchainResult( new_account_return );
    }

}
