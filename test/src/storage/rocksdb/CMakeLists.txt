
addtest(rocksdb_fs_test rocksdb_fs_test.cpp)
target_link_libraries(rocksdb_fs_test
    rocksdb
    base_fs_test
    database_error
    )

addtest(rocksdb_integration_test
    rocksdb_integration_test.cpp
    )
target_link_libraries(rocksdb_integration_test
    rocksdb
    base_rocksdb_test
    Boost::filesystem
    )
