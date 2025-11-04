/**
 * @file       Blockchain.hpp
 * @brief      Header file for the Blockchain class, which provides an interface for block storage operations.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once
#include <memory>
#include <map>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>
#include "outcome/outcome.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "account/GeniusAccount.hpp"
#include "blockchain/impl/proto/SGBlockchain.pb.h"
#include "base/buffer.hpp"
#include "crdt/crdt_callback_manager.hpp"
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
            GENESIS_BLOCK_MISSING,              ///< Genesis block wasn't received
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
         * @brief       Tries to start blockchain.
         * @return      A @ref outcome::result<void>
         * @note        Only works after calling Start with callback.
         */
        outcome::result<void> Start();

        /**
         * @brief Handle received genesis block from pubsub
         * @param serialized_genesis The received genesis block data
         */
        outcome::result<void> OnGenesisBlockReceived( const base::Buffer &serialized_genesis );

        /**
         * @brief Set the authorized full node public address (for testing purposes)
         * @param pub_address The public address that is authorized to create genesis blocks
         */
        void SetAuthorizedFullNodeAddress( const std::string &pub_address );

        /**
         * @brief Get the current authorized full node public address
         * @return The authorized full node public address
         */
        const std::string &GetAuthorizedFullNodeAddress() const;

    private:
        /// Make constructor private to force use of factory method
        Blockchain( std::shared_ptr<crdt::GlobalDB> global_db, std::shared_ptr<GeniusAccount> account );

        std::vector<uint8_t>  ComputeSignatureData( const sgns::blockchain::GenesisBlock &g );
        bool                  VerifySignature( const sgns::blockchain::GenesisBlock &g );
        outcome::result<void> CheckGenesisBlockSync();
        outcome::result<void> CreateGenesisBlock();
        outcome::result<void> VerifyGenesisBlock( const std::string &serialized_genesis );

        void GenesisReceivedCallback( crdt::CRDTCallbackManager::NewDataPair new_data, const std::string &cid );
        outcome::result<void> InformGenesisResult( outcome::result<void> result );

        /// Topic used for the blockchain CRDT
        static constexpr std::string_view BLOCKCHAIN_TOPIC = "gnus-blockchain";
        /// Default CID / hex of the Genesis block creator pub address (authorized)
        static constexpr std::string_view DEFAULT_FULL_NODE_PUB_ADDRESS =
            "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2";
        static constexpr std::string_view GENESIS_KEY     = "gnus-genesis-block";
        static constexpr std::string_view GENESIS_CID_KEY = "gnus-genesis-block-cid";

        std::shared_ptr<crdt::GlobalDB> db_;      ///< CRDT database instance
        std::shared_ptr<GeniusAccount>  account_; ///< GeniusAccount instance

        GenesisCallback                genesis_processed_callback_; ///< Callback waiting for genesis block
        sgns::blockchain::GenesisBlock genesis_block_;              ///< Cached genesis block for easy access
        std::string                    genesis_cid_;
        std::string                    authorized_full_node_address_; ///< Configurable authorized full node address
        base::Logger                   logger_ = base::createLogger( "Blockchain" ); ///< Logger instance
    };

}

/**
 * @brief       Macro for declaring error handling in the IBasicProof class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns, Blockchain::Error );
