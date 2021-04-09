
add_library(base_fs_test
    base_fs_test.hpp
    base_fs_test.cpp
    )
target_link_libraries(base_fs_test
    Boost::filesystem
    Boost::boost
    logger
    )

add_library(base_rocksdb_test
    base_rocksdb_test.hpp
    base_rocksdb_test.cpp
    )
target_link_libraries(base_rocksdb_test
    base_fs_test
    Boost::filesystem
    Boost::boost
    logger
    rocksdb
    )

add_library(std_list_adapter INTERFACE)

target_link_libraries(std_list_adapter INTERFACE
    outcome
    )
