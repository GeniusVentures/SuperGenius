/**
 * @file       Blockchain.hpp
 * @brief      Header file for the Blockchain class, which provides an interface for block storage operations.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once
#include <memory>
#include <sstream>
#include <map>
#include <functional>
#include <outcome/outcome.hpp>
#include "crdt/globaldb/globaldb.hpp"
#include "account/GeniusAccount.hpp"
#include "genesis.pb.h"
#include "base/sgns_version.hpp"

namespace sgns
{

    class Blockchain : public std::enable_shared_from_this<Blockchain>
    {
    public:
        /**
         * @brief      Error class of the Blockchain module
         */
        enum class Error
        {
            GENESIS_BLOCK_CREATION_FAILED = 0,  ///< Failed to create the genesis block
            GENESIS_BLOCK_INVALID_SIGNATURE,    ///< Genesis block has invalid signature
            GENESIS_BLOCK_UNAUTHORIZED_CREATOR, ///< Genesis block created by unauthorized key
            GENESIS_BLOCK_SERIALIZATION_FAILED, ///< Failed to serialize/deserialize genesis block
        };

        // Callback type for genesis block operations
        using GenesisCallback = std::function<void( outcome::result<void> )>;

        /**
         * @brief Factory method to create Blockchain as shared_ptr
         * @param global_db CRDT database instance
         * @param account GeniusAccount instance
         * @return shared_ptr to Blockchain instance
         */
        static std::shared_ptr<Blockchain> New( std::shared_ptr<crdt::GlobalDB> global_db,
                                                std::shared_ptr<GeniusAccount>  account );

        ~Blockchain();

        /**
         * @brief Start the blockchain with async genesis block handling
         * @param callback Called when genesis block check/creation/retrieval completes
         */
        outcome::result<void> Start( GenesisCallback callback );

        /**
         * @brief Handle received genesis block from pubsub
         * @param serialized_genesis The received genesis block data
         */
        outcome::result<void> OnGenesisBlockReceived( const base::Buffer &serialized_genesis );

    private:
        /// Make constructor private to force use of factory method
        Blockchain( std::shared_ptr<crdt::GlobalDB> global_db, std::shared_ptr<GeniusAccount> account );

        /// Topic used for the blockchain CRDT
        static constexpr std::string_view BLOCKCHAIN_TOPIC = "gnus-blockchain";
        /// CID / hex of the Genesis block creator pub address (authorized)
        static constexpr std::string_view FULL_NODE_PUB_ADDRESS =
            "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2";
        static constexpr std::string_view GENESIS_KEY     = "gnus-genesis-block";
        static constexpr std::string_view GENESIS_CID_KEY = "gnus-genesis-block-cid";

        std::shared_ptr<crdt::GlobalDB> db_;      ///< CRDT database instance
        std::shared_ptr<GeniusAccount>  account_; ///< GeniusAccount instance

        GenesisCallback                genesis_processed_callback_; ///< Callback waiting for genesis block
        sgns::blockchain::GenesisBlock genesis_block_;              ///< Cached genesis block for easy access
        std::string                    genesis_cid_;

        std::vector<uint8_t> ComputeSignatureData( const sgns::blockchain::GenesisBlock &g );
        bool                 VerifySignature( const sgns::blockchain::GenesisBlock &g );

        outcome::result<void> CheckGenesisBlockSync();
        outcome::result<void> CreateGenesisBlock();
        outcome::result<void> VerifyGenesisBlock( const std::string &serialized_genesis );

        void GenesisReceivedCallback( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid );
        void InformGenesisResult( outcome::result<void> result );
    };

    // Factory method implementation
    std::shared_ptr<Blockchain> Blockchain::New( std::shared_ptr<crdt::GlobalDB> global_db,
                                                 std::shared_ptr<GeniusAccount>  account )
    {
        auto instance = std::shared_ptr<Blockchain>( new Blockchain( std::move( global_db ), std::move( account ) ) );

        (void)instance->db_->RegisterNewElementCallback(
            "gnus-genesis-block",
            [weak_ptr( std::weak_ptr<Blockchain>( instance ) )]( crdt::CRDTCallbackManager::NewDataPair new_data,
                                                                 const std::string                     &cid )
            {
                if ( auto strong = weak_ptr.lock() )
                {
                    strong->GenesisReceivedCallback( std::move( new_data ), cid );
                }
            } );
        return instance;
    }

    // Private constructor
    Blockchain::Blockchain( std::shared_ptr<crdt::GlobalDB> global_db, std::shared_ptr<GeniusAccount> account ) :
        db_( std::move( global_db ) ),   //
        account_( std::move( account ) ) //
    {
    }

    Blockchain::~Blockchain() {}

    outcome::result<void> Blockchain::Start( GenesisCallback callback )
    {
        auto full_blockchain_topic = std::string( BLOCKCHAIN_TOPIC ) + sgns::version::GetNetAndVersionAppendix();
        db_->AddListenTopic( full_blockchain_topic );

        genesis_processed_callback_ = std::move( callback );

        // Try to get genesis block synchronously first
        auto get_genesis_result = db_->Get( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ) );

        if ( !get_genesis_result.has_error() )
        {
            // Genesis block found, verify it immediately
            OUTCOME_TRY( OnGenesisBlockReceived( get_genesis_result.value() ) );

            sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_key;
            genesis_cid_buffer_key.put( std::string( GENESIS_CID_KEY ) );
            auto genesis_cid = db_->GetDataStore()->get( genesis_cid_buffer_key );
            if ( genesis_cid.has_value() )
            {
                genesis_cid_ = std::string( genesis_cid.value().toString() );
            }
            return outcome::success();
        }
        // Genesis block not found locally
        if ( account_->GetAddress() == FULL_NODE_PUB_ADDRESS )
        {
            // Full node creates genesis block immediately
            auto create_result = CreateGenesisBlock();
            InformGenesisResult( create_result );
            return create_result;
        }
        else
        {
            // Regular node requests genesis block via pubsub
            account_->RequestGenesis();
        }
        return outcome::success();
    }

    outcome::result<void> Blockchain::OnGenesisBlockReceived( const base::Buffer &serialized_genesis )
    {
        // Store the received genesis block in member variable
        std::vector<uint8_t> data( serialized_genesis.begin(), serialized_genesis.end() );
        if ( !genesis_block_.ParseFromArray( data.data(), data.size() ) )
        {
            return outcome::failure( Error::GENESIS_BLOCK_SERIALIZATION_FAILED );
        }
        return VerifyGenesisBlock( serialized_genesis );
        ;
    }

    outcome::result<void> Blockchain::InformGenesisResult( outcome::result<void> result )
    {
        if ( genesis_processed_callback_ )
        {
            genesis_processed_callback_( result );
        }
        return result;
    }

    void Blockchain::GenesisReceivedCallback( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid )
    {
        sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_key;
        genesis_cid_buffer_key.put( std::string( GENESIS_CID_KEY ) );
        auto genesis_cid = db_->GetDataStore()->get( genesis_cid_buffer_key );

        if ( !genesis_cid.has_value() )
        {
            auto [genesis_key, serialized_genesis] = new_data;
            // New genesis block CID, process it

            auto genesis_validation_result = OnGenesisBlockReceived( serialized_genesis );
            if ( !genesis_validation_result.has_error() )
            {
                sgns::crdt::GlobalDB::Buffer genesis_cid_buffer_value;
                genesis_cid_buffer_value.put( cid );

                db_->GetDataStore()->put( genesis_cid_buffer_key, genesis_cid_buffer_value );
                genesis_cid_ = cid;
            }
            InformGenesisResult( genesis_validation_result );
        }
    }

    outcome::result<void> Blockchain::CreateGenesisBlock()
    {
        sgns::blockchain::GenesisBlock g;
        auto                           timestamp = std::chrono::system_clock::now();

        g.set_chain_id( "supergenius" );
        g.set_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>( timestamp.time_since_epoch() ).count() );
        g.set_version( version::SuperGeniusVersionFullString() );

        // compute or fill hash as needed (placeholder empty for now)
        g.set_hash( std::string{} );

        // creator public key - store authorized creator pub (as bytes)
        g.set_creator_public_key( std::string( FULL_NODE_PUB_ADDRESS ) );

        // compute signature data
        auto sig_data = ComputeSignatureData( g );

        // Sign using account
        auto signature_bytes = account_->Sign( sig_data );
        g.set_signature( std::string( signature_bytes.begin(), signature_bytes.end() ) );

        // serialize using SerializeToArray like EscrowTransaction
        size_t               size = g.ByteSizeLong();
        std::vector<uint8_t> serialized_proto( size );

        if ( !g.SerializeToArray( serialized_proto.data(), serialized_proto.size() ) )
        {
            return outcome::failure( Error::GENESIS_BLOCK_SERIALIZATION_FAILED );
        }

        // Convert to string for storage
        std::string serialized( serialized_proto.begin(), serialized_proto.end() );
        auto        put_res = db_->Put( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ),
                                 serialized,
                                        { BLOCKCHAIN_TOPIC } );
        if ( put_res.has_error() )
        {
            return outcome::failure( Error::GENESIS_BLOCK_CREATION_FAILED );
        }
        crdt::GlobalDB::Buffer genesis_buffer_cid_key;
        genesis_buffer_cid_key.put( std::string( GENESIS_CID_KEY ) );
        crdt::GlobalDB::Buffer genesis_buffer_cid_value;
        genesis_buffer_cid_value.put( put_res.value().toString() );
        auto store_cid_res = db_->GetDataStore()->put( genesis_buffer_cid_key, genesis_buffer_cid_value );
        if ( store_cid_res.has_error() )
        {
            (void)db_->Remove( crdt::HierarchicalKey( std::string( GENESIS_KEY ) ), { BLOCKCHAIN_TOPIC } );

            return outcome::failure( Error::GENESIS_BLOCK_CREATION_FAILED );
        }

        return outcome::success();
    }

    outcome::result<void> Blockchain::VerifyGenesisBlock( const std::string &serialized_genesis )
    {
        sgns::blockchain::GenesisBlock g;

        // Convert string back to byte vector for ParseFromArray
        std::vector<uint8_t> data( serialized_genesis.begin(), serialized_genesis.end() );

        if ( !g.ParseFromArray( data.data(), data.size() ) )
        {
            return outcome::failure( Error::GENESIS_BLOCK_SERIALIZATION_FAILED );
        }

        // check authorized creator (compare as raw bytes)
        if ( g.creator_public_key() != std::string( FULL_NODE_PUB_ADDRESS ) )
        {
            return outcome::failure( Error::GENESIS_BLOCK_UNAUTHORIZED_CREATOR );
        }

        if ( !VerifySignature( g ) )
        {
            return outcome::failure( Error::GENESIS_BLOCK_INVALID_SIGNATURE );
        }

        return outcome::success();
    }

    std::vector<uint8_t> Blockchain::ComputeSignatureData( const sgns::blockchain::GenesisBlock &g )
    {
        // Create a copy without signature for deterministic signing
        sgns::blockchain::GenesisBlock g_copy = g;
        g_copy.clear_signature();

        // Serialize the unsigned block
        size_t               size = g_copy.ByteSizeLong();
        std::vector<uint8_t> signature_data( size );

        g_copy.SerializeToArray( signature_data.data(), signature_data.size() );
        return signature_data;
    }

    bool Blockchain::VerifySignature( const sgns::blockchain::GenesisBlock &g )
    {
        auto sig_data = ComputeSignatureData( g );

        // Convert signature from string to vector<uint8_t>
        std::vector<uint8_t> signature_bytes( g.signature().begin(), g.signature().end() );

        // Use GeniusAccount static method for signature verification
        return GeniusAccount::VerifySignature(
            g.creator_public_key(),                                        // address (public key)
            std::string( signature_bytes.begin(), signature_bytes.end() ), // signature as string
            sig_data                                                       // data to verify
        );
    }
}
