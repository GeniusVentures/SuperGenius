#ifndef SUPERGENIUS_CRDT_GLOBALDB_HPP
#define SUPERGENIUS_CRDT_GLOBALDB_HPP

#include <boost/asio/io_context.hpp>
#include <boost/filesystem/path.hpp>
#include "outcome/outcome.hpp"
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include "crdt/crdt_options.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/atomic_transaction.hpp"
#include <libp2p/protocol/identify/identify.hpp>
#include <libp2p/protocol/autonat/autonat.hpp>
#include <libp2p/protocol/holepunch/holepunch_server.hpp>
#include <libp2p/protocol/holepunch/holepunch_client.hpp>

namespace sgns::crdt
{
    class GlobalDB : public std::enable_shared_from_this<GlobalDB>
    {
    public:
        using Buffer      = base::Buffer;
        using QueryResult = CrdtDatastore::QueryResult;

        GlobalDB( std::shared_ptr<boost::asio::io_context>              context,
                  std::string                                           databasePath,
                  int                                                   dagSyncPort,
                  std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> broadcastChannel,
                  std::string                                           gsaddresses = {} );
                  
        GlobalDB( std::shared_ptr<boost::asio::io_context> context,
                  std::string databasePath,
                  int dagSyncPort,
                  const std::vector<std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic>> &broadcastChannels,
                  std::string gsaddresses = {} );


        ~GlobalDB();

        /// Pair of key and value to be stored in CRDT
        using DataPair = std::pair<HierarchicalKey, Buffer>;

        /**
         * @enum        Error
         * @brief       Enumeration of error codes used in the proof classes.
         */
        enum class Error
        {
            ROCKSDB_IO = 0,             ///< RocksDB wasn't opened
            IPFS_DB_NOT_CREATED,        ///< IPFS datastore not created
            DAG_SYNCHER_NOT_LISTENING,  ///< DAG Syncher listen error
            CRDT_DATASTORE_NOT_CREATED, ///< CRDT DataStore not created
        };

        outcome::result<void> Init( std::shared_ptr<CrdtOptions> crdtOptions );

        /** Puts key-value pair to storage
        * @param key The path in which the data will be stored
        * @param value The data to be stored
        * @return outcome::failure on error or success otherwise
        */
        outcome::result<void> Put( const HierarchicalKey &key, const Buffer &value );

        /**
         * @brief       Writes a batch of CRDT data all at once
         * @param[in]   data_vector A set of crdt to be written in a single transaction
         * @return      outcome::failure on error or success otherwise
         */
        outcome::result<void> Put( const std::vector<DataPair> &data_vector );
        
        /** Gets a value that corresponds to specified key.
    * @param key - value key
    * @return value as a Buffer
    */
        outcome::result<Buffer> Get( const HierarchicalKey &key );

        /** Removes value for a given key.
    * @param key to remove from storage
    * @return outcome::failure on error or success otherwise
    */
        outcome::result<void> Remove( const HierarchicalKey &key );

        /** Queries CRDT key-value pairs by prefix. If the prefix is empty returns all elements that were not tombstoned
    * @param prefix - keys prefix to match. An empty prefix matches any key.
    * @return list of key-value pairs matches prefix
    */
        outcome::result<QueryResult> QueryKeyValues( const std::string &keyPrefix );

        /** Converts a unique key part to a string representation
    * @param key - binary key to convert
    * @return string represenation of a unique key part
    */
        outcome::result<std::string> KeyToString( const Buffer &key ) const;

        /** Create a transaction object
    * @return new transaction
    */
        std::shared_ptr<AtomicTransaction> BeginTransaction();

    void PrintDataStore();

        auto GetDB()
        {
            return m_crdtDatastore->GetDB();
        }

    private:
        void scheduleBootstrap( std::shared_ptr<boost::asio::io_context> io_context,
                                std::shared_ptr<libp2p::Host>            host );

        std::shared_ptr<boost::asio::io_context>              m_context;
        std::string                                           m_databasePath;
        int                                                   m_dagSyncPort;
        std::string                                           m_graphSyncAddrs;

        std::vector<std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic>> m_broadcastChannels;


        //std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht_;
        //std::shared_ptr<libp2p::protocol::Identify> identify_;
        //std::shared_ptr<libp2p::protocol::IdentifyMessageProcessor> identifymsgproc_;
        //std::shared_ptr<libp2p::protocol::HolepunchClient> holepunch_;
        //std::shared_ptr<libp2p::protocol::HolepunchClientMsgProc> holepunchmsgproc_;

        int obsAddrRetries = 0;

