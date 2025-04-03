

#ifndef SUPERGENIUS_rocksdb_HPP
#define SUPERGENIUS_rocksdb_HPP

#include <rocksdb/rocksdb_namespace.h>
#include <rocksdb/db.h>
#include <rocksdb/slice.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>

#include "base/logger.hpp"
#include "storage/buffer_map_types.hpp"

namespace sgns::storage
{

    /**
   * @brief An implementation of PersistentBufferMap interface, which uses
   * rocksdb as underlying storage.
   */
    class rocksdb : public BufferStorage
    {
    public:
        class Batch;
        class Cursor;

        using Iterator     = ::ROCKSDB_NAMESPACE::Iterator;
        using Options      = ::ROCKSDB_NAMESPACE::Options;
        using ReadOptions  = ::ROCKSDB_NAMESPACE::ReadOptions;
        using WriteOptions = ::ROCKSDB_NAMESPACE::WriteOptions;
        using DB           = ::ROCKSDB_NAMESPACE::DB;
        using Status       = ::ROCKSDB_NAMESPACE::Status;
        using Slice        = ::ROCKSDB_NAMESPACE::Slice;
        using QueryResult  = std::map<Buffer, Buffer>;
        using KeyValuePair = std::pair<Buffer, Buffer>;

        ~rocksdb() override;

        /**
     * @brief Factory method to create an instance of rocksdb class.
     * @param path filesystem path where database is going to be
     * @param options rocksdb options, such as caching, logging, etc.
     * @return instance of rocksdb
     */
        static outcome::result<std::shared_ptr<rocksdb>> create( std::string_view path, const Options& options = Options() );

        /**
    * @brief Factory method to create an instance of rocksdb class.
    * @param db pointer to rocksdb database instance
    */
        static outcome::result<std::shared_ptr<rocksdb>> create( const std::shared_ptr<DB> &db );

        /**
     * @brief Set read options, which are used in @see rocksdb#get
     * @param ro options
     */
        void setReadOptions( ReadOptions ro );

        /**
     * @brief Set write options, which are used in @see rocksdb#put
     * @param wo options
     */
        void setWriteOptions( WriteOptions wo );

        std::unique_ptr<BufferMapCursor> cursor() override;

        std::unique_ptr<BufferBatch> batch() override;

        outcome::result<Buffer> get( const Buffer &key ) const override;

        outcome::result<QueryResult> query( const Buffer &keyPrefix ) const;

        [[nodiscard]] bool contains( const Buffer &key ) const override;

        bool empty() const override;

        outcome::result<void> put( const Buffer &key, const Buffer &value ) override;

        // value will be copied, not moved, due to internal structure of rocksdb
        outcome::result<void> put( const Buffer &key, Buffer &&value ) override;

        outcome::result<void> remove( const Buffer &key ) override;

        std::string GetName() override
        {
            return "rocksdb";
        }

        [[nodiscard]] std::shared_ptr<DB> getDB() const
        {
            return db_;
        }

        /**
         * @brief      Gets all key value pairs on rocksdb
         */
        std::vector<KeyValuePair> GetAll() const;

    private:
        std::shared_ptr<DB> db_;
        ReadOptions         ro_;
        WriteOptions        wo_;
        base::Logger        logger_;
        std::shared_ptr<Options> options_;
    };

} // namespace sgns::storage

#endif // SUPERGENIUS_rocksdb_HPP
