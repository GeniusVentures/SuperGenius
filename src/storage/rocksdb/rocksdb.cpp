#include <memory>
#include <utility>

#include <boost/filesystem.hpp>

#include <rocksdb/table.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/slice_transform.h>

#include <storage/rocksdb/rocksdb.hpp>
#include "storage/rocksdb/rocksdb_cursor.hpp"
#include "storage/rocksdb/rocksdb_batch.hpp"
#include "storage/rocksdb/rocksdb_util.hpp"

namespace sgns::storage
{
    using BlockBasedTableOptions = ::ROCKSDB_NAMESPACE::BlockBasedTableOptions;

    rocksdb::~rocksdb()
    {
        db_->Close();
    }

    outcome::result<std::shared_ptr<rocksdb>> rocksdb::create( std::string_view path, Options options )
    {
        // Set up bloom filter
        BlockBasedTableOptions table_options;
        table_options.filter_policy.reset( ::ROCKSDB_NAMESPACE::NewBloomFilterPolicy( 10, false ) );
        table_options.whole_key_filtering = true; // If you also need Get() to use whole key filters, leave it to true.
        // For multiple column family setting, set up specific column family's ColumnFamilyOptions.table_factory instead.
        options.table_factory.reset( NewBlockBasedTableFactory( table_options ) );

        // Define a prefix. In this way, a fixed length prefix extractor. A recommended one to use.
        options.prefix_extractor.reset( ::ROCKSDB_NAMESPACE::NewCappedPrefixTransform( 3 ) );

        DB  *db     = nullptr;
        auto status = DB::Open( options, std::string( path ), &db );
        if ( status.ok() )
        {
            auto l     = std::make_unique<rocksdb>();
            l->db_     = std::shared_ptr<DB>( db );
            l->logger_ = base::createLogger( "rocksdb" );
            return l;
        }

        return error_as_result<std::shared_ptr<rocksdb>>( status );
    }

    outcome::result<std::shared_ptr<rocksdb>> rocksdb::create( const std::shared_ptr<DB> &db )
    {
        if ( db == nullptr )
        {
            return error_as_result<std::shared_ptr<rocksdb>>( rocksdb::Status( rocksdb::Status::PathNotFound() ) );
        }

        auto l     = std::make_unique<rocksdb>();
        l->db_     = db;
        l->logger_ = base::createLogger( "rocksdb" );
        return l;
    }

    std::unique_ptr<BufferMapCursor> rocksdb::cursor()
    {
        auto it = std::unique_ptr<Iterator>( db_->NewIterator( ro_ ) );
        return std::make_unique<Cursor>( std::move( it ) );
    }

    std::unique_ptr<BufferBatch> rocksdb::batch()
    {
        return std::make_unique<Batch>( *this );
    }

    void rocksdb::setReadOptions( ReadOptions ro )
    {
        ro_ = std::move( ro );
    }

    void rocksdb::setWriteOptions( WriteOptions wo )
    {
        wo_ = wo;
    }

    outcome::result<Buffer> rocksdb::get( const Buffer &key ) const
    {
        std::string value;
        auto        status = db_->Get( ro_, make_slice( key ), &value );
        if ( status.ok() )
        {
            // FIXME: is it possible to avoid copying string -> Buffer?
            return Buffer{}.put( value );
        }

        // not always an actual error so don't log it
        if ( status.IsNotFound() )
        {
            return error_as_result<Buffer>( status );
        }

        return error_as_result<Buffer>( status, logger_ );
    }

    outcome::result<rocksdb::QueryResult> rocksdb::query( const Buffer &keyPrefix ) const
    {
        ReadOptions read_options      = ro_;
        read_options.auto_prefix_mode = true; //Adaptive Prefix Mode

    auto strKeyPrefix = std::string(keyPrefix.toString());

    QueryResult results;
    auto iter = std::unique_ptr<rocksdb::Iterator>(db_->NewIterator(read_options));
    auto slicePrefix = make_slice(keyPrefix);
    for (iter->Seek(slicePrefix); iter->Valid() && iter->key().starts_with(slicePrefix); iter->Next())
    {
      Buffer key;
      key.put(iter->key().ToString());
      Buffer value;
      value.put(iter->value().ToString());
      results.emplace(std::move(key), std::move(value));
    }
    return results;
  }

    bool rocksdb::contains( const Buffer &key ) const
    {
        // here we interpret all kinds of errors as "not found".
        // is there a better way?
        return get( key ).has_value();
    }

    bool rocksdb::empty() const
    {
        auto it = std::unique_ptr<Iterator>( db_->NewIterator( ro_ ) );
        it->SeekToFirst();
        return it->Valid();
    }

    outcome::result<void> rocksdb::put( const Buffer &key, const Buffer &value )
    {
        auto status = db_->Put( wo_, make_slice( key ), make_slice( value ) );
        if ( status.ok() )
        {
            return outcome::success();
        }

        return error_as_result<void>( status, logger_ );
    }

    outcome::result<void> rocksdb::put( const Buffer &key, Buffer &&value )
    {
        Buffer copy( std::move( value ) );
        return put( key, copy );
    }

    outcome::result<void> rocksdb::remove( const Buffer &key )
    {
        auto status = db_->Delete( wo_, make_slice( key ) );
        if ( status.ok() )
        {
            return outcome::success();
        }

        return error_as_result<void>( status, logger_ );
    }

    std::vector<rocksdb::KeyValuePair> rocksdb::GetAll() const
    {
        std::vector<KeyValuePair> ret_val;
        auto iter = std::unique_ptr<rocksdb::Iterator>( db_->NewIterator( rocksdb::ReadOptions() ) );

        for ( iter->SeekToFirst(); iter->Valid(); iter->Next() )
        {
            Buffer key;
            Buffer value;
            key.put( iter->key().ToString() );

            value.put( iter->value().ToString() );
            ret_val.push_back( std::make_pair( key, value ) );
        }
        return ret_val;
    }

} // namespace sgns::storage
