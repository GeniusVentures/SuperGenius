#ifndef SUPERGENIUS_CRDT_HEADS_HPP
#define SUPERGENIUS_CRDT_HEADS_HPP

#include <mutex>
#include <storage/rocksdb/rocksdb.hpp>
#include "base/logger.hpp"
#include "crdt/hierarchical_key.hpp"
#include <primitives/cid/cid.hpp>
#include <map>
#include <set>

namespace sgns::crdt
{
    /** @brief CrdtHeads manages the current Merkle-CRDT heads.
  */
    class CrdtHeads
    {
    public:
        using DataStore      = storage::rocksdb;
        using Buffer         = base::Buffer;
        using CRDTHeadList   = std::unordered_map<std::string, std::set<CID>>;
        using CRDTListResult = std::pair<CRDTHeadList, uint64_t>;

        /** Constructor
        * @param aDatastore Pointer to datastore
        * @param aNamespace Namespce key (e.g "/namespace")
        */
        CrdtHeads( std::shared_ptr<DataStore> aDatastore, const HierarchicalKey &aNamespace );

        /** Copy constructor
         */
        CrdtHeads( const CrdtHeads & );

        /**
         * @brief      Destroy the Crdt Heads object
         */
        virtual ~CrdtHeads() = default;

        /** Equality operator
        * @return true if equal otherwise, it returns false.
        */
        bool operator==( const CrdtHeads & );

        /** Equality operator
    * @return true if NOT equal otherwise, it returns false.
    */
        bool operator!=( const CrdtHeads & );

        /** Assignment operator
    */
        CrdtHeads &operator=( const CrdtHeads & );

        /** Get namespace hierarchical key
    */
        HierarchicalKey GetNamespaceKey() const;

        /** Get full path to CID key
         *   /<namespace>/<topic>/<cid>
         * @param aCid Content identifier
         * @return full path to CID key as HierarchicalKey or outcome::failure on error
         */
        outcome::result<HierarchicalKey> GetKey( const std::string &topic, const CID &aCid );

        /** Check if CID is among the current heads.
         * @param aCid Content identifier
         * @return true is CID is head, false otherwise
         */
        bool IsHead( const CID &aCid, const std::string &topic );

        /** Check if CID is head and return it height if it is
         * @param aCid Content identifier
         * @return Height of head or outcome::failure on error
         */
        outcome::result<uint64_t> GetHeadHeight( const CID &aCid, const std::string &topic );

        /** Get current number of heads
    * @return lenght, current number of heads or outcome::failure on error
    */
        outcome::result<int> GetLength( const std::string &topic = "" );

        /** Add head CID to datastore with full namespace
    * @param aCid Content identifier
    * @param aHeight height of head
    * @return outcome::failure on error
    */
        outcome::result<void> Add( const CID &aCid, uint64_t aHeight, const std::string &topic );

        /** Replace a head with a new cid.
    * @param aCidHead Content identifier of head to replace
    * @param aNewHeadCid Content identifier of new head
    * @param aHeight height of head
    * @return outcome::failure on error
    */
        outcome::result<void> Replace( const CID         &aCidHead,
                                       const CID         &aNewHeadCid,
                                       uint64_t           aHeight,
                                       const std::string &topic );

        /** Returns the list of current heads plus the max height.
    * @param aHeads output reference to list of CIDs
    * @param aMaxHeight output reference to maximum height
    * @return outcome::failure on error
    */
        outcome::result<CRDTListResult> GetList( const std::set<std::string> &topics = {} );

        /** primeCache builds the heads cache based on what's in storage; since
    * it is called from the constructor only we don't bother locking.
    * @return outcome::failure on error
    */
        outcome::result<void> PrimeCache();

    protected:
        /** Write data to datastore in batch mode
    * @param aDataStore Pointer to datastore batch
    * @param aCid Content identifier to add
    * @param aHeight height of CID head
    * @return outcome::failure on error
    */
        outcome::result<void> Write( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                     const CID                                   &aCid,
                                     uint64_t                                     aHeight,
                                     const std::string                           &topic );

        /** Delete data from datastore in batch mode
        * @param aDataStore Pointer to datastore batch
        * @param aCid Content identifier to remove
        */
        outcome::result<void> Delete( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                      const CID                                   &aCid,
                                      const std::string                           &topic );

    private:
        CrdtHeads() = default;

        std::shared_ptr<DataStore>                               dataStore_;
        std::unordered_map<std::string, std::map<CID, uint64_t>> cache_;
        HierarchicalKey                                          namespaceKey_;
        std::recursive_mutex                                     mutex_;
        base::Logger                                             logger_ = base::createLogger( "CrdtHeads" );
    };

} // namespace sgns::crdt

#endif //SUPERGENIUS_CRDT_HEADS_HPP
