/**
 * @file       Blockchain.hpp
 * @brief      Header file for the Blockchain class, which provides an interface for block storage operations.
 * @date       2025-10-16
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#pragma once
#include <memory>
#include <outcome/outcome.hpp>
#include "crdt/globaldb/globaldb.hpp"
#include "account/GeniusAccount.hpp"

namespace sgns
{

    class Blockchain
    {
    public:
        Blockchain( std::shared_ptr<crdt::GlobalDB> global_db, std::shared_ptr<GeniusAccount> account );
        ~Blockchain();
        outcome::result<void> Start();

        outcome::result<void> Stop()
        {
            return outcome::success();
        }

        outcome::result<void> CheckGenesisBlock()
        {
            return outcome::success();
        }

    private:
        /// Topic used for the blockchain CRDT
        static constexpr std::string_view BLOCKCHAIN_TOPIC = "gnus-blockchain";
        /// CID of the Genesis block created by the full node
        static constexpr std::string_view FULL_NODE_PUB_ADDRESS =
            "8a33bdf1445a68736429d1773be8682362753a0efc6fb9d8b3e8dffe3b74fc91e26b203fd521547a5219eddf1d3ac51fd17a7646c9bca5ef065da131add4e5a2";

        std::shared_ptr<crdt::GlobalDB> db_;      ///< CRDT database instance
        std::shared_ptr<GeniusAccount>  account_; ///< GeniusAccount instance
    };

    Blockchain::Blockchain( std::shared_ptr<crdt::GlobalDB> global_db, std::shared_ptr<GeniusAccount> account ) :
        db_( std::move( global_db ) ),    //
        account_( std::move( account ) ), //
    {
    }

    Blockchain::~Blockchain() {}

    outcome::result<void> Blockchain::Start()
    {
        auto full_blockchain_topic = std::string( BLOCKCHAIN_TOPIC ) + sgns::version::GetNetAndVersionAppendix();
        db_->AddListenTopic( full_blockchain_topic );

        if ()
        {
            return outcome::success();
        }
    }
}
