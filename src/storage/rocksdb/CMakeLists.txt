

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
    RocksDB::rocksdb
    buffer
    database_error
    logger
    )
supergenius_install(rocksdb)