        std::shared_ptr<CrdtDatastore> m_crdtDatastore;

        //Default Bootstrap Servers
        std::vector<std::string> bootstrapAddresses_ = {
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmNnooDu7bfjPFoTZYxMNLWUQJyrVwtbZg5gBMjTezGAJN",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmQCU2EcMqAqQPR2i9bChDtGNJchTbq5TbXJJ16u19uLTa",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmbLHAnMoJPWSCR5Zhtx6BHJX9KiKNN6tpvbUcqanj75Nb",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmcZf59bWwK5XFi76CZX8cbJ4BhTzzA3gU1ZjYZcYW3dwt",
            //"/ip4/64.225.105.42/tcp/4001/p2p/QmPo1ygpngghu5it8u4Mr3ym6SEU2Wp2wA66Z91Y1S1g29",
            //"/ip4/3.92.45.153/tcp/4001/ipfs/12D3KooWP6R6XVCBK7t76o8VDwZdxpzAqVeDtHYQNmntP2y8NHvK",
            "/ip4/104.131.131.82/tcp/4001/ipfs/QmaCpDMGvV2BGHeYERUEnRQAwe3N8SzbUtfsmvsqQLuvuJ",  // mars.i.ipfs.io
            "/ip4/104.236.179.241/tcp/4001/ipfs/QmSoLPppuBtQSGwKDZT2M73ULpjvfd3aZ6ha4oFGL1KrGM", // pluto.i.ipfs.io
            "/ip4/128.199.219.111/tcp/4001/ipfs/QmSoLSafTMBsPKadTEgaXctDQVcqN88CNLHXMkTNwMKPnu", // saturn.i.ipfs.io
            "/ip4/104.236.76.40/tcp/4001/ipfs/QmSoLV4Bbm51jM9C4gDYZQ9Cy3U6aXMJDAbzgu2fzaDs64",   // venus.i.ipfs.io
            "/ip4/178.62.158.247/tcp/4001/ipfs/QmSoLer265NRgSp2LA3dPaeykiS1J6DifTC88f5uVQKNAd",  // earth.i.ipfs.io
            "/ip6/2604:a880:1:20::203:d001/tcp/4001/ipfs/QmSoLPppuBtQSGwKDZT2M73ULpjvfd3aZ6ha4oFGL1KrGM", // pluto.i.ipfs.io
            "/ip6/2400:6180:0:d0::151:6001/tcp/4001/ipfs/QmSoLSafTMBsPKadTEgaXctDQVcqN88CNLHXMkTNwMKPnu", // saturn.i.ipfs.io
            "/ip6/2604:a880:800:10::4a:5001/tcp/4001/ipfs/QmSoLV4Bbm51jM9C4gDYZQ9Cy3U6aXMJDAbzgu2fzaDs64", // venus.i.ipfs.io
            "/ip6/2a03:b0c0:0:1010::23:1001/tcp/4001/ipfs/QmSoLer265NRgSp2LA3dPaeykiS1J6DifTC88f5uVQKNAd", // earth.i.ipfs.io
            //"/dnsaddr/fra1-1.hostnodes.pinata.cloud/ipfs/QmWaik1eJcGHq1ybTWe7sezRfqKNcDRNkeBaLnGwQJz1Cj",
            //"/dnsaddr/fra1-2.hostnodes.pinata.cloud/ipfs/QmNfpLrQQZr5Ns9FAJKpyzgnDL2GgC6xBug1yUZozKFgu4",
            //"/dnsaddr/fra1-3.hostnodes.pinata.cloud/ipfs/QmPo1ygpngghu5it8u4Mr3ym6SEU2Wp2wA66Z91Y1S1g29",
            //"/dnsaddr/nyc1-1.hostnodes.pinata.cloud/ipfs/QmRjLSisUCHVpFa5ELVvX3qVPfdxajxWJEHs9kN3EcxAW6",
            //"/dnsaddr/nyc1-2.hostnodes.pinata.cloud/ipfs/QmPySsdmbczdZYBpbi2oq2WMJ8ErbfxtkG8Mo192UHkfGP",
            //"/dnsaddr/nyc1-3.hostnodes.pinata.cloud/ipfs/QmSarArpxemsPESa6FNkmuu9iSE1QWqPX2R3Aw6f5jq4D5",
        };

        sgns::base::Logger m_logger = sgns::base::createLogger( "GlobalDB" );
    };
}

/**
 * @brief       Macro for declaring error handling in the GlobalDB class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, GlobalDB::Error );

#endif // SUPERGENIUS_CRDT_GLOBALDB_HPP
