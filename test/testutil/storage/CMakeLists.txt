
add_library(base_fs_test
    base_fs_test.hpp
    base_fs_test.cpp
    )
target_link_libraries(base_fs_test
    PUBLIC
    Boost::filesystem
    Boost::headers
    GTest::gtest
    logger
)

add_library(base_rocksdb_test
    base_rocksdb_test.hpp
    base_rocksdb_test.cpp
    )
add_library(base_crdt_test
    base_crdt_test.hpp
    base_crdt_test.cpp
        ../../src/crdt/crdt_atomic_transaction_test.cpp
        ../../src/crdt/crdt_custom_broadcaster.cpp
        ../../src/crdt/crdt_custom_broadcaster.hpp
        ../../src/crdt/crdt_custom_dagsyncer.cpp
        ../../src/crdt/crdt_custom_dagsyncer.hpp
)
target_link_libraries(base_rocksdb_test
    base_fs_test
    Boost::filesystem
    Boost::headers
    logger
    rocksdb
)
target_link_libraries(base_crdt_test
    base_fs_test
    crdt_globaldb
    )

add_library(std_list_adapter INTERFACE)

target_link_libraries(std_list_adapter INTERFACE
    outcome
    )
