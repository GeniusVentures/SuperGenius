
addtest(rocksdb_fs_test rocksdb_fs_test.cpp)
target_link_libraries(rocksdb_fs_test
    rocksdb
    base_fs_test
    database_error
    )

if(FORCE_MULTIPLE)
  set_target_properties(rocksdb_fs_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()

addtest(rocksdb_integration_test
    rocksdb_integration_test.cpp
    )
target_link_libraries(rocksdb_integration_test
    rocksdb
    base_rocksdb_test
    Boost::filesystem
    )

if(FORCE_MULTIPLE)
  set_target_properties(rocksdb_integration_test PROPERTIES LINK_FLAGS "${MULTIPLE_OPTION}")
endif()
