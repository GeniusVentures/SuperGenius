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
    }
    return "Unknown error";
}

namespace sgns
{

    std::shared_ptr<Blockchain> Blockchain::New( std::shared_ptr<crdt::GlobalDB> global_db,
                                                 std::shared_ptr<GeniusAccount>  account )
    {
        auto instance = std::shared_ptr<Blockchain>( new Blockchain( std::move( global_db ), std::move( account ) ) );

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

        instance->account_->SetGetGenesisCIDMethod(
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )]() -> outcome::result<std::string>
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    return strong->genesis_cid_;
                }
                return outcome::failure( std::errc::owner_dead );
            } );

        instance->logger_->debug( "[{}] Genesis block callback registered",
                                  instance->account_->GetAddress().substr( 0, 8 ) );

        return instance;
    }

    // Private constructor
    Blockchain::Blockchain( std::shared_ptr<crdt::GlobalDB> global_db, std::shared_ptr<GeniusAccount> account ) :
        db_( std::move( global_db ) ),                                 //
        account_( std::move( account ) ),                              //
        authorized_full_node_address_( DEFAULT_FULL_NODE_PUB_ADDRESS ) //
    {
        logger_->debug( "[{}] Blockchain constructor called", account_->GetAddress().substr( 0, 8 ) );
    }

    Blockchain::~Blockchain()
    {
        logger_->debug( "[{}] ~Blockchain destructor called", account_->GetAddress().substr( 0, 8 ) );
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

    outcome::result<void> Blockchain::Start( GenesisCallback callback )
    {
        genesis_processed_callback_ = std::move( callback );
        return Start();
    }

    outcome::result<void> Blockchain::Start()
    {
        logger_->info( "[{}] Starting blockchain with authorized full node: {}",
                       account_->GetAddress().substr( 0, 8 ),
                       authorized_full_node_address_.substr( 0, 8 ) );
        db_->AddListenTopic( std::string( BLOCKCHAIN_TOPIC ) );

        logger_->debug( "[{}] Added listen topic: {}", account_->GetAddress().substr( 0, 8 ), BLOCKCHAIN_TOPIC );

        // Try to get genesis block synchronously first
        logger_->debug( "[{}] Attempting to retrieve genesis block from local storage",
                        account_->GetAddress().substr( 0, 8 ) );

        auto get_genesis_result = db_->Get( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ) );

        if ( !get_genesis_result.has_error() )
        {
            logger_->info( "[{}] Genesis block found locally, verifying", account_->GetAddress().substr( 0, 8 ) );

            // Genesis block found, verify it immediately
            OUTCOME_TRY( OnGenesisBlockReceived( get_genesis_result.value() ) );

            sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_key;
            genesis_cid_buffer_key.put( std::string( GENESIS_CID_KEY ) );
            auto genesis_cid = db_->GetDataStore()->get( genesis_cid_buffer_key );
            if ( genesis_cid.has_value() )
            {
                genesis_cid_ = std::string( genesis_cid.value().toString() );
                logger_->debug( "[{}] Genesis CID retrieved: {}", account_->GetAddress().substr( 0, 8 ), genesis_cid_ );
            }
            else
            {
                logger_->error( "[{}] Genesis block found but CID not available",
                                account_->GetAddress().substr( 0, 8 ) );
            }

            logger_->info( "[{}] Genesis block verification completed successfully",
                           account_->GetAddress().substr( 0, 8 ) );
            return outcome::success();
        }

        // Genesis block not found locally
        logger_->debug( "[{}] Genesis block not found locally", account_->GetAddress().substr( 0, 8 ) );

        if ( account_->GetAddress() == authorized_full_node_address_ )
        {
            logger_->info( "[{}] Full node detected (matches authorized address), creating genesis block",
                           account_->GetAddress().substr( 0, 8 ) );

            // Full node creates genesis block immediately
            auto create_result = CreateGenesisBlock();
            InformGenesisResult( create_result );
            return create_result;
        }
        else
        {
            logger_->info( "[{}] Regular node detected (authorized: {}), requesting genesis block via pubsub",
                           account_->GetAddress().substr( 0, 8 ),
                           authorized_full_node_address_.substr( 0, 8 ) );

            // Regular node requests genesis block via pubsub
            account_->RequestGenesis();
        }
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

    outcome::result<void> Blockchain::InformGenesisResult( outcome::result<void> result )
    {
        if ( genesis_processed_callback_ )
        {
            if ( result.has_error() )
            {
                logger_->error( "[{}] Genesis processing failed, informing callback",
                                account_->GetAddress().substr( 0, 8 ) );
            }
            else
            {
                logger_->info( "[{}] Genesis processing succeeded, informing callback",
                               account_->GetAddress().substr( 0, 8 ) );
            }
            genesis_processed_callback_( result );
        }
        else
        {
            logger_->warn( "[{}] No genesis callback registered", account_->GetAddress().substr( 0, 8 ) );
        }
        return result;
    }

    void Blockchain::GenesisReceivedCallback( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
    {
        logger_->debug( "[{}] Genesis received callback triggered with CID: {}",
                        account_->GetAddress().substr( 0, 8 ),
                        cid );

        sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_key;
        genesis_cid_buffer_key.put( std::string( GENESIS_CID_KEY ) );
        auto genesis_cid = db_->GetDataStore()->get( genesis_cid_buffer_key );

        if ( !genesis_cid.has_value() )
        {
            logger_->info( "[{}] New genesis block CID received, processing", account_->GetAddress().substr( 0, 8 ) );

            auto [genesis_key, serialized_genesis] = new_data;
            // New genesis block CID, process it

            auto genesis_validation_result = OnGenesisBlockReceived( serialized_genesis );
            if ( !genesis_validation_result.has_error() )
            {
                logger_->info( "[{}] Genesis block validation successful, storing CID",
                               account_->GetAddress().substr( 0, 8 ) );

                sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_value;
                genesis_cid_buffer_value.put( cid );

                db_->GetDataStore()->put( genesis_cid_buffer_key, genesis_cid_buffer_value );
                genesis_cid_ = cid;

                logger_->debug( "[{}] Genesis CID stored: {}", account_->GetAddress().substr( 0, 8 ), genesis_cid_ );
            }
            else
            {
                logger_->error( "[{}] Genesis block validation failed", account_->GetAddress().substr( 0, 8 ) );
            }
            InformGenesisResult( genesis_validation_result );
        }
        else
        {
            logger_->debug( "[{}] Genesis CID already exists, ignoring callback",
                            account_->GetAddress().substr( 0, 8 ) );
        }
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

        crdt::GlobalDB::Buffer genesis_buffer_cid_key;
        genesis_buffer_cid_key.put( std::string( GENESIS_CID_KEY ) );
        crdt::GlobalDB::Buffer genesis_buffer_cid_value;
        genesis_buffer_cid_value.put( put_res.value().toString().value() );
        auto store_cid_res = db_->GetDataStore()->put( genesis_buffer_cid_key, genesis_buffer_cid_value );
        if ( store_cid_res.has_error() )
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
}
