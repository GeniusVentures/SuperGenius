

add_library(leveldb
    leveldb.cpp
    leveldb_batch.cpp
    leveldb_cursor.cpp
    )
target_link_libraries(leveldb
    leveldb::leveldb
    buffer
    database_error
    logger
    )
supergenius_install(leveldb)
