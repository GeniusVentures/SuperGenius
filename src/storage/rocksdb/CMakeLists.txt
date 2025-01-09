add_library(rocksdb
    rocksdb.cpp
    rocksdb.hpp
    rocksdb_batch.cpp
    rocksdb_batch.hpp
    rocksdb_cursor.cpp
    rocksdb_cursor.hpp
    rocksdb_util.hpp
)

target_link_libraries(rocksdb
    PUBLIC
    RocksDB::rocksdb
    buffer
    logger
    PRIVATE
    database_error
)

supergenius_install(rocksdb)
