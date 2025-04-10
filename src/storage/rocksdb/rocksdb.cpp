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

    rocksdb::~rocksdb() {}

    outcome::result<std::shared_ptr<rocksdb>> rocksdb::create( std::string_view path, const Options &options )
    {
        // Create a shared_ptr immediately to avoid manual memory management
        auto l = std::make_shared<rocksdb>();

        // Store a deep copy of options
        l->options_ = std::make_shared<Options>( options );

        // Set up bloom filter
        BlockBasedTableOptions table_options;
        table_options.filter_policy.reset( ::ROCKSDB_NAMESPACE::NewBloomFilterPolicy( 10, false ) );
        table_options.whole_key_filtering = true;
        l->options_->table_factory.reset( NewBlockBasedTableFactory( table_options ) );

        // Define a prefix extractor
        l->options_->prefix_extractor.reset( ::ROCKSDB_NAMESPACE::NewCappedPrefixTransform( 3 ) );

        l->options_->info_log_level = ::ROCKSDB_NAMESPACE::InfoLogLevel::ERROR_LEVEL;
        // Configure threading environment
        l->options_->env = ::rocksdb::Env::Default();
        l->options_->env->SetBackgroundThreads( 4, ::rocksdb::Env::Priority::HIGH );

        // Open the RocksDB database
        DB  *db     = nullptr;
        auto status = DB::Open( *( l->options_ ), std::string( path ), &db );

        if ( status.ok() )
        {
            // Wrap DB* into a shared_ptr with a custom deleter to ensure cleanup
            l->db_ = std::shared_ptr<DB>( db );
            // Create logger
            l->logger_ = base::createLogger( "rocksdb" );
            rocksdb::WriteOptions write_options;
            write_options.sync = true;
            l->setWriteOptions( write_options );
            return l; // Return the shared_ptr
        }

        // Clean up manually allocated DB if Open() succeeded but logic fails
        if ( db )
        {
            delete db;
        }

        // Return an error result
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

        auto strKeyPrefix = std::string( keyPrefix.toString() );

        QueryResult results;
        auto        iter        = std::unique_ptr<rocksdb::Iterator>( db_->NewIterator( read_options ) );
        auto        slicePrefix = make_slice( keyPrefix );
        for ( iter->Seek( slicePrefix ); iter->Valid() && iter->key().starts_with( slicePrefix ); iter->Next() )
        {
            Buffer key;
            key.put( iter->key().ToString() );
            Buffer value;
            value.put( iter->value().ToString() );
            results.emplace( std::move( key ), std::move( value ) );
        }
        return results;
    }

    outcome::result<rocksdb::QueryResult> rocksdb::query( const std::string &prefix_base,
                                                          const std::string &middle_part,
                                                          const std::string &remainder_prefix ) const
    {
        ReadOptions read_options      = ro_;
        read_options.auto_prefix_mode = true; //Adaptive Prefix Mode
        bool negated_query            = ( !middle_part.empty() && middle_part[0] == '!' );
        bool simplified_query         = ( !( middle_part == "*" ) ) && ( !negated_query );

        auto strKeyPrefix = prefix_base;
        if ( simplified_query )
        {
            strKeyPrefix += middle_part + remainder_prefix;
        }

        QueryResult results;
        auto        iter = std::unique_ptr<rocksdb::Iterator>( db_->NewIterator( read_options ) );
        for ( iter->Seek( strKeyPrefix ); iter->Valid() && iter->key().starts_with( strKeyPrefix ); iter->Next() )
        {
            const std::string &key_string = iter->key().ToString();

            if ( !simplified_query )
            {
                size_t pos = key_string.find( remainder_prefix, strKeyPrefix.size() );
                if ( pos == std::string::npos )
                {
                    continue;
                }
                if ( negated_query )
                {
                    size_t pos2 = key_string.find( middle_part.substr( 1 ), strKeyPrefix.size() );
                    if ( pos2 != std::string::npos )
                    {
                        continue;
                    }
                }
            }
            Buffer key;
            key.put( key_string );
            Buffer value;
            value.put( iter->value().ToString() );
            results.emplace( std::move( key ), std::move( value ) );
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
