#ifndef SUPERGENIUS_CRDT_SET_HPP
#define SUPERGENIUS_CRDT_SET_HPP

#include <mutex>
#include <storage/rocksdb/rocksdb.hpp>
#include <crdt/hierarchical_key.hpp>
#include <crdt/proto/delta.pb.h>

namespace sgns::crdt
{
  /** @brief CrdtSet implements an Add-Wins Observed-Remove Set using delta-CRDTs
  * (https://arxiv.org/abs/1410.2803) and backing all the data in a
  * datastore. It is fully agnostic to MerkleCRDTs or the delta distribution
  * layer.  It chooses the Value with most priority for a Key as the current
  * Value. When two values have the same priority, it chooses by alphabetically
  * sorting their unique IDs alphabetically.
  */
  class CrdtSet
  {
  public:
    using Delta = pb::Delta;
    using Element = pb::Element;
    using Buffer = base::Buffer;
    using DataStore = storage::rocksdb;
    using QueryResult = DataStore::QueryResult;

    enum class QuerySuffix
    {
      QUERY_ALL,
      QUERY_VALUESUFFIX,
      QUERY_PRIORITYSUFFIX,
    };

    /** Function pointer to notify caller if key added to datastore
    * @param k key name
    * @param v buffer value
    */
    using PutHookPtr = std::function<void(const std::string& k, const Buffer& v)>;

    /** Function pointer to notify caller when key deleted from datastore
    * @param k key name
    */
    using DeleteHookPtr = std::function<void(const std::string& k)>;

    /** Constructor
    * @param aDatastore Pointer to datastore
    * @param aNamespace Namespce key (e.g "/namespace")
    * @param aPutHookPtr Function pointer to nofify when key added to datastore, default nullptr
    * @param aDeleteHookPtr Function pointer to nofify when key deleted from datastore, default nullptr
    */
    CrdtSet( std::shared_ptr<DataStore> aDatastore,
             const HierarchicalKey     &aNamespace,
             PutHookPtr                 aPutHookPtr    = nullptr,
             DeleteHookPtr              aDeleteHookPtr = nullptr );

    /** Copy constructor
    */
    CrdtSet(const CrdtSet&);

    /** Destructor
    */
    virtual ~CrdtSet() = default;

    /** Equality operator
    * @return true if equal otherwise, it returns false.
    */
    bool operator==(const CrdtSet&);

    /** Equality operator
    * @return true if NOT equal otherwise, it returns false.
    */
    bool operator!=(const CrdtSet&);

    /** Assignment operator
    */
    CrdtSet& operator=(const CrdtSet&);

    /** Get priority suffix
    */
    static std::string GetPrioritySuffix()
    {
        return std::string( prioritySuffix_ );
    }

    /** Get value suffix
    */
    static std::string GetValueSuffix()
    {
        return std::string( valueSuffix_ );
    }

    /** Get value from datastore for HierarchicalKey defined
    * @param aKey HierarchicalKey to get value from datastore
    * @return buffer value as string or outcome::failure on error
    */
    outcome::result<std::string> GetValueFromDatastore(const HierarchicalKey& aKey);

    /** Returns a new delta-set adding the given key/value.
    * @param aKey delta key to add to datastore
    * @param aValue delta value to add to datastore
    * @return pointer to new delta or outcome::failure on error
    */
    static outcome::result<std::shared_ptr<Delta>> CreateDeltaToAdd( const std::string &aKey,
                                                                     const std::string &aValue );

    /** Returns a new delta-set removing the given keys with prefix /namespace/s/<key>
    * @param aKey delta key to remove from datastore
    * @return pointer to delta or outcome::failure on error
    */
    outcome::result<std::shared_ptr<Delta>> CreateDeltaToRemove(const std::string& aKey);

    /** Get the value of an element from the CRDT set /namespace/k/<key>/v
    * @param aKey Key name
    * @return buffer value or outcome::failure on error
    */
    outcome::result<Buffer> GetElement(const std::string& aKey);

    /** Query datastore key-value pairs by prefix, if prefix empty return all elements /namespace/k/<prefix>
    * @param aPrefix prefix to search, if empty string, return all
    * @param aSuffix suffix to search
    * @return list of key-value pairs matches prefix and suffix
    * \sa QuerySuffix
    */
    outcome::result<QueryResult> QueryElements(const std::string& aPrefix, const QuerySuffix& aSuffix = QuerySuffix::QUERY_ALL);

    // TODO: Need to implement query with prefix from datastore
    //func (s *set) Elements(q query.Query) (query.Results, error) {

    /** Returns true if the key belongs to one of the elements in the
    * /namespace/k/<key>/v set, and this element is not tombstoned.
    * @param aKey key name
    * @return true if the key belongs to one of the elements and is not tombstoned or outcome::failure on error
    */
    outcome::result<bool> IsValueInSet(const std::string& aKey);

    /** Returns in we have a key/block combinations in the
    * elements set that has not been tombstoned.
    * @param aKey key name
    * @return true if the key has not been tombstoned, false otherwise or outcome::failure on error
    */
    outcome::result<bool> InElemsNotTombstoned(const std::string& aKey);

    /** Get full path prefix in namespace for a key
    * /namespace/<key>
    * @param aKey key string
    * @return HierarchicalKey with key prefix
    */
    HierarchicalKey KeyPrefix(const std::string& aKey);

    /** Get elems full path prefix in namespace for a key
    * /namespace/s/<key>
    * @param aKey key string
    * @return HierarchicalKey with elems prefix
    */
    HierarchicalKey ElemsPrefix(const std::string& aKey);

