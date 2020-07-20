
addtest(leveldb_fs_test leveldb_fs_test.cpp)
target_link_libraries(leveldb_fs_test
    leveldb
    base_fs_test
    database_error
    )

addtest(leveldb_integration_test
    leveldb_integration_test.cpp
    )
set_target_properties(leveldb_fs_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
set_target_properties(leveldb_integration_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")

target_link_libraries(leveldb_integration_test
    leveldb
    base_leveldb_test
    Boost::filesystem
    )
