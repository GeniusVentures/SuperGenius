#ifndef SUPERGENIUS_CRDT_GLOBALDB_HPP
#define SUPERGENIUS_CRDT_GLOBALDB_HPP

#include <crdt/crdt_options.hpp>
#include <outcome/outcome.hpp>

#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <crdt/crdt_datastore.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/filesystem/path.hpp>

namespace sgns::crdt
{
class GlobalDB
{
public:
    using Buffer = base::Buffer;
    using QueryResult = CrdtDatastore::QueryResult;

    GlobalDB(
        std::shared_ptr<boost::asio::io_context> context,
        std::string databasePath,
        int dagSyncPort,
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> broadcastChannel);

    outcome::result<void> Init(std::shared_ptr<CrdtOptions> crdtOptions);

    /** Puts key-value pair to storage
    * @param key
    * @param value
    * @return outcome::failure on error or success otherwise
    */
    outcome::result<void> Put(const HierarchicalKey& key, const Buffer& value);

    /** Gets a value that corresponds to specified key.
    * @param key - value key
    * @return value as a Buffer
    */
    outcome::result<Buffer> Get(const HierarchicalKey& key);

    /** Removes value for a given key.
    * @param key to remove from storage
    * @return outcome::failure on error or success otherwise
    */
    outcome::result<void> Remove(const HierarchicalKey& key);

    /** Queries CRDT key-value pairs by prefix. If the prefix is empty returns all elements that were not tombstoned
    * @param prefix - keys prefix to match. An empty prefix matches any key.
    * @return list of key-value pairs matches prefix
    */
    outcome::result<QueryResult> QueryKeyValues(const std::string& keyPrefix);

    /** Converts a unique key part to a string representation
    * @param key - binary key to convert
    * @return string represenation of a unique key part
    */
    outcome::result<std::string> KeyToString(const Buffer& key) const;

    /** Create a transaction object
    * @return new transaction
    */
    std::shared_ptr<CrdtDataStoreTransaction> BeginTransaction();

    auto GetDB()
    {
        return m_crdtDatastore->GetDB();
    }

private:
    std::shared_ptr<boost::asio::io_context> m_context;
    std::string m_databasePath;
    int m_dagSyncPort;
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> m_broadcastChannel;

    std::shared_ptr<CrdtDatastore> m_crdtDatastore;

    sgns::base::Logger m_logger = sgns::base::createLogger("GlobalDB");
};
}
#endif // SUPERGENIUS_CRDT_GLOBALDB_HPP
