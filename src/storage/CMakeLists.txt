add_subdirectory(rocksdb)

add_library(database_error
    database_error.cpp
)
target_link_libraries(database_error
    PUBLIC
    Boost::headers
    outcome
    RocksDB::rocksdb
)
supergenius_install(database_error)