    /** Get tombs full path prefix in namespace for a key
    * /namespace/t/<key>
    * @param aKey key string
    * @return HierarchicalKey with tombs prefix
    */
    HierarchicalKey TombsPrefix(const std::string& aKey);

    /** Get keys full path prefix in namespace for a key
    * /namespace/k/<key>
    * @param aKey key string
    * @return HierarchicalKey with key prefix
    */
    HierarchicalKey KeysKey(const std::string & aKey);

    /** Get value full path prefix in namespace for a key
    * /namespace/k/<key>/v
    * @param aKey key string
    * @return HierarchicalKey with value prefix
    */
    HierarchicalKey ValueKey(const std::string& aKey);

    /** Get priority full path prefix in namespace for a key
    * /namespace/k/<key>/p
    * @param aKey key string
    * @return HierarchicalKey with priority prefix
    */
    HierarchicalKey PriorityKey(const std::string& aKey);

    /** Get priority for a key from datastore
    * @param aKey key string
    * @return priority of the key or outcome::failure on error
    */
    outcome::result<uint64_t> GetPriority(const std::string& aKey);

    /** Set priority for a key and put into datastore
    * @param aKey key string
    * @param aPriority priority to save
    * @return priority of the key or outcome::failure on error
    */
    outcome::result<void> SetPriority( const std::string &aKey, uint64_t aPriority );

    /** Sets a value to datastore if priority is higher. When equal, it sets if the
    * value is lexicographically higher than the current value.
    * @param aKey key string
    * @param aID tomb key ID
    * @param aValue buffer value to set
    * @param aPriority priority to save
    * @return priority of the key or outcome::failure on error
    */
    outcome::result<void> SetValue( const std::string &aKey,
                                    const std::string &aID,
                                    const Buffer      &aValue,
                                    uint64_t           aPriority );

    /** Sets a value to datastore in batch mode if priority is higher. When equal, it sets if the
    * value is lexicographically higher than the current value.
    * @param aDataStore datastore batch
    * @param aKey key string
    * @param aID tomb key ID
    * @param aValue buffer value to set
    * @param aPriority priority to save
    * @return priority of the key or outcome::failure on error
    */
    outcome::result<void> SetValue( const std::unique_ptr<storage::BufferBatch> &aDataStore,
                                    const std::string                           &aKey,
                                    const std::string                           &aID,
                                    const Buffer                                &aValue,
                                    uint64_t                                     aPriority );

    /** putElems adds items to the "elems" set. It will also set current
    * values and priorities for each element. This needs to run in a lock,
    * as otherwise races may occur when reading/writing the priorities, resulting
    * in bad behaviours.
    *
    * Technically the lock should only affect the keys that are being written,
    * but with the batching optimization the locks would need to be hold until
    * the batch is written), and one lock per key might be way worse than a single
    * global lock in the end.
    * @param aElems list of elems to to into datastore
    * @param aID tomb key ID
    * @param aPriority priority to save
    * @return outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> PutElems( std::vector<Element> &aElems, const std::string &aID, uint64_t aPriority );

    /** PutTombs adds items to the "tombs" set (marked as deleted)
    * @param aElems list of elems to to into datastore
    * @return outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> PutTombs(const std::vector<Element>& aTombs);

    /** Merge elems and tombs from delta into datastore
    * @param aDelta delta with elems and tombs to save into datastore
    * @param aID tomb key ID
    * @return outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> Merge(const std::shared_ptr<CrdtSet::Delta>& aDelta, const std::string& aID);

    /** Check if key is tombstoned with tomb ID and found in datastore
    * @param aKey key string
    * @param aID tomb key ID
    * @return true if key with ID is tombstoned, false otherwise or outcome::failure on error
    */
    outcome::result<bool> InTombsKeyID(const std::string& aKey, const std::string& aID);

    /** The PutHook function is triggered whenever an element
    * is successfully added to the datastore (either by a local
    * or remote update), and only when that addition is considered the
    * prevalent value.
    * @param putHookPtr Function pointer to callback function
    */
    void SetPutHook(const PutHookPtr& putHookPtr);

    /** The DeleteHook function is triggered whenever a version of an
    * element is successfully removed from the datastore (either by a
    * local or remote update). Unordered and concurrent updates may
    * result in the DeleteHook being triggered even though the element is
    * still present in the datastore because it was re-added. If that is
    * relevant, use Has() to check if the removed element is still part
    * of the datastore.
    * @param deleteHookPtr Function pointer to callback function
    */
    void SetDeleteHook(const DeleteHookPtr& deleteHookPtr);

    /**
    * Perform a Sync against all the paths associated with a key prefix
    * @param aKeyList all heads and the set entries related to the given prefix
    * @return outcome::success on success or outcome::failure otherwise
    */
   outcome::result<void> DataStoreSync(const std::vector<HierarchicalKey>& aKeyList);
  private:
    CrdtSet() = default;

    std::shared_ptr<DataStore> dataStore_ = nullptr;
    HierarchicalKey namespaceKey_;
    std::mutex mutex_;
    PutHookPtr putHookFunc_ = nullptr;
    DeleteHookPtr deleteHookFunc_ = nullptr;

    static constexpr std::string_view elemsNamespace_ = "s"; // "s" -> elements namespace /set/s/<key>/<block>
    static constexpr std::string_view tombsNamespace_ = "t"; // "t" -> tombstones namespace /set/t/<key>/<block>
    static constexpr std::string_view keysNamespace_ = "k"; // "k" -> keys namespace /set/k/<key>/{v,p}
    static constexpr std::string_view valueSuffix_ = "v"; // "v" for /keys namespace
    static constexpr std::string_view prioritySuffix_ = "p"; // "p" for /keys namespace
  };

} // namespace sgns::crdt

#endif //SUPERGENIUS_CRDT_SET_HPP
